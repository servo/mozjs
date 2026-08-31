/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_OrderedHashTableObject_inl_h
#define builtin_OrderedHashTableObject_inl_h

#include "builtin/OrderedHashTableObject.h"

#include "gc/Nursery-inl.h"

inline void* js::detail::OrderedHashTableObject::allocateCellBuffer(
    JSContext* cx, size_t numBytes) {
  return AllocateCellBuffer<uint8_t>(cx, this, numBytes);
}

inline void js::detail::OrderedHashTableObject::freeCellBuffer(
    JSContext* cx, void* data, size_t numBytes) {
  FreeCellBuffer<uint8_t>(cx, this, reinterpret_cast<uint8_t*>(data), numBytes);
}

#endif /* builtin_OrderedHashTableObject_inl_h */
