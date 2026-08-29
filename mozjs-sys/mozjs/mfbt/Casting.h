/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Cast operations to supplement the built-in casting operations. */

#ifndef mozilla_Casting_h
#define mozilla_Casting_h

#include "mozilla/Assertions.h"

#include <cstdint>
#include <cmath>
#include <limits>
#include <type_traits>

#ifndef __clang__
#  include <cstring>
#endif

#ifdef DEBUG
#  include "fmt/format.h"  // IWYU pragma: keep(for fmt::)
#  include <cstdio>
#endif

namespace mozilla {

/**
 * Sets the outparam value of type |To| with the same underlying bit pattern of
 * |aFrom|.
 *
 * |To| and |From| must be types of the same size; be careful of cross-platform
 * size differences, or this might fail to compile on some but not all
 * platforms.
 *
 * There is also a variant that returns the value directly.  In most cases, the
 * two variants should be identical.  However, in the specific case of x86
 * chips, the behavior differs: returning floating-point values directly is done
 * through the x87 stack, and x87 loads and stores turn signaling NaNs into
 * quiet NaNs... silently.  Returning floating-point values via outparam,
 * however, is done entirely within the SSE registers when SSE2 floating-point
 * is enabled in the compiler, which has semantics-preserving behavior you would
 * expect.
 *
 * If preserving the distinction between signaling NaNs and quiet NaNs is
 * important to you, you should use the outparam version.  In all other cases,
 * you should use the direct return version.
 */
template <typename To, typename From>
inline void BitwiseCast(const From aFrom, To* aResult) {
  // FIXME: Use std::bit_cast for the value returning version once we move to
  // C++20 and minimum GCC to 11. The outparam version can't use std::bit_cast
  // because of the x87 stack issue described above.
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 11)
  *aResult = __builtin_bit_cast(To, aFrom);
#else
  static_assert(sizeof(From) == sizeof(To),
                "To and From must have the same size");

  // We could maybe downgrade these to std::is_trivially_copyable, but the
  // various STLs we use don't all provide it.
  static_assert(std::is_trivial<From>::value,
                "shouldn't bitwise-copy a type having non-trivial "
                "initialization");
  static_assert(std::is_trivial<To>::value,
                "shouldn't bitwise-copy a type having non-trivial "
                "initialization");
  std::memcpy(static_cast<void*>(aResult), static_cast<const void*>(&aFrom),
              sizeof(From));
#endif
}

template <typename To, typename From>
inline To BitwiseCast(const From aFrom) {
  To temp;
  BitwiseCast<To, From>(aFrom, &temp);
  return temp;
}

namespace detail {

template <typename T>
constexpr int64_t safe_integer() {
  static_assert(std::is_floating_point_v<T>);
  return std::pow(2, std::numeric_limits<T>::digits);
}

template <typename T>
constexpr uint64_t safe_integer_unsigned() {
  static_assert(std::is_floating_point_v<T>);
  return std::pow(2, std::numeric_limits<T>::digits);
}

template <typename T>
const char* TypeToStringFallback();

template <typename T>
inline constexpr const char* TypeToString() {
  return TypeToStringFallback<T>();
}

#define T2S(type)                                     \
  template <>                                         \
  inline constexpr const char* TypeToString<type>() { \
    return #type;                                     \
  }

#define T2SF(type)                                            \
  template <>                                                 \
  inline constexpr const char* TypeToStringFallback<type>() { \
    return #type;                                             \
  }

T2S(uint8_t);
T2S(uint16_t);
T2S(uint32_t);
T2S(uint64_t);
T2S(int8_t);
T2S(int16_t);
T2S(int32_t);
T2S(int64_t);
T2S(char16_t);
T2S(char32_t);
T2SF(int);
T2SF(unsigned int);
T2SF(long);
T2SF(unsigned long);
T2S(float);
T2S(double);

#undef T2S
#undef T2SF

#ifdef DEBUG
template <typename In, typename Out>
inline void DiagnosticMessage(In aIn, char aDiagnostic[1024]) {
  if constexpr (std::is_same_v<In, char> || std::is_same_v<In, wchar_t> ||
                std::is_same_v<In, char16_t> || std::is_same_v<In, char32_t>) {
    static_assert(sizeof(wchar_t) <= sizeof(int32_t));
    // Characters types are printed in hexadecimal for two reasons:
    // - to easily debug problems with non-printable characters.
    // - {fmt} refuses to format a string with mixed character type.
    // It's always possible to cast them to int64_t for lossless printing of the
    // value.
    auto [out, size] = fmt::format_to_n(
        aDiagnostic, 1023, "Cannot cast {:x} from {} to {}: out of range",
        static_cast<int64_t>(aIn), TypeToString<In>(), TypeToString<Out>());
    *out = 0;
  } else {
    auto [out, size] = fmt::format_to_n(
        aDiagnostic, 1023, "Cannot cast {} from {} to {}: out of range", aIn,
        TypeToString<In>(), TypeToString<Out>());
    *out = 0;
  }
}
#endif

template <typename In, typename Out>
bool IsInBounds(In aIn) {
  constexpr bool inSigned = std::is_signed_v<In>;
  constexpr bool outSigned = std::is_signed_v<Out>;
  constexpr bool bothSigned = inSigned && outSigned;
  constexpr bool bothUnsigned = !inSigned && !outSigned;
  constexpr bool inFloat = std::is_floating_point_v<In>;
  constexpr bool outFloat = std::is_floating_point_v<Out>;
  constexpr bool bothFloat = inFloat && outFloat;
  constexpr bool noneFloat = !inFloat && !outFloat;
  constexpr Out outMax = std::numeric_limits<Out>::max();
  constexpr Out outMin = std::numeric_limits<Out>::lowest();

  // This selects the widest of two types, and is used to cast throughout.
  using select_widest = std::conditional_t<(sizeof(In) > sizeof(Out)), In, Out>;

  if constexpr (bothFloat) {
    return select_widest(outMin) <= aIn && aIn <= select_widest(outMax);
  }
  // Normal casting applies, the floating point number is floored.
  else if constexpr (inFloat && !outFloat) {
    static_assert(sizeof(aIn) <= sizeof(int64_t));
    // Check if the input floating point is larger than the output bounds. This
    // catches situations where the input is a float larger than the max of the
    // output type.
    if (aIn < static_cast<double>(outMin) ||
        aIn > static_cast<double>(outMax)) {
      return false;
    }
    // At this point we know that the input can be converted to an integer.
    // Check if it's larger than the bounds of the target integer.
    if constexpr (outSigned) {
      int64_t asInteger = static_cast<int64_t>(aIn);
      return outMin <= asInteger && asInteger <= outMax;
    } else {
      uint64_t asInteger = static_cast<uint64_t>(aIn);
      return asInteger <= outMax;
    }
  }

  // Checks if the integer is representable exactly as a floating point value of
  // a specific width.
  else if constexpr (!inFloat && outFloat) {
    if constexpr (inSigned) {
      return -safe_integer<Out>() <= aIn && aIn <= safe_integer<Out>();
    } else {
      return aIn < safe_integer_unsigned<Out>();
    }
  }

  else if constexpr (noneFloat) {
    if constexpr (bothUnsigned) {
      return aIn <= select_widest(outMax);
    } else if constexpr (bothSigned) {
      return select_widest(outMin) <= aIn && aIn <= select_widest(outMax);
    } else if constexpr (inSigned && !outSigned) {
      return aIn >= 0 && std::make_unsigned_t<In>(aIn) <= outMax;
    } else if constexpr (!inSigned && outSigned) {
      return aIn <= select_widest(outMax);
    }
  }
}

}  // namespace detail

/**
 * Cast a value of type |From| to a value of type |To|, asserting that the cast
 * will be a safe cast per C++ (that is, that |to| is in the range of values
 * permitted for the type |From|).
 * In particular, this will fail if a integer cannot be represented exactly as a
 * floating point value, because it's too large.
 */
template <typename To, typename From>
inline To AssertedCast(const From aFrom) {
  static_assert(std::is_arithmetic_v<To> && std::is_arithmetic_v<From>);
#ifdef DEBUG
  if (!detail::IsInBounds<From, To>(aFrom)) {
    char buf[1024];
    detail::DiagnosticMessage<From, To>(aFrom, buf);
    fprintf(stderr, "AssertedCast error: %s\n", buf);
    MOZ_CRASH();
  }
#endif
  return static_cast<To>(aFrom);
}

/**
 * Cast a value of numeric type |From| to a value of numeric type |To|, release
 * asserting that the cast will be a safe cast per C++ (that is, that |to| is in
 * the range of values permitted for the type |From|).
 * In particular, this will fail if a integer cannot be represented exactly as a
 * floating point value, because it's too large.
 */
template <typename To, typename From>
inline To ReleaseAssertedCast(const From aFrom) {
  static_assert(std::is_arithmetic_v<To> && std::is_arithmetic_v<From>);
  MOZ_RELEASE_ASSERT((detail::IsInBounds<From, To>(aFrom)));
  return static_cast<To>(aFrom);
}

/**
 * Cast from type From to type To, clamping to minimum and maximum value of the
 * destination type if needed.
 */
template <typename To, typename From>
inline To SaturatingCast(const From aFrom) {
  static_assert(std::is_arithmetic_v<To> && std::is_arithmetic_v<From>);
  // This implementation works up to 64-bits integers.
  static_assert(sizeof(From) <= 8 && sizeof(To) <= 8);
  constexpr bool fromFloat = std::is_floating_point_v<From>;
  constexpr bool toFloat = std::is_floating_point_v<To>;

  // It's not clear what the caller wants here, it could be round, truncate,
  // closest value, etc.
  static_assert((fromFloat && !toFloat) || (!fromFloat && !toFloat),
                "Handle manually depending on desired behaviour");

  // If the source is floating point and the destination isn't, it can be that
  // casting changes the value unexpectedly. Casting to double and clamping to
  // the max of the destination type is correct, this also handles infinity.
  if constexpr (fromFloat) {
    if (aFrom > static_cast<double>(std::numeric_limits<To>::max())) {
      return std::numeric_limits<To>::max();
    }
    if (aFrom < static_cast<double>(std::numeric_limits<To>::lowest())) {
      return std::numeric_limits<To>::lowest();
    }
    return static_cast<To>(aFrom);
  }
  // Source and destination are of opposite signedness
  if constexpr (std::is_signed_v<From> != std::is_signed_v<To>) {
    // Input is negative, output is unsigned, return 0
    if (std::is_signed_v<From> && aFrom < 0) {
      return 0;
    }
    // At this point the input is positive, cast everything to uint64_t for
    // simplicity and compare
    uint64_t inflated = AssertedCast<uint64_t>(aFrom);
    if (inflated > static_cast<uint64_t>(std::numeric_limits<To>::max())) {
      return std::numeric_limits<To>::max();
    }
    return static_cast<To>(aFrom);
  } else {
    // Regular case: clamp to destination type range
    if (aFrom > std::numeric_limits<To>::max()) {
      return std::numeric_limits<To>::max();
    }
    if (aFrom < std::numeric_limits<To>::lowest()) {
      return std::numeric_limits<To>::lowest();
    }
    return static_cast<To>(aFrom);
  }
}

namespace detail {

template <typename From>
class LazyAssertedCastT final {
  const From mVal;

 public:
  explicit LazyAssertedCastT(const From val) : mVal(val) {}

  template <typename To>
  operator To() const {
    return AssertedCast<To>(mVal);
  }
};

}  // namespace detail

/**
 * Like AssertedCast, but infers |To| for AssertedCast lazily based on usage.
 * > uint8_t foo = LazyAssertedCast(1000);  // boom
 */
template <typename From>
inline auto LazyAssertedCast(const From val) {
  return detail::LazyAssertedCastT<From>(val);
}

}  // namespace mozilla

#endif /* mozilla_Casting_h */
