/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_ReciprocalMulConstants_h
#define jit_ReciprocalMulConstants_h

#include "mozilla/MathAlgorithms.h"

#include <stdint.h>

#include "vm/Int128.h"

namespace js::jit {

struct ReciprocalMulConstants {
  template <typename Multiplier, typename ShiftAmount>
  struct DivConstants {
    Multiplier multiplier;
    ShiftAmount shiftAmount;
  };

  using Div32Constants = DivConstants<int64_t, int32_t>;

  static auto computeSignedDivisionConstants(int32_t d) {
    return computeDivisionConstants(mozilla::Abs(d), 31);
  }

  static auto computeUnsignedDivisionConstants(uint32_t d) {
    return computeDivisionConstants(d, 32);
  }

  using Div64Constants = DivConstants<Int128, int32_t>;

  static auto computeSignedDivisionConstants(int64_t d) {
    return computeDivisionConstants(mozilla::Abs(d), 63);
  }

  static auto computeUnsignedDivisionConstants(uint64_t d) {
    return computeDivisionConstants(d, 64);
  }

 private:
  static Div32Constants computeDivisionConstants(uint32_t d, int maxLog);
  static Div64Constants computeDivisionConstants(uint64_t d, int maxLog);
};

}  // namespace js::jit

#endif /* jit_ReciprocalMulConstants_h */
