/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "vm/GetterSetter.h"

#include "vm/JSObject.h"

#include "vm/JSContext-inl.h"

using namespace js;

js::GetterSetter::GetterSetter(HandleObject getter, HandleObject setter)
    : CellWithGCPointer(getter), setter_(setter) {}

// static
GetterSetter* GetterSetter::create(JSContext* cx, Handle<NativeObject*> owner,
                                   HandleObject getter, HandleObject setter) {
  // Try to allocate the GetterSetter in the nursery iff the owner object is
  // also in the nursery.
  gc::Heap heap = owner->isTenured() ? gc::Heap::Tenured : gc::Heap::Default;
  return cx->newCell<GetterSetter>(heap, getter, setter);
}

JS::ubi::Node::Size JS::ubi::Concrete<GetterSetter>::size(
    mozilla::MallocSizeOf mallocSizeOf) const {
  GetterSetter& gs = get();
  size_t size = sizeof(js::GetterSetter);
  if (IsInsideNursery(&gs)) {
    size += Nursery::nurseryCellHeaderSize();
  }
  return size;
}
