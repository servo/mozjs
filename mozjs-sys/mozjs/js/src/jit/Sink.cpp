/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/Sink.h"

#include "jit/IonOptimizationLevels.h"
#include "jit/JitSpewer.h"
#include "jit/MIR-wasm.h"
#include "jit/MIR.h"
#include "jit/MIRGenerator.h"
#include "jit/MIRGraph.h"

namespace js {
namespace jit {

// Given the last found common dominator and a new definition to dominate, the
// CommonDominator function returns the basic block which dominate the last
// common dominator and the definition. If no such block exists, then this
// functions return null.
static MBasicBlock* CommonDominator(MBasicBlock* commonDominator,
                                    MBasicBlock* defBlock) {
  // This is the first instruction visited, record its basic block as being
  // the only interesting one.
  if (!commonDominator) {
    return defBlock;
  }

  // Iterate on immediate dominators of the known common dominator to find a
  // block which dominates all previous uses as well as this instruction.
  while (!commonDominator->dominates(defBlock)) {
    MBasicBlock* nextBlock = commonDominator->immediateDominator();
    // All uses are dominated, so, this cannot happen unless the graph
    // coherency is not respected.
    MOZ_ASSERT(commonDominator != nextBlock);
    commonDominator = nextBlock;
  }

  return commonDominator;
}

bool Sink(const MIRGenerator* mir, MIRGraph& graph) {
  JitSpew(JitSpew_Sink, "Begin");

  for (PostorderIterator block = graph.poBegin(); block != graph.poEnd();
       block++) {
    if (mir->shouldCancel("Sink")) {
      return false;
    }

    for (MInstructionReverseIterator iter = block->rbegin();
         iter != block->rend();) {
      MInstruction* ins = *iter++;

      // Only instructions which can be recovered on bailout can be moved
      // into the bailout paths.
      if (ins->isGuard() || ins->isGuardRangeBailouts() ||
          ins->isRecoveredOnBailout() || !ins->canRecoverOnBailout()) {
        continue;
      }

      // Compute a common dominator for all uses of the current
      // instruction.
      bool hasLiveUses = false;
      bool hasUses = false;
      MBasicBlock* usesDominator = nullptr;
      for (MUseIterator i(ins->usesBegin()), e(ins->usesEnd()); i != e; i++) {
        hasUses = true;
        MNode* consumerNode = (*i)->consumer();
        if (consumerNode->isResumePoint()) {
          if (!consumerNode->toResumePoint()->isRecoverableOperand(*i)) {
            hasLiveUses = true;
          }
          continue;
        }

        MDefinition* consumer = consumerNode->toDefinition();
        if (consumer->isRecoveredOnBailout()) {
          continue;
        }

        hasLiveUses = true;

        // If the instruction is a Phi, then we should dominate the
        // predecessor from which the value is coming from.
        MBasicBlock* consumerBlock = consumer->block();
        if (consumer->isPhi()) {
          consumerBlock = consumerBlock->getPredecessor(consumer->indexOf(*i));
        }

        usesDominator = CommonDominator(usesDominator, consumerBlock);
        if (usesDominator == *block) {
          break;
        }
      }

      // Leave this instruction for DCE.
      if (!hasUses) {
        continue;
      }

      // We have no uses, so sink this instruction in all the bailout
      // paths.
      if (!hasLiveUses) {
        MOZ_ASSERT(!usesDominator);
        ins->setRecoveredOnBailout();
        JitSpewDef(JitSpew_Sink,
                   "  No live uses, recover the instruction on bailout\n", ins);
        continue;
      }
    }
  }

  return true;
}

}  // namespace jit
}  // namespace js
