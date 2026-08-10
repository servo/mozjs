/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Provides checked integers operations, detecting overflow. */

#ifndef mozilla_CheckedArithmetic_h
#define mozilla_CheckedArithmetic_h

namespace mozilla {

template <typename T>
[[nodiscard]] constexpr bool SafeAdd(T lhs, T rhs, T* res) {
  return !__builtin_add_overflow(lhs, rhs, res);
}

template <typename T>
[[nodiscard]] constexpr bool SafeSub(T lhs, T rhs, T* res) {
  return !__builtin_sub_overflow(lhs, rhs, res);
}

template <typename T>
[[nodiscard]] constexpr bool SafeMul(T lhs, T rhs, T* res) {
  return !__builtin_mul_overflow(lhs, rhs, res);
}

}  // namespace mozilla

#endif /* mozilla_CheckedArithmetic_h */
