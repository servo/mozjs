/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_glean_GleanWebIDL_h
#define mozilla_glean_GleanWebIDL_h

#include "js/TypeDecls.h"

namespace mozilla::dom {

/*
 * WebIDL permission function for whether Glean APIs are permitted for aCx.
 *
 * Here instead of nsIGlobalWindowInner or BindingUtils for organization and
 * header include optimization reasons.
 */
bool GleanWebidlEnabled(JSContext* aCx, JSObject* aObj);

}  // namespace mozilla::dom

#endif /* mozilla_glean_GleanWebIDL */
