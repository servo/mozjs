/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_StubFolding_h
#define jit_StubFolding_h

#include "js/TypeDecls.h"

namespace js {

namespace gc {
class AutoMarkingLock;
}  // namespace gc

namespace jit {

class CacheIRWriter;
class ICFallbackStub;
class ICScript;

bool TryFoldingStubs(JSContext* cx, ICFallbackStub* fallback, JSScript* script,
                     ICScript* icScript);

bool TryFoldingStubsLocked(JSContext* cx, ICFallbackStub* fallback,
                           JSScript* script, ICScript* icScript,
                           gc::AutoMarkingLock& lock);

bool AddToFoldedStub(JSContext* cx, const CacheIRWriter& writer,
                     ICScript* icScript, ICFallbackStub* fallback);

}  // namespace jit
}  // namespace js

#endif  // jit_StubFolding_h
