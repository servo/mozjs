/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/intl/Region.h"

#include "mozilla/Assertions.h"

#include <algorithm>

#include "unicode/uregion.h"
#include "unicode/utypes.h"

namespace mozilla::intl {

/* static */
Result<Maybe<Region>, ICUError> Region::From(const RegionSubtag& aRegion) {
  // Input is a valid region subtag.
  auto regionSpan = aRegion.Span();
  MOZ_ASSERT(IsStructurallyValidRegionTag(regionSpan));

  // Zero-terminated region string.
  char region[LanguageTagLimits::RegionLength + 1] = {};
  std::copy_n(regionSpan.Elements(), LanguageTagLimits::RegionLength, region);

  UErrorCode status = U_ZERO_ERROR;
  const URegion* uregion = uregion_getRegionFromCode(region, &status);

  // Returns "Illegal Argument" error for invalid region codes.
  if (U_FAILURE(status)) {
    if (status != U_ILLEGAL_ARGUMENT_ERROR) {
      return Err(ToICUError(status));
    }
    return Maybe<Region>{};
  }
  return Some(Region{uregion, aRegion});
}

bool Region::IsRegular() const {
  // Compares the region code in addition to the region type to reject
  // deprecated regions. (`uregion_getRegionFromCode` implicitly canonicalizes
  // to modern replacements.)
  return uregion_getType(mURegion) == URGN_TERRITORY &&
         mozilla::MakeStringSpan(uregion_getRegionCode(mURegion)) ==
             mRegion.Span();
}

}  // namespace mozilla::intl
