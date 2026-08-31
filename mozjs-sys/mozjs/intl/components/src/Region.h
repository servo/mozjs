/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef intl_components_Region_h_
#define intl_components_Region_h_

#include "mozilla/intl/ICU4CGlue.h"
#include "mozilla/intl/ICUError.h"
#include "mozilla/intl/Locale.h"
#include "mozilla/Maybe.h"
#include "mozilla/Result.h"

struct URegion;

namespace mozilla::intl {

/**
 * This component is a Mozilla-focused API for working with regions in
 * internationalization code.
 */
class Region final {
  const URegion* mURegion;
  const RegionSubtag mRegion;

  Region(const URegion* aURegion, const RegionSubtag& aRegion)
      : mURegion(aURegion), mRegion(aRegion) {}

 public:
  /**
   * Return the `Region` for the requested region subtag. Return Nothing if the
   * region code is invalid.
   */
  static Result<Maybe<Region>, ICUError> From(const RegionSubtag& aRegion);

  /**
   * Return `true` if this is a regular region per validity data in CLDR's
   * "common/validity/region.xml" file.
   */
  bool IsRegular() const;
};

}  // namespace mozilla::intl

#endif
