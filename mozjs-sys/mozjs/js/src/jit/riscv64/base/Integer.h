// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef jit_riscv64_base_Integer_h
#define jit_riscv64_base_Integer_h

#include "mozilla/Assertions.h"

#include <stdint.h>

namespace js::jit {

//
// Constants from v8/src/common/globals.h
//

constexpr uint64_t kBitsPerByte = 8UL;

//
// Functions from v8/src/utils/utils.h
//

// Check number width.
inline constexpr bool is_intn(int64_t x, unsigned n) {
  MOZ_ASSERT((0 < n) && (n < 64));
  int64_t limit = static_cast<int64_t>(1) << (n - 1);
  return (-limit <= x) && (x < limit);
}

inline constexpr bool is_uintn(int64_t x, unsigned n) {
  MOZ_ASSERT((0 < n) && (n < (sizeof(x) * kBitsPerByte)));
  return !(x >> n);
}

// clang-format off
#define INT_1_TO_63_LIST(V)                                   \
  V(1) V(2) V(3) V(4) V(5) V(6) V(7) V(8) V(9) V(10)          \
  V(11) V(12) V(13) V(14) V(15) V(16) V(17) V(18) V(19) V(20) \
  V(21) V(22) V(23) V(24) V(25) V(26) V(27) V(28) V(29) V(30) \
  V(31) V(32) V(33) V(34) V(35) V(36) V(37) V(38) V(39) V(40) \
  V(41) V(42) V(43) V(44) V(45) V(46) V(47) V(48) V(49) V(50) \
  V(51) V(52) V(53) V(54) V(55) V(56) V(57) V(58) V(59) V(60) \
  V(61) V(62) V(63)
// clang-format on

#define DECLARE_IS_INT_N(N) \
  inline constexpr bool is_int##N(int64_t x) { return is_intn(x, N); }

#define DECLARE_IS_UINT_N(N)              \
  template <class T>                      \
  inline constexpr bool is_uint##N(T x) { \
    return is_uintn(x, N);                \
  }
INT_1_TO_63_LIST(DECLARE_IS_INT_N)
INT_1_TO_63_LIST(DECLARE_IS_UINT_N)

#undef DECLARE_IS_INT_N
#undef DECLARE_IS_UINT_N
#undef INT_1_TO_63_LIST

}  // namespace js::jit

#endif  // jit_riscv64_base_Integer_h
