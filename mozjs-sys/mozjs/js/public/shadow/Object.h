/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Shadow definition of |JSObject| innards.  Do not use this directly!
 */

#ifndef js_shadow_Object_h
#define js_shadow_Object_h

#include "mozilla/Assertions.h"  // MOZ_ASSERT

#include <stddef.h>  // size_t

#include "js/shadow/Shape.h"  // JS::shadow::Shape
#include "js/Value.h"         // JS::Value

class JS_PUBLIC_API JSObject;

namespace JS {

class JS_PUBLIC_API Value;

namespace shadow {

inline size_t NumObjectFixedSlots(Shape* shape) {
  return (shape->immutableFlags & shadow::Shape::FIXED_SLOTS_MASK) >>
         shadow::Shape::FIXED_SLOTS_SHIFT;
}

/**
 * Layout shared by all JSObjects.
 */
struct Object {
  shadow::Shape* shape;

  static constexpr size_t MAX_FIXED_SLOTS = 16;
};

/**
 * Layout for all NativeObjects.
 */
struct NativeObject : public Object {
#ifndef JS_64BIT
  uint32_t padding_;
#endif
  Value* slots;
  void* _1;

  size_t numFixedSlots() const { return NumObjectFixedSlots(shape); }

  Value* fixedSlots() const {
    auto address = reinterpret_cast<uintptr_t>(this);
    return reinterpret_cast<JS::Value*>(address + sizeof(NativeObject));
  }

  Value& slotRef(size_t slot) const {
    size_t nfixed = numFixedSlots();
    if (slot < nfixed) {
      return fixedSlots()[slot];
    }
    return slots[slot - nfixed];
  }

  // Like slotRef, but optimized for reserved slots. This relies on the fact
  // that the first reserved slots (up to MAX_FIXED_SLOTS) are always stored in
  // fixed slots. This lets the compiler optimize away the branch below when
  // |slot| is a constant (after inlining).
  MOZ_ALWAYS_INLINE Value& reservedSlotRef(size_t slot) const {
    MOZ_ASSERT((slot < numFixedSlots()) == (slot < MAX_FIXED_SLOTS));
    if (slot < MAX_FIXED_SLOTS) {
      return fixedSlots()[slot];
    }
    return slots[slot - MAX_FIXED_SLOTS];
  }
};

}  // namespace shadow

/** Get the |JSClass| of an object. */
inline const JSClass* GetClass(const JSObject* obj) {
  return reinterpret_cast<const shadow::Object*>(obj)->shape->base->clasp;
}

}  // namespace JS

#endif  // js_shadow_Object_h
