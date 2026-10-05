/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_ShapeList_h
#define jit_ShapeList_h

#include <stddef.h>
#include <stdint.h>

#include "jstypes.h"

#include "js/Class.h"
#include "vm/List.h"

namespace js::jit {

// Special object used for storing a list of shapes to guard against. These are
// only used in the fields of CacheIR stubs and do not escape.
class ShapeListObject : public ListObject {
 public:
  static const JSClass class_;
  static const JSClassOps classOps_;

  static constexpr size_t MaxLength = 16;

  static ShapeListObject* create(JSContext* cx);
  static void trace(JSTracer* trc, JSObject* obj);

  Shape* get(uint32_t index) const;
  Shape* getUnbarriered(uint32_t index) const;

  bool traceWeak(JSTracer* trc);
};

// Similar to ShapeListObject. But here we keep a list of the shape and the
// corresponding offset of a particular access (e.g. obj.a).
// The values are saved as: [shape1, offset1, shape2, offset2 ...].
class ShapeListWithOffsetsObject : public ListObject {
 public:
  static const JSClass class_;
  static const JSClassOps classOps_;

  static constexpr size_t MaxLength = 16;

  static ShapeListWithOffsetsObject* create(JSContext* cx);
  static void trace(JSTracer* trc, JSObject* obj);

  uint32_t numShapes() const;
  Shape* getShape(uint32_t index) const;
  Shape* getShapeUnbarriered(uint32_t index) const;

  uint32_t getOffset(uint32_t index) const;

  bool traceWeak(JSTracer* trc);
};

}  // namespace js::jit

#endif  // jit_ShapeList_h
