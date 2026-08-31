/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_Boolean_inl_h
#define builtin_Boolean_inl_h

#include "builtin/Boolean.h"

#include "vm/JSContext.h"
#include "vm/WrapperObject.h"

namespace js {

inline bool EmulatesUndefined(JSObject* obj) {
  // Called from ProcessTryNotes via ToBoolean with a pending exception.
  AutoUnsafeCallWithABI unsafe(UnsafeABIStrictness::AllowPendingExceptions);
  JSObject* actual =
      MOZ_LIKELY(!obj->is<WrapperObject>()) ? obj : UncheckedUnwrap(obj);
  return actual->getClass()->emulatesUndefined();
}

inline bool EmulatesUndefinedCheckFuse(JSObject* obj, size_t fuseValue) {
  AutoUnsafeCallWithABI unsafe;
  JSObject* actual =
      MOZ_LIKELY(!obj->is<WrapperObject>()) ? obj : UncheckedUnwrap(obj);
  bool emulatesUndefined = actual->getClass()->emulatesUndefined();
  if (emulatesUndefined) {
    MOZ_RELEASE_ASSERT(fuseValue != 0);
  }
  return emulatesUndefined;
}

} /* namespace js */

#endif /* builtin_Boolean_inl_h */
