/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/ShapeList.h"

#include "gc/GC.h"

#include "vm/List-inl.h"

using namespace js;
using namespace js::jit;

const JSClass ShapeListObject::class_ = {
    "JIT ShapeList",
    0,
    &classOps_,
};

const JSClassOps ShapeListObject::classOps_ = {
    .trace = ShapeListObject::trace,
};

/* static */ ShapeListObject* ShapeListObject::create(JSContext* cx) {
  NativeObject* obj = NewTenuredObjectWithGivenProto(cx, &class_, nullptr);
  if (!obj) {
    return nullptr;
  }

  // Register this object so the GC can sweep its weak pointers.
  if (!cx->zone()->registerObjectWithWeakPointers(obj)) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  return &obj->as<ShapeListObject>();
}

Shape* ShapeListObject::get(uint32_t index) const {
  Shape* shape = getUnbarriered(index);
  gc::ReadBarrier(shape);
  return shape;
}

Shape* ShapeListObject::getUnbarriered(uint32_t index) const {
  Value value = ListObject::get(index);
  return static_cast<Shape*>(value.toPrivate());
}

void ShapeListObject::trace(JSTracer* trc, JSObject* obj) {
  if (trc->traceWeakEdges()) {
    obj->as<ShapeListObject>().traceWeak(trc);
  }
}

bool ShapeListObject::traceWeak(JSTracer* trc) {
  uint32_t length = getDenseInitializedLength();
  if (length == 0) {
    return false;  // Object may be uninitialized.
  }

  const HeapSlot* src = elements_;
  const HeapSlot* end = src + length;
  HeapSlot* dst = elements_;
  while (src < end) {
    Shape* shape = static_cast<Shape*>(src->toPrivate());
    MOZ_ASSERT(shape->is<Shape>());
    if (TraceManuallyBarrieredWeakEdge(trc, &shape, "ShapeListObject shape")) {
      dst->unbarrieredSet(PrivateValue(shape));
      dst++;
    }
    src++;
  }

  MOZ_ASSERT(dst <= end);
  uint32_t newLength = dst - elements_;
  setDenseInitializedLength(newLength);

  if (length != newLength) {
    JitSpew(JitSpew_StubFolding, "Cleared %u/%u shapes from %p",
            length - newLength, length, this);
  }

  return length != 0;
}

const JSClass ShapeListWithOffsetsObject::class_ = {
    "JIT ShapeList",
    0,
    &classOps_,
};

const JSClassOps ShapeListWithOffsetsObject::classOps_ = {
    .trace = ShapeListWithOffsetsObject::trace,
};

/* static */ ShapeListWithOffsetsObject* ShapeListWithOffsetsObject::create(
    JSContext* cx) {
  NativeObject* obj = NewTenuredObjectWithGivenProto(cx, &class_, nullptr);
  if (!obj) {
    return nullptr;
  }

  // Register this object so the GC can sweep its weak pointers.
  if (!cx->zone()->registerObjectWithWeakPointers(obj)) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  return &obj->as<ShapeListWithOffsetsObject>();
}

Shape* ShapeListWithOffsetsObject::getShape(uint32_t index) const {
  Shape* shape = getShapeUnbarriered(index);
  gc::ReadBarrier(shape);
  return shape;
}

Shape* ShapeListWithOffsetsObject::getShapeUnbarriered(uint32_t index) const {
  Value value = ListObject::get(index * 2);
  return static_cast<Shape*>(value.toPrivate());
}

uint32_t ShapeListWithOffsetsObject::getOffset(uint32_t index) const {
  Value value = ListObject::get(index * 2 + 1);
  return value.toPrivateUint32();
}

uint32_t ShapeListWithOffsetsObject::numShapes() const {
  MOZ_ASSERT(length() % 2 == 0);
  return length() / 2;
};

void ShapeListWithOffsetsObject::trace(JSTracer* trc, JSObject* obj) {
  if (trc->traceWeakEdges()) {
    obj->as<ShapeListWithOffsetsObject>().traceWeak(trc);
  }
}

bool ShapeListWithOffsetsObject::traceWeak(JSTracer* trc) {
  uint32_t length = getDenseInitializedLength();
  if (length == 0) {
    return false;  // Object may be uninitialized.
  }

  MOZ_RELEASE_ASSERT(length % 2 == 0, "elements must be shape/offset pairs");

  const HeapSlot* src = elements_;
  const HeapSlot* end = src + length;
  HeapSlot* dst = elements_;
  while (src < end) {
    Shape* shape = static_cast<Shape*>(src[0].toPrivate());
    uint32_t offset = src[1].toPrivateUint32();
    MOZ_ASSERT(shape->is<Shape>());
    if (TraceManuallyBarrieredWeakEdge(trc, &shape,
                                       "ShapeListWithOffsetsObject shape")) {
      dst[0].unbarrieredSet(PrivateValue(shape));
      dst[1].unbarrieredSet(PrivateUint32Value(offset));
      dst += 2;
    }
    src += 2;
  }

  MOZ_ASSERT(dst <= end);
  uint32_t newLength = dst - elements_;
  setDenseInitializedLength(newLength);

  if (length != newLength) {
    JitSpew(JitSpew_StubFolding, "Cleared %u/%u shapes from %p",
            (length - newLength) / 2, (length) / 2, this);
  }

  return length != 0;
}
