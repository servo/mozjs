/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Provides checked integers, detecting integer overflow and divide-by-0. */

#ifndef mozilla_CheckedInt_h
#define mozilla_CheckedInt_h

#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/CheckedArithmetic.h"

#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace mozilla {

template <typename T>
class CheckedInt;

namespace detail {

/*
 * Step 1: manually record supported types
 *
 * What's nontrivial here is that there are different families of integer
 * types: basic integer types and stdint types. It is merrily undefined which
 * types from one family may be just typedefs for a type from another family.
 *
 * For example, on GCC 4.6, aside from the basic integer types, the only other
 * type that isn't just a typedef for some of them, is int8_t.
 */

template <typename IntegerType>
struct IsSupportedPass2 {
  static const bool value = false;
};

template <typename IntegerType>
struct IsSupported {
  static const bool value = IsSupportedPass2<IntegerType>::value;
};

template <>
struct IsSupported<int8_t> {
  static const bool value = true;
};

template <>
struct IsSupported<uint8_t> {
  static const bool value = true;
};

template <>
struct IsSupported<int16_t> {
  static const bool value = true;
};

template <>
struct IsSupported<uint16_t> {
  static const bool value = true;
};

template <>
struct IsSupported<int32_t> {
  static const bool value = true;
};

template <>
struct IsSupported<uint32_t> {
  static const bool value = true;
};

template <>
struct IsSupported<int64_t> {
  static const bool value = true;
};

template <>
struct IsSupported<uint64_t> {
  static const bool value = true;
};

template <>
struct IsSupportedPass2<signed char> {
  static const bool value = true;
};

template <>
struct IsSupportedPass2<unsigned char> {
  static const bool value = true;
};

template <>
struct IsSupportedPass2<short> {
  static const bool value = true;
};

template <>
struct IsSupportedPass2<unsigned short> {
  static const bool value = true;
};

template <>
struct IsSupportedPass2<int> {
  static const bool value = true;
};

template <>
struct IsSupportedPass2<unsigned int> {
  static const bool value = true;
};

template <>
struct IsSupportedPass2<long> {
  static const bool value = true;
};

template <>
struct IsSupportedPass2<unsigned long> {
  static const bool value = true;
};

template <>
struct IsSupportedPass2<long long> {
  static const bool value = true;
};

template <>
struct IsSupportedPass2<unsigned long long> {
  static const bool value = true;
};

/*
 * Step 2: Implement the actual validity checks.
 *
 * Ideas taken from IntegerLib, code different.
 */

template <typename T>
constexpr bool IsDivValid(T aX, T aY) {
  // Keep in mind that in the signed case, min/-1 is invalid because
  // abs(min)>max.
  return aY != 0 && !(std::is_signed_v<T> &&
                      aX == std::numeric_limits<T>::min() && aY == T(-1));
}

template <typename T>
constexpr bool IsModValid(T aX, T aY) {
  // Mod is valid iff Div is valid (since C++11).
  return IsDivValid(aX, aY);
}

template <typename T, bool IsSigned = std::is_signed_v<T>>
struct NegateImpl;

template <typename T>
struct NegateImpl<T, false> {
  static constexpr CheckedInt<T> negate(const CheckedInt<T>& aVal) {
    // Handle negation separately for signed/unsigned, for simpler code and to
    // avoid an MSVC warning negating an unsigned value.
    static_assert(std::in_range<T>(0), "Integer type can't represent 0");
    return CheckedInt<T>(T(0), aVal.isValid() && aVal.mValue == 0);
  }
};

template <typename T>
struct NegateImpl<T, true> {
  static constexpr CheckedInt<T> negate(const CheckedInt<T>& aVal) {
    // Watch out for the min-value, which (with twos-complement) can't be
    // negated as -min-value is then (max-value + 1).
    if (!aVal.isValid() || aVal.mValue == std::numeric_limits<T>::min()) {
      return CheckedInt<T>(aVal.mValue, false);
    }
    /* For some T, arithmetic ops automatically promote to a wider type, so
     * explicitly do the narrowing cast here.  The narrowing cast is valid
     * because we did the check for min value above. */
    return CheckedInt<T>(T(-aVal.mValue), true);
  }
};

}  // namespace detail

/*
 * Step 3: Now define the CheckedInt class.
 */

/**
 * @class CheckedInt
 * @brief Integer wrapper class checking for integer overflow and other errors
 * @param T the integer type to wrap. Can be any type among the following:
 *            - any basic integer type such as |int|
 *            - any stdint type such as |int8_t|
 *
 * This class implements guarded integer arithmetic. Do a computation, check
 * that isValid() returns true, you then have a guarantee that no problem, such
 * as integer overflow, happened during this computation, and you can call
 * value() to get the plain integer value.
 *
 * The arithmetic operators in this class are guaranteed not to raise a signal
 * (e.g. in case of a division by zero).
 *
 * For example, suppose that you want to implement a function that computes
 * (aX+aY)/aZ, that doesn't crash if aZ==0, and that reports on error (divide by
 * zero or integer overflow). You could code it as follows:
   @code
   bool computeXPlusYOverZ(int aX, int aY, int aZ, int* aResult)
   {
     CheckedInt<int> checkedResult = (CheckedInt<int>(aX) + aY) / aZ;
     if (checkedResult.isValid()) {
       *aResult = checkedResult.value();
       return true;
     } else {
       return false;
     }
   }
   @endcode
 *
 * Implicit conversion from plain integers to checked integers is allowed. The
 * plain integer is checked to be in range before being casted to the
 * destination type. This means that the following lines all compile, and the
 * resulting CheckedInts are correctly detected as valid or invalid:
 * @code
   // 1 is of type int, is found to be in range for uint8_t, x is valid
   CheckedInt<uint8_t> x(1);
   // -1 is of type int, is found not to be in range for uint8_t, x is invalid
   CheckedInt<uint8_t> x(-1);
   // -1 is of type int, is found to be in range for int8_t, x is valid
   CheckedInt<int8_t> x(-1);
   // 1000 is of type int16_t, is found not to be in range for int8_t,
   // x is invalid
   CheckedInt<int8_t> x(int16_t(1000));
   // 3123456789 is of type uint32_t, is found not to be in range for int32_t,
   // x is invalid
   CheckedInt<int32_t> x(uint32_t(3123456789));
 * @endcode
 * Implicit conversion from
 * checked integers to plain integers is not allowed. As shown in the
 * above example, to get the value of a checked integer as a normal integer,
 * call value().
 *
 * Arithmetic operations between checked and plain integers is allowed; the
 * result type is the type of the checked integer.
 *
 * Checked integers of different types cannot be used in the same arithmetic
 * expression.
 *
 * There are convenience typedefs for all stdint types, of the following form
 * (these are just 2 examples):
   @code
   typedef CheckedInt<int32_t> CheckedInt32;
   typedef CheckedInt<uint16_t> CheckedUint16;
   @endcode
 */
template <typename T>
class CheckedInt {
 protected:
  T mValue;
  bool mIsValid;

  template <typename U>
  constexpr CheckedInt(U aValue, bool aIsValid)
      : mValue(aValue), mIsValid(aIsValid) {
    static_assert(std::is_same_v<T, U>,
                  "this constructor must accept only T values");
    static_assert(detail::IsSupported<T>::value,
                  "This type is not supported by CheckedInt");
  }

  friend struct detail::NegateImpl<T>;

 public:
  /**
   * Constructs a checked integer with given @a value. The checked integer is
   * initialized as valid or invalid depending on whether the @a value
   * is in range.
   *
   * This constructor is not explicit. Instead, the type of its argument is a
   * separate template parameter, ensuring that no conversion is performed
   * before this constructor is actually called. As explained in the above
   * documentation for class CheckedInt, this constructor checks that its
   * argument is valid.
   */
  template <
      typename U,
      std::enable_if_t<!std::is_enum_v<U> && !std::is_same_v<U, bool>, int> = 0>
  MOZ_IMPLICIT MOZ_NO_ARITHMETIC_EXPR_IN_ARGUMENT constexpr CheckedInt(U aValue)
      : mValue(T(aValue)), mIsValid(std::in_range<T>(aValue)) {
    static_assert(
        detail::IsSupported<T>::value && detail::IsSupported<U>::value,
        "This type is not supported by CheckedInt");
  }

  template <typename U, std::enable_if_t<std::is_enum_v<U>, int> = 0>
  MOZ_IMPLICIT constexpr CheckedInt(U aValue)
      : CheckedInt(static_cast<std::underlying_type_t<U>>(aValue)) {}

  template <typename U, std::enable_if_t<std::is_same_v<U, bool>, int> = 0>
  MOZ_IMPLICIT constexpr CheckedInt(U aValue)
      : CheckedInt(static_cast<uint8_t>(aValue)) {}

  template <typename U>
  friend class CheckedInt;

  template <typename U>
  constexpr CheckedInt<U> toChecked() const {
    CheckedInt<U> ret(mValue);
    ret.mIsValid = ret.mIsValid && mIsValid;
    return ret;
  }

  /** Constructs a valid checked integer with initial value 0 */
  constexpr CheckedInt() : mValue(T(0)), mIsValid(true) {
    static_assert(detail::IsSupported<T>::value,
                  "This type is not supported by CheckedInt");
    static_assert(std::in_range<T>(0), "Integer type can't represent 0");
  }

  /** @returns the actual value */
  constexpr T value() const {
    MOZ_RELEASE_ASSERT(
        mIsValid,
        "Invalid checked integer (division by zero or integer overflow)");
    return mValue;
  }

  /**
   * @returns true if the checked integer is valid, i.e. is not the result
   * of an invalid operation or of an operation involving an invalid checked
   * integer
   */
  constexpr bool isValid() const { return mIsValid; }

  template <typename U>
  friend constexpr CheckedInt<U> operator+(const CheckedInt<U>& aLhs,
                                           const CheckedInt<U>& aRhs);
  template <typename U>
  constexpr CheckedInt& operator+=(U aRhs);
  constexpr CheckedInt& operator+=(const CheckedInt<T>& aRhs);

  template <typename U>
  friend constexpr CheckedInt<U> operator-(const CheckedInt<U>& aLhs,
                                           const CheckedInt<U>& aRhs);
  template <typename U>
  constexpr CheckedInt& operator-=(U aRhs);
  constexpr CheckedInt& operator-=(const CheckedInt<T>& aRhs);

  template <typename U>
  friend constexpr CheckedInt<U> operator*(const CheckedInt<U>& aLhs,
                                           const CheckedInt<U>& aRhs);
  template <typename U>
  constexpr CheckedInt& operator*=(U aRhs);
  constexpr CheckedInt& operator*=(const CheckedInt<T>& aRhs);

  template <typename U>
  friend constexpr CheckedInt<U> operator/(const CheckedInt<U>& aLhs,
                                           const CheckedInt<U>& aRhs);
  template <typename U>
  constexpr CheckedInt& operator/=(U aRhs);
  constexpr CheckedInt& operator/=(const CheckedInt<T>& aRhs);

  template <typename U>
  friend constexpr CheckedInt<U> operator%(const CheckedInt<U>& aLhs,
                                           const CheckedInt<U>& aRhs);
  template <typename U>
  constexpr CheckedInt& operator%=(U aRhs);
  constexpr CheckedInt& operator%=(const CheckedInt<T>& aRhs);

  constexpr CheckedInt operator-() const {
    return detail::NegateImpl<T>::negate(*this);
  }

  /**
   * @returns true if the left and right hand sides are valid
   * and have the same value.
   *
   * Note that these semantics are the reason why we don't offer
   * a operator!=. Indeed, we'd want to have a!=b be equivalent to !(a==b)
   * but that would mean that whenever a or b is invalid, a!=b
   * is always true, which would be very confusing.
   *
   * For similar reasons, operators <, >, <=, >= would be very tricky to
   * specify, so we just avoid offering them.
   *
   * Notice that these == semantics are made more reasonable by these facts:
   *  1. a==b implies equality at the raw data level
   *     (the converse is false, as a==b is never true among invalids)
   *  2. This is similar to the behavior of IEEE floats, where a==b
   *     means that a and b have the same value *and* neither is NaN.
   */
  constexpr bool operator==(const CheckedInt& aOther) const {
    return mIsValid && aOther.mIsValid && mValue == aOther.mValue;
  }

  /** prefix ++ */
  constexpr CheckedInt& operator++() {
    *this += 1;
    return *this;
  }

  /** postfix ++ */
  constexpr CheckedInt operator++(int) {
    CheckedInt tmp = *this;
    *this += 1;
    return tmp;
  }

  /** prefix -- */
  constexpr CheckedInt& operator--() {
    *this -= 1;
    return *this;
  }

  /** postfix -- */
  constexpr CheckedInt operator--(int) {
    CheckedInt tmp = *this;
    *this -= 1;
    return tmp;
  }

 private:
  /**
   * The !=, <, <=, >, >= operators are disabled:
   * see the comment on operator==.
   */
  template <typename U>
  bool operator!=(U aOther) const = delete;
  template <typename U>
  bool operator<(U aOther) const = delete;
  template <typename U>
  bool operator<=(U aOther) const = delete;
  template <typename U>
  bool operator>(U aOther) const = delete;
  template <typename U>
  bool operator>=(U aOther) const = delete;
};

#define MOZ_CHECKEDINT_BASIC_BINARY_OPERATOR(NAME, OP)                      \
  template <typename T>                                                     \
  constexpr CheckedInt<T> operator OP(const CheckedInt<T>& aLhs,            \
                                      const CheckedInt<T>& aRhs) {          \
    if (!detail::Is##NAME##Valid(aLhs.mValue, aRhs.mValue)) {               \
      static_assert(std::in_range<T>(0), "Integer type can't represent 0"); \
      return CheckedInt<T>(T(0), false);                                    \
    }                                                                       \
    /* For some T, arithmetic ops automatically promote to a wider type, so \
     * explicitly do the narrowing cast here.  The narrowing cast is valid  \
     * because we did the "Is##NAME##Valid" check above. */                 \
    return CheckedInt<T>(T(aLhs.mValue OP aRhs.mValue),                     \
                         aLhs.mIsValid && aRhs.mIsValid);                   \
  }

#define MOZ_CHECKEDINT_BASIC_BINARY_OPERATOR2(NAME, OP, FUN)                \
  template <typename T>                                                     \
  constexpr CheckedInt<T> operator OP(const CheckedInt<T>& aLhs,            \
                                      const CheckedInt<T>& aRhs) {          \
    auto result = T{};                                                      \
    if (MOZ_UNLIKELY(!FUN(aLhs.mValue, aRhs.mValue, &result))) {            \
      static_assert(std::in_range<T>(0), "Integer type can't represent 0"); \
      return CheckedInt<T>(T(0), false);                                    \
    }                                                                       \
    return CheckedInt<T>(result, aLhs.mIsValid && aRhs.mIsValid);           \
  }
MOZ_CHECKEDINT_BASIC_BINARY_OPERATOR2(Add, +, SafeAdd)
MOZ_CHECKEDINT_BASIC_BINARY_OPERATOR2(Sub, -, SafeSub)
MOZ_CHECKEDINT_BASIC_BINARY_OPERATOR2(Mul, *, SafeMul)
#undef MOZ_CHECKEDINT_BASIC_BINARY_OPERATOR2

MOZ_CHECKEDINT_BASIC_BINARY_OPERATOR(Div, /)
MOZ_CHECKEDINT_BASIC_BINARY_OPERATOR(Mod, %)
#undef MOZ_CHECKEDINT_BASIC_BINARY_OPERATOR

// Implement castToCheckedInt<T>(x), making sure that
//  - it allows x to be either a CheckedInt<T> or any integer type
//    that can be cast to T
//  - if x is already a CheckedInt<T>, we just return a reference to it,
//    instead of copying it (optimization)

namespace detail {

template <typename T, typename U>
struct CastToCheckedIntImpl {
  typedef CheckedInt<T> ReturnType;
  static constexpr CheckedInt<T> run(U aU) { return aU; }
};

template <typename T>
struct CastToCheckedIntImpl<T, CheckedInt<T>> {
  typedef const CheckedInt<T>& ReturnType;
  static constexpr const CheckedInt<T>& run(const CheckedInt<T>& aU) {
    return aU;
  }
};

}  // namespace detail

template <typename T, typename U>
constexpr typename detail::CastToCheckedIntImpl<T, U>::ReturnType
castToCheckedInt(U aU) {
  static_assert(detail::IsSupported<T>::value && detail::IsSupported<U>::value,
                "This type is not supported by CheckedInt");
  return detail::CastToCheckedIntImpl<T, U>::run(aU);
}

#define MOZ_CHECKEDINT_CONVENIENCE_BINARY_OPERATORS(OP, COMPOUND_OP)       \
  template <typename T>                                                    \
  template <typename U>                                                    \
  constexpr CheckedInt<T>& CheckedInt<T>::operator COMPOUND_OP(U aRhs) {   \
    *this = *this OP castToCheckedInt<T>(aRhs);                            \
    return *this;                                                          \
  }                                                                        \
  template <typename T>                                                    \
  constexpr CheckedInt<T>& CheckedInt<T>::operator COMPOUND_OP(            \
      const CheckedInt<T>& aRhs) {                                         \
    *this = *this OP aRhs;                                                 \
    return *this;                                                          \
  }                                                                        \
  template <typename T, typename U>                                        \
  constexpr CheckedInt<T> operator OP(const CheckedInt<T>& aLhs, U aRhs) { \
    return aLhs OP castToCheckedInt<T>(aRhs);                              \
  }                                                                        \
  template <typename T, typename U>                                        \
  constexpr CheckedInt<T> operator OP(U aLhs, const CheckedInt<T>& aRhs) { \
    return castToCheckedInt<T>(aLhs) OP aRhs;                              \
  }

MOZ_CHECKEDINT_CONVENIENCE_BINARY_OPERATORS(+, +=)
MOZ_CHECKEDINT_CONVENIENCE_BINARY_OPERATORS(*, *=)
MOZ_CHECKEDINT_CONVENIENCE_BINARY_OPERATORS(-, -=)
MOZ_CHECKEDINT_CONVENIENCE_BINARY_OPERATORS(/, /=)
MOZ_CHECKEDINT_CONVENIENCE_BINARY_OPERATORS(%, %=)

#undef MOZ_CHECKEDINT_CONVENIENCE_BINARY_OPERATORS

template <typename T, typename U>
constexpr bool operator==(const CheckedInt<T>& aLhs, U aRhs) {
  return aLhs == castToCheckedInt<T>(aRhs);
}

template <typename T, typename U>
constexpr bool operator==(U aLhs, const CheckedInt<T>& aRhs) {
  return castToCheckedInt<T>(aLhs) == aRhs;
}

// Convenience typedefs.
typedef CheckedInt<int8_t> CheckedInt8;
typedef CheckedInt<uint8_t> CheckedUint8;
typedef CheckedInt<int16_t> CheckedInt16;
typedef CheckedInt<uint16_t> CheckedUint16;
typedef CheckedInt<int32_t> CheckedInt32;
typedef CheckedInt<uint32_t> CheckedUint32;
typedef CheckedInt<int64_t> CheckedInt64;
typedef CheckedInt<uint64_t> CheckedUint64;

}  // namespace mozilla

#endif /* mozilla_CheckedInt_h */
