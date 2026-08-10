/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/WasmRefTypeAnalysis.h"

#include "jit/MIRGraph.h"

using namespace js;
using namespace js::jit;

// Updates the wasm ref type of a node.
static bool UpdateWasmRefType(MDefinition* def) {
  wasm::MaybeRefType newRefType = def->computeWasmRefType();
  bool changed = newRefType != def->wasmRefType();
  def->setWasmRefType(newRefType);
  return changed;
}

// Since wasm has a fairly rich type system enforced in validation, we can use
// this type system within MIR to robustly track the types of ref values. This
// allows us to make MIR-level optimizations such as eliding null checks or
// omitting redundant casts.
//
// This analysis pass performs simple data flow analysis by assigning ref types
// to each definition, then revisiting phis and their uses as necessary until
// the types have narrowed to a fixed point.
bool jit::TrackWasmRefTypes(MIRGraph& graph) {
  // The worklist tracks nodes whose types have changed and whose uses must
  // therefore be re-evaluated.
  Vector<MDefinition*, 16, SystemAllocPolicy> worklist;

  // Assign an initial ref type to each definition. Reverse postorder ensures
  // that nodes are always visited before their uses, with the exception of loop
  // backedge phis.
  for (ReversePostorderIterator blockIter = graph.rpoBegin();
       blockIter != graph.rpoEnd(); blockIter++) {
    MBasicBlock* block = *blockIter;
    for (MDefinitionIterator def(block); def; def++) {
      // Set the initial type on all nodes. If a type is produced, then any
      // loop backedge phis that use this node must have been previously
      // visited, and must be updated and possibly added to the worklist. (Any
      // other uses of this node will be visited later in this first pass.)

      if (def->type() != MIRType::WasmAnyRef) {
        continue;
      }

      bool hasType = UpdateWasmRefType(*def);
      if (hasType) {
        for (MUseIterator use(def->usesBegin()); use != def->usesEnd(); use++) {
          MNode* consumer = use->consumer();
          if (!consumer->isDefinition() || !consumer->toDefinition()->isPhi()) {
            continue;
          }
          MPhi* phi = consumer->toDefinition()->toPhi();
          if (phi->block()->isLoopHeader() &&
              *def == phi->getLoopBackedgeOperand()) {
            bool changed = UpdateWasmRefType(phi);
            if (changed && !worklist.append(phi)) {
              return false;
            }
          } else {
            // Any other type of use must not have a ref type yet, because we
            // are yet to hit it in this forward pass.
            MOZ_ASSERT(consumer->toDefinition()->wasmRefType().isNothing());
          }
        }
      }
    }
  }

  // Until the worklist is empty, update the uses of any worklist nodes and
  // track the ones whose types change.
  while (!worklist.empty()) {
    MDefinition* def = worklist.popCopy();

    for (MUseIterator use(def->usesBegin()); use != def->usesEnd(); use++) {
      if (!use->consumer()->isDefinition()) {
        continue;
      }
      bool changed = UpdateWasmRefType(use->consumer()->toDefinition());
      if (changed && !worklist.append(use->consumer()->toDefinition())) {
        return false;
      }
    }
  }

  return true;
}

static bool IsWasmRefTest(MDefinition* def) {
  return def->isWasmRefTestAbstract() || def->isWasmRefTestConcrete();
}

static bool IsWasmRefCast(MDefinition* def) {
  return def->isWasmRefCastAbstract() || def->isWasmRefCastConcrete() ||
         def->isWasmRefCastInfallible();
}

static MDefinition* WasmRefCastOrTestSourceRef(MDefinition* refTestOrCast) {
  switch (refTestOrCast->op()) {
    case MDefinition::Opcode::WasmRefCastAbstract:
      return refTestOrCast->toWasmRefCastAbstract()->ref();
    case MDefinition::Opcode::WasmRefCastConcrete:
      return refTestOrCast->toWasmRefCastConcrete()->ref();
    case MDefinition::Opcode::WasmRefCastInfallible:
      return refTestOrCast->toWasmRefCastInfallible()->ref();
    case MDefinition::Opcode::WasmRefTestAbstract:
      return refTestOrCast->toWasmRefTestAbstract()->ref();
    case MDefinition::Opcode::WasmRefTestConcrete:
      return refTestOrCast->toWasmRefTestConcrete()->ref();
    default:
      MOZ_CRASH();
  }
}

static wasm::RefType WasmRefTestOrCastDestType(MDefinition* refTestOrCast) {
  switch (refTestOrCast->op()) {
    case MDefinition::Opcode::WasmRefCastAbstract:
      return refTestOrCast->toWasmRefCastAbstract()->destType();
    case MDefinition::Opcode::WasmRefCastConcrete:
      return refTestOrCast->toWasmRefCastConcrete()->destType();
    case MDefinition::Opcode::WasmRefCastInfallible:
      return refTestOrCast->toWasmRefCastInfallible()->destType();
    case MDefinition::Opcode::WasmRefTestAbstract:
      return refTestOrCast->toWasmRefTestAbstract()->destType();
    case MDefinition::Opcode::WasmRefTestConcrete:
      return refTestOrCast->toWasmRefTestConcrete()->destType();
    default:
      MOZ_CRASH();
  }
}

static void TryOptimizeWasmCast(MDefinition* cast, MIRGraph& graph) {
  MDefinition* ref = WasmRefCastOrTestSourceRef(cast);

  if (ref->wasmRefType().isSome() &&
      !ref->wasmRefType().value().isInhabitable()) {
    return;
  }

  // Find all uses of the ref we are casting
  for (MUseIterator refUse(ref->usesBegin()); refUse != ref->usesEnd();
       refUse++) {
    // If the ref we are casting is used in a ref.test instruction...
    if (IsWasmRefTest(refUse->consumer()->toDefinition())) {
      MDefinition* refTest = refUse->consumer()->toDefinition();
      // And that ref.test instruction is used in an MTest instruction...
      for (MUseIterator testUse(refTest->usesBegin());
           testUse != refTest->usesEnd(); testUse++) {
        if (testUse->consumer()->toDefinition()->isTest()) {
          // And the MTest instruction true block dominates the block of
          // the cast...
          MTest* test = testUse->consumer()->toDefinition()->toTest();
          if (test->ifTrue()->dominates(cast->block())) {
            // And the type of the dominating ref.test is <: the type of
            // the current cast...
            wasm::RefType refTestDestType = WasmRefTestOrCastDestType(refTest);
            wasm::RefType refCastDestType = WasmRefTestOrCastDestType(cast);

            // (And neither type is uninhabitable...)
            if (!refTestDestType.isInhabitable() ||
                !refCastDestType.isInhabitable()) {
              continue;
            }

            if (wasm::RefType::isSubTypeOf(refTestDestType, refCastDestType)) {
              // Then the cast is redundant because it is dominated by a
              // tighter ref.test. Replace it with a dummy cast at the top of
              // the MTest's true block.
              if (!graph.alloc().ensureBallast()) {
                return;
              }
              auto* dummy = MWasmRefCastInfallible::New(graph.alloc(), ref,
                                                        refCastDestType);
              cast->replaceAllUsesWith(dummy);
              test->ifTrue()->insertBefore(test->ifTrue()->safeInsertTop(),
                                           dummy->toInstruction());
              cast->block()->discard(cast->toInstruction());
              return;
            }
          }
        }
      }
    }

    // If the ref we are casting is used in a different ref.cast instruction...
    if (IsWasmRefCast(refUse->consumer()->toDefinition()) &&
        refUse->consumer() != cast) {
      MDefinition* otherCast = refUse->consumer()->toDefinition();
      // And that ref.cast instruction dominates us...
      if (otherCast->dominates(cast)) {
        // And the type of the dominating ref.cast is <: the type of the
        // current cast...
        wasm::RefType dominatingDestType = WasmRefTestOrCastDestType(otherCast);
        wasm::RefType currentDestType = WasmRefTestOrCastDestType(cast);

        // (And neither type is uninhabitable...)
        if (!dominatingDestType.isInhabitable() ||
            !currentDestType.isInhabitable()) {
          continue;
        }

        if (wasm::RefType::isSubTypeOf(dominatingDestType, currentDestType)) {
          // Then the cast is redundant because it is dominated by a tighter
          // ref.cast. Discard the cast and fall back on the other.
          cast->replaceAllUsesWith(otherCast);
          cast->block()->discard(cast->toInstruction());
          return;
        }
      }
    }
  }
}

static void TryOptimizeWasmTest(MDefinition* refTest, MIRGraph& graph) {
  MDefinition* ref = WasmRefCastOrTestSourceRef(refTest);

  // Find all uses of the ref we are testing
  for (MUseIterator refUse(ref->usesBegin()); refUse != ref->usesEnd();
       refUse++) {
    // If the ref we are testing is used in a different ref.test instruction...
    if (IsWasmRefTest(refUse->consumer()->toDefinition()) &&
        refUse->consumer() != refTest) {
      MDefinition* otherRefTest = refUse->consumer()->toDefinition();
      // And that ref.test instruction is used in an MTest instruction...
      for (MUseIterator testUse(otherRefTest->usesBegin());
           testUse != otherRefTest->usesEnd(); testUse++) {
        if (testUse->consumer()->toDefinition()->isTest()) {
          MTest* test = testUse->consumer()->toDefinition()->toTest();

          wasm::RefType otherDestType = WasmRefTestOrCastDestType(otherRefTest);
          wasm::RefType currentDestType = WasmRefTestOrCastDestType(refTest);

          // (And neither type is uninhabitable...)
          if (!otherDestType.isInhabitable() ||
              !currentDestType.isInhabitable()) {
            continue;
          }

          MInstruction* replacement = nullptr;

          if (!graph.alloc().ensureBallast()) {
            return;
          }

          // And the MTest instruction true block dominates the block of the
          // current test...
          if (test->ifTrue()->dominates(refTest->block())) {
            // And the type of the DOMINATING ref.test is <: the type of the
            // CURRENT ref.test...
            if (wasm::RefType::isSubTypeOf(otherDestType, currentDestType)) {
              // Then the ref.test is redundant because it is dominated by the
              // success of a tighter ref.test. Replace it with a constant 1.
              replacement = MConstant::NewInt32(graph.alloc(), 1);
            }
          }

          // Or the MTest instruction false block dominates the block of the
          // current test...
          if (test->ifFalse()->dominates(refTest->block())) {
            // And the type of the CURRENT ref.test is <: the type of the
            // DOMINATING ref.test...
            if (wasm::RefType::isSubTypeOf(currentDestType, otherDestType)) {
              // Then the ref.test is redundant because it is dominated by the
              // failure of a looser ref.test. Replace it with a constant 0.
              replacement = MConstant::NewInt32(graph.alloc(), 0);
            }
          }

          if (replacement) {
            refTest->block()->insertBefore(refTest->toInstruction(),
                                           replacement);
            refTest->replaceAllUsesWith(replacement);
            refTest->block()->discard(refTest->toInstruction());
            return;
          }
        }
      }
    }

    // If the ref we are testing is used in a ref.cast instruction...
    if (IsWasmRefCast(refUse->consumer()->toDefinition())) {
      MDefinition* refCast = refUse->consumer()->toDefinition();
      // And that ref.cast instruction dominates us...
      if (refCast->dominates(refTest)) {
        // And the type of the dominating ref.cast is <: the type of the
        // current ref.test...
        wasm::RefType dominatingDestType = WasmRefTestOrCastDestType(refCast);
        wasm::RefType currentDestType = WasmRefTestOrCastDestType(refTest);

        // (And neither type is uninhabitable...)
        if (!dominatingDestType.isInhabitable() ||
            !currentDestType.isInhabitable()) {
          continue;
        }

        if (wasm::RefType::isSubTypeOf(dominatingDestType, currentDestType)) {
          // Then the ref.test is redundant because it is dominated by a
          // tighter ref.cast. Replace with a constant 1.
          if (!graph.alloc().ensureBallast()) {
            return;
          }
          auto* replacement = MConstant::NewInt32(graph.alloc(), 1);
          refTest->block()->insertBefore(refTest->toInstruction(), replacement);
          refTest->replaceAllUsesWith(replacement);
          refTest->block()->discard(refTest->toInstruction());
          return;
        }
      }
    }
  }
}

bool jit::OptimizeWasmCasts(MIRGraph& graph) {
  for (ReversePostorderIterator blockIter = graph.rpoBegin();
       blockIter != graph.rpoEnd(); blockIter++) {
    MBasicBlock* block = *blockIter;
    for (MDefinitionIterator def(block); def;) {
      MDefinition* castOrTest = *def;
      def++;

      if (IsWasmRefCast(castOrTest)) {
        TryOptimizeWasmCast(castOrTest, graph);
      } else if (IsWasmRefTest(castOrTest)) {
        TryOptimizeWasmTest(castOrTest, graph);
      }
    }
  }

  return true;
}
