/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/glean/bindings/GleanWebIDL.h"

#include "mozilla/BasePrincipal.h"
#include "nsContentUtils.h"
#include "nsError.h"
#include "nsIAboutModule.h"
#include "nsIPrincipal.h"

namespace mozilla::dom {

bool GleanWebidlEnabled(JSContext* aCx, JSObject* aObj) {
  // Glean is needed in ChromeOnly contexts and also in privileged about pages.

  nsIPrincipal* principal = nsContentUtils::SubjectPrincipal(aCx);

  if (principal->IsSystemPrincipal()) {
    return true;
  }

  uint32_t flags = 0;
  if (NS_FAILED(principal->GetAboutModuleFlags(&flags))) {
    return false;
  }

  return flags & nsIAboutModule::IS_SECURE_CHROME_UI;
}

}  // namespace mozilla::dom
