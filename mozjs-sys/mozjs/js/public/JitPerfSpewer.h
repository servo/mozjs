/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* SpiderMonkey JIT perf spewer control API. */

#ifndef js_JitPerfSpewer_h
#define js_JitPerfSpewer_h

namespace js {
namespace jit {

// Reset the perf spewer state and enable/disable gecko profiling mode.
// When enabled, JIT code generation will collect accurate native-to-bytecode
// mappings for use by the Gecko Profiler.
void ResetPerfSpewer(bool enabled);

}  // namespace jit
}  // namespace js

#endif /* js_JitPerfSpewer_h */
