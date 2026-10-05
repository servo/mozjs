/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_Int128_h
#define vm_Int128_h

#include "mozilla/Assertions.h"
#include "mozilla/Compiler.h"

#include <bit>
#include <climits>
#include <limits>
#include <stdint.h>
#include <utility>

namespace js {

// Workaround for <https://gcc.gnu.org/bugzilla/show_bug.cgi?id=98712>.
#if MOZ_IS_GCC
#  if MOZ_GCC_VERSION_AT_LEAST(12, 1, 0)
#    define CONSTEXPR_DEFAULT_COMPARE_WITH_INHERITANCE 1
#  else
#    define CONSTEXPR_DEFAULT_COMPARE_WITH_INHERITANCE 0
#  endif
#else
#  define CONSTEXPR_DEFAULT_COMPARE_WITH_INHERITANCE 1
#endif

namespace detail {
template <std::endian endianness>
struct Uint128Layout;

template <>
struct Uint128Layout<std::endian::little> {
  uint64_t low = 0;
  uint64_t high = 0;

  constexpr Uint128Layout(uint64_t low, uint64_t high) : low(low), high(high) {}
  constexpr Uint128Layout() = default;

  constexpr bool operator==(const Uint128Layout&) const = default;
};

template <>
struct Uint128Layout<std::endian::big> {
  uint64_t high = 0;
  uint64_t low = 0;

  constexpr Uint128Layout(uint64_t low, uint64_t high) : high(high), low(low) {}
  constexpr Uint128Layout() = default;

  constexpr bool operator==(const Uint128Layout&) const = default;
};

template <std::endian endianness>
using Int128Layout = Uint128Layout<endianness>;
}  // namespace detail

class Int128;
class Uint128;

/**
 * Unsigned 128-bit integer, implemented as a pair of unsigned 64-bit integers.
 *
 * Supports all basic arithmetic operators.
 */
class alignas(16) Uint128 final
    : private detail::Uint128Layout<std::endian::native> {
  friend class Int128;

  using Uint128Layout = detail::Uint128Layout<std::endian::native>;

  constexpr Uint128(uint64_t low, uint64_t high) : Uint128Layout(low, high) {}

  /**
   * Return the high double-word of the multiplication of `u * v`.
   *
   * Based on "Multiply high unsigned" from Hacker's Delight, 2nd edition,
   * figure 8-2.
   */
  static constexpr uint64_t mulhu(uint64_t u, uint64_t v) {
    uint64_t u0 = u & 0xffff'ffff;
    uint64_t u1 = u >> 32;

    uint64_t v0 = v & 0xffff'ffff;
    uint64_t v1 = v >> 32;

    uint64_t w0 = u0 * v0;
    uint64_t t = u1 * v0 + (w0 >> 32);
    uint64_t w1 = t & 0xffff'ffff;
    uint64_t w2 = t >> 32;
    w1 = u0 * v1 + w1;
    return u1 * v1 + w2 + (w1 >> 32);
  }

  static double toDouble(const Uint128& x, bool negative);

 public:
  constexpr Uint128() = default;
  constexpr Uint128(const Uint128&) = default;

  explicit constexpr Uint128(int value)
      : Uint128(uint64_t(value), uint64_t(value < 0 ? -1 : 0)) {}
  explicit constexpr Uint128(long value)
      : Uint128(uint64_t(value), uint64_t(value < 0 ? -1 : 0)) {}
  explicit constexpr Uint128(long long value)
      : Uint128(uint64_t(value), uint64_t(value < 0 ? -1 : 0)) {}

  explicit constexpr Uint128(unsigned int value)
      : Uint128(uint64_t(value), uint64_t(0)) {}
  explicit constexpr Uint128(unsigned long value)
      : Uint128(uint64_t(value), uint64_t(0)) {}
  explicit constexpr Uint128(unsigned long long value)
      : Uint128(uint64_t(value), uint64_t(0)) {}

  constexpr auto operator<=>(const Uint128& other) const {
    if (high == other.high) {
      return low <=> other.low;
    }
    return high <=> other.high;
  }

#if CONSTEXPR_DEFAULT_COMPARE_WITH_INHERITANCE
  constexpr bool operator==(const Uint128&) const = default;
#else
  constexpr bool operator==(const Uint128& other) const {
    return Uint128Layout::operator==(other);
  }
#endif

  explicit constexpr operator bool() const { return !(*this == Uint128{}); }

  explicit constexpr operator int8_t() const { return int8_t(low); }
  explicit constexpr operator int16_t() const { return int16_t(low); }
  explicit constexpr operator int32_t() const { return int32_t(low); }
  explicit constexpr operator int64_t() const { return int64_t(low); }

  explicit constexpr operator uint8_t() const { return uint8_t(low); }
  explicit constexpr operator uint16_t() const { return uint16_t(low); }
  explicit constexpr operator uint32_t() const { return uint32_t(low); }
  explicit constexpr operator uint64_t() const { return uint64_t(low); }

  explicit constexpr operator Int128() const;

  explicit operator double() const { return toDouble(*this, false); }

  constexpr Uint128 operator+(const Uint128& other) const {
    // "§2-16 Double-Length Add/Subtract" from Hacker's Delight, 2nd edition.
    Uint128 result;
    result.low = low + other.low;
    result.high = high + other.high + static_cast<uint64_t>(result.low < low);
    return result;
  }

  constexpr Uint128 operator-(const Uint128& other) const {
    // "§2-16 Double-Length Add/Subtract" from Hacker's Delight, 2nd edition.
    Uint128 result;
    result.low = low - other.low;
    result.high = high - other.high - static_cast<uint64_t>(low < other.low);
    return result;
  }

  constexpr Uint128 operator*(const Uint128& other) const {
    uint64_t w01 = low * other.high;
    uint64_t w10 = high * other.low;
    uint64_t w00 = mulhu(low, other.low);

    uint64_t w1 = w00 + w10 + w01;
    uint64_t w0 = low * other.low;

    return Uint128{w0, w1};
  }

  /**
   * Return the quotient and remainder of the division.
   */
  constexpr std::pair<Uint128, Uint128> divrem(const Uint128& divisor) const {
    return udivdi(*this, divisor);
  }

  constexpr Uint128 operator/(const Uint128& other) const {
    auto [quot, rem] = divrem(other);
    return quot;
  }

  constexpr Uint128 operator%(const Uint128& other) const {
    auto [quot, rem] = divrem(other);
    return rem;
  }

  constexpr Uint128 operator<<(int shift) const {
    MOZ_ASSERT(0 <= shift && shift <= 127, "undefined shift amount");

    // Ensure the shift amount is in range.
    shift &= 127;

    // "§2-17 Double-Length Shifts" from Hacker's Delight, 2nd edition.
    if (shift >= 64) {
      uint64_t y0 = 0;
      uint64_t y1 = low << (shift - 64);
      return Uint128{y0, y1};
    }
    if (shift > 0) {
      uint64_t y0 = low << shift;
      uint64_t y1 = high << shift | low >> (64 - shift);
      return Uint128{y0, y1};
    }
    return *this;
  }

  constexpr Uint128 operator>>(int shift) const {
    MOZ_ASSERT(0 <= shift && shift <= 127, "undefined shift amount");

    // Ensure the shift amount is in range.
    shift &= 127;

    // "§2-17 Double-Length Shifts" from Hacker's Delight, 2nd edition.
    if (shift >= 64) {
      uint64_t y0 = high >> (shift - 64);
      uint64_t y1 = 0;
      return Uint128{y0, y1};
    }
    if (shift > 0) {
      uint64_t y0 = low >> shift | high << (64 - shift);
      uint64_t y1 = high >> shift;
      return Uint128{y0, y1};
    }
    return *this;
  }

  constexpr Uint128 operator&(const Uint128& other) const {
    return Uint128{low & other.low, high & other.high};
  }

  constexpr Uint128 operator|(const Uint128& other) const {
    return Uint128{low | other.low, high | other.high};
  }

  constexpr Uint128 operator^(const Uint128& other) const {
    return Uint128{low ^ other.low, high ^ other.high};
  }

  constexpr Uint128 operator~() const { return Uint128{~low, ~high}; }

  constexpr Uint128 operator-() const { return Uint128{} - *this; }

  constexpr Uint128 operator+() const { return *this; }

  constexpr Uint128& operator++() {
    *this = *this + Uint128{1, 0};
    return *this;
  }

  constexpr Uint128 operator++(int) {
    auto result = *this;
    ++*this;
    return result;
  }

  constexpr Uint128& operator--() {
    *this = *this - Uint128{1, 0};
    return *this;
  }

  constexpr Uint128 operator--(int) {
    auto result = *this;
    --*this;
    return result;
  }

  constexpr Uint128 operator+=(const Uint128& other) {
    *this = *this + other;
    return *this;
  }

  constexpr Uint128 operator-=(const Uint128& other) {
    *this = *this - other;
    return *this;
  }

  constexpr Uint128 operator*=(const Uint128& other) {
    *this = *this * other;
    return *this;
  }

  constexpr Uint128 operator/=(const Uint128& other) {
    *this = *this / other;
    return *this;
  }

  constexpr Uint128 operator%=(const Uint128& other) {
    *this = *this % other;
    return *this;
  }

  constexpr Uint128 operator&=(const Uint128& other) {
    *this = *this & other;
    return *this;
  }

  constexpr Uint128 operator|=(const Uint128& other) {
    *this = *this | other;
    return *this;
  }

  constexpr Uint128 operator^=(const Uint128& other) {
    *this = *this ^ other;
    return *this;
  }

  constexpr Uint128 operator<<=(int shift) {
    *this = *this << shift;
    return *this;
  }

  constexpr Uint128 operator>>=(int shift) {
    *this = *this >> shift;
    return *this;
  }

 private:
  /**
   * Based on "Divide long unsigned, using fullword division instructions" from
   * Hacker's Delight, 2nd edition, figure 9-3.
   */
  static constexpr std::pair<uint64_t, uint64_t> divlu(uint64_t u1, uint64_t u0,
                                                       uint64_t v) {
    // Number base (32 bits).
    constexpr uint64_t base = 4294967296;

    // If overflow, set the remainder to an impossible value and return the
    // largest possible quotient.
    if (u1 >= v) {
      return {UINT64_MAX, UINT64_MAX};
    }

    // Shift amount for normalization. (0 <= s <= 63)
    int64_t s = std::countl_zero(v);

    // Normalize the divisor.
    v = v << s;

    // Normalized divisor digits.
    //
    // Break divisor up into two 32-bit digits.
    uint64_t vn1 = v >> 32;
    uint64_t vn0 = uint32_t(v);

    // Dividend digit pairs.
    //
    // Shift dividend left.
    uint64_t un32 = (u1 << s) | ((u0 >> ((64 - s) & 63)) & (-s >> 63));
    uint64_t un10 = u0 << s;

    // Normalized dividend least significant digits.
    //
    // Break right half of dividend into two digits.
    uint64_t un1 = un10 >> 32;
    uint64_t un0 = uint32_t(un10);

    // Compute the first quotient digit and its remainder.
    uint64_t q1 = un32 / vn1;
    uint64_t rhat = un32 - q1 * vn1;
    while (q1 >= base || q1 * vn0 > base * rhat + un1) {
      q1 -= 1;
      rhat += vn1;
      if (rhat >= base) {
        break;
      }
    }

    // Multiply and subtract.
    uint64_t un21 = un32 * base + un1 - q1 * v;

    // Compute the second quotient digit and its remainder.
    uint64_t q0 = un21 / vn1;
    rhat = un21 - q0 * vn1;
    while (q0 >= base || q0 * vn0 > base * rhat + un0) {
      q0 -= 1;
      rhat += vn1;
      if (rhat >= base) {
        break;
      }
    }

    // Return the quotient and remainder.
    uint64_t q = q1 * base + q0;
    uint64_t r = (un21 * base + un0 - q0 * v) >> s;
    return {q, r};
  }

  /**
   * Based on "Unsigned doubleword division from long division" from
   * Hacker's Delight, 2nd edition, figure 9-5.
   */
  static constexpr std::pair<Uint128, Uint128> udivdi(const Uint128& u,
                                                      const Uint128& v) {
    MOZ_ASSERT(v != Uint128{});

    // If v < 2**64
    if (v.high == 0) {
      // If u < 2**64
      if (u.high == 0) {
        // Prefer built-in division if possible.
        return {Uint128{u.low / v.low, 0}, Uint128{u.low % v.low, 0}};
      }

      // If u/v cannot overflow, just do one division.
      if (Uint128{u.high, 0} < v) {
        auto [q, r] = divlu(u.high, u.low, v.low);
        return {Uint128{q, 0}, Uint128{r, 0}};
      }

      // If u/v would overflow: Break u up into two halves.

      // First quotient digit and first remainder, < v.
      auto [q1, r1] = divlu(0, u.high, v.low);

      // Second quotient digit.
      auto [q0, r0] = divlu(r1, u.low, v.low);

      // Return quotient and remainder.
      return {Uint128{q0, q1}, Uint128{r0, 0}};
    }

    // Here v >= 2**64.

    // 0 <= n <= 63
    auto n = std::countl_zero(v.high);

    // Normalize the divisor so its MSB is 1.
    auto v1 = (v << n).high;

    // To ensure no overflow.
    auto u1 = u >> 1;

    // Get quotient from divide unsigned instruction.
    auto [q1, r1] = divlu(u1.high, u1.low, v1);

    // Undo normalization and division of u by 2.
    auto q0 = (Uint128{q1, 0} << n) >> 63;

    // Make q0 correct or too small by 1.
    if (q0 != Uint128{0}) {
      q0 -= Uint128{1};
    }

    // Now q0 is correct.
    auto r0 = u - q0 * v;
    if (r0 >= v) {
      q0 += Uint128{1};
      r0 -= v;
    }

    // Return quotient and remainder.
    return {q0, r0};
  }
};

/**
 * Signed 128-bit integer, implemented as a pair of unsigned 64-bit integers.
 *
 * Supports all basic arithmetic operators.
 */
class alignas(16) Int128 final
    : private detail::Int128Layout<std::endian::native> {
  friend class Uint128;

  using Int128Layout = detail::Int128Layout<std::endian::native>;

  constexpr Int128(uint64_t low, uint64_t high) : Int128Layout(low, high) {}

  /**
   * Based on "Signed doubleword division from unsigned doubleword division"
   * from Hacker's Delight, 2nd edition, figure 9-6.
   */
  static constexpr std::pair<Int128, Int128> divdi(const Int128& u,
                                                   const Int128& v) {
    auto [q, r] = Uint128::udivdi(u.abs(), v.abs());

    // If u and v have different signs, negate q.
    // If is negative, negate r.
    auto t = static_cast<Uint128>((u ^ v) >> 127);
    auto s = static_cast<Uint128>(u >> 127);
    return {static_cast<Int128>((q ^ t) - t), static_cast<Int128>((r ^ s) - s)};
  }

 public:
  constexpr Int128() = default;
  constexpr Int128(const Int128&) = default;

  explicit constexpr Int128(int value)
      : Int128(uint64_t(value), uint64_t(value < 0 ? -1 : 0)) {}
  explicit constexpr Int128(long value)
      : Int128(uint64_t(value), uint64_t(value < 0 ? -1 : 0)) {}
  explicit constexpr Int128(long long value)
      : Int128(uint64_t(value), uint64_t(value < 0 ? -1 : 0)) {}

  explicit constexpr Int128(unsigned int value)
      : Int128(uint64_t(value), uint64_t(0)) {}
  explicit constexpr Int128(unsigned long value)
      : Int128(uint64_t(value), uint64_t(0)) {}
  explicit constexpr Int128(unsigned long long value)
      : Int128(uint64_t(value), uint64_t(0)) {}

  constexpr auto operator<=>(const Int128& other) const {
    if (high == other.high) {
      return low <=> other.low;
    }
    return int64_t(high) <=> int64_t(other.high);
  }

#if CONSTEXPR_DEFAULT_COMPARE_WITH_INHERITANCE
  constexpr bool operator==(const Int128&) const = default;
#else
  constexpr bool operator==(const Int128& other) const {
    return Int128Layout::operator==(other);
  }
#endif

  explicit constexpr operator bool() const { return !(*this == Int128{}); }

  explicit constexpr operator int8_t() const { return int8_t(low); }
  explicit constexpr operator int16_t() const { return int16_t(low); }
  explicit constexpr operator int32_t() const { return int32_t(low); }
  explicit constexpr operator int64_t() const { return int64_t(low); }

  explicit constexpr operator uint8_t() const { return uint8_t(low); }
  explicit constexpr operator uint16_t() const { return uint16_t(low); }
  explicit constexpr operator uint32_t() const { return uint32_t(low); }
  explicit constexpr operator uint64_t() const { return uint64_t(low); }

  explicit constexpr operator Uint128() const { return Uint128{low, high}; }

  explicit operator double() const {
    return Uint128::toDouble(abs(), *this < Int128{0});
  }

  /**
   * Return the quotient and remainder of the division.
   */
  constexpr std::pair<Int128, Int128> divrem(const Int128& divisor) const {
    return divdi(*this, divisor);
  }

  /**
   * Return the absolute value of this integer.
   */
  constexpr Uint128 abs() const {
    if (*this >= Int128{}) {
      return Uint128{low, high};
    }
    auto neg = -*this;
    return Uint128{neg.low, neg.high};
  }

  constexpr Int128 operator+(const Int128& other) const {
    return Int128{Uint128{*this} + Uint128{other}};
  }

  constexpr Int128 operator-(const Int128& other) const {
    return Int128{Uint128{*this} - Uint128{other}};
  }

  constexpr Int128 operator*(const Int128& other) const {
    return Int128{Uint128{*this} * Uint128{other}};
  }

  constexpr Int128 operator/(const Int128& other) const {
    auto [quot, rem] = divrem(other);
    return quot;
  }

  constexpr Int128 operator%(const Int128& other) const {
    auto [quot, rem] = divrem(other);
    return rem;
  }

  constexpr Int128 operator<<(int shift) const {
    return Int128{Uint128{*this} << shift};
  }

  constexpr Int128 operator>>(int shift) const {
    MOZ_ASSERT(0 <= shift && shift <= 127, "undefined shift amount");

    // Ensure the shift amount is in range.
    shift &= 127;

    // "§2-17 Double-Length Shifts" from Hacker's Delight, 2nd edition.
    if (shift >= 64) {
      uint64_t y0 = uint64_t(int64_t(high) >> (shift - 64));
      uint64_t y1 = uint64_t(int64_t(high) >> 63);
      return Int128{y0, y1};
    }
    if (shift > 0) {
      uint64_t y0 = low >> shift | high << (64 - shift);
      uint64_t y1 = uint64_t(int64_t(high) >> shift);
      return Int128{y0, y1};
    }
    return *this;
  }

  constexpr Int128 operator&(const Int128& other) const {
    return Int128{low & other.low, high & other.high};
  }

  constexpr Int128 operator|(const Int128& other) const {
    return Int128{low | other.low, high | other.high};
  }

  constexpr Int128 operator^(const Int128& other) const {
    return Int128{low ^ other.low, high ^ other.high};
  }

  constexpr Int128 operator~() const { return Int128{~low, ~high}; }

  constexpr Int128 operator-() const { return Int128{} - *this; }

  constexpr Int128 operator+() const { return *this; }

  constexpr Int128& operator++() {
    *this = *this + Int128{1, 0};
    return *this;
  }

  constexpr Int128 operator++(int) {
    auto result = *this;
    ++*this;
    return result;
  }

  constexpr Int128& operator--() {
    *this = *this - Int128{1, 0};
    return *this;
  }

  constexpr Int128 operator--(int) {
    auto result = *this;
    --*this;
    return result;
  }

  constexpr Int128 operator+=(const Int128& other) {
    *this = *this + other;
    return *this;
  }

  constexpr Int128 operator-=(const Int128& other) {
    *this = *this - other;
    return *this;
  }

  constexpr Int128 operator*=(const Int128& other) {
    *this = *this * other;
    return *this;
  }

  constexpr Int128 operator/=(const Int128& other) {
    *this = *this / other;
    return *this;
  }

  constexpr Int128 operator%=(const Int128& other) {
    *this = *this % other;
    return *this;
  }

  constexpr Int128 operator&=(const Int128& other) {
    *this = *this & other;
    return *this;
  }

  constexpr Int128 operator|=(const Int128& other) {
    *this = *this | other;
    return *this;
  }

  constexpr Int128 operator^=(const Int128& other) {
    *this = *this ^ other;
    return *this;
  }

  constexpr Int128 operator<<=(int shift) {
    *this = *this << shift;
    return *this;
  }

  constexpr Int128 operator>>=(int shift) {
    *this = *this >> shift;
    return *this;
  }
};

constexpr Uint128::operator Int128() const { return Int128{low, high}; }

#undef CONSTEXPR_DEFAULT_COMPARE_WITH_INHERITANCE

} /* namespace js */

template <>
class std::numeric_limits<js::Int128> {
 public:
  static constexpr bool is_specialized = true;
  static constexpr bool is_signed = true;
  static constexpr bool is_integer = true;
  static constexpr bool is_exact = true;
  static constexpr bool has_infinity = false;
  static constexpr bool has_quiet_NaN = false;
  static constexpr bool has_signaling_NaN = false;
  static constexpr std::float_denorm_style has_denorm = std::denorm_absent;
  static constexpr bool has_denorm_loss = false;
  static constexpr std::float_round_style round_style = std::round_toward_zero;
  static constexpr bool is_iec559 = false;
  static constexpr bool is_bounded = true;
  static constexpr bool is_modulo = true;
  static constexpr int digits = CHAR_BIT * sizeof(js::Int128) - 1;
  static constexpr int digits10 = int(digits * /* std::log10(2) */ 0.30102999);
  static constexpr int max_digits10 = 0;
  static constexpr int radix = 2;
  static constexpr int min_exponent = 0;
  static constexpr int min_exponent10 = 0;
  static constexpr int max_exponent = 0;
  static constexpr int max_exponent10 = 0;
  static constexpr bool traps = true;
  static constexpr bool tinyness_before = false;

  static constexpr auto min() noexcept { return js::Int128{1} << 127; }
  static constexpr auto lowest() noexcept { return min(); }
  static constexpr auto max() noexcept { return ~min(); }
  static constexpr auto epsilon() noexcept { return js::Int128{}; }
  static constexpr auto round_error() noexcept { return js::Int128{}; }
  static constexpr auto infinity() noexcept { return js::Int128{}; }
  static constexpr auto quiet_NaN() noexcept { return js::Int128{}; }
  static constexpr auto signaling_NaN() noexcept { return js::Int128{}; }
  static constexpr auto denorm_min() noexcept { return js::Int128{}; }
};

template <>
class std::numeric_limits<js::Uint128> {
 public:
  static constexpr bool is_specialized = true;
  static constexpr bool is_signed = false;
  static constexpr bool is_integer = true;
  static constexpr bool is_exact = true;
  static constexpr bool has_infinity = false;
  static constexpr bool has_quiet_NaN = false;
  static constexpr bool has_signaling_NaN = false;
  static constexpr std::float_denorm_style has_denorm = std::denorm_absent;
  static constexpr bool has_denorm_loss = false;
  static constexpr std::float_round_style round_style = std::round_toward_zero;
  static constexpr bool is_iec559 = false;
  static constexpr bool is_bounded = true;
  static constexpr bool is_modulo = true;
  static constexpr int digits = CHAR_BIT * sizeof(js::Uint128);
  static constexpr int digits10 = int(digits * /* std::log10(2) */ 0.30102999);
  static constexpr int max_digits10 = 0;
  static constexpr int radix = 2;
  static constexpr int min_exponent = 0;
  static constexpr int min_exponent10 = 0;
  static constexpr int max_exponent = 0;
  static constexpr int max_exponent10 = 0;
  static constexpr bool traps = true;
  static constexpr bool tinyness_before = false;

  static constexpr auto min() noexcept { return js::Uint128{}; }
  static constexpr auto lowest() noexcept { return min(); }
  static constexpr auto max() noexcept { return ~js::Uint128{}; }
  static constexpr auto epsilon() noexcept { return js::Uint128{}; }
  static constexpr auto round_error() noexcept { return js::Uint128{}; }
  static constexpr auto infinity() noexcept { return js::Uint128{}; }
  static constexpr auto quiet_NaN() noexcept { return js::Uint128{}; }
  static constexpr auto signaling_NaN() noexcept { return js::Uint128{}; }
  static constexpr auto denorm_min() noexcept { return js::Uint128{}; }
};

#endif /* vm_Int128_h */
