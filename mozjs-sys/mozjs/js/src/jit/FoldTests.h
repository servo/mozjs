/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_FoldTests_h
#define jit_FoldTests_h

#include "js/Utility.h"

namespace js {
namespace jit {

class MIRGraph;

[[nodiscard]] bool FoldTests(MIRGraph& graph);

}  // namespace jit
}  // namespace js

#endif /* jit_FoldTests_h */
