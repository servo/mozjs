/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/BranchPruning.h"

#include <utility>  // for ::std::pair

#include "jit/IonAnalysis.h"
#include "jit/MIRGenerator.h"
#include "jit/MIRGraph.h"

using namespace js;
using namespace js::jit;

// Stack used by FlagPhiInputsAsImplicitlyUsed. It stores the Phi instruction
// pointer and the MUseIterator which should be visited next.
using MPhiUseIteratorStack =
    Vector<std::pair<MPhi*, MUseIterator>, 16, SystemAllocPolicy>;

// Look for Phi uses with a depth-first search. If any uses are found the stack
// of MPhi instructions is returned in the |worklist| argument.
[[nodiscard]] static bool DepthFirstSearchUse(const MIRGenerator* mir,
                                              MPhiUseIteratorStack& worklist,
                                              MPhi* phi) {
  // Push a Phi and the next use to iterate over in the worklist.
  auto push = [&worklist](MPhi* phi, MUseIterator use) -> bool {
    phi->setInWorklist();
    return worklist.append(std::make_pair(phi, use));
  };

#ifdef DEBUG
  // Used to assert that when we have no uses, we at least visited all the
  // transitive uses.
  size_t refUseCount = phi->useCount();
  size_t useCount = 0;
#endif
  MOZ_ASSERT(worklist.empty());
  if (!push(phi, phi->usesBegin())) {
    return false;
  }

  while (!worklist.empty()) {
    // Resume iterating over the last phi-use pair added by the next loop.
    auto pair = worklist.popCopy();
    MPhi* producer = pair.first;
    MUseIterator use = pair.second;
    MUseIterator end(producer->usesEnd());
    producer->setNotInWorklist();

    // Keep going down the tree of uses, skipping (continue)
    // non-observable/unused cases and Phi which are already listed in the
    // worklist. Stop (return) as soon as one use is found.
    while (use != end) {
      MNode* consumer = (*use)->consumer();
      MUseIterator it = use;
      use++;
#ifdef DEBUG
      useCount++;
#endif
      if (mir->shouldCancel("FlagPhiInputsAsImplicitlyUsed inner loop")) {
        return false;
      }

      if (consumer->isResumePoint()) {
        MResumePoint* rp = consumer->toResumePoint();
        // Observable operands are similar to potential uses.
        if (rp->isObservableOperand(*it)) {
          return push(producer, use);
        }
        continue;
      }

      MDefinition* cdef = consumer->toDefinition();
      if (!cdef->isPhi()) {
        // The producer is explicitly used by a definition.
        return push(producer, use);
      }

      MPhi* cphi = cdef->toPhi();
      if (cphi->getUsageAnalysis() == PhiUsage::Used ||
          cphi->isImplicitlyUsed()) {
        // The information got cached on the Phi the last time it
        // got visited, or when flagging operands of implicitly used
        // instructions.
        return push(producer, use);
      }

      if (cphi->isInWorklist() || cphi == producer) {
        // We are already iterating over the uses of this Phi instruction which
        // are part of a loop, instead of trying to handle loops, conservatively
        // mark them as used.
        return push(producer, use);
      }

      if (cphi->getUsageAnalysis() == PhiUsage::Unused) {
        // The instruction already got visited and is known to have
        // no uses. Skip it.
        continue;
      }

      // We found another Phi instruction, move the use iterator to
      // the next use push it to the worklist stack. Then, continue
      // with a depth search.
      if (!push(producer, use)) {
        return false;
      }
      producer = cphi;
      use = producer->usesBegin();
      end = producer->usesEnd();
#ifdef DEBUG
      refUseCount += producer->useCount();
#endif
    }

    // When unused, we cannot bubble up this information without iterating
    // over the rest of the previous Phi instruction consumers.
    MOZ_ASSERT(use == end);
    producer->setUsageAnalysis(PhiUsage::Unused);
  }

  MOZ_ASSERT(useCount == refUseCount);
  return true;
}

[[nodiscard]] static bool FlagPhiInputsAsImplicitlyUsed(
    const MIRGenerator* mir, MBasicBlock* block, MBasicBlock* succ,
    MPhiUseIteratorStack& worklist) {
  // When removing an edge between 2 blocks, we might remove the ability of
  // later phases to figure out that the uses of a Phi should be considered as
  // a use of all its inputs. Thus we need to mark the Phi inputs as being
  // implicitly used iff the phi has any uses.
  //
  //
  //        +--------------------+         +---------------------+
  //        |12 MFoo 6           |         |32 MBar 5            |
  //        |                    |         |                     |
  //        |   ...              |         |   ...               |
  //        |                    |         |                     |
  //        |25 MGoto Block 4    |         |43 MGoto Block 4     |
  //        +--------------------+         +---------------------+
  //                   |                              |
  //             |     |                              |
  //             |     |                              |
  //             |     +-----X------------------------+
  //             |         Edge       |
  //             |        Removed     |
  //             |                    |
  //             |       +------------v-----------+
  //             |       |50 MPhi 12 32           |
  //             |       |                        |
  //             |       |   ...                  |
  //             |       |                        |
  //             |       |70 MReturn 50           |
  //             |       +------------------------+
  //             |
  //   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  //             |
  //             v
  //
  //    ^   +--------------------+         +---------------------+
  //   /!\  |12 MConst opt-out   |         |32 MBar 5            |
  //  '---' |                    |         |                     |
  //        |   ...              |         |   ...               |
  //        |78 MBail            |         |                     |
  //        |80 MUnreachable     |         |43 MGoto Block 4     |
  //        +--------------------+         +---------------------+
  //                                                  |
  //                                                  |
  //                                                  |
  //                                  +---------------+
  //                                  |
  //                                  |
  //                                  |
  //                     +------------v-----------+
  //                     |50 MPhi 32              |
  //                     |                        |
  //                     |   ...                  |
  //                     |                        |
  //                     |70 MReturn 50           |
  //                     +------------------------+
  //
  //
  // If the inputs of the Phi are not flagged as implicitly used, then
  // later compilation phase might optimize them out. The problem is that a
  // bailout will use this value and give it back to baseline, which will then
  // use the OptimizedOut magic value in a computation.
  //
  // Unfortunately, we cannot be too conservative about flagging Phi inputs as
  // having implicit uses, as this would prevent many optimizations from being
  // used. Thus, the following code is in charge of flagging Phi instructions
  // as Unused or Used, and setting ImplicitlyUsed accordingly.
  size_t predIndex = succ->getPredecessorIndex(block);
  MPhiIterator end = succ->phisEnd();
  MPhiIterator it = succ->phisBegin();
  for (; it != end; it++) {
    MPhi* phi = *it;

    if (mir->shouldCancel("FlagPhiInputsAsImplicitlyUsed outer loop")) {
      return false;
    }

    // We are looking to mark the Phi inputs which are used across the edge
    // between the |block| and its successor |succ|.
    MDefinition* def = phi->getOperand(predIndex);
    if (def->isImplicitlyUsed()) {
      continue;
    }

    // If the Phi is either Used or Unused, set the ImplicitlyUsed flag
    // accordingly.
    if (phi->getUsageAnalysis() == PhiUsage::Used || phi->isImplicitlyUsed()) {
      def->setImplicitlyUsedUnchecked();
      continue;
    } else if (phi->getUsageAnalysis() == PhiUsage::Unused) {
      continue;
    }

    // We do not know if the Phi was Used or Unused, iterate over all uses
    // with a depth-search of uses. Returns the matching stack in the
    // worklist as soon as one use is found.
    MOZ_ASSERT(worklist.empty());
    if (!DepthFirstSearchUse(mir, worklist, phi)) {
      return false;
    }

    MOZ_ASSERT_IF(worklist.empty(),
                  phi->getUsageAnalysis() == PhiUsage::Unused);
    if (!worklist.empty()) {
      // One of the Phis is used, set Used flags on all the Phis which are
      // in the use chain.
      def->setImplicitlyUsedUnchecked();
      do {
        auto pair = worklist.popCopy();
        MPhi* producer = pair.first;
        producer->setUsageAnalysis(PhiUsage::Used);
        producer->setNotInWorklist();
      } while (!worklist.empty());
    }
    MOZ_ASSERT(phi->getUsageAnalysis() != PhiUsage::Unknown);
  }

  return true;
}

static MInstructionIterator FindFirstInstructionAfterBail(MBasicBlock* block) {
  MOZ_ASSERT(block->alwaysBails());
  for (MInstructionIterator it = block->begin(); it != block->end(); it++) {
    MInstruction* ins = *it;
    if (ins->isBail()) {
      it++;
      return it;
    }
  }
  MOZ_CRASH("Expected MBail in alwaysBails block");
}

// Given an iterator pointing to the first removed instruction, mark
// the operands of each removed instruction as having implicit uses.
[[nodiscard]] static bool FlagOperandsAsImplicitlyUsedAfter(
    const MIRGenerator* mir, MBasicBlock* block,
    MInstructionIterator firstRemoved) {
  MOZ_ASSERT(firstRemoved->block() == block);

  const CompileInfo& info = block->info();

  // Flag operands of removed instructions as having implicit uses.
  MInstructionIterator end = block->end();
  for (MInstructionIterator it = firstRemoved; it != end; it++) {
    if (mir->shouldCancel("FlagOperandsAsImplicitlyUsedAfter (loop 1)")) {
      return false;
    }

    MInstruction* ins = *it;
    for (size_t i = 0, e = ins->numOperands(); i < e; i++) {
      ins->getOperand(i)->setImplicitlyUsedUnchecked();
    }

    // Flag observable resume point operands as having implicit uses.
    if (MResumePoint* rp = ins->resumePoint()) {
      // Note: no need to iterate over the caller's of the resume point as
      // this is the same as the entry resume point.
      MOZ_ASSERT(&rp->block()->info() == &info);
      for (size_t i = 0, e = rp->numOperands(); i < e; i++) {
        if (info.isObservableSlot(i)) {
          rp->getOperand(i)->setImplicitlyUsedUnchecked();
        }
      }
    }
  }

  // Flag Phi inputs of the successors as having implicit uses.
  MPhiUseIteratorStack worklist;
  for (size_t i = 0, e = block->numSuccessors(); i < e; i++) {
    if (mir->shouldCancel("FlagOperandsAsImplicitlyUsedAfter (loop 2)")) {
      return false;
    }

    if (!FlagPhiInputsAsImplicitlyUsed(mir, block, block->getSuccessor(i),
                                       worklist)) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] static bool FlagEntryResumePointOperands(const MIRGenerator* mir,
                                                       MBasicBlock* block) {
  // Flag observable operands of the entry resume point as having implicit uses.
  MResumePoint* rp = block->entryResumePoint();
  while (rp) {
    if (mir->shouldCancel("FlagEntryResumePointOperands")) {
      return false;
    }

    const CompileInfo& info = rp->block()->info();
    for (size_t i = 0, e = rp->numOperands(); i < e; i++) {
      if (info.isObservableSlot(i)) {
        rp->getOperand(i)->setImplicitlyUsedUnchecked();
      }
    }

    rp = rp->caller();
  }

  return true;
}

[[nodiscard]] static bool FlagAllOperandsAsImplicitlyUsed(
    const MIRGenerator* mir, MBasicBlock* block) {
  return FlagEntryResumePointOperands(mir, block) &&
         FlagOperandsAsImplicitlyUsedAfter(mir, block, block->begin());
}

// WarpBuilder sets the alwaysBails flag on blocks that contain an
// unconditional bailout. We trim any instructions in those blocks
// after the first unconditional bailout, and remove any blocks that
// are only reachable through bailing blocks.
bool jit::PruneUnusedBranches(const MIRGenerator* mir, MIRGraph& graph) {
  JitSpew(JitSpew_Prune, "Begin");

  // Pruning is guided by unconditional bailouts. Wasm does not have bailouts.
  MOZ_ASSERT(!mir->compilingWasm());

  Vector<MBasicBlock*, 16, SystemAllocPolicy> worklist;
  uint32_t numMarked = 0;
  bool needsTrim = false;

  auto markReachable = [&](MBasicBlock* block) -> bool {
    block->mark();
    numMarked++;
    if (block->alwaysBails()) {
      needsTrim = true;
    }
    return worklist.append(block);
  };

  // The entry block is always reachable.
  if (!markReachable(graph.entryBlock())) {
    return false;
  }

  // The OSR entry block is always reachable if it exists.
  if (graph.osrBlock() && !markReachable(graph.osrBlock())) {
    return false;
  }

  // Iteratively mark all reachable blocks.
  while (!worklist.empty()) {
    if (mir->shouldCancel("Prune unused branches (marking reachable)")) {
      return false;
    }
    MBasicBlock* block = worklist.popCopy();

    JitSpew(JitSpew_Prune, "Visit block %u:", block->id());
    JitSpewIndent indent(JitSpew_Prune);

    // If this block always bails, then it does not reach its successors.
    if (block->alwaysBails()) {
      continue;
    }

    for (size_t i = 0; i < block->numSuccessors(); i++) {
      MBasicBlock* succ = block->getSuccessor(i);
      if (succ->isMarked()) {
        continue;
      }
      JitSpew(JitSpew_Prune, "Reaches block %u", succ->id());
      if (!markReachable(succ)) {
        return false;
      }
    }
  }

  if (!needsTrim && numMarked == graph.numBlocks()) {
    // There is nothing to prune.
    graph.unmarkBlocks();
    return true;
  }

  JitSpew(JitSpew_Prune, "Remove unreachable instructions and blocks:");
  JitSpewIndent indent(JitSpew_Prune);

  // The operands of removed instructions may be needed in baseline
  // after bailing out.
  for (PostorderIterator it(graph.poBegin()); it != graph.poEnd();) {
    if (mir->shouldCancel("Prune unused branches (marking operands)")) {
      return false;
    }

    MBasicBlock* block = *it++;
    if (!block->isMarked()) {
      // If we are removing the block entirely, mark the operands of every
      // instruction as being implicitly used.
      if (!FlagAllOperandsAsImplicitlyUsed(mir, block)) {
        return false;
      }
    } else if (block->alwaysBails()) {
      // If we are only trimming instructions after a bail, only mark operands
      // of removed instructions.
      MInstructionIterator firstRemoved = FindFirstInstructionAfterBail(block);
      if (!FlagOperandsAsImplicitlyUsedAfter(mir, block, firstRemoved)) {
        return false;
      }
    }
  }

  // Remove the blocks in post-order such that consumers are visited before
  // the predecessors, the only exception being the Phi nodes of loop headers.
  for (PostorderIterator it(graph.poBegin()); it != graph.poEnd();) {
    if (mir->shouldCancel("Prune unused branches (removal loop)")) {
      return false;
    }
    if (!graph.alloc().ensureBallast()) {
      return false;
    }

    MBasicBlock* block = *it++;
    if (block->isMarked() && !block->alwaysBails()) {
      continue;
    }

    // As we are going to replace/remove the last instruction, we first have
    // to remove this block from the predecessor list of its successors.
    size_t numSucc = block->numSuccessors();
    for (uint32_t i = 0; i < numSucc; i++) {
      MBasicBlock* succ = block->getSuccessor(i);
      if (succ->isDead()) {
        continue;
      }

      // Our dominators code expects all loop headers to have two predecessors.
      // If we are removing the normal entry to a loop, but can still reach
      // the loop header via OSR, we create a fake unreachable predecessor.
      if (succ->isLoopHeader() && block != succ->backedge()) {
        MOZ_ASSERT(graph.osrBlock());
        if (!graph.alloc().ensureBallast()) {
          return false;
        }

        MBasicBlock* fake = MBasicBlock::NewFakeLoopPredecessor(graph, succ);
        if (!fake) {
          return false;
        }
        // Mark the block to avoid removing it as unreachable.
        fake->mark();

        JitSpew(JitSpew_Prune,
                "Header %u only reachable by OSR. Add fake predecessor %u",
                succ->id(), fake->id());
      }

      JitSpew(JitSpew_Prune, "Remove block edge %u -> %u.", block->id(),
              succ->id());
      succ->removePredecessor(block);
    }

    if (!block->isMarked()) {
      // Remove unreachable blocks from the CFG.
      JitSpew(JitSpew_Prune, "Remove block %u.", block->id());
      graph.removeBlock(block);
    } else {
      // Remove unreachable instructions after unconditional bailouts.
      JitSpew(JitSpew_Prune, "Trim block %u.", block->id());

      // Discard all instructions after the first MBail.
      MInstructionIterator firstRemoved = FindFirstInstructionAfterBail(block);
      block->discardAllInstructionsStartingAt(firstRemoved);

      if (block->outerResumePoint()) {
        block->clearOuterResumePoint();
      }

      block->end(MUnreachable::New(graph.alloc()));
    }
  }
  graph.unmarkBlocks();

  return true;
}

// Remove all blocks not marked with isMarked(). Unmark all remaining blocks.
// Alias analysis dependencies may be invalid after calling this function.
bool jit::RemoveUnmarkedBlocks(const MIRGenerator* mir, MIRGraph& graph,
                               uint32_t numMarkedBlocks) {
  if (numMarkedBlocks == graph.numBlocks()) {
    // If all blocks are marked, no blocks need removal. Just clear the
    // marks. We'll still need to update the dominator tree below though,
    // since we may have removed edges even if we didn't remove any blocks.
    graph.unmarkBlocks();
  } else {
    // As we are going to remove edges and basic blocks, we have to mark
    // instructions which would be needed by baseline if we were to
    // bailout.
    for (PostorderIterator it(graph.poBegin()); it != graph.poEnd();) {
      MBasicBlock* block = *it++;
      if (block->isMarked()) {
        continue;
      }

      if (!FlagAllOperandsAsImplicitlyUsed(mir, block)) {
        return false;
      }
    }

    // Find unmarked blocks and remove them.
    for (ReversePostorderIterator iter(graph.rpoBegin());
         iter != graph.rpoEnd();) {
      MBasicBlock* block = *iter++;

      if (block->isMarked()) {
        block->unmark();
        continue;
      }

      // The block is unreachable. Clear out the loop header flag, as
      // we're doing the sweep of a mark-and-sweep here, so we no longer
      // need to worry about whether an unmarked block is a loop or not.
      if (block->isLoopHeader()) {
        block->clearLoopHeader();
      }

      for (size_t i = 0, e = block->numSuccessors(); i != e; ++i) {
        block->getSuccessor(i)->removePredecessor(block);
      }
      graph.removeBlock(block);
    }
  }

  // Renumber the blocks and update the dominator tree.
  return AccountForCFGChanges(mir, graph, /*updateAliasAnalysis=*/false);
}
