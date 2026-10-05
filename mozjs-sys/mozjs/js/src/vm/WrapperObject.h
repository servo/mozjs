/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_WrapperObject_h
#define vm_WrapperObject_h

#include "js/Wrapper.h"
#include "vm/JSObject.h"
#include "vm/ProxyObject.h"

namespace js {

// Proxy family for wrappers.
// This variable exists solely to provide a unique address for use as an
// identifier.
extern const char sWrapperFamily;

class WrapperObject : public ProxyObject {};

class CrossCompartmentWrapperObject : public WrapperObject {
 public:
  static const unsigned GrayLinkReservedSlot = 1;
};

extern void NukeCrossCompartmentWrapper(JSContext* cx, JSObject* wrapper);
extern void RemapWrapper(JSContext* cx, JSObject* wobj, JSObject* newTarget);
extern void RemapDeadWrapper(JSContext* cx, JS::HandleObject wobj,
                             JS::HandleObject newTarget);

// This is a variant of js::NukeNonCCWProxy() for CCWs. It should only be called
// on CCWs that have been removed from CCW tables.
extern void NukeRemovedCrossCompartmentWrapper(JSContext* cx,
                                               JSObject* wrapper);

}  // namespace js

template <>
inline bool JSObject::is<js::WrapperObject>() const {
  return js::IsWrapper(this);
}

template <>
inline bool JSObject::is<js::CrossCompartmentWrapperObject>() const {
  return js::IsCrossCompartmentWrapper(this);
}

#endif /* vm_WrapperObject_h */
