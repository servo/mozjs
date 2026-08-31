/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_intl_Packed_h
#define builtin_intl_Packed_h

#include "mozilla/Assertions.h"
#include "mozilla/Casting.h"
#include "mozilla/FloatingPoint.h"
#include "mozilla/Maybe.h"

#include <bit>
#include <concepts>
#include <iterator>
#include <limits>
#include <stdint.h>
#include <type_traits>

#include "js/Value.h"

namespace js::intl::packed {

namespace detail {
// js::Bit extended to uint64_t.
constexpr uint64_t Bit(uint64_t n) { return uint64_t(1) << n; }
constexpr uint64_t BitMask(uint64_t n) { return Bit(n) - 1; }

// Concept declaring the required static members for |Field| types.
template <typename T>
concept Field = std::unsigned_integral<typename T::Representation> &&
                std::unsigned_integral<decltype(T::Shift)> &&
                std::unsigned_integral<decltype(T::Bits)>;

template <typename T>
concept FieldOrUnsigned = Field<T> || std::unsigned_integral<T>;

template <FieldOrUnsigned T>
constexpr uint32_t ShiftAmount() {
  if constexpr (Field<T>) {
    return T::Shift + T::Bits;
  } else {
    return 0;
  }
}

template <FieldOrUnsigned T>
struct RepresentationSelector;

template <Field T>
struct RepresentationSelector<T> {
  using Type = typename T::Representation;
};

template <std::unsigned_integral T>
struct RepresentationSelector<T> {
  using Type = T;
};

template <Field F>
consteval auto LargestBitRepresentation() {
  using R = typename F::Representation;

  // Largest bit representation of the field with all trailing bits set to ones.
  constexpr auto shift = F::Bits + F::Shift;
  return shift == std::numeric_limits<R>::digits ? R(-1)
                                                 : R{(R(1) << shift) - 1};
}

/**
 * Return true if the bit representation fits into JS::PrivateUint32Value.
 */
template <Field F>
consteval auto CanRepresentAsPrivateUint32Value() {
  return LargestBitRepresentation<F>() <= UINT32_MAX;
}

/**
 * Return true if the bit representation fits into JS::DoubleValue.
 */
template <Field F>
consteval auto CanRepresentAsDoubleValue() {
  constexpr auto largest = LargestBitRepresentation<F>();

  // If the largest representation is smaller than the bit representation of
  // positive infinity, than this field (and any preceding fields) definitely
  // fit into DoubleValue.
  return largest < mozilla::InfinityBits<double, 0>::value;
}
}  // namespace detail

/**
 * Helper struct to provide conversion methods from and to JS::Value.
 */
template <detail::Field LastField, typename Enable = void>
struct PackedValue;

template <detail::Field LastField>
struct PackedValue<LastField,
                   std::enable_if_t<std::is_same_v<
                       typename LastField::Representation, uint32_t>>> {
  static_assert(detail::CanRepresentAsPrivateUint32Value<LastField>(),
                "packed representation fits into PrivateUint32Value");

  static uint32_t fromValue(JS::Value value) {
    uint32_t rawValue = value.toPrivateUint32();
    MOZ_ASSERT(rawValue <= detail::LargestBitRepresentation<LastField>());
    return rawValue;
  }

  static JS::Value toValue(uint32_t rawValue) {
    MOZ_ASSERT(rawValue <= detail::LargestBitRepresentation<LastField>());
    return JS::PrivateUint32Value(rawValue);
  }
};

template <detail::Field LastField>
struct PackedValue<LastField,
                   std::enable_if_t<std::is_same_v<
                       typename LastField::Representation, uint64_t>>> {
  static_assert(detail::CanRepresentAsDoubleValue<LastField>(),
                "packed representation fits into DoubleValue");

  // Assert we don't use a too large raw value representation.
  static_assert(!detail::CanRepresentAsPrivateUint32Value<LastField>(),
                "packed representation could fit into PrivateUint32Value");

  static uint64_t fromValue(JS::Value value) {
    uint64_t rawValue = mozilla::BitwiseCast<uint64_t>(value.toDouble());
    MOZ_ASSERT(rawValue <= detail::LargestBitRepresentation<LastField>());
    return rawValue;
  }

  static JS::Value toValue(uint64_t rawValue) {
    MOZ_ASSERT(rawValue <= detail::LargestBitRepresentation<LastField>());
    return JS::DoubleValue(mozilla::BitwiseCast<double>(rawValue));
  }
};

/**
 * Packed field for enums.
 *
 * Template parameters:
 * 1. Previous field or representation.
 * 2. First enum value.
 * 3. Last enum value.
 *
 * Usage:
 * ```
 * enum class TestEnum {
 *   First, Second, Third
 * };
 *
 * using TestEnumField = EnumField<
 *   FieldOrRepresentation,
 *   TestEnum::First,
 *   TestEnum::Third
 * >;
 * ```
 */
template <detail::FieldOrUnsigned FieldOrRepresentation, auto First, auto Last>
struct EnumField {
  using Representation =
      detail::RepresentationSelector<FieldOrRepresentation>::Type;

  static_assert(std::is_same_v<decltype(First), decltype(Last)>,
                "First and Last have the same type");
  static_assert(First <= Last, "First and Last are ordered");

  using Enum = decltype(First);
  static_assert(std::is_enum_v<Enum>);

  using EnumType = std::underlying_type_t<Enum>;

  static constexpr auto FirstValue = static_cast<EnumType>(First);
  static constexpr auto LastValue = static_cast<EnumType>(Last);
  static constexpr auto ValueRange = LastValue - FirstValue;

  static constexpr uint32_t Shift =
      detail::ShiftAmount<FieldOrRepresentation>();

  static constexpr uint32_t Bits =
      std::bit_width(std::make_unsigned_t<decltype(ValueRange)>(ValueRange));
  static_assert(Bits <= std::numeric_limits<Representation>::digits - Shift,
                "too few available bits");

  /**
   * Return the packed representation of |e|.
   */
  static constexpr Representation pack(Enum e) {
    MOZ_ASSERT(First <= e && e <= Last);
    auto t = static_cast<EnumType>(e) - FirstValue;
    return static_cast<Representation>(t) << Shift;
  }

  /**
   * Unpack the packed representation value |v|.
   */
  static constexpr Enum unpack(Representation v) {
    auto w = (v >> Shift) & detail::BitMask(Bits);
    MOZ_ASSERT(w <= ValueRange);
    auto t = static_cast<EnumType>(w + FirstValue);
    return Enum{t};
  }

  static_assert(unpack(pack(First)) == First, "First value can be unpacked");
  static_assert(unpack(pack(Last)) == Last, "Last value can be unpacked");
};

/**
 * Packed field for optional enums.
 *
 * Template parameters:
 * 1. Previous field or representation.
 * 2. First enum value.
 * 3. Last enum value.
 *
 * Usage:
 * ```
 * enum class TestEnum {
 *   First, Second, Third
 * };
 *
 * using TestEnumField = OptionalEnumField<
 *   FieldOrRepresentation,
 *   TestEnum::First,
 *   TestEnum::Third
 * >;
 * ```
 */
template <detail::FieldOrUnsigned FieldOrRepresentation, auto First, auto Last>
struct OptionalEnumField {
  using Representation =
      detail::RepresentationSelector<FieldOrRepresentation>::Type;

  static_assert(std::is_same_v<decltype(First), decltype(Last)>,
                "First and Last have the same type");
  static_assert(First <= Last, "First and Last are ordered");

  using Enum = decltype(First);
  static_assert(std::is_enum_v<Enum>);

  using EnumType = std::underlying_type_t<Enum>;

  static constexpr auto FirstValue = static_cast<EnumType>(First);
  static constexpr auto LastValue = static_cast<EnumType>(Last);
  static constexpr auto NoneValue = LastValue + 1;
  static constexpr auto ValueRange = (NoneValue - FirstValue);

  static constexpr uint32_t Shift =
      detail::ShiftAmount<FieldOrRepresentation>();

  static constexpr uint32_t Bits =
      std::bit_width(std::make_unsigned_t<decltype(ValueRange)>(ValueRange));
  static_assert(Bits <= std::numeric_limits<Representation>::digits - Shift,
                "too few available bits");

  /**
   * Return the packed representation of |e|.
   */
  static constexpr Representation pack(mozilla::Maybe<Enum> e) {
    MOZ_ASSERT_IF(e.isSome(), First <= *e && *e <= Last);
    auto t = (e.isSome() ? static_cast<EnumType>(e.value()) : NoneValue) -
             FirstValue;
    return static_cast<Representation>(t) << Shift;
  }

  /**
   * Unpack the packed representation value |v|.
   */
  static constexpr mozilla::Maybe<Enum> unpack(Representation v) {
    auto w = ((v >> Shift) & detail::BitMask(Bits));
    MOZ_ASSERT(w <= ValueRange);
    auto t = static_cast<EnumType>(w + FirstValue);
    if (t == NoneValue) {
      return mozilla::Nothing();
    }
    return mozilla::Some(Enum{t});
  }

  static_assert(unpack(pack(mozilla::Some(First))) == mozilla::Some(First),
                "First value can be unpacked");
  static_assert(unpack(pack(mozilla::Some(Last))) == mozilla::Some(Last),
                "Last value can be unpacked");
  static_assert(unpack(pack(mozilla::Nothing())) == mozilla::Nothing(),
                "Nothing value can be unpacked");
};

/**
 * Packed field for boolean values.
 *
 * Template parameters:
 * 1. Previous field or representation.
 *
 * Usage:
 * ```
 * using TestBooleanField = BooleanField<FieldOrRepresentation>;
 * ```
 */
template <detail::FieldOrUnsigned FieldOrRepresentation>
struct BooleanField {
  using Representation =
      detail::RepresentationSelector<FieldOrRepresentation>::Type;

  static constexpr auto FirstValue = uint32_t(false);
  static constexpr auto LastValue = uint32_t(true);
  static constexpr auto ValueRange = (LastValue - FirstValue);

  static constexpr uint32_t Shift =
      detail::ShiftAmount<FieldOrRepresentation>();

  static constexpr uint32_t Bits =
      std::bit_width(std::make_unsigned_t<decltype(ValueRange)>(ValueRange));
  static_assert(Bits <= std::numeric_limits<Representation>::digits - Shift,
                "too few available bits");

  /**
   * Return the packed representation of |e|.
   */
  static constexpr Representation pack(bool e) {
    auto t = uint32_t(e) - FirstValue;
    return static_cast<Representation>(t) << Shift;
  }

  /**
   * Unpack the packed representation value |v|.
   */
  static constexpr bool unpack(Representation v) {
    auto w = ((v >> Shift) & detail::BitMask(Bits));
    MOZ_ASSERT(w <= ValueRange);
    auto t = uint32_t(w + FirstValue);
    return bool(t);
  }

  static_assert(unpack(pack(false)) == false, "False value can be unpacked");
  static_assert(unpack(pack(true)) == true, "True value can be unpacked");
};

/**
 * Packed field for optional boolean values.
 *
 * Template parameters:
 * 1. Previous field or representation.
 *
 * Usage:
 * ```
 * using TestBooleanField = OptionalBooleanField<FieldOrRepresentation>;
 * ```
 */
template <detail::FieldOrUnsigned FieldOrRepresentation>
struct OptionalBooleanField {
  using Representation =
      detail::RepresentationSelector<FieldOrRepresentation>::Type;

  static constexpr auto FirstValue = uint32_t(false);
  static constexpr auto LastValue = uint32_t(true);
  static constexpr auto NoneValue = LastValue + 1;
  static constexpr auto ValueRange = (NoneValue - FirstValue);

  static constexpr uint32_t Shift =
      detail::ShiftAmount<FieldOrRepresentation>();

  static constexpr uint32_t Bits =
      std::bit_width(std::make_unsigned_t<decltype(ValueRange)>(ValueRange));
  static_assert(Bits <= std::numeric_limits<Representation>::digits - Shift,
                "too few available bits");

  /**
   * Return the packed representation of |e|.
   */
  static constexpr Representation pack(mozilla::Maybe<bool> e) {
    auto t = (e.isSome() ? uint32_t(e.value()) : NoneValue) - FirstValue;
    return static_cast<Representation>(t) << Shift;
  }

  /**
   * Unpack the packed representation value |v|.
   */
  static constexpr mozilla::Maybe<bool> unpack(Representation v) {
    auto w = ((v >> Shift) & detail::BitMask(Bits));
    MOZ_ASSERT(w <= ValueRange);
    auto t = uint32_t(w + FirstValue);
    if (t == NoneValue) {
      return mozilla::Nothing();
    }
    return mozilla::Some(bool(t));
  }

  static_assert(unpack(pack(mozilla::Some(false))) == mozilla::Some(false),
                "True value can be unpacked");
  static_assert(unpack(pack(mozilla::Some(true))) == mozilla::Some(true),
                "False value can be unpacked");
  static_assert(unpack(pack(mozilla::Nothing())) == mozilla::Nothing(),
                "Nothing value can be unpacked");
};

/**
 * Packed field for a range of values.
 *
 * Template parameters:
 * 1. Previous field or representation.
 * 2. Range value type.
 * 3. First range value.
 * 4. Last range value.
 *
 * Usage:
 * ```
 * using TestRangeField = RangeField<FieldOrRepresentation, int32_t, 1, 10>;
 * ```
 */
template <detail::FieldOrUnsigned FieldOrRepresentation, typename RangeType,
          auto First, auto Last>
struct RangeField {
  using Representation =
      detail::RepresentationSelector<FieldOrRepresentation>::Type;

  static_assert(std::is_same_v<decltype(First), decltype(Last)>);
  static_assert(First < Last);

  using Type = RangeType;

  static constexpr auto FirstValue = First;
  static constexpr auto LastValue = Last;
  static constexpr auto ValueRange = (LastValue - FirstValue);

  static constexpr uint32_t Shift =
      detail::ShiftAmount<FieldOrRepresentation>();

  static constexpr uint32_t Bits =
      std::bit_width(std::make_unsigned_t<decltype(ValueRange)>(ValueRange));
  static_assert(Bits <= std::numeric_limits<Representation>::digits - Shift,
                "too few available bits");

  /**
   * Return the packed representation of |e|.
   */
  static constexpr Representation pack(Type e) {
    MOZ_ASSERT(e >= FirstValue);
    MOZ_ASSERT(e <= LastValue);
    return static_cast<Representation>(e - FirstValue) << Shift;
  }

  /**
   * Unpack the packed representation value |v|.
   */
  static constexpr Type unpack(Representation v) {
    auto w = (v >> Shift) & detail::BitMask(Bits);
    MOZ_ASSERT(w <= ValueRange);
    return static_cast<Type>(w + FirstValue);
  }

  static_assert(unpack(pack(First)) == First, "First value can be unpacked");
  static_assert(unpack(pack(Last)) == Last, "Last value can be unpacked");
};

/**
 * Packed field for a list of values.
 *
 * Template parameters:
 * 1. Previous field or representation.
 * 2. List of valid values.
 *
 * Usage:
 * ```
 * using TestListField = ListField<
 *   FieldOrRepresentation,
 *   std::to_array<uint32_t>({1, 2, 5, 10})
 * >;
 * ```
 */
template <detail::FieldOrUnsigned FieldOrRepresentation, auto List>
struct ListField {
  using Representation =
      detail::RepresentationSelector<FieldOrRepresentation>::Type;

  using Type = decltype(List)::value_type;

  static constexpr auto ValueRange = std::size(List);

  static constexpr uint32_t Shift =
      detail::ShiftAmount<FieldOrRepresentation>();

  static constexpr uint32_t Bits = std::bit_width(ValueRange);
  static_assert(Bits <= std::numeric_limits<Representation>::digits - Shift,
                "too few available bits");

  /**
   * Return the packed representation of |e|.
   */
  static constexpr Representation pack(Type e) {
    auto it = std::find(std::begin(List), std::end(List), e);
    MOZ_ASSERT(it != std::end(List));
    auto index = std::distance(std::begin(List), it);
    return static_cast<Representation>(index) << Shift;
  }

  /**
   * Unpack the packed representation value |v|.
   */
  static constexpr Type unpack(Representation v) {
    size_t index = (v >> Shift) & detail::BitMask(Bits);
    MOZ_ASSERT(index < std::size(List));
    return static_cast<Type>(*std::next(std::begin(List), index));
  }

  static_assert(std::empty(List) ||
                    unpack(pack(*std::begin(List))) == *std::begin(List),
                "First value can be unpacked");
  static_assert(std::empty(List) ||
                    unpack(pack(*std::rbegin(List))) == *std::rbegin(List),
                "Last value can be unpacked");
};

}  // namespace js::intl::packed

#endif  // builtin_intl_Packed_h
