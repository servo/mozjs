/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_UnrollLoops_h
#define jit_UnrollLoops_h

namespace js {
namespace jit {

class MIRGraph;
class MIRGenerator;

[[nodiscard]] bool UnrollLoops(const MIRGenerator* mir, MIRGraph& graph,
                               bool* changed);

}  // namespace jit
}  // namespace js

#endif /* jit_UnrollLoops_h */
