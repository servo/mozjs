/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_glean_RateStandalone_h
#define mozilla_glean_RateStandalone_h

#include <cstdint>  // uint32_t

namespace mozilla::glean::impl {

class RateStandalone {
 public:
  constexpr explicit RateStandalone(uint32_t aId) : mId(aId) {}

  /*
   * Increases the numerator by `amount`.
   *
   * @param aAmount The amount to increase by. Should be positive.
   */
  void AddToNumerator(int32_t aAmount = 1) const;

  /*
   * Increases the denominator by `amount`.
   *
   * @param aAmount The amount to increase by. Should be positive.
   */
  void AddToDenominator(int32_t aAmount = 1) const;

 protected:
  const uint32_t mId;
};

}  // namespace mozilla::glean::impl

#endif /* mozilla_glean_RateStandalone_h */
