/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string.h>

#include "js/RealmOptions.h"
#include "jsapi-tests/tests.h"

BEGIN_TEST(testRealmOverrideStrings) {
  JS::RealmBehaviors b1;
  b1.setTimeZoneOverride("Iceland");
  b1.setLocaleOverride("en-GB");

  // The copy constructor shares the override strings.
  JS::RealmBehaviors b2(b1);
  CHECK(b1.timeZoneOverride() == b2.timeZoneOverride());
  CHECK(b1.localeOverride() == b2.localeOverride());

  // copyOverrideStrings must allocate new strings.
  for (size_t i = 0; i < 2; i++) {
    b2.copyOverrideStrings();
    CHECK(b1.timeZoneOverride() != b2.timeZoneOverride());
    CHECK(b1.localeOverride() != b2.localeOverride());
    CHECK(strcmp(b1.timeZoneOverride()->chars(),
                 b2.timeZoneOverride()->chars()) == 0);
    CHECK(strcmp(b1.localeOverride()->chars(), b2.localeOverride()->chars()) ==
          0);
  }
  return true;
}
END_TEST(testRealmOverrideStrings)
