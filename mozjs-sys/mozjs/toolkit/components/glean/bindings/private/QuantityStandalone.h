/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_glean_QuantityStandalone_h
#define mozilla_glean_QuantityStandalone_h

#include <cstdint>  // uint32_t

namespace mozilla::glean::impl {

class QuantityStandalone {
 public:
  constexpr explicit QuantityStandalone(uint32_t id) : mId(id) {}

  /**
   * Set to the specified value.
   *
   * @param aValue the value to set.
   */
  void Set(int64_t aValue) const;

 protected:
  const uint32_t mId;
};

}  // namespace mozilla::glean::impl

#endif /* mozilla_glean_QuantityStandalone.h */
