/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_List_inl_h
#define vm_List_inl_h

#include "vm/List.h"

#include "mozilla/Assertions.h"  // MOZ_ASSERT

#include <stdint.h>  // uint32_t

#include "js/Value.h"         // JS::Value
#include "vm/JSContext.h"     // JSContext
#include "vm/NativeObject.h"  // js::NativeObject

#include "vm/JSObject-inl.h"      // js::NewObjectWithGivenProto
#include "vm/NativeObject-inl.h"  // js::NativeObject::*

inline /* static */ js::ListObject* js::ListObject::create(JSContext* cx) {
  return NewObjectWithGivenProto<ListObject>(cx, nullptr);
}

inline bool js::ListObject::append(JSContext* cx, Value value) {
  uint32_t len = length();

  if (!ensureElements(cx, len + 1)) {
    return false;
  }

  ensureDenseInitializedLength(len, 1);
  setDenseElement(len, value);
  return true;
}

inline bool js::ListObject::append(JSContext* cx, Value v1, Value v2) {
  uint32_t len = length();

  if (!ensureElements(cx, len + 2)) {
    return false;
  }

  ensureDenseInitializedLength(len, 2);
  setDenseElement(len, v1);
  setDenseElement(len + 1, v2);
  return true;
}

inline JS::Value js::ListObject::popFirst(JSContext* cx) {
  uint32_t len = length();
  MOZ_ASSERT(len > 0);

  JS::Value entry = get(0);
  if (!tryShiftDenseElements(1)) {
    moveDenseElements(0, 1, len - 1);
    setDenseInitializedLength(len - 1);
    shrinkElements(cx, len - 1);
  }

  MOZ_ASSERT(length() == len - 1);
  return entry;
}

template <class T>
inline T& js::ListObject::popFirstAs(JSContext* cx) {
  return popFirst(cx).toObject().as<T>();
}

#endif  // vm_List_inl_h
