/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef js_friend_Wrapper_h
#define js_friend_Wrapper_h

#include "js/Wrapper.h"

namespace js {

/*
 * APIs to nuke cross compartment wrappers.
 *
 * These are implemented in js/src/proxy/CrossCompartmentWrapper.cpp.
 */

enum NukeReferencesToWindow { NukeWindowReferences, DontNukeWindowReferences };

enum NukeReferencesFromTarget {
  NukeAllReferences,
  NukeIncomingReferences,
};

/*
 * These filters are designed to be ephemeral stack classes, and thus don't
 * do any rooting or holding of their members.
 */
struct CompartmentFilter {
  virtual bool match(JS::Compartment* c) const = 0;
};

struct AllCompartments : public CompartmentFilter {
  virtual bool match(JS::Compartment* c) const override { return true; }
};

struct SingleCompartment : public CompartmentFilter {
  JS::Compartment* ours;
  explicit SingleCompartment(JS::Compartment* c) : ours(c) {}
  virtual bool match(JS::Compartment* c) const override { return c == ours; }
};

extern JS_PUBLIC_API bool NukeCrossCompartmentWrappers(
    JSContext* cx, const CompartmentFilter& sourceFilter, JS::Realm* target,
    NukeReferencesToWindow nukeReferencesToWindow,
    NukeReferencesFromTarget nukeReferencesFromTarget);

extern JS_PUBLIC_API bool AllowNewWrapper(JS::Compartment* target,
                                          JSObject* obj);

extern JS_PUBLIC_API bool NukedObjectRealm(JSObject* obj);

// If a cross-compartment wrapper source => target exists, nuke it.
extern JS_PUBLIC_API void NukeCrossCompartmentWrapperIfExists(
    JSContext* cx, JS::Compartment* source, JSObject* target);

} /* namespace js */

#endif  // js_friend_Wrapper_h
