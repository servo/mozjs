/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef js_public_Object_h
#define js_public_Object_h

#include "js/shadow/Object.h"  // JS::shadow::Object

#include "mozilla/Assertions.h"  // MOZ_ASSERT

#include <stddef.h>  // size_t
#include <stdint.h>  // uint32_t

#include "jstypes.h"  // JS_PUBLIC_API

#include "js/Class.h"  // js::ESClass, JSCLASS_RESERVED_SLOTS
#include "js/Proxy.h"  // js::IsProxy, js::GetProxyReservedSlot, js::SetProxyReservedSlot
#include "js/Realm.h"       // JS::GetCompartmentForRealm
#include "js/RootingAPI.h"  // JS::{,Mutable}Handle
#include "js/Value.h"       // JS::Value

struct JS_PUBLIC_API JSContext;
class JS_PUBLIC_API JSObject;

namespace JS {

class JS_PUBLIC_API Compartment;

/**
 * Determine the ECMAScript "class" -- Date, String, RegExp, and all the other
 * builtin object types (described in ECMAScript in terms of an objecting having
 * "an [[ArrayBufferData]] internal slot" or similar language for other kinds of
 * object -- of the provided object.
 *
 * If this function is passed a wrapper that can be unwrapped, the determination
 * is performed on that object.  If the wrapper can't be unwrapped, and it's not
 * a wrapper that prefers to treat this operation as a failure, this function
 * will indicate that the object is |js::ESClass::Other|.
 */
extern JS_PUBLIC_API bool GetBuiltinClass(JSContext* cx, Handle<JSObject*> obj,
                                          js::ESClass* cls);

/**
 * Returns true if |obj| is a plain object: a standard JS object with no exotic
 * behavior, such as one created from a '{}' object literal or 'Object.create'.
 * Unlike |GetBuiltinClass|, this does not unwrap proxies and tests |obj|
 * directly.
 */
extern JS_PUBLIC_API bool IsPlainObject(JSObject* obj);

/**
 * Get the |JS::Compartment*| of an object.
 *
 * Note that the compartment of an object in this realm, that is a
 * cross-compartment wrapper around an object from another realm, is the
 * compartment of this realm.
 */
static MOZ_ALWAYS_INLINE Compartment* GetCompartment(JSObject* obj) {
  Realm* realm = reinterpret_cast<shadow::Object*>(obj)->shape->base->realm;
  return GetCompartmentForRealm(realm);
}

namespace detail {

extern JS_PUBLIC_API void SetNativeObjectReservedSlotWithBarrier(
    JSObject* obj, size_t slot, const Value& value);

}  // namespace detail

/**
 * Get the value stored in a reserved slot of a native (non-proxy) object.
 *
 * Faster than |GetReservedSlot| because it skips the runtime kind check, but
 * the caller must guarantee that |obj| is not a proxy.
 */
inline const Value& GetNativeObjectReservedSlot(const JSObject* obj,
                                                size_t slot) {
  MOZ_ASSERT(GetClass(obj)->isNativeObject());
  MOZ_ASSERT(slot < JSCLASS_RESERVED_SLOTS(GetClass(obj)));
  auto* nobj = reinterpret_cast<const shadow::NativeObject*>(obj);
  return nobj->reservedSlotRef(slot);
}

/**
 * Store a value in a reserved slot of a native (non-proxy) object.
 *
 * Faster than |SetReservedSlot| because it skips the runtime kind check, but
 * the caller must guarantee that |obj| is not a proxy.
 */
inline void SetNativeObjectReservedSlot(JSObject* obj, size_t slot,
                                        const Value& value) {
  MOZ_ASSERT(GetClass(obj)->isNativeObject());
  MOZ_ASSERT(slot < JSCLASS_RESERVED_SLOTS(GetClass(obj)));
  auto* nobj = reinterpret_cast<shadow::NativeObject*>(obj);
  if (nobj->reservedSlotRef(slot).isGCThing() || value.isGCThing()) {
    detail::SetNativeObjectReservedSlotWithBarrier(obj, slot, value);
  } else {
#ifdef JS_GC_CONCURRENT_MARKING
    nobj->reservedSlotRef(slot).atomicSet(value);
#else
    nobj->reservedSlotRef(slot) = value;
#endif
  }
}

/**
 * Get the value stored in a reserved slot in an object.
 *
 * If |obj| is known to be a proxy or native, the |js::GetProxyReservedSlot| /
 * |JS::GetNativeObjectReservedSlot| variants are slightly more efficient.
 */
inline const Value& GetReservedSlot(const JSObject* obj, size_t slot) {
  if (js::IsProxy(obj)) {
    return js::GetProxyReservedSlot(obj, slot);
  }
  return GetNativeObjectReservedSlot(obj, slot);
}

/**
 * Store a value in an object's reserved slot.
 *
 * If |obj| is known to be a proxy or native, the |js::SetProxyReservedSlot| /
 * |JS::SetNativeObjectReservedSlot| variants are slightly more efficient.
 */
inline void SetReservedSlot(JSObject* obj, size_t slot, const Value& value) {
  if (js::IsProxy(obj)) {
    js::SetProxyReservedSlot(obj, slot, value);
  } else {
    SetNativeObjectReservedSlot(obj, slot, value);
  }
}

/**
 * Helper function to get the pointer value (or nullptr if not set) from an
 * object's reserved slot. The slot must contain either a PrivateValue(T*) or
 * UndefinedValue.
 */
template <typename T>
inline T* GetMaybePtrFromReservedSlot(JSObject* obj, size_t slot) {
  Value v = GetReservedSlot(obj, slot);
  return v.isUndefined() ? nullptr : static_cast<T*>(v.toPrivate());
}

/**
 * Like GetMaybePtrFromReservedSlot, but for native objects. The caller must
 * guarantee that |obj| is not a proxy.
 */
template <typename T>
inline T* GetMaybePtrFromNativeObjectReservedSlot(JSObject* obj, size_t slot) {
  Value v = GetNativeObjectReservedSlot(obj, slot);
  return v.isUndefined() ? nullptr : static_cast<T*>(v.toPrivate());
}

/**
 * Helper function to get the pointer value (or nullptr if not set) from the
 * object's first reserved slot. Must only be used for objects with a JSClass
 * that has the JSCLASS_SLOT0_IS_NSISUPPORTS flag.
 */
template <typename T>
inline T* GetObjectISupports(JSObject* obj) {
  MOZ_ASSERT(GetClass(obj)->slot0IsISupports());
  return GetMaybePtrFromReservedSlot<T>(obj, 0);
}

/**
 * Helper function to store |PrivateValue(nsISupportsValue)| in the object's
 * first reserved slot. Must only be used for objects with a JSClass that has
 * the JSCLASS_SLOT0_IS_NSISUPPORTS flag.
 *
 * Note: the pointer is opaque to the JS engine (including the GC) so it's the
 * embedding's responsibility to trace or free this value.
 */
inline void SetObjectISupports(JSObject* obj, void* nsISupportsValue) {
  MOZ_ASSERT(GetClass(obj)->slot0IsISupports());
  SetReservedSlot(obj, 0, PrivateValue(nsISupportsValue));
}

/**
 * Returns true if the native object has own named properties, i.e. user-added
 * properties (expandos). Must not be called on proxy objects.
 */
extern JS_PUBLIC_API bool NativeObjectHasOwnProperties(const JSObject* obj);

}  // namespace JS

// JSObject* is an aligned pointer, but this information isn't available in the
// public header. We specialize HasFreeLSB here so that JS::Result<JSObject*>
// compiles.

namespace mozilla {
namespace detail {
template <>
struct HasFreeLSB<JSObject*> {
  static constexpr bool value = true;
};
}  // namespace detail
}  // namespace mozilla

#endif  // js_public_Object_h
