/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/BranchHinting.h"

#include "jit/IonAnalysis.h"
#include "jit/JitSpewer.h"
#include "jit/MIRGenerator.h"
#include "jit/MIRGraph.h"

using namespace js;
using namespace js::jit;

// Implementation of the branch hinting proposal
// Some control instructions (if and br_if) can have a hint of the form
// likely or unlikely. That means a specific branch will be likely/unlikely
// to be executed at runtime.

// This pass will propagate the branch hints to successor blocks in the CFG.
// Currently, we use the branch hinting information for the following:
// - Unlikely blocks are moved out of line, this is done at the LIR level.
// - TODO: use branch hinting to optimize likely blocks.
bool jit::BranchHinting(const MIRGenerator* mir, MIRGraph& graph) {
  JitSpew(JitSpew_BranchHint, "Beginning BranchHinting pass");

  /* This pass propagates branch hints across the control flow graph
    using dominator information. Branch hints are read at compile-time for
    specific basic blocks. This pass propagates this property to successor
    blocks in a conservative way. The algorithm works as follows:
    - The CFG is traversed in reverse-post-order (RPO). Dominator parents are
      visited before the blocks they dominate.

    - For each basic block, if we have a hint, it is propagated to the
     blocks it immediately dominates (its children in the dominator tree).

    - The pass will then continue to work its way through the CFG.

    Because we only propagate along dominator-tree edges (parent -> child),
    each block receives information from exactly one source. This avoids
    conflicts that would otherwise arise at CFG join points.
  */
  for (ReversePostorderIterator block(graph.rpoBegin());
       block != graph.rpoEnd(); block++) {
    if (block->isUnknownFrequency()) {
      continue;
    }

    for (MBasicBlock** it = block->immediatelyDominatedBlocksBegin();
         it != block->immediatelyDominatedBlocksEnd(); it++) {
      // Don't propagate the information if this successor block has already
      // some branch hints.
      if ((*it)->isUnknownFrequency()) {
        (*it)->setFrequency(block->getFrequency());
      }
    }
  }

  return true;
}
