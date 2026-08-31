/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/TypeAnalysis.h"

#include "jit/MIRGenerator.h"
#include "jit/MIRGraph.h"

#include "vm/BytecodeUtil-inl.h"

using namespace js;
using namespace js::jit;

using mozilla::DebugOnly;

namespace {

// The type analysis algorithm inserts conversions and box/unbox instructions
// to make the IR graph well-typed for future passes.
//
// Phi adjustment: If a phi's inputs are all the same type, the phi is
// specialized to return that type.
//
// Input adjustment: Each input is asked to apply conversion operations to its
// inputs. This may include Box, Unbox, or other instruction-specific type
// conversion operations.
//
class TypeAnalyzer {
  const MIRGenerator* mir;
  MIRGraph& graph;
  Vector<MPhi*, 0, SystemAllocPolicy> phiWorklist_;

  TempAllocator& alloc() const { return graph.alloc(); }

  bool addPhiToWorklist(MPhi* phi) {
    if (phi->isInWorklist()) {
      return true;
    }
    if (!phiWorklist_.append(phi)) {
      return false;
    }
    phi->setInWorklist();
    return true;
  }
  MPhi* popPhi() {
    MPhi* phi = phiWorklist_.popCopy();
    phi->setNotInWorklist();
    return phi;
  }

  [[nodiscard]] bool propagateAllPhiSpecializations();

  bool respecialize(MPhi* phi, MIRType type);
  bool propagateSpecialization(MPhi* phi);
  bool specializePhis();
  bool specializeOsrOnlyPhis();
  void replaceRedundantPhi(MPhi* phi);
  bool adjustPhiInputs(MPhi* phi);
  bool adjustInputs(MDefinition* def);
  bool insertConversions();

  bool checkFloatCoherency();
  bool graphContainsFloat32();
  bool markPhiConsumers();
  bool markPhiProducers();
  bool specializeValidFloatOps();
  bool tryEmitFloatOperations();
  bool propagateUnbox();

  bool shouldSpecializeOsrPhis() const;
  MIRType guessPhiType(MPhi* phi) const;

 public:
  TypeAnalyzer(const MIRGenerator* mir, MIRGraph& graph)
      : mir(mir), graph(graph) {}

  bool analyze();
};

} /* anonymous namespace */

bool TypeAnalyzer::shouldSpecializeOsrPhis() const {
  // [SMDOC] OSR Phi Type Specialization
  //
  // Without special handling for OSR phis, we end up with unspecialized phis
  // (MIRType::Value) in the loop (pre)header and other blocks, resulting in
  // unnecessary boxing and unboxing in the loop body.
  //
  // To fix this, phi type specialization needs special code to deal with the
  // OSR entry block. Recall that OSR results in the following basic block
  // structure:
  //
  //  +------------------+                 +-----------------+
  //  | Code before loop |                 | OSR entry block |
  //  +------------------+                 +-----------------+
  //          |                                       |
  //          |                                       |
  //          |           +---------------+           |
  //          +---------> | OSR preheader | <---------+
  //                      +---------------+
  //                              |
  //                              V
  //                      +---------------+
  //                      | Loop header   |<-----+
  //                      +---------------+      |
  //                              |              |
  //                             ...             |
  //                              |              |
  //                      +---------------+      |
  //                      | Loop backedge |------+
  //                      +---------------+
  //
  // OSR phi specialization happens in three steps:
  //
  // (1) Specialize phis but ignore MOsrValue phi inputs. In other words,
  //     pretend the OSR entry block doesn't exist. See guessPhiType.
  //
  // (2) Once phi specialization is done, look at the types of loop header phis
  //     and add these types to the corresponding preheader phis. This way, the
  //     types of the preheader phis are based on the code before the loop and
  //     the code in the loop body. These are exactly the types we expect for
  //     the OSR Values. See the last part of TypeAnalyzer::specializePhis.
  //
  // (3) For type-specialized preheader phis, add guard/unbox instructions to
  //     the OSR entry block to guard the incoming Value indeed has this type.
  //     This happens in:
  //
  //     * TypeAnalyzer::adjustPhiInputs: adds a fallible unbox for values that
  //       can be unboxed.
  //
  //     * TypeAnalyzer::replaceRedundantPhi: adds a type guard for values that
  //       can't be unboxed (null/undefined/magic Values).
  if (!graph.osrBlock()) {
    return false;
  }

  return !mir->outerInfo().hadSpeculativePhiBailout();
}

// Try to specialize this phi based on its non-cyclic inputs.
MIRType TypeAnalyzer::guessPhiType(MPhi* phi) const {
#ifdef DEBUG
  // Check that different magic constants aren't flowing together. Ignore
  // JS_OPTIMIZED_OUT, since an operand could be legitimately optimized
  // away.
  MIRType magicType = MIRType::None;
  for (size_t i = 0; i < phi->numOperands(); i++) {
    MDefinition* in = phi->getOperand(i);
    if (in->type() == MIRType::MagicHole ||
        in->type() == MIRType::MagicIsConstructing) {
      if (magicType == MIRType::None) {
        magicType = in->type();
      }
      MOZ_ASSERT(magicType == in->type());
    }
  }
#endif

  MIRType type = MIRType::None;
  bool convertibleToFloat32 = false;
  bool hasOSRValueInput = false;
  DebugOnly<bool> hasSpecializableInput = false;
  for (size_t i = 0, e = phi->numOperands(); i < e; i++) {
    MDefinition* in = phi->getOperand(i);
    if (in->isPhi()) {
      hasSpecializableInput = true;
      if (!in->toPhi()->triedToSpecialize()) {
        continue;
      }
      if (in->type() == MIRType::None) {
        // The operand is a phi we tried to specialize, but we were
        // unable to guess its type. propagateSpecialization will
        // propagate the type to this phi when it becomes known.
        continue;
      }
    }

    // See shouldSpecializeOsrPhis comment. This is the first step mentioned
    // there.
    if (shouldSpecializeOsrPhis() && in->isOsrValue()) {
      hasOSRValueInput = true;
      hasSpecializableInput = true;
      continue;
    }

    if (type == MIRType::None) {
      type = in->type();
      if (in->canProduceFloat32() &&
          !mir->outerInfo().hadSpeculativePhiBailout()) {
        convertibleToFloat32 = true;
      }
      continue;
    }

    if (type == in->type()) {
      convertibleToFloat32 = convertibleToFloat32 && in->canProduceFloat32();
    } else {
      if (convertibleToFloat32 && in->type() == MIRType::Float32) {
        // If we only saw definitions that can be converted into Float32 before
        // and encounter a Float32 value, promote previous values to Float32
        type = MIRType::Float32;
      } else if (IsTypeRepresentableAsDouble(type) &&
                 IsTypeRepresentableAsDouble(in->type())) {
        // Specialize phis with int32 and double operands as double.
        type = MIRType::Double;
        convertibleToFloat32 = convertibleToFloat32 && in->canProduceFloat32();
      } else {
        return MIRType::Value;
      }
    }
  }

  if (hasOSRValueInput && type == MIRType::Float32) {
    // TODO(post-Warp): simplify float32 handling in this function or (better)
    // make the float32 analysis a stand-alone optimization pass instead of
    // complicating type analysis. See bug 1655773.
    type = MIRType::Double;
  }

  MOZ_ASSERT_IF(type == MIRType::None, hasSpecializableInput);
  return type;
}

bool TypeAnalyzer::respecialize(MPhi* phi, MIRType type) {
  if (phi->type() == type) {
    return true;
  }
  phi->specialize(type);
  return addPhiToWorklist(phi);
}

bool TypeAnalyzer::propagateSpecialization(MPhi* phi) {
  MOZ_ASSERT(phi->type() != MIRType::None);

  // Verify that this specialization matches any phis depending on it.
  for (MUseDefIterator iter(phi); iter; iter++) {
    if (!iter.def()->isPhi()) {
      continue;
    }
    MPhi* use = iter.def()->toPhi();
    if (!use->triedToSpecialize()) {
      continue;
    }
    if (use->type() == MIRType::None) {
      // We tried to specialize this phi, but were unable to guess its
      // type. Now that we know the type of one of its operands, we can
      // specialize it. If it can't be specialized as float32, specialize
      // as double.
      MIRType type = phi->type();
      if (type == MIRType::Float32 && !use->canProduceFloat32()) {
        type = MIRType::Double;
      }
      if (!respecialize(use, type)) {
        return false;
      }
      continue;
    }
    if (use->type() != phi->type()) {
      // Specialize phis with int32 that can be converted to float and float
      // operands as floats.
      if ((use->type() == MIRType::Int32 && use->canProduceFloat32() &&
           phi->type() == MIRType::Float32) ||
          (phi->type() == MIRType::Int32 && phi->canProduceFloat32() &&
           use->type() == MIRType::Float32)) {
        if (!respecialize(use, MIRType::Float32)) {
          return false;
        }
        continue;
      }

      // Specialize phis with int32 and double operands as double.
      if (IsTypeRepresentableAsDouble(use->type()) &&
          IsTypeRepresentableAsDouble(phi->type())) {
        if (!respecialize(use, MIRType::Double)) {
          return false;
        }
        continue;
      }

      // This phi in our use chain can now no longer be specialized.
      if (!respecialize(use, MIRType::Value)) {
        return false;
      }
    }
  }

  return true;
}

bool TypeAnalyzer::propagateAllPhiSpecializations() {
  while (!phiWorklist_.empty()) {
    if (mir->shouldCancel("Specialize Phis (worklist)")) {
      return false;
    }

    MPhi* phi = popPhi();
    if (!propagateSpecialization(phi)) {
      return false;
    }
  }

  return true;
}

// If branch pruning removes the path from the entry block to the OSR
// preheader, we may have phis (or chains of phis) with no operands
// other than OsrValues. These phis will still have MIRType::None.
// Since we don't have any information about them, we specialize them
// as MIRType::Value.
bool TypeAnalyzer::specializeOsrOnlyPhis() {
  MOZ_ASSERT(graph.osrBlock());
  MOZ_ASSERT(graph.osrPreHeaderBlock()->numPredecessors() == 1);

  for (PostorderIterator block(graph.poBegin()); block != graph.poEnd();
       block++) {
    if (mir->shouldCancel("Specialize osr-only phis (main loop)")) {
      return false;
    }

    for (MPhiIterator phi(block->phisBegin()); phi != block->phisEnd(); phi++) {
      if (mir->shouldCancel("Specialize osr-only phis (inner loop)")) {
        return false;
      }

      if (phi->type() == MIRType::None) {
        phi->specialize(MIRType::Value);
      }
    }
  }
  return true;
}

bool TypeAnalyzer::specializePhis() {
  for (PostorderIterator block(graph.poBegin()); block != graph.poEnd();
       block++) {
    if (mir->shouldCancel("Specialize Phis (main loop)")) {
      return false;
    }

    for (MPhiIterator phi(block->phisBegin()); phi != block->phisEnd(); phi++) {
      if (mir->shouldCancel("Specialize Phis (inner loop)")) {
        return false;
      }

      MIRType type = guessPhiType(*phi);
      phi->specialize(type);
      if (type == MIRType::None) {
        // We tried to guess the type but failed because all operands are
        // phis we still have to visit. Set the triedToSpecialize flag but
        // don't propagate the type to other phis, propagateSpecialization
        // will do that once we know the type of one of the operands.
        continue;
      }
      if (!propagateSpecialization(*phi)) {
        return false;
      }
    }
  }

  if (!propagateAllPhiSpecializations()) {
    return false;
  }

  if (shouldSpecializeOsrPhis()) {
    // See shouldSpecializeOsrPhis comment. This is the second step, propagating
    // loop header phi types to preheader phis.
    MBasicBlock* preHeader = graph.osrPreHeaderBlock();
    MBasicBlock* header = preHeader->getSingleSuccessor();

    if (preHeader->numPredecessors() == 1) {
      MOZ_ASSERT(preHeader->getPredecessor(0) == graph.osrBlock());
      // Branch pruning has removed the path from the entry block
      // to the preheader. Specialize any phis with no non-osr inputs.
      if (!specializeOsrOnlyPhis()) {
        return false;
      }
    } else if (header->isLoopHeader()) {
      for (MPhiIterator phi(header->phisBegin()); phi != header->phisEnd();
           phi++) {
        MPhi* preHeaderPhi = phi->getOperand(0)->toPhi();
        MOZ_ASSERT(preHeaderPhi->block() == preHeader);

        if (preHeaderPhi->type() == MIRType::Value) {
          // Already includes everything.
          continue;
        }

        MIRType loopType = phi->type();
        if (!respecialize(preHeaderPhi, loopType)) {
          return false;
        }
      }
      if (!propagateAllPhiSpecializations()) {
        return false;
      }
    } else {
      // Edge case: there is no backedge in this loop. This can happen
      // if the header is a 'pending' loop header when control flow in
      // the loop body is terminated unconditionally, or if a block
      // that dominates the backedge unconditionally bails out. In
      // this case the header only has the preheader as predecessor
      // and we don't need to do anything.
      MOZ_ASSERT(header->numPredecessors() == 1);
    }
  }

  MOZ_ASSERT(phiWorklist_.empty());
  return true;
}

bool TypeAnalyzer::adjustPhiInputs(MPhi* phi) {
  MIRType phiType = phi->type();
  MOZ_ASSERT(phiType != MIRType::None);

  // If we specialized a type that's not Value, there are 3 cases:
  // 1. Every input is of that type.
  // 2. Every observed input is of that type (i.e., some inputs haven't been
  // executed yet).
  // 3. Inputs were numbers, and was specialized to floating point type.
  if (phiType != MIRType::Value) {
    for (size_t i = 0, e = phi->numOperands(); i < e; i++) {
      MDefinition* in = phi->getOperand(i);
      if (in->type() == phiType) {
        continue;
      }

      if (in->isBox() && in->toBox()->input()->type() == phiType) {
        phi->replaceOperand(i, in->toBox()->input());
        continue;
      }

      if (!alloc().ensureBallast()) {
        return false;
      }

      MBasicBlock* predecessor = phi->block()->getPredecessor(i);

      MInstruction* replacement;
      if (IsFloatingPointType(phiType) &&
          IsTypeRepresentableAsDouble(in->type())) {
        // Convert number operands to |phiType|.
        if (phiType == MIRType::Double) {
          replacement = MToDouble::New(alloc(), in);
        } else {
          MOZ_ASSERT(phiType == MIRType::Float32);
          replacement = MToFloat32::New(alloc(), in);
        }
      } else {
        // If we know this branch will fail to convert to phiType, insert a box
        // that'll immediately fail in the fallible unbox below.
        if (in->type() != MIRType::Value) {
          auto* box = MBox::New(alloc(), in);
          predecessor->insertAtEnd(box);
          in = box;
        }

        // Be optimistic and insert unboxes when the operand is a value.
        if (phiType == MIRType::Float32) {
          // Float32 is unboxed as Double, then converted.
          auto* unbox =
              MUnbox::New(alloc(), in, MIRType::Double, MUnbox::Fallible);
          unbox->setBailoutKind(BailoutKind::SpeculativePhi);
          predecessor->insertAtEnd(unbox);
          replacement = MToFloat32::New(alloc(), unbox);
        } else {
          replacement = MUnbox::New(alloc(), in, phiType, MUnbox::Fallible);
          replacement->setBailoutKind(BailoutKind::SpeculativePhi);
        }
      }
      MOZ_ASSERT(replacement->type() == phiType);

      predecessor->insertAtEnd(replacement);
      phi->replaceOperand(i, replacement);
    }

    return true;
  }

  // Box every typed input.
  for (size_t i = 0, e = phi->numOperands(); i < e; i++) {
    MDefinition* in = phi->getOperand(i);
    if (in->type() == MIRType::Value) {
      continue;
    }

    // The input is being explicitly unboxed, so sneak past and grab the
    // original box. Don't bother optimizing if magic values are involved.
    if (in->isUnbox()) {
      MDefinition* unboxInput = in->toUnbox()->input();
      if (!IsMagicType(unboxInput->type()) && phi->typeIncludes(unboxInput)) {
        in = in->toUnbox()->input();
      }
    }

    if (in->type() != MIRType::Value) {
      if (!alloc().ensureBallast()) {
        return false;
      }

      MBasicBlock* pred = phi->block()->getPredecessor(i);
      in = BoxAt(alloc(), pred->lastIns(), in);
    }

    phi->replaceOperand(i, in);
  }

  return true;
}

bool TypeAnalyzer::adjustInputs(MDefinition* def) {
  // Definitions such as MPhi have no type policy.
  if (!def->isInstruction()) {
    return true;
  }

  MInstruction* ins = def->toInstruction();
  const TypePolicy* policy = ins->typePolicy();
  if (policy && !policy->adjustInputs(alloc(), ins)) {
    return false;
  }
  return true;
}

void TypeAnalyzer::replaceRedundantPhi(MPhi* phi) {
  MBasicBlock* block = phi->block();
  js::Value v;
  switch (phi->type()) {
    case MIRType::Undefined:
      v = UndefinedValue();
      break;
    case MIRType::Null:
      v = NullValue();
      break;
    case MIRType::MagicOptimizedOut:
      v = MagicValue(JS_OPTIMIZED_OUT);
      break;
    case MIRType::MagicUninitializedLexical:
      v = MagicValue(JS_UNINITIALIZED_LEXICAL);
      break;
    case MIRType::MagicIsConstructing:
      v = MagicValue(JS_IS_CONSTRUCTING);
      break;
    case MIRType::MagicHole:
    default:
      MOZ_CRASH("unexpected type");
  }
  MConstant* c = MConstant::New(alloc(), v);
  // The instruction pass will insert the box
  block->insertBefore(*(block->begin()), c);
  phi->justReplaceAllUsesWith(c);

  if (shouldSpecializeOsrPhis()) {
    // See shouldSpecializeOsrPhis comment. This is part of the third step,
    // guard the incoming MOsrValue is of this type.
    for (uint32_t i = 0; i < phi->numOperands(); i++) {
      MDefinition* def = phi->getOperand(i);
      if (def->type() != phi->type()) {
        MOZ_ASSERT(def->isOsrValue() || def->isPhi());
        MOZ_ASSERT(def->type() == MIRType::Value);
        MGuardValue* guard = MGuardValue::New(alloc(), def, v);
        guard->setBailoutKind(BailoutKind::SpeculativePhi);
        def->block()->insertBefore(def->block()->lastIns(), guard);
      }
    }
  }
}

bool TypeAnalyzer::insertConversions() {
  // Instructions are processed in reverse postorder: all uses are defs are
  // seen before uses. This ensures that output adjustment (which may rewrite
  // inputs of uses) does not conflict with input adjustment.
  for (ReversePostorderIterator block(graph.rpoBegin());
       block != graph.rpoEnd(); block++) {
    if (mir->shouldCancel("Insert Conversions")) {
      return false;
    }

    for (MPhiIterator iter(block->phisBegin()), end(block->phisEnd());
         iter != end;) {
      MPhi* phi = *iter++;
      if (IsNullOrUndefined(phi->type()) || IsMagicType(phi->type())) {
        // We can replace this phi with a constant.
        if (!alloc().ensureBallast()) {
          return false;
        }
        replaceRedundantPhi(phi);
        block->discardPhi(phi);
      } else {
        if (!adjustPhiInputs(phi)) {
          return false;
        }
      }
    }

    // AdjustInputs can add/remove/mutate instructions before and after the
    // current instruction. Only increment the iterator after it is finished.
    for (MInstructionIterator iter(block->begin()); iter != block->end();
         iter++) {
      if (!alloc().ensureBallast()) {
        return false;
      }

      if (!adjustInputs(*iter)) {
        return false;
      }
    }
  }
  return true;
}

/* clang-format off */
//
// This function tries to emit Float32 specialized operations whenever it's possible.
// MIR nodes are flagged as:
// - Producers, when they can create Float32 that might need to be coerced into a Double.
//   Loads in Float32 arrays and conversions to Float32 are producers.
// - Consumers, when they can have Float32 as inputs and validate a legal use of a Float32.
//   Stores in Float32 arrays and conversions to Float32 are consumers.
// - Float32 commutative, when using the Float32 instruction instead of the Double instruction
//   does not result in a compound loss of precision. This is the case for +, -, /, * with 2
//   operands, for instance. However, an addition with 3 operands is not commutative anymore,
//   so an intermediate coercion is needed.
// Except for phis, all these flags are known after Ion building, so they cannot change during
// the process.
//
// The idea behind the algorithm is easy: whenever we can prove that a commutative operation
// has only producers as inputs and consumers as uses, we can specialize the operation as a
// float32 operation. Otherwise, we have to convert all float32 inputs to doubles. Even
// if a lot of conversions are produced, GVN will take care of eliminating the redundant ones.
//
// Phis have a special status. Phis need to be flagged as producers or consumers as they can
// be inputs or outputs of commutative instructions. Fortunately, producers and consumers
// properties are such that we can deduce the property using all non phis inputs first (which form
// an initial phi graph) and then propagate all properties from one phi to another using a
// fixed point algorithm. The algorithm is ensured to terminate as each iteration has less or as
// many flagged phis as the previous iteration (so the worst steady state case is all phis being
// flagged as false).
//
// In a nutshell, the algorithm applies three passes:
// 1 - Determine which phis are consumers. Each phi gets an initial value by making a global AND on
// all its non-phi inputs. Then each phi propagates its value to other phis. If after propagation,
// the flag value changed, we have to reapply the algorithm on all phi operands, as a phi is a
// consumer if all of its uses are consumers.
// 2 - Determine which phis are producers. It's the same algorithm, except that we have to reapply
// the algorithm on all phi uses, as a phi is a producer if all of its operands are producers.
// 3 - Go through all commutative operations and ensure their inputs are all producers and their
// uses are all consumers.
//
/* clang-format on */
bool TypeAnalyzer::markPhiConsumers() {
  MOZ_ASSERT(phiWorklist_.empty());

  // Iterate in postorder so worklist is initialized to RPO.
  for (PostorderIterator block(graph.poBegin()); block != graph.poEnd();
       ++block) {
    if (mir->shouldCancel(
            "Ensure Float32 commutativity - Consumer Phis - Initial state")) {
      return false;
    }

    for (MPhiIterator phi(block->phisBegin()); phi != block->phisEnd(); ++phi) {
      MOZ_ASSERT(!phi->isInWorklist());
      bool canConsumeFloat32 = !phi->isImplicitlyUsed();
      for (MUseDefIterator use(*phi); canConsumeFloat32 && use; use++) {
        MDefinition* usedef = use.def();
        canConsumeFloat32 &=
            usedef->isPhi() || usedef->canConsumeFloat32(use.use());
      }
      phi->setCanConsumeFloat32(canConsumeFloat32);
      if (canConsumeFloat32 && !addPhiToWorklist(*phi)) {
        return false;
      }
    }
  }

  while (!phiWorklist_.empty()) {
    if (mir->shouldCancel(
            "Ensure Float32 commutativity - Consumer Phis - Fixed point")) {
      return false;
    }

    MPhi* phi = popPhi();
    MOZ_ASSERT(phi->canConsumeFloat32(nullptr /* unused */));

    bool validConsumer = true;
    for (MUseDefIterator use(phi); use; use++) {
      MDefinition* def = use.def();
      if (def->isPhi() && !def->canConsumeFloat32(use.use())) {
        validConsumer = false;
        break;
      }
    }

    if (validConsumer) {
      continue;
    }

    // Propagate invalidated phis
    phi->setCanConsumeFloat32(false);
    for (size_t i = 0, e = phi->numOperands(); i < e; ++i) {
      MDefinition* input = phi->getOperand(i);
      if (input->isPhi() && !input->isInWorklist() &&
          input->canConsumeFloat32(nullptr /* unused */)) {
        if (!addPhiToWorklist(input->toPhi())) {
          return false;
        }
      }
    }
  }
  return true;
}

bool TypeAnalyzer::markPhiProducers() {
  MOZ_ASSERT(phiWorklist_.empty());

  // Iterate in reverse postorder so worklist is initialized to PO.
  for (ReversePostorderIterator block(graph.rpoBegin());
       block != graph.rpoEnd(); ++block) {
    if (mir->shouldCancel(
            "Ensure Float32 commutativity - Producer Phis - initial state")) {
      return false;
    }

    for (MPhiIterator phi(block->phisBegin()); phi != block->phisEnd(); ++phi) {
      MOZ_ASSERT(!phi->isInWorklist());
      bool canProduceFloat32 = true;
      for (size_t i = 0, e = phi->numOperands(); canProduceFloat32 && i < e;
           ++i) {
        MDefinition* input = phi->getOperand(i);
        canProduceFloat32 &= input->isPhi() || input->canProduceFloat32();
      }
      phi->setCanProduceFloat32(canProduceFloat32);
      if (canProduceFloat32 && !addPhiToWorklist(*phi)) {
        return false;
      }
    }
  }

  while (!phiWorklist_.empty()) {
    if (mir->shouldCancel(
            "Ensure Float32 commutativity - Producer Phis - Fixed point")) {
      return false;
    }

    MPhi* phi = popPhi();
    MOZ_ASSERT(phi->canProduceFloat32());

    bool validProducer = true;
    for (size_t i = 0, e = phi->numOperands(); i < e; ++i) {
      MDefinition* input = phi->getOperand(i);
      if (input->isPhi() && !input->canProduceFloat32()) {
        validProducer = false;
        break;
      }
    }

    if (validProducer) {
      continue;
    }

    // Propagate invalidated phis
    phi->setCanProduceFloat32(false);
    for (MUseDefIterator use(phi); use; use++) {
      MDefinition* def = use.def();
      if (def->isPhi() && !def->isInWorklist() && def->canProduceFloat32()) {
        if (!addPhiToWorklist(def->toPhi())) {
          return false;
        }
      }
    }
  }
  return true;
}

bool TypeAnalyzer::specializeValidFloatOps() {
  for (ReversePostorderIterator block(graph.rpoBegin());
       block != graph.rpoEnd(); ++block) {
    if (mir->shouldCancel("Ensure Float32 commutativity - Instructions")) {
      return false;
    }

    for (MInstructionIterator ins(block->begin()); ins != block->end(); ++ins) {
      if (!ins->isFloat32Commutative()) {
        continue;
      }

      if (ins->type() == MIRType::Float32) {
        continue;
      }

      if (!alloc().ensureBallast()) {
        return false;
      }

      // This call will try to specialize the instruction iff all uses are
      // consumers and all inputs are producers.
      ins->trySpecializeFloat32(alloc());
    }
  }
  return true;
}

bool TypeAnalyzer::graphContainsFloat32() {
  for (ReversePostorderIterator block(graph.rpoBegin());
       block != graph.rpoEnd(); ++block) {
    for (MDefinitionIterator def(*block); def; def++) {
      if (mir->shouldCancel(
              "Ensure Float32 commutativity - Graph contains Float32")) {
        return false;
      }

      if (def->type() == MIRType::Float32) {
        return true;
      }
    }
  }
  return false;
}

bool TypeAnalyzer::tryEmitFloatOperations() {
  // Asm.js uses the ahead of time type checks to specialize operations, no need
  // to check them again at this point.
  if (mir->compilingWasm()) {
    return true;
  }

  // Check ahead of time that there is at least one definition typed as Float32,
  // otherwise we don't need this pass.
  if (!graphContainsFloat32()) {
    return true;
  }

  // WarpBuilder skips over code that can't be reached except through
  // a catch block. Locals and arguments may be observable in such
  // code after bailing out, so we can't rely on seeing all uses.
  if (graph.hasTryBlock()) {
    return true;
  }

  if (!markPhiConsumers()) {
    return false;
  }
  if (!markPhiProducers()) {
    return false;
  }
  if (!specializeValidFloatOps()) {
    return false;
  }
  return true;
}

bool TypeAnalyzer::checkFloatCoherency() {
#ifdef DEBUG
  // Asserts that all Float32 instructions are flowing into Float32 consumers or
  // specialized operations
  for (ReversePostorderIterator block(graph.rpoBegin());
       block != graph.rpoEnd(); ++block) {
    if (mir->shouldCancel("Check Float32 coherency")) {
      return false;
    }

    for (MDefinitionIterator def(*block); def; def++) {
      if (def->type() != MIRType::Float32) {
        continue;
      }

      for (MUseDefIterator use(*def); use; use++) {
        MDefinition* consumer = use.def();
        MOZ_ASSERT(consumer->isConsistentFloat32Use(use.use()));
      }
    }
  }
#endif
  return true;
}

static bool HappensBefore(const MDefinition* earlier,
                          const MDefinition* later) {
  MOZ_ASSERT(earlier->block() == later->block());

  for (auto* ins : *earlier->block()) {
    if (ins == earlier) {
      return true;
    }
    if (ins == later) {
      return false;
    }
  }
  MOZ_CRASH("earlier and later are instructions in the block");
}

// Propagate type information from dominating unbox instructions.
//
// This optimization applies for example for self-hosted String.prototype
// functions.
//
// Example:
// ```
// String.prototype.id = function() {
//   // Strict mode to avoid ToObject on primitive this-values.
//   "use strict";
//
//   // Template string to apply ToString on the this-value.
//   return `${this}`;
// };
//
// function f(s) {
//   // Assume |s| is a string value.
//   return s.id();
// }
// ```
//
// Compiles into: (Graph after Scalar Replacement)
//
// ┌───────────────────────────────────────────────────────────────────────────┐
// │                             Block 0                                       │
// │ resumepoint 1 0 2 2                                                       │
// │ 0 parameter THIS_SLOT                                           Value     │
// │ 1 parameter 0                                                   Value     │
// │ 2 constant undefined                                            Undefined │
// │ 3 start                                                                   │
// │ 4 checkoverrecursed                                                       │
// │ 5 unbox parameter1 to String (fallible)                         String    │
// │ 6 constant object 1d908053e088 (String)                         Object    │
// │ 7 guardshape constant6:Object                                   Object    │
// │ 8 slots guardshape7:Object                                      Slots     │
// │ 9 loaddynamicslot slots8:Slots (slot 53)                        Value     │
// │ 10 constant 0x0                                                 Int32     │
// │ 11 unbox loaddynamicslot9 to Object (fallible)                  Object    │
// │ 12 nurseryobject                                                Object    │
// │ 13 guardspecificfunction unbox11:Object nurseryobject12:Object  Object    │
// │ 14 goto block1                                                            │
// └──────────────────────────────────┬────────────────────────────────────────┘
//                                    │
// ┌──────────────────────────────────▼────────────────────────────────────────┐
// │                               Block 1                                     │
// │ ((0)) resumepoint 15 1 15 15 | 1 13 1 0 2 2                               │
// │ 15 constant undefined                                           Undefined │
// │ 16 tostring parameter1:Value                                    String    │
// │ 18 goto block2                                                            │
// └──────────────────────────────────┬────────────────────────────────────────┘
//                                    │
//                     ┌──────────────▼──────────────┐
//                     │           Block 2           │
//                     │ resumepoint 16 1 0 2 2      │
//                     │ 19 return tostring16:String │
//                     └─────────────────────────────┘
//
// The Unbox instruction is used as a type guard. The ToString instruction
// doesn't use the type information from the preceding Unbox instruction and
// therefore has to assume its operand can be any value.
//
// When instead propagating the type information from the preceding Unbox
// instruction, this graph is constructed after the "Apply types" phase:
//
// ┌───────────────────────────────────────────────────────────────────────────┐
// │                             Block 0                                       │
// │ resumepoint 1 0 2 2                                                       │
// │ 0 parameter THIS_SLOT                                           Value     │
// │ 1 parameter 0                                                   Value     │
// │ 2 constant undefined                                            Undefined │
// │ 3 start                                                                   │
// │ 4 checkoverrecursed                                                       │
// │ 5 unbox parameter1 to String (fallible)                         String    │
// │ 6 constant object 1d908053e088 (String)                         Object    │
// │ 7 guardshape constant6:Object                                   Object    │
// │ 8 slots guardshape7:Object                                      Slots     │
// │ 9 loaddynamicslot slots8:Slots (slot 53)                        Value     │
// │ 10 constant 0x0                                                 Int32     │
// │ 11 unbox loaddynamicslot9 to Object (fallible)                  Object    │
// │ 12 nurseryobject                                                Object    │
// │ 13 guardspecificfunction unbox11:Object nurseryobject12:Object  Object    │
// │ 14 goto block1                                                            │
// └──────────────────────────────────┬────────────────────────────────────────┘
//                                    │
// ┌──────────────────────────────────▼────────────────────────────────────────┐
// │                               Block 1                                     │
// │ ((0)) resumepoint 15 1 15 15 | 1 13 1 0 2 2                               │
// │ 15 constant undefined                                           Undefined │
// │ 20 unbox parameter1 to String (fallible)                        String    │
// │ 16 tostring parameter1:Value                                    String    │
// │ 18 goto block2                                                            │
// └──────────────────────────────────┬────────────────────────────────────────┘
//                                    │
//                     ┌──────────────▼─────────────────────┐
//                     │           Block 2                  │
//                     │ resumepoint 16 1 0 2 2             │
//                     │ 21 box tostring16:String     Value │
//                     │ 19 return box21:Value              │
//                     └────────────────────────────────────┘
//
// GVN will later merge both Unbox instructions and fold away the ToString
// instruction, so we get this final graph:
//
// ┌───────────────────────────────────────────────────────────────────────────┐
// │                             Block 0                                       │
// │ resumepoint 1 0 2 2                                                       │
// │ 0 parameter THIS_SLOT                                           Value     │
// │ 1 parameter 0                                                   Value     │
// │ 2 constant undefined                                            Undefined │
// │ 3 start                                                                   │
// │ 4 checkoverrecursed                                                       │
// │ 5 unbox parameter1 to String (fallible)                         String    │
// │ 6 constant object 1d908053e088 (String)                         Object    │
// │ 7 guardshape constant6:Object                                   Object    │
// │ 8 slots guardshape7:Object                                      Slots     │
// │ 22 loaddynamicslotandunbox slots8:Slots (slot 53)               Object    │
// │ 11 nurseryobject                                                Object    │
// │ 12 guardspecificfunction load22:Object nurseryobject11:Object   Object    │
// │ 13 goto block1                                                            │
// └──────────────────────────────────┬────────────────────────────────────────┘
//                                    │
// ┌──────────────────────────────────▼────────────────────────────────────────┐
// │                               Block 1                                     │
// │ ((0)) resumepoint 2 1 2 2 | 1 12 1 0 2 2                                  │
// │ 14 goto block2                                                            │
// └──────────────────────────────────┬────────────────────────────────────────┘
//                                    │
//                     ┌──────────────▼─────────────────────┐
//                     │           Block 2                  │
//                     │ resumepoint 5 1 0 2 2              │
//                     │ 15 return parameter1:Value         │
//                     └────────────────────────────────────┘
//
bool TypeAnalyzer::propagateUnbox() {
  // Visit the blocks in post-order, so that the type information of the closest
  // unbox operation is used.
  for (PostorderIterator block(graph.poBegin()); block != graph.poEnd();
       block++) {
    if (mir->shouldCancel("Propagate Unbox")) {
      return false;
    }

    // Iterate over all instructions to look for unbox instructions.
    for (MInstructionIterator iter(block->begin()); iter != block->end();
         iter++) {
      if (!iter->isUnbox()) {
        continue;
      }

      auto* unbox = iter->toUnbox();
      auto* input = unbox->input();

      // Ignore unbox operations on typed values.
      if (input->type() != MIRType::Value) {
        continue;
      }

      // Ignore unbox to floating point types, because propagating boxed Int32
      // values as Double can lead to repeated bailouts when later instructions
      // expect Int32 inputs.
      if (IsFloatingPointType(unbox->type())) {
        continue;
      }

      // Inspect other uses of |input| to propagate the unboxed type information
      // from |unbox|.
      for (auto uses = input->usesBegin(); uses != input->usesEnd();) {
        auto* use = *uses++;

        // Ignore resume points.
        if (!use->consumer()->isDefinition()) {
          continue;
        }
        auto* def = use->consumer()->toDefinition();

        // Ignore any unbox operations, including the current |unbox|.
        if (def->isUnbox()) {
          continue;
        }

        // Ignore phi nodes, because we don't yet support them.
        if (def->isPhi()) {
          continue;
        }

        // The unbox operation needs to happen before the other use, otherwise
        // we can't propagate the type information.
        if (unbox->block() == def->block()) {
          if (!HappensBefore(unbox, def)) {
            continue;
          }
        } else {
          if (!unbox->block()->dominates(def->block())) {
            continue;
          }
        }

        // Replace the use with |unbox|, so that GVN knows about the actual
        // value type and can more easily fold unnecessary operations. If the
        // instruction actually needs a boxed input, the BoxPolicy type policy
        // will simply unwrap the unbox instruction.
        use->replaceProducer(unbox);

        // The uses in the MIR graph no longer reflect the uses in the bytecode,
        // so we have to mark |input| as implicitly used.
        input->setImplicitlyUsedUnchecked();
      }
    }
  }
  return true;
}

bool TypeAnalyzer::analyze() {
  if (!tryEmitFloatOperations()) {
    return false;
  }
  if (!specializePhis()) {
    return false;
  }
  if (!propagateUnbox()) {
    return false;
  }
  if (!insertConversions()) {
    return false;
  }
  if (!checkFloatCoherency()) {
    return false;
  }
  return true;
}

bool jit::ApplyTypeInformation(const MIRGenerator* mir, MIRGraph& graph) {
  TypeAnalyzer analyzer(mir, graph);

  if (!analyzer.analyze()) {
    return false;
  }

  return true;
}
