/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_intl_NumberFormatOptions_h
#define builtin_intl_NumberFormatOptions_h

#include "mozilla/intl/NumberFormat.h"
#include "mozilla/intl/PluralRules.h"

#include <array>
#include <stdint.h>

#include "builtin/intl/Packed.h"
#include "ds/IdValuePair.h"
#include "js/TypeDecls.h"

namespace mozilla::intl {
struct PluralRulesOptions;
}

namespace js {
class ArrayObject;
}

namespace js::intl {

struct NumberFormatDigitOptions {
  // integer ∈ (1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 2500,
  // 5000)
  int16_t roundingIncrement = 0;

  int8_t minimumIntegerDigits = 0;  // integer ∈ [1, 21]

  // optional, mutually exclusive with the significant-digits option
  int8_t minimumFractionDigits = 0;  // integer ∈ [0, 100]
  int8_t maximumFractionDigits = 0;  // integer ∈ [0, 100]

  // optional, mutually exclusive with the fraction-digits option
  int8_t minimumSignificantDigits = 0;  // integer ∈ [1, 21]
  int8_t maximumSignificantDigits = 0;  // integer ∈ [1, 21]

  using RoundingMode = mozilla::intl::NumberFormatOptions::RoundingMode;
  RoundingMode roundingMode = RoundingMode::HalfExpand;

  using RoundingPriority = mozilla::intl::NumberFormatOptions::RoundingPriority;
  RoundingPriority roundingPriority = RoundingPriority::Auto;

  enum class TrailingZeroDisplay : int8_t { Auto, StripIfInteger };
  TrailingZeroDisplay trailingZeroDisplay = TrailingZeroDisplay::Auto;

  static constexpr auto defaultOptions() {
    return NumberFormatDigitOptions{
        .roundingIncrement = 1,
        .minimumIntegerDigits = 1,
        .minimumFractionDigits = 0,
        .maximumFractionDigits = 3,
        .minimumSignificantDigits = 0,
        .maximumSignificantDigits = 0,
        .roundingMode = RoundingMode::HalfExpand,
        .roundingPriority = RoundingPriority::Auto,
        .trailingZeroDisplay = TrailingZeroDisplay::Auto,
    };
  }
};

struct NumberFormatUnitOptions {
  using Style = mozilla::intl::NumberFormatOptions::Style;
  Style style = Style::Decimal;

  using CurrencyDisplay = mozilla::intl::NumberFormatOptions::CurrencyDisplay;
  CurrencyDisplay currencyDisplay = CurrencyDisplay::Symbol;

  using CurrencySign = mozilla::intl::NumberFormatOptions::CurrencySign;
  CurrencySign currencySign = CurrencySign::Standard;

  using UnitDisplay = mozilla::intl::NumberFormatOptions::UnitDisplay;
  UnitDisplay unitDisplay = UnitDisplay::Short;

  struct Currency {
    char code[3] = {};

    constexpr bool operator==(const Currency&) const = default;

    constexpr std::string_view to_string_view() const {
      return {code, std::size(code)};
    }

    constexpr uint16_t toIndex() const {
      // Prefer small integer values because they can be more likely encoded as
      // literals in assembly code.
      //
      // Each character is in A..Z, so there are 26 possible values, which can
      // be represented in five bits. That means 15 bits are needed in total to
      // hash a currency, which fits in int16 and therefore can be encoded
      // directly for x86 and arm64 assembly.
      return ((code[0] - 'A') << 10) | ((code[1] - 'A') << 5) |
             ((code[2] - 'A') << 0);
    }

    static constexpr Currency fromIndex(uint16_t hash) {
      constexpr auto emptyCurrencyIndex = Currency{}.toIndex();
      static_assert(emptyCurrencyIndex == 0xFFFF);

      if (hash == emptyCurrencyIndex) {
        return {};
      }

      return Currency{
          .code =
              {
                  char(((hash >> 10) & 0x1F) + 'A'),
                  char(((hash >> 5) & 0x1F) + 'A'),
                  char(((hash >> 0) & 0x1F) + 'A'),
              },
      };
    }
  };
  Currency currency{};

  struct Unit {
    static constexpr uint8_t InvalidUnit = 0xff;

    uint8_t numerator = InvalidUnit;
    uint8_t denominator = InvalidUnit;

    bool hasNumerator() const { return numerator != InvalidUnit; }
    bool hasDenominator() const { return denominator != InvalidUnit; }

    constexpr uint16_t toIndex() const {
      return (uint16_t(numerator) << 8) | uint16_t(denominator);
    }

    static constexpr Unit fromIndex(uint16_t index) {
      return Unit{
          .numerator = uint8_t(index >> 8),
          .denominator = uint8_t(index),
      };
    }
  };
  Unit unit{};
};

struct NumberFormatOptions {
  NumberFormatDigitOptions digitOptions{};

  NumberFormatUnitOptions unitOptions{};

  enum class Notation : int8_t { Standard, Scientific, Engineering, Compact };
  Notation notation = Notation::Standard;

  enum class CompactDisplay : int8_t { Short, Long };
  CompactDisplay compactDisplay = CompactDisplay::Short;

  using UseGrouping = mozilla::intl::NumberFormatOptions::Grouping;
  UseGrouping useGrouping = UseGrouping::Auto;

  using SignDisplay = mozilla::intl::NumberFormatOptions::SignDisplay;
  SignDisplay signDisplay = SignDisplay::Auto;
};

struct PackedNumberFormatDigitOptions {
  using RawValue = uint64_t;

  using RoundingIncrementField =
      packed::ListField<RawValue, std::to_array<int16_t>(
                                      {1, 2, 5, 10, 20, 25, 50, 100, 200, 250,
                                       500, 1000, 2000, 2500, 5000})>;

  using MinimumIntegerDigitsField =
      packed::RangeField<RoundingIncrementField, int8_t, 1, 21>;

  using MinimumFractionDigitsField =
      packed::RangeField<MinimumIntegerDigitsField, int8_t, -1, 100>;
  using MaximumFractionDigitsField =
      packed::RangeField<MinimumFractionDigitsField, int8_t, -1, 100>;

  using MinimumSignificantDigitsField =
      packed::RangeField<MaximumFractionDigitsField, int8_t, 0, 21>;
  using MaximumSignificantDigitsField =
      packed::RangeField<MinimumSignificantDigitsField, int8_t, 0, 21>;

  using RoundingModeField =
      packed::EnumField<MaximumSignificantDigitsField,
                        NumberFormatDigitOptions::RoundingMode::Ceil,
                        NumberFormatDigitOptions::RoundingMode::HalfEven>;

  using RoundingPriorityField = packed::EnumField<
      RoundingModeField, NumberFormatDigitOptions::RoundingPriority::Auto,
      NumberFormatDigitOptions::RoundingPriority::LessPrecision>;

  using TrailingZeroDisplayField = packed::EnumField<
      RoundingPriorityField,
      NumberFormatDigitOptions::TrailingZeroDisplay::Auto,
      NumberFormatDigitOptions::TrailingZeroDisplay::StripIfInteger>;

  using LastField = TrailingZeroDisplayField;

  static auto pack(const NumberFormatDigitOptions& options) {
    RawValue rawValue =
        RoundingIncrementField::pack(options.roundingIncrement) |
        MinimumIntegerDigitsField::pack(options.minimumIntegerDigits) |
        MinimumFractionDigitsField::pack(options.minimumFractionDigits) |
        MaximumFractionDigitsField::pack(options.maximumFractionDigits) |
        MinimumSignificantDigitsField::pack(options.minimumSignificantDigits) |
        MaximumSignificantDigitsField::pack(options.maximumSignificantDigits) |
        RoundingModeField::pack(options.roundingMode) |
        RoundingPriorityField::pack(options.roundingPriority) |
        TrailingZeroDisplayField::pack(options.trailingZeroDisplay);
    return rawValue;
  }

  static auto unpack(RawValue rawValue) {
    return NumberFormatDigitOptions{
        .roundingIncrement = RoundingIncrementField::unpack(rawValue),
        .minimumIntegerDigits = MinimumIntegerDigitsField::unpack(rawValue),
        .minimumFractionDigits = MinimumFractionDigitsField::unpack(rawValue),
        .maximumFractionDigits = MaximumFractionDigitsField::unpack(rawValue),
        .minimumSignificantDigits =
            MinimumSignificantDigitsField::unpack(rawValue),
        .maximumSignificantDigits =
            MaximumSignificantDigitsField::unpack(rawValue),
        .roundingMode = RoundingModeField::unpack(rawValue),
        .roundingPriority = RoundingPriorityField::unpack(rawValue),
        .trailingZeroDisplay = TrailingZeroDisplayField::unpack(rawValue),
    };
  }
};

struct PackedNumberFormatUnitOptions {
  using RawValue = uint64_t;

  using StyleField =
      packed::EnumField<RawValue, NumberFormatUnitOptions::Style::Decimal,
                        NumberFormatUnitOptions::Style::Unit>;

  using CurrencyDisplayField =
      packed::EnumField<StyleField,
                        NumberFormatUnitOptions::CurrencyDisplay::Symbol,
                        NumberFormatUnitOptions::CurrencyDisplay::NarrowSymbol>;

  using CurrencySignField =
      packed::EnumField<CurrencyDisplayField,
                        NumberFormatUnitOptions::CurrencySign::Standard,
                        NumberFormatUnitOptions::CurrencySign::Accounting>;

  using UnitDisplayField =
      packed::EnumField<CurrencySignField,
                        NumberFormatUnitOptions::UnitDisplay::Short,
                        NumberFormatUnitOptions::UnitDisplay::Long>;

  using CurrencyField =
      packed::RangeField<UnitDisplayField, uint16_t, 0, 0xFFFF>;

  using UnitField = packed::RangeField<CurrencyField, uint16_t, 0, 0xFFFF>;

  using LastField = UnitField;

  static auto pack(const NumberFormatUnitOptions& options) {
    RawValue rawValue = StyleField::pack(options.style) |
                        CurrencyDisplayField::pack(options.currencyDisplay) |
                        CurrencySignField::pack(options.currencySign) |
                        UnitDisplayField::pack(options.unitDisplay) |
                        CurrencyField::pack(options.currency.toIndex()) |
                        UnitField::pack(options.unit.toIndex());
    return rawValue;
  }

  static auto unpack(RawValue rawValue) {
    return NumberFormatUnitOptions{
        .style = StyleField::unpack(rawValue),
        .currencyDisplay = CurrencyDisplayField::unpack(rawValue),
        .currencySign = CurrencySignField::unpack(rawValue),
        .unitDisplay = UnitDisplayField::unpack(rawValue),
        .currency = NumberFormatUnitOptions::Currency::fromIndex(
            CurrencyField::unpack(rawValue)),
        .unit = NumberFormatUnitOptions::Unit::fromIndex(
            UnitField::unpack(rawValue)),
    };
  }
};

struct PackedNumberFormatOptions {
  using RawValue = PackedNumberFormatUnitOptions::RawValue;

  using NotationField =
      packed::EnumField<PackedNumberFormatUnitOptions::LastField,
                        NumberFormatOptions::Notation::Standard,
                        NumberFormatOptions::Notation::Compact>;

  using CompactDisplayField =
      packed::EnumField<NotationField,
                        NumberFormatOptions::CompactDisplay::Short,
                        NumberFormatOptions::CompactDisplay::Long>;

  using UseGroupingField =
      packed::EnumField<CompactDisplayField,
                        NumberFormatOptions::UseGrouping::Auto,
                        NumberFormatOptions::UseGrouping::Never>;

  using SignDisplayField =
      packed::EnumField<UseGroupingField,
                        NumberFormatOptions::SignDisplay::Auto,
                        NumberFormatOptions::SignDisplay::Negative>;

  using PackedValue = packed::PackedValue<SignDisplayField>;
  using PackedDigitsValue =
      packed::PackedValue<PackedNumberFormatDigitOptions::LastField>;

  static auto pack(const NumberFormatOptions& options) {
    RawValue rawValue =
        PackedNumberFormatUnitOptions::pack(options.unitOptions) |
        NotationField::pack(options.notation) |
        CompactDisplayField::pack(options.compactDisplay) |
        UseGroupingField::pack(options.useGrouping) |
        SignDisplayField::pack(options.signDisplay);
    RawValue rawDigitsValue =
        PackedNumberFormatDigitOptions::pack(options.digitOptions);
    return std::pair{PackedValue::toValue(rawValue),
                     PackedDigitsValue::toValue(rawDigitsValue)};
  }

  static auto unpack(JS::Value value, JS::Value digitsValue) {
    RawValue rawValue = PackedValue::fromValue(value);
    RawValue rawDigitsValue = PackedDigitsValue::fromValue(digitsValue);
    return NumberFormatOptions{
        .digitOptions = PackedNumberFormatDigitOptions::unpack(rawDigitsValue),
        .unitOptions = PackedNumberFormatUnitOptions::unpack(rawValue),
        .notation = NotationField::unpack(rawValue),
        .compactDisplay = CompactDisplayField::unpack(rawValue),
        .useGrouping = UseGroupingField::unpack(rawValue),
        .signDisplay = SignDisplayField::unpack(rawValue),
    };
  }
};

struct PluralRulesOptions {
  NumberFormatDigitOptions digitOptions{};

  using Type = mozilla::intl::PluralRules::Type;
  Type type = Type::Cardinal;

  using Notation = NumberFormatOptions::Notation;
  Notation notation = Notation::Standard;

  using CompactDisplay = NumberFormatOptions::CompactDisplay;
  CompactDisplay compactDisplay = CompactDisplay::Short;
};

struct PackedPluralRulesOptions {
  using RawValue = PackedNumberFormatDigitOptions::RawValue;

  using TypeField = packed::EnumField<PackedNumberFormatDigitOptions::LastField,
                                      PluralRulesOptions::Type::Cardinal,
                                      PluralRulesOptions::Type::Ordinal>;

  using NotationField =
      packed::EnumField<TypeField, PluralRulesOptions::Notation::Standard,
                        PluralRulesOptions::Notation::Compact>;

  using CompactDisplayField =
      packed::EnumField<NotationField,
                        PluralRulesOptions::CompactDisplay::Short,
                        PluralRulesOptions::CompactDisplay::Long>;

  using PackedValue = packed::PackedValue<CompactDisplayField>;

  static auto pack(const PluralRulesOptions& options) {
    RawValue rawValue =
        PackedNumberFormatDigitOptions::pack(options.digitOptions) |
        TypeField::pack(options.type) | NotationField::pack(options.notation) |
        CompactDisplayField::pack(options.compactDisplay);
    return PackedValue::toValue(rawValue);
  }

  static auto unpack(JS::Value value) {
    RawValue rawValue = PackedValue::fromValue(value);
    return PluralRulesOptions{
        .digitOptions = PackedNumberFormatDigitOptions::unpack(rawValue),
        .type = TypeField::unpack(rawValue),
        .notation = NotationField::unpack(rawValue),
        .compactDisplay = CompactDisplayField::unpack(rawValue),
    };
  }
};

/**
 * SetNumberFormatDigitOptions ( intlObj, options, mnfdDefault, mxfdDefault,
 * notation )
 */
bool SetNumberFormatDigitOptions(JSContext* cx, NumberFormatDigitOptions& obj,
                                 JS::Handle<JSObject*> options,
                                 int32_t mnfdDefault, int32_t mxfdDefault,
                                 NumberFormatOptions::Notation notation);

/**
 * Set the plural rules options.
 */
void SetPluralRulesOptions(const PluralRulesOptions& plOptions,
                           mozilla::intl::PluralRulesOptions& options);

/**
 * Resolve plural rules options.
 */
bool ResolvePluralRulesOptions(JSContext* cx,
                               const PluralRulesOptions& plOptions,
                               JS::Handle<ArrayObject*> pluralCategories,
                               JS::MutableHandle<IdValueVector> options);

}  // namespace js::intl

#endif /* builtin_intl_NumberFormatOptions_h */
