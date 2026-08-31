/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_TypeAnalysis_h
#define jit_TypeAnalysis_h

// This file declares the type analysis pass that inserts conversions and
// box/unbox instructions to make the IR graph well-typed.

namespace js {
namespace jit {

class MIRGenerator;
class MIRGraph;

[[nodiscard]] bool ApplyTypeInformation(const MIRGenerator* mir,
                                        MIRGraph& graph);

}  // namespace jit
}  // namespace js

#endif /* jit_TypeAnalysis_h */
