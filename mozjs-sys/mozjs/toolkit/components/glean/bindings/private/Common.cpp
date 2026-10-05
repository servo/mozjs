/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Common.h"
#include "nsComponentManagerUtils.h"
#include "nsIConsoleService.h"
#include "nsIScriptError.h"
#include "nsServiceManagerUtils.h"

namespace mozilla::glean {

// This is copied from TelemetryCommons.cpp (and modified because consoleservice
// handles threading), but that one is not exported.
// There's _at least_ a third instance of `LogToBrowserConsole`,
// but that one is slightly different.
void LogToBrowserConsole(uint32_t aLogLevel, const nsAString& aMsg) {
  nsCOMPtr<nsIConsoleService> console(
      do_GetService("@mozilla.org/consoleservice;1"));
  if (!console) {
    NS_WARNING("Failed to log message to console.");
    return;
  }

  nsCOMPtr<nsIScriptError> error(do_CreateInstance(NS_SCRIPTERROR_CONTRACTID));
  error->Init(aMsg, ""_ns, 0, 0, aLogLevel, "chrome javascript"_ns,
              false /* from private window */, true /* from chrome context */);
  console->LogMessage(error);
}

bool IsCamelCase(const nsAString& aStr) {
  const char16_t* cur = aStr.BeginReading();
  const char16_t* end = aStr.EndReading();

  if (cur == end) {
    return false;
  }

  char16_t wc = *cur;
  if ((wc < u'a' || wc > u'z')) {
    return false;
  }
  cur++;

  for (; cur < end; ++cur) {
    wc = *cur;
    if ((wc < u'A' || wc > u'Z') && (wc < u'a' || wc > u'z') &&
        (wc < u'0' || wc > u'9')) {
      return false;
    }
  }
  return true;
}

}  // namespace mozilla::glean
