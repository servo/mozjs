/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_DominatorTree_h
#define jit_DominatorTree_h

namespace js::jit {

class MIRGenerator;
class MIRGraph;

[[nodiscard]] bool BuildDominatorTree(const MIRGenerator* mir, MIRGraph& graph);
void ClearDominatorTree(MIRGraph& graph);

}  // namespace js::jit

#endif /* jit_DominatorTree_h */
