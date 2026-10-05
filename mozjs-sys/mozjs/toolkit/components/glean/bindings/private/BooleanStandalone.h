/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_glean_BooleanStandalone_h
#define mozilla_glean_BooleanStandalone_h

#include <cstdint>  // uint32_t

namespace mozilla::glean::impl {

class BooleanStandalone {
 public:
  constexpr explicit BooleanStandalone(uint32_t id) : mId(id) {}

  /**
   * Set to the specified boolean value.
   *
   * @param aValue the value to set.
   */
  void Set(bool aValue) const;

 protected:
  const uint32_t mId;
};

}  // namespace mozilla::glean::impl

#endif /* mozilla_glean_BooleanStandalone.h */
