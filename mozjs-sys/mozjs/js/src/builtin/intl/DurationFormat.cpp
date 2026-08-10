/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Intl.DurationFormat implementation. */

#include "builtin/intl/DurationFormat.h"

#include "mozilla/Assertions.h"
#include "mozilla/intl/DateTimeFormat.h"
#include "mozilla/intl/ListFormat.h"
#include "mozilla/intl/NumberFormat.h"
#include "mozilla/Maybe.h"
#include "mozilla/Span.h"
#include "mozilla/UsingEnum.h"

#include <array>
#include <charconv>
#include <utility>

#include "jspubtd.h"
#include "NamespaceImports.h"

#include "builtin/intl/CommonFunctions.h"
#include "builtin/intl/FormatBuffer.h"
#include "builtin/intl/LanguageTag.h"
#include "builtin/intl/ListFormat.h"
#include "builtin/intl/LocaleNegotiation.h"
#include "builtin/intl/NumberFormat.h"
#include "builtin/intl/Packed.h"
#include "builtin/intl/ParameterNegotiation.h"
#include "builtin/temporal/Duration.h"
#include "gc/AllocKind.h"
#include "gc/GCContext.h"
#include "js/CallArgs.h"
#include "js/PropertyDescriptor.h"
#include "js/PropertySpec.h"
#include "js/RootingAPI.h"
#include "js/TypeDecls.h"
#include "js/Value.h"
#include "vm/GlobalObject.h"
#include "vm/JSContext.h"
#include "vm/PlainObject.h"
#include "vm/SelfHosting.h"
#include "vm/WellKnownAtom.h"

#include "vm/JSObject-inl.h"
#include "vm/NativeObject-inl.h"

using namespace js;
using namespace js::intl;

using js::temporal::TemporalUnit;

static constexpr auto durationUnits = std::array{
    TemporalUnit::Year,        TemporalUnit::Month,
    TemporalUnit::Week,        TemporalUnit::Day,
    TemporalUnit::Hour,        TemporalUnit::Minute,
    TemporalUnit::Second,      TemporalUnit::Millisecond,
    TemporalUnit::Microsecond, TemporalUnit::Nanosecond,
};

const JSClass DurationFormatObject::class_ = {
    "Intl.DurationFormat",
    JSCLASS_HAS_RESERVED_SLOTS(DurationFormatObject::SLOT_COUNT) |
        JSCLASS_HAS_CACHED_PROTO(JSProto_DurationFormat) |
        JSCLASS_BACKGROUND_FINALIZE,
    &DurationFormatObject::classOps_,
    &DurationFormatObject::classSpec_,
};

const JSClass& DurationFormatObject::protoClass_ = PlainObject::class_;

static bool durationFormat_format(JSContext* cx, unsigned argc, Value* vp);

static bool durationFormat_formatToParts(JSContext* cx, unsigned argc,
                                         Value* vp);

static bool durationFormat_resolvedOptions(JSContext* cx, unsigned argc,
                                           Value* vp);

static bool durationFormat_supportedLocalesOf(JSContext* cx, unsigned argc,
                                              Value* vp);

static bool durationFormat_toSource(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  args.rval().setString(cx->names().DurationFormat);
  return true;
}

static const JSFunctionSpec durationFormat_static_methods[] = {
    JS_FN("supportedLocalesOf", durationFormat_supportedLocalesOf, 1, 0),
    JS_FS_END,
};

static const JSFunctionSpec durationFormat_methods[] = {
    JS_FN("resolvedOptions", durationFormat_resolvedOptions, 0, 0),
    JS_FN("format", durationFormat_format, 1, 0),
    JS_FN("formatToParts", durationFormat_formatToParts, 1, 0),
    JS_FN("toSource", durationFormat_toSource, 0, 0),
    JS_FS_END,
};

static const JSPropertySpec durationFormat_properties[] = {
    JS_STRING_SYM_PS(toStringTag, "Intl.DurationFormat", JSPROP_READONLY),
    JS_PS_END,
};

static bool DurationFormat(JSContext* cx, unsigned argc, Value* vp);

const JSClassOps DurationFormatObject::classOps_ = {
    .finalize = DurationFormatObject::finalize,
};

const ClassSpec DurationFormatObject::classSpec_ = {
    GenericCreateConstructor<DurationFormat, 0, gc::AllocKind::FUNCTION>,
    GenericCreatePrototype<DurationFormatObject>,
    durationFormat_static_methods,
    nullptr,
    durationFormat_methods,
    durationFormat_properties,
    nullptr,
    ClassSpec::DontDefineConstructor,
};

enum class DurationDisplay : uint8_t { Auto, Always };
enum class DurationStyle : uint8_t { Long, Short, Narrow, Numeric, TwoDigit };
enum class DurationBaseStyle : uint8_t { Long, Short, Narrow, Digital };

#define FOR_EACH_DURATION_UNIT(MACRO) \
  MACRO(Year, year)                   \
  MACRO(Month, month)                 \
  MACRO(Week, week)                   \
  MACRO(Day, day)                     \
  MACRO(Hour, hour)                   \
  MACRO(Minute, minute)               \
  MACRO(Second, second)               \
  MACRO(Millisecond, millisecond)     \
  MACRO(Microsecond, microsecond)     \
  MACRO(Nanosecond, nanosecond)

struct js::intl::DurationFormatOptions {
// Packed representation to keep the unit options as small as possible.
#define DECLARE_DURATION_UNIT(UPPER, LOWER)                    \
  DurationDisplay LOWER##sDisplay : 1 = DurationDisplay::Auto; \
  DurationStyle LOWER##sStyle : 3 = DurationStyle::Short;

  FOR_EACH_DURATION_UNIT(DECLARE_DURATION_UNIT)

#undef DECLARE_DURATION_UNIT

  DurationBaseStyle style = DurationBaseStyle::Short;
  int8_t fractionalDigits = -1;
};

struct DurationUnitOptions {
  // Use the same bit-widths for fast extraction from DurationFormatOptions.
  DurationDisplay display : 1;
  DurationStyle style : 3;
};

struct PackedDurationFormatOptions {
  using RawValue = uint64_t;

  template <typename T>
  using DisplayField =
      packed::EnumField<T, DurationDisplay::Auto, DurationDisplay::Always>;

  template <typename T>
  using StyleField =
      packed::EnumField<T, DurationStyle::Long, DurationStyle::TwoDigit>;

#define DECLARE_DURATION_UNIT(Name, Previous)        \
  using Name##DisplayField = DisplayField<Previous>; \
  using Name##StyleField = StyleField<Name##DisplayField>;

  DECLARE_DURATION_UNIT(Years, RawValue);
  DECLARE_DURATION_UNIT(Months, YearsStyleField);
  DECLARE_DURATION_UNIT(Weeks, MonthsStyleField);
  DECLARE_DURATION_UNIT(Days, WeeksStyleField);
  DECLARE_DURATION_UNIT(Hours, DaysStyleField);
  DECLARE_DURATION_UNIT(Minutes, HoursStyleField);
  DECLARE_DURATION_UNIT(Seconds, MinutesStyleField);
  DECLARE_DURATION_UNIT(Milliseconds, SecondsStyleField);
  DECLARE_DURATION_UNIT(Microseconds, MillisecondsStyleField);
  DECLARE_DURATION_UNIT(Nanoseconds, MicrosecondsStyleField);

#undef DECLARE_DURATION_UNIT

  using BaseStyleField =
      packed::EnumField<NanosecondsStyleField, DurationBaseStyle::Long,
                        DurationBaseStyle::Digital>;

  using FractionalDigitsField =
      packed::RangeField<BaseStyleField, int8_t, -1, 9>;

  using PackedValue = packed::PackedValue<FractionalDigitsField>;

  static auto pack(const DurationFormatOptions& options) {
#define PACK_DURATION_UNIT(UPPER, LOWER)                \
  UPPER##sDisplayField::pack(options.LOWER##sDisplay) | \
      UPPER##sStyleField::pack(options.LOWER##sStyle) |

    RawValue rawValue = FOR_EACH_DURATION_UNIT(PACK_DURATION_UNIT)
                            BaseStyleField::pack(options.style) |
                        FractionalDigitsField::pack(options.fractionalDigits);
    return PackedValue::toValue(rawValue);

#undef PACK_DURATION_UNIT
  }

  static auto unpack(JS::Value value) {
#define UNPACK_DURATION_UNIT(UPPER, LOWER)                   \
  .LOWER##sDisplay = UPPER##sDisplayField::unpack(rawValue), \
  .LOWER##sStyle = UPPER##sStyleField::unpack(rawValue),

    RawValue rawValue = PackedValue::fromValue(value);
    return DurationFormatOptions{
        FOR_EACH_DURATION_UNIT(UNPACK_DURATION_UNIT).style =
            BaseStyleField::unpack(rawValue),
        .fractionalDigits = FractionalDigitsField::unpack(rawValue),
    };

#undef UNPACK_DURATION_UNIT
  }
};

DurationFormatOptions js::intl::DurationFormatObject::getOptions() const {
  const auto& slot = getFixedSlot(OPTIONS_SLOT);
  if (slot.isUndefined()) {
    // GCC complains when returning `{}` without an explicit type. This happens
    // because DurationFormatOptions contains bit-fields of scoped enum types.
    //
    // Simplified test case: https://godbolt.org/z/dxfWEGn6b
    return DurationFormatOptions{};
  }
  return PackedDurationFormatOptions::unpack(slot);
}

void js::intl::DurationFormatObject::setOptions(
    const DurationFormatOptions& options) {
  setFixedSlot(OPTIONS_SLOT, PackedDurationFormatOptions::pack(options));
}

void js::intl::DurationFormatObject::finalize(JS::GCContext* gcx,
                                              JSObject* obj) {
  auto* durationFormat = &obj->as<DurationFormatObject>();

  for (auto unit : durationUnits) {
    if (auto* nf = durationFormat->getNumberFormat(unit)) {
      RemoveICUCellMemory(gcx, obj, NumberFormatObject::EstimatedMemoryUse);
      delete nf;
    }
  }

  if (auto* lf = durationFormat->getListFormat()) {
    RemoveICUCellMemory(gcx, obj, ListFormatObject::EstimatedMemoryUse);
    delete lf;
  }
}

static constexpr std::string_view DisplayToString(DurationDisplay display) {
  MOZ_USING_ENUM(DurationDisplay, Auto, Always);
  switch (display) {
    case Auto:
      return "auto";
    case Always:
      return "always";
  }
  MOZ_CRASH("invalid duration format display");
}

static constexpr std::string_view DurationStyleToString(DurationStyle style) {
  MOZ_USING_ENUM(DurationStyle, Long, Short, Narrow, Numeric, TwoDigit);
  switch (style) {
    case Long:
      return "long";
    case Short:
      return "short";
    case Narrow:
      return "narrow";
    case Numeric:
      return "numeric";
    case TwoDigit:
      return "2-digit";
  }
  MOZ_CRASH("invalid duration format style");
}

static constexpr std::string_view BaseStyleToString(DurationBaseStyle style) {
  MOZ_USING_ENUM(DurationBaseStyle, Long, Short, Narrow, Digital);
  switch (style) {
    case Long:
      return "long";
    case Short:
      return "short";
    case Narrow:
      return "narrow";
    case Digital:
      return "digital";
  }
  MOZ_CRASH("invalid duration format base style");
}

/**
 * Return the singular name for |unit|.
 */
static std::string_view SingularUnitName(TemporalUnit unit) {
  switch (unit) {
#define SINGULAR_UNIT_NAME(UPPER, LOWER) \
  case TemporalUnit::UPPER:              \
    return #LOWER;

    FOR_EACH_DURATION_UNIT(SINGULAR_UNIT_NAME)

#undef SINGULAR_UNIT_NAME

    case TemporalUnit::Unset:
    case TemporalUnit::Auto:
      break;
  }
  MOZ_CRASH("invalid temporal unit");
}

/**
 * Return the plural name for |unit|.
 */
static std::string_view PluralUnitName(TemporalUnit unit) {
  switch (unit) {
#define PLURAL_UNIT_NAME(UPPER, LOWER) \
  case TemporalUnit::UPPER:            \
    return #LOWER "s";

    FOR_EACH_DURATION_UNIT(PLURAL_UNIT_NAME)

#undef PLURAL_UNIT_NAME

    case TemporalUnit::Unset:
    case TemporalUnit::Auto:
      break;
  }
  MOZ_CRASH("invalid temporal unit");
}

/**
 * Return the "style" property name for |unit|.
 */
static Handle<PropertyName*> DurationStyleName(TemporalUnit unit,
                                               JSContext* cx) {
  switch (unit) {
#define DURATION_STYLE_NAME(UPPER, LOWER) \
  case TemporalUnit::UPPER:               \
    return cx->names().LOWER##s;

    FOR_EACH_DURATION_UNIT(DURATION_STYLE_NAME)

#undef DURATION_STYLE_NAME

    case TemporalUnit::Unset:
    case TemporalUnit::Auto:
      break;
  }
  MOZ_CRASH("invalid temporal unit");
}

/**
 * Return the "display" property name for |unit|.
 */
static Handle<PropertyName*> DurationDisplayName(TemporalUnit unit,
                                                 JSContext* cx) {
  switch (unit) {
#define DURATION_DISPLAY_NAME(UPPER, LOWER) \
  case TemporalUnit::UPPER:                 \
    return cx->names().LOWER##sDisplay;

    FOR_EACH_DURATION_UNIT(DURATION_DISPLAY_NAME)

#undef DURATION_DISPLAY_NAME

    case TemporalUnit::Unset:
    case TemporalUnit::Auto:
      break;
  }
  MOZ_CRASH("invalid temporal unit");
}

/**
 * IsFractionalSecondUnitName ( unit )
 */
static inline bool IsFractionalSecondUnitName(TemporalUnit unit) {
  return TemporalUnit::Millisecond <= unit && unit <= TemporalUnit::Nanosecond;
}

/**
 * GetDurationUnitOptions ( unit, options, baseStyle, stylesList, digitalBase,
 * prevStyle, twoDigitHours )
 */
static bool GetDurationUnitOptions(
    JSContext* cx, TemporalUnit unit, Handle<JSObject*> options,
    DurationBaseStyle baseStyle, DurationStyle digitalBase,
    DurationStyle prevStyle,
    std::pair<DurationStyle, DurationDisplay>* result) {
  // Step 1.
  mozilla::Maybe<DurationStyle> styleOption{};
  switch (unit) {
    case TemporalUnit::Year:
    case TemporalUnit::Month:
    case TemporalUnit::Week:
    case TemporalUnit::Day: {
      static constexpr auto styles = MapOptions<DurationStyleToString>(
          DurationStyle::Long, DurationStyle::Short, DurationStyle::Narrow);
      if (!GetStringOption(cx, options, DurationStyleName(unit, cx), styles,
                           &styleOption)) {
        return false;
      }
      break;
    }

    case TemporalUnit::Hour:
    case TemporalUnit::Minute:
    case TemporalUnit::Second: {
      static constexpr auto styles = MapOptions<DurationStyleToString>(
          DurationStyle::Long, DurationStyle::Short, DurationStyle::Narrow,
          DurationStyle::Numeric, DurationStyle::TwoDigit);
      if (!GetStringOption(cx, options, DurationStyleName(unit, cx), styles,
                           &styleOption)) {
        return false;
      }
      break;
    }

    case TemporalUnit::Millisecond:
    case TemporalUnit::Microsecond:
    case TemporalUnit::Nanosecond: {
      static constexpr auto styles = MapOptions<DurationStyleToString>(
          DurationStyle::Long, DurationStyle::Short, DurationStyle::Narrow,
          DurationStyle::Numeric);
      if (!GetStringOption(cx, options, DurationStyleName(unit, cx), styles,
                           &styleOption)) {
        return false;
      }
      break;
    }

    case TemporalUnit::Unset:
    case TemporalUnit::Auto:
      MOZ_CRASH("invalid temporal unit");
  }

  // Step 2.
  auto displayDefault = DurationDisplay::Always;

  // Step 3.
  if (styleOption.isNothing()) {
    // Step 3.a.
    if (baseStyle == DurationBaseStyle::Digital) {
      // Step 3.a.i.
      styleOption = mozilla::Some(digitalBase);

      // Step 3.a.ii.
      if (!(TemporalUnit::Hour <= unit && unit <= TemporalUnit::Second)) {
        displayDefault = DurationDisplay::Auto;
      }
    }

    // Step 3.b. ("fractional" handled implicitly)
    else if (prevStyle == DurationStyle::Numeric ||
             prevStyle == DurationStyle::TwoDigit) {
      // Step 3.b.i.
      styleOption = mozilla::Some(DurationStyle::Numeric);

      // Step 3.b.ii.
      if (unit != TemporalUnit::Minute && unit != TemporalUnit::Second) {
        displayDefault = DurationDisplay::Auto;
      }
    }

    // Step 3.c.
    else {
      // Step 3.c.i.
      styleOption = mozilla::Some(static_cast<DurationStyle>(baseStyle));

      // Step 3.c.ii.
      displayDefault = DurationDisplay::Auto;
    }
  }
  auto style = *styleOption;

  // Step 4.
  bool isFractional =
      style == DurationStyle::Numeric && IsFractionalSecondUnitName(unit);
  if (isFractional) {
    // Step 4.a. (Not applicable in our implementation)

    // Step 4.b.
    displayDefault = DurationDisplay::Auto;
  }

  // Steps 5-6.
  static constexpr auto displays = MapOptions<DisplayToString>(
      DurationDisplay::Auto, DurationDisplay::Always);

  mozilla::Maybe<DurationDisplay> displayOption{};
  if (!GetStringOption(cx, options, DurationDisplayName(unit, cx), displays,
                       &displayOption)) {
    return false;
  }
  auto display = displayOption.valueOr(displayDefault);

  // Step 7. (Inlined ValidateDurationUnitStyle)

  // ValidateDurationUnitStyle, step 1.
  if (display == DurationDisplay::Always && isFractional) {
    MOZ_ASSERT(styleOption.isSome() || displayOption.isSome(),
               "no error is thrown when both 'style' and 'display' are absent");

    JSErrNum errorNumber =
        styleOption.isSome() && displayOption.isSome()
            ? JSMSG_INTL_DURATION_INVALID_DISPLAY_OPTION
        : displayOption.isSome()
            ? JSMSG_INTL_DURATION_INVALID_DISPLAY_OPTION_DEFAULT_STYLE
            : JSMSG_INTL_DURATION_INVALID_DISPLAY_OPTION_DEFAULT_DISPLAY;
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, errorNumber,
                              PluralUnitName(unit).data());
    return false;
  }

  // ValidateDurationUnitStyle, steps 2-3.
  if ((prevStyle == DurationStyle::Numeric ||
       prevStyle == DurationStyle::TwoDigit) &&
      !(style == DurationStyle::Numeric || style == DurationStyle::TwoDigit)) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_INTL_DURATION_INVALID_NON_NUMERIC_OPTION,
                              PluralUnitName(unit).data(),
                              DurationStyleToString(style).data());
    return false;
  }

  // Step 8. (Our implementation doesn't use |twoDigitHours|.)

  // Step 9.
  if ((TemporalUnit::Minute == unit || unit == TemporalUnit::Second) &&
      (prevStyle == DurationStyle::Numeric ||
       prevStyle == DurationStyle::TwoDigit)) {
    style = DurationStyle::TwoDigit;
  }

  // Step 10.
  *result = {style, display};
  return true;
}

/**
 * Intl.DurationFormat ( [ locales [ , options ] ] )
 */
static bool InitializeDurationFormat(
    JSContext* cx, Handle<DurationFormatObject*> durationFormat,
    const CallArgs& args) {
  // Step 3. (Inlined ResolveOptions)

  // ResolveOptions, step 1.
  auto* requestedLocales = CanonicalizeLocaleList(cx, args.get(0));
  if (!requestedLocales) {
    return false;
  }
  durationFormat->setRequestedLocales(requestedLocales);

  DurationFormatOptions dfOptions{};

  if (args.hasDefined(1)) {
    // ResolveOptions, steps 2-3.
    Rooted<JSObject*> options(cx, JS::ToObject(cx, args[1]));
    if (!options) {
      return false;
    }

    // ResolveOptions, step 4.
    LocaleMatcher matcher;
    if (!GetLocaleMatcherOption(cx, options, &matcher)) {
      return false;
    }

    // ResolveOptions, step 5.
    //
    // This implementation only supports the "lookup" locale matcher, therefore
    // the "localeMatcher" option doesn't need to be stored.

    // ResolveOptions, step 6.
    Rooted<JSLinearString*> numberingSystem(cx);
    if (!GetUnicodeExtensionOption(cx, options,
                                   UnicodeExtensionKey::NumberingSystem,
                                   &numberingSystem)) {
      return false;
    }
    if (numberingSystem) {
      durationFormat->setNumberingSystem(numberingSystem);
    }

    // ResolveOptions, step 7. (Not applicable)

    // ResolveOptions, step 8. (Performed in ResolveLocale)

    // ResolveOptions, step 9. (Return)

    // Step 4. (Not applicable when ResolveOptions is inlined.)

    // Steps 5-11. (Performed in ResolveLocale)

    // Steps 12-13.
    static constexpr auto styles = MapOptions<BaseStyleToString>(
        DurationBaseStyle::Long, DurationBaseStyle::Short,
        DurationBaseStyle::Narrow, DurationBaseStyle::Digital);
    DurationBaseStyle style;
    if (!GetStringOption(cx, options, cx->names().style, styles,
                         DurationBaseStyle::Short, &style)) {
      return false;
    }
    dfOptions.style = style;

    // Step 14.
    //
    // This implementation doesn't support passing an empty string for
    // |prevStyle|. Using one of the textual styles has the same effect, so we
    // use "long" here.
    constexpr auto emptyPrevStyle = DurationStyle::Long;

    // Step 15. (Loop unrolled)
    using DurationUnitOption = std::pair<DurationStyle, DurationDisplay>;

    DurationUnitOption years;
    if (!GetDurationUnitOptions(cx, TemporalUnit::Year, options, style,
                                DurationStyle::Short, emptyPrevStyle, &years)) {
      return false;
    }
    dfOptions.yearsStyle = years.first;
    dfOptions.yearsDisplay = years.second;

    DurationUnitOption months;
    if (!GetDurationUnitOptions(cx, TemporalUnit::Month, options, style,
                                DurationStyle::Short, emptyPrevStyle,
                                &months)) {
      return false;
    }
    dfOptions.monthsStyle = months.first;
    dfOptions.monthsDisplay = months.second;

    DurationUnitOption weeks;
    if (!GetDurationUnitOptions(cx, TemporalUnit::Week, options, style,
                                DurationStyle::Short, emptyPrevStyle, &weeks)) {
      return false;
    }
    dfOptions.weeksStyle = weeks.first;
    dfOptions.weeksDisplay = weeks.second;

    DurationUnitOption days;
    if (!GetDurationUnitOptions(cx, TemporalUnit::Day, options, style,
                                DurationStyle::Short, emptyPrevStyle, &days)) {
      return false;
    }
    dfOptions.daysStyle = days.first;
    dfOptions.daysDisplay = days.second;

    DurationUnitOption hours;
    if (!GetDurationUnitOptions(cx, TemporalUnit::Hour, options, style,
                                DurationStyle::Numeric, emptyPrevStyle,
                                &hours)) {
      return false;
    }
    dfOptions.hoursStyle = hours.first;
    dfOptions.hoursDisplay = hours.second;

    DurationUnitOption minutes;
    if (!GetDurationUnitOptions(cx, TemporalUnit::Minute, options, style,
                                DurationStyle::Numeric, hours.first,
                                &minutes)) {
      return false;
    }
    dfOptions.minutesStyle = minutes.first;
    dfOptions.minutesDisplay = minutes.second;

    DurationUnitOption seconds;
    if (!GetDurationUnitOptions(cx, TemporalUnit::Second, options, style,
                                DurationStyle::Numeric, minutes.first,
                                &seconds)) {
      return false;
    }
    dfOptions.secondsStyle = seconds.first;
    dfOptions.secondsDisplay = seconds.second;

    DurationUnitOption milliseconds;
    if (!GetDurationUnitOptions(cx, TemporalUnit::Millisecond, options, style,
                                DurationStyle::Numeric, seconds.first,
                                &milliseconds)) {
      return false;
    }
    dfOptions.millisecondsStyle = milliseconds.first;
    dfOptions.millisecondsDisplay = milliseconds.second;

    DurationUnitOption microseconds;
    if (!GetDurationUnitOptions(cx, TemporalUnit::Microsecond, options, style,
                                DurationStyle::Numeric, milliseconds.first,
                                &microseconds)) {
      return false;
    }
    dfOptions.microsecondsStyle = microseconds.first;
    dfOptions.microsecondsDisplay = microseconds.second;

    DurationUnitOption nanoseconds;
    if (!GetDurationUnitOptions(cx, TemporalUnit::Nanosecond, options, style,
                                DurationStyle::Numeric, microseconds.first,
                                &nanoseconds)) {
      return false;
    }
    dfOptions.nanosecondsStyle = nanoseconds.first;
    dfOptions.nanosecondsDisplay = nanoseconds.second;

    // Step 16.
    mozilla::Maybe<int32_t> fractionalDigits{};
    if (!GetNumberOption(cx, options, cx->names().fractionalDigits, 0, 9,
                         &fractionalDigits)) {
      return false;
    }
    dfOptions.fractionalDigits =
        static_cast<int8_t>(fractionalDigits.valueOr(-1));
  }
  durationFormat->setOptions(dfOptions);

  return true;
}

/**
 * Intl.DurationFormat ( [ locales [ , options ] ] )
 */
static bool DurationFormat(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Step 1.
  if (!ThrowIfNotConstructing(cx, args, "Intl.DurationFormat")) {
    return false;
  }

  // Step 2 (Inlined 9.1.14, OrdinaryCreateFromConstructor).
  Rooted<JSObject*> proto(cx);
  if (!GetPrototypeFromBuiltinConstructor(cx, args, JSProto_DurationFormat,
                                          &proto)) {
    return false;
  }

  Rooted<DurationFormatObject*> durationFormat(
      cx, NewObjectWithClassProto<DurationFormatObject>(cx, proto));
  if (!durationFormat) {
    return false;
  }

  // Steps 3-16.
  if (!InitializeDurationFormat(cx, durationFormat, args)) {
    return false;
  }

  // Step 17.
  args.rval().setObject(*durationFormat);
  return true;
}

/**
 * Resolve the actual locale to finish initialization of the DurationFormat.
 */
static bool ResolveLocale(JSContext* cx,
                          Handle<DurationFormatObject*> durationFormat) {
  // Return if the locale was already resolved.
  if (durationFormat->isLocaleResolved()) {
    return true;
  }

  Rooted<ArrayObject*> requestedLocales(
      cx, &durationFormat->getRequestedLocales()->as<ArrayObject>());

  // %Intl.DurationFormat%.[[RelevantExtensionKeys]] is « "nu" ».
  mozilla::EnumSet<UnicodeExtensionKey> relevantExtensionKeys{
      UnicodeExtensionKey::NumberingSystem,
  };

  // Initialize locale options from constructor arguments.
  Rooted<LocaleOptions> localeOptions(cx);
  if (auto* nu = durationFormat->getNumberingSystem()) {
    localeOptions.setUnicodeExtension(UnicodeExtensionKey::NumberingSystem, nu);
  }

  // Use the default locale data.
  auto localeData = LocaleData::Default;

  // Resolve the actual locale.
  Rooted<ResolvedLocale> resolved(cx);
  if (!ResolveLocale(cx, AvailableLocaleKind::DurationFormat, requestedLocales,
                     localeOptions, relevantExtensionKeys, localeData,
                     &resolved)) {
    return false;
  }

  // Finish initialization by setting the actual locale and numbering system.
  auto* locale = resolved.toLocale(cx);
  if (!locale) {
    return false;
  }
  durationFormat->setLocale(locale);

  if (auto nu = resolved.extension(UnicodeExtensionKey::NumberingSystem)) {
    durationFormat->setNumberingSystem(nu);
  } else {
    durationFormat->setNumberingSystem(cx->names().default_);
  }

  MOZ_ASSERT(durationFormat->isLocaleResolved(),
             "locale successfully resolved");
  return true;
}

static JSLinearString* ResolveNumberingSystem(
    JSContext* cx, Handle<DurationFormatObject*> durationFormat) {
  MOZ_ASSERT(durationFormat->isLocaleResolved());

  auto* numberingSystem = durationFormat->getNumberingSystem();
  if (numberingSystem == cx->names().default_) {
    numberingSystem = DefaultNumberingSystem(cx, durationFormat->getLocale());
    if (!numberingSystem) {
      return nullptr;
    }
    durationFormat->setNumberingSystem(numberingSystem);
  }
  return numberingSystem;
}

/**
 * Returns the time separator string for the given locale and numbering system.
 */
static JSString* GetTimeSeparator(
    JSContext* cx, Handle<DurationFormatObject*> durationFormat) {
  if (auto* separator = durationFormat->getTimeSeparator()) {
    return separator;
  }

  if (!ResolveLocale(cx, durationFormat)) {
    return nullptr;
  }

  auto locale = EncodeLocale(cx, durationFormat->getLocale());
  if (!locale) {
    return nullptr;
  }

  auto* numberingSystem = ResolveNumberingSystem(cx, durationFormat);
  if (!numberingSystem) {
    return nullptr;
  }

  auto numberingSystemChars = EncodeAscii(cx, numberingSystem);
  if (!numberingSystemChars) {
    return nullptr;
  }

  FormatBuffer<char16_t, INITIAL_CHAR_BUFFER_SIZE> separator(cx);
  auto result = mozilla::intl::DateTimeFormat::GetTimeSeparator(
      mozilla::MakeStringSpan(locale.get()),
      mozilla::MakeStringSpan(numberingSystemChars.get()), separator);
  if (result.isErr()) {
    ReportInternalError(cx, result.unwrapErr());
    return nullptr;
  }

  auto* string = separator.toString(cx);
  if (!string) {
    return nullptr;
  }

  durationFormat->setTimeSeparator(string);
  return string;
}

struct DurationValue {
  // The seconds part in a `temporal::TimeDuration` can't exceed
  // 9'007'199'254'740'991 and the nanoseconds part can't exceed 999'999'999.
  // This means the string representation needs at most 27 characters.
  static constexpr size_t MaximumDecimalStringLength =
      /* sign */ 1 +
      /* seconds part */ 16 +
      /* decimal dot */ 1 +
      /* nanoseconds part */ 9;

  // Next power of two after `MaximumDecimalStringLength`.
  static constexpr size_t DecimalStringCapacity = 32;

  double number = 0;
  char decimal[DecimalStringCapacity] = {};

  explicit DurationValue() = default;
  explicit DurationValue(double number) : number(number) {}

  bool isNegative() const {
    return mozilla::IsNegative(number) || decimal[0] == '-';
  }

  auto abs() const {
    // Return unchanged if not negative.
    if (!isNegative()) {
      return *this;
    }

    // Call |std::abs| for non-decimal values.
    if (!isDecimal()) {
      return DurationValue{std::abs(number)};
    }

    // Copy decimal strings without the leading '-' sign character.
    auto result = DurationValue{};
    std::copy(std::next(decimal), std::end(decimal), result.decimal);
    return result;
  }

  // |number| is active by default unless |decimal| is used.
  bool isDecimal() const { return decimal[0] != '\0'; }

  // Return true if this value represents either +0 or -0.
  bool isZero() const { return number == 0 && !isDecimal(); }

  operator std::string_view() const {
    MOZ_ASSERT(isDecimal());
    return {decimal};
  }
};

/**
 * Return the |unit| value from |duration|.
 */
static auto ToDurationValue(const temporal::Duration& duration,
                            TemporalUnit unit) {
  switch (unit) {
#define TO_DURATION_VALUE(UPPER, LOWER) \
  case TemporalUnit::UPPER:             \
    return DurationValue{duration.LOWER##s};

    FOR_EACH_DURATION_UNIT(TO_DURATION_VALUE)

#undef TO_DURATION_VALUE

    case TemporalUnit::Unset:
    case TemporalUnit::Auto:
      break;
  }
  MOZ_CRASH("invalid temporal unit");
}

/**
 * Return the fractional digits setting from |durationFormat|.
 */
static std::pair<uint32_t, uint32_t> GetFractionalDigits(
    const DurationFormatObject* durationFormat) {
  auto options = durationFormat->getOptions();

  int8_t digits = options.fractionalDigits;
  MOZ_ASSERT(digits <= 9);

  if (digits < 0) {
    return {0U, 9U};
  }
  return {uint32_t(digits), uint32_t(digits)};
}

static DurationUnitOptions GetUnitOptions(const DurationFormatOptions& options,
                                          TemporalUnit unit) {
  switch (unit) {
#define GET_UNIT_OPTIONS(UPPER, LOWER) \
  case TemporalUnit::UPPER:            \
    return DurationUnitOptions{options.LOWER##sDisplay, options.LOWER##sStyle};

    FOR_EACH_DURATION_UNIT(GET_UNIT_OPTIONS)

#undef GET_UNIT_OPTIONS

    case TemporalUnit::Unset:
    case TemporalUnit::Auto:
      break;
  }
  MOZ_CRASH("invalid duration unit");
}

/**
 * Create a `mozilla::intl::NumberFormat` instance based on
 * |durationFormat.locale| and |options|.
 */
static mozilla::intl::NumberFormat* NewDurationNumberFormat(
    JSContext* cx, Handle<DurationFormatObject*> durationFormat,
    const mozilla::intl::NumberFormatOptions& options) {
  if (!ResolveLocale(cx, durationFormat)) {
    return nullptr;
  }

  // ICU expects numberingSystem as a Unicode locale extensions on locale.
  //
  // We don't add any Unicode extension keywords when the default values can be
  // used, because ICU optimizes for this case.

  Rooted<JSLinearString*> localeStr(cx, durationFormat->getLocale());

  JS::RootedVector<UnicodeExtensionKeyword> keywords(cx);

  auto* numberingSystem = durationFormat->getNumberingSystem();
  if (numberingSystem != cx->names().default_) {
    if (!keywords.emplaceBack("nu", numberingSystem)) {
      return nullptr;
    }
  }

  auto locale = FormatLocale(cx, localeStr, keywords);
  if (!locale) {
    return nullptr;
  }

  auto result = mozilla::intl::NumberFormat::TryCreate(locale.get(), options);
  if (result.isErr()) {
    ReportInternalError(cx, result.unwrapErr());
    return nullptr;
  }
  return result.unwrap().release();
}

/**
 * Return the number format unit for |unit|.
 */
static auto ToNumberFormatUnit(TemporalUnit unit) {
  switch (unit) {
#define TO_NUMBER_FORMAT_UNIT(UPPER, LOWER) \
  case TemporalUnit::UPPER:                 \
    return NumberFormatUnit::UPPER;

    FOR_EACH_DURATION_UNIT(TO_NUMBER_FORMAT_UNIT)

#undef TO_NUMBER_FORMAT_UNIT

    case TemporalUnit::Unset:
    case TemporalUnit::Auto:
      break;
  }
  MOZ_CRASH("invalid temporal unit");
}

/**
 * Convert a duration-style to the corresponding NumberFormat unit-display.
 */
static auto UnitDisplay(DurationStyle style) {
  using UnitDisplay = mozilla::intl::NumberFormatOptions::UnitDisplay;

  switch (style) {
    case DurationStyle::Long:
      return UnitDisplay::Long;
    case DurationStyle::Short:
      return UnitDisplay::Short;
    case DurationStyle::Narrow:
      return UnitDisplay::Narrow;
    case DurationStyle::Numeric:
    case DurationStyle::TwoDigit:
      // Both numeric styles are invalid inputs for this function.
      break;
  }
  MOZ_CRASH("invalid duration style");
}

/**
 * ComputeFractionalDigits ( durationFormat, duration )
 *
 * Return the fractional seconds from |duration| as an exact value. This is
 * either an integer Number value when the fractional part is zero, or a
 * decimal string when the fractional part is non-zero.
 */
static auto ComputeFractionalDigits(const temporal::Duration& duration,
                                    TemporalUnit unit) {
  MOZ_ASSERT(IsValidDuration(duration));
  MOZ_ASSERT(TemporalUnit::Second <= unit && unit <= TemporalUnit::Microsecond);

  // Directly return the duration amount when no sub-seconds are present, i.e.
  // the fractional part is zero.
  temporal::TimeDuration timeDuration;
  int32_t exponent;
  switch (unit) {
    case TemporalUnit::Second: {
      if (duration.milliseconds == 0 && duration.microseconds == 0 &&
          duration.nanoseconds == 0) {
        return DurationValue{duration.seconds};
      }
      timeDuration = temporal::TimeDurationFromComponents({
          0,
          0,
          0,
          0,
          0,
          0,
          duration.seconds,
          duration.milliseconds,
          duration.microseconds,
          duration.nanoseconds,
      });
      exponent = 100'000'000;
      break;
    }

    case TemporalUnit::Millisecond: {
      if (duration.microseconds == 0 && duration.nanoseconds == 0) {
        return DurationValue{duration.milliseconds};
      }
      timeDuration = temporal::TimeDurationFromComponents({
          0,
          0,
          0,
          0,
          0,
          0,
          0,
          duration.milliseconds,
          duration.microseconds,
          duration.nanoseconds,
      });
      exponent = 100'000;
      break;
    }

    case TemporalUnit::Microsecond: {
      if (duration.nanoseconds == 0) {
        return DurationValue{duration.microseconds};
      }
      timeDuration = temporal::TimeDurationFromComponents({
          0,
          0,
          0,
          0,
          0,
          0,
          0,
          0,
          duration.microseconds,
          duration.nanoseconds,
      });
      exponent = 100;
      break;
    }

    default:
      MOZ_CRASH("bad temporal unit");
  }

  // Return the result as a decimal string when the fractional part is non-zero.

  DurationValue result{};

  char* chars = result.decimal;

  // Leading '-' sign when the duration is negative.
  if (timeDuration < temporal::TimeDuration{}) {
    *chars++ = '-';
    timeDuration = timeDuration.abs();
  }

  // Start of the integer part, without leading sign character.
  [[maybe_unused]] const char* integer = chars;

  // Avoid writing leading zeros.
  bool isZero = timeDuration.seconds == 0;

  // Next the string representation of the seconds value.
  if (!isZero) {
    auto res =
        std::to_chars(chars, std::end(result.decimal), timeDuration.seconds);
    MOZ_ASSERT(res.ec == std::errc());

    // Set |chars| to one past the last character written by `std::to_chars`.
    chars = res.ptr;
  }

  // Finish with string representation of the nanoseconds value, without any
  // trailing zeros.
  int32_t nanos = timeDuration.nanoseconds;
  [[maybe_unused]] bool hasFractional = false;
  for (int32_t k = 100'000'000; k != 0 && (k > exponent || nanos != 0);
       k /= 10) {
    // Add decimal separator at the correct position based on |exponent|.
    if (k == exponent) {
      // Add leading zero if necessary.
      if (isZero) {
        isZero = false;
        *chars++ = '0';
      }
      hasFractional = true;
      *chars++ = '.';
    }

    int32_t digit = (nanos / k);
    nanos %= k;

    isZero = isZero && digit == 0;
    if (!isZero) {
      *chars++ = char('0' + digit);
    }
  }

  MOZ_ASSERT((chars - result.decimal) <=
                 ptrdiff_t(DurationValue::MaximumDecimalStringLength),
             "unexpected decimal string length");
  MOZ_ASSERT(chars > integer, "unexpected empty decimal string");
  MOZ_ASSERT(!isZero, "unexpected all zero decimal string");
  MOZ_ASSERT(integer[0] != '0' || integer[1] == '.', "unexpected leading zero");
  MOZ_ASSERT(!hasFractional || chars[-1] != '0', "unexpected trailing zero");

  return result;
}

/**
 * FormatNumericHours ( durationFormat, hoursValue, signDisplayed )
 *
 * FormatNumericMinutes ( durationFormat, minutesValue, hoursDisplayed,
 * signDisplayed )
 *
 * FormatNumericSeconds ( durationFormat, secondsValue, minutesDisplayed,
 * signDisplayed )
 */
static mozilla::intl::NumberFormat* NewNumericFormatter(
    JSContext* cx, Handle<DurationFormatObject*> durationFormat,
    TemporalUnit unit) {
  // FormatNumericHours, step 1. (Not applicable in our implementation.)
  // FormatNumericMinutes, steps 1-2. (Not applicable in our implementation.)
  // FormatNumericSeconds, steps 1-2. (Not applicable in our implementation.)

  // FormatNumericHours, step 2.
  // FormatNumericMinutes, step 3.
  // FormatNumericSeconds, step 3.
  auto dfOptions = durationFormat->getOptions();
  auto style = GetUnitOptions(dfOptions, unit).style;

  // FormatNumericHours, step 3.
  // FormatNumericMinutes, step 4.
  // FormatNumericSeconds, step 4.
  MOZ_ASSERT(style == DurationStyle::Numeric ||
             style == DurationStyle::TwoDigit);

  // FormatNumericHours, step 4.
  // FormatNumericMinutes, step 5.
  // FormatNumericSeconds, step 5.
  mozilla::intl::NumberFormatOptions options{};

  // FormatNumericHours, steps 5-6. (Not applicable in our implementation.)
  // FormatNumericMinutes, steps 6-7. (Not applicable in our implementation.)
  // FormatNumericSeconds, steps 6-7. (Not applicable in our implementation.)

  // FormatNumericHours, step 7.
  // FormatNumericMinutes, step 8.
  // FormatNumericSeconds, step 8.
  if (style == DurationStyle::TwoDigit) {
    options.mMinIntegerDigits = mozilla::Some(2);
  }

  // FormatNumericHours, step 8. (Not applicable in our implementation.)
  // FormatNumericMinutes, step 9. (Not applicable in our implementation.)
  // FormatNumericSeconds, step 9. (Not applicable in our implementation.)

  // FormatNumericHours, step 9.
  // FormatNumericMinutes, step 10.
  // FormatNumericSeconds, step 10.
  options.mGrouping = mozilla::intl::NumberFormatOptions::Grouping::Never;

  // FormatNumericSeconds, steps 11-14.
  if (unit == TemporalUnit::Second) {
    // FormatNumericSeconds, step 11.
    auto fractionalDigits = GetFractionalDigits(durationFormat);

    // FormatNumericSeconds, steps 12-13.
    options.mFractionDigits = mozilla::Some(fractionalDigits);

    // FormatNumericSeconds, step 14.
    options.mRoundingMode =
        mozilla::intl::NumberFormatOptions::RoundingMode::Trunc;
  }

  // FormatNumericHours, step 10.
  // FormatNumericMinutes, step 11.
  // FormatNumericSeconds, step 15.
  return NewDurationNumberFormat(cx, durationFormat, options);
}

static mozilla::intl::NumberFormat* GetOrCreateNumericFormatter(
    JSContext* cx, Handle<DurationFormatObject*> durationFormat,
    TemporalUnit unit) {
  // Obtain a cached mozilla::intl::NumberFormat object.
  auto* nf = durationFormat->getNumberFormat(unit);
  if (nf) {
    return nf;
  }

  nf = NewNumericFormatter(cx, durationFormat, unit);
  if (!nf) {
    return nullptr;
  }
  durationFormat->setNumberFormat(unit, nf);

  AddICUCellMemory(durationFormat, NumberFormatObject::EstimatedMemoryUse);
  return nf;
}

/**
 * NextUnitFractional ( durationFormat, unit )
 */
static bool NextUnitFractional(const DurationFormatObject* durationFormat,
                               TemporalUnit unit) {
  // Steps 1-3.
  if (TemporalUnit::Second <= unit && unit <= TemporalUnit::Microsecond) {
    auto options = durationFormat->getOptions();

    using TemporalUnitType = std::underlying_type_t<TemporalUnit>;

    auto nextUnit =
        static_cast<TemporalUnit>(static_cast<TemporalUnitType>(unit) + 1);
    auto nextStyle = GetUnitOptions(options, nextUnit).style;
    return nextStyle == DurationStyle::Numeric;
  }

  // Step 4.
  return false;
}

/**
 * PartitionDurationFormatPattern ( durationFormat, duration )
 */
static mozilla::intl::NumberFormat* NewNumberFormat(
    JSContext* cx, Handle<DurationFormatObject*> durationFormat,
    TemporalUnit unit, DurationStyle style) {
  // Step 4.h.i.
  mozilla::intl::NumberFormatOptions options = {
      .mStyle = mozilla::intl::NumberFormatOptions::Style::Unit,
  };

  // Step 4.h.ii.
  if (NextUnitFractional(durationFormat, unit)) {
    // Steps 4.h.ii.2-4.
    auto fractionalDigits = GetFractionalDigits(durationFormat);
    options.mFractionDigits = mozilla::Some(fractionalDigits);

    // Step 4.h.ii.5.
    options.mRoundingMode =
        mozilla::intl::NumberFormatOptions::RoundingMode::Trunc;
  }

  // Steps 4.h.iii.4-6.
  options.mUnit =
      mozilla::Some(std::pair{SingularUnitName(unit), UnitDisplay(style)});

  // Step 4.h.iii.7.
  return NewDurationNumberFormat(cx, durationFormat, options);
}

static mozilla::intl::NumberFormat* GetOrCreateNumberFormat(
    JSContext* cx, Handle<DurationFormatObject*> durationFormat,
    TemporalUnit unit, DurationStyle style) {
  // Obtain a cached mozilla::intl::NumberFormat object.
  if (auto* nf = durationFormat->getNumberFormat(unit)) {
    return nf;
  }

  auto* nf = NewNumberFormat(cx, durationFormat, unit, style);
  if (!nf) {
    return nullptr;
  }
  durationFormat->setNumberFormat(unit, nf);

  AddICUCellMemory(durationFormat, NumberFormatObject::EstimatedMemoryUse);
  return nf;
}

static JSLinearString* FormatDurationValueToString(
    JSContext* cx, mozilla::intl::NumberFormat* nf,
    const DurationValue& value) {
  if (value.isDecimal()) {
    return FormatNumber(cx, nf, std::string_view{value});
  }
  return FormatNumber(cx, nf, value.number);
}

static ArrayObject* FormatDurationValueToParts(JSContext* cx,
                                               mozilla::intl::NumberFormat* nf,
                                               const DurationValue& value,
                                               TemporalUnit unit) {
  if (value.isDecimal()) {
    return FormatNumberToParts(cx, nf, std::string_view{value},
                               ToNumberFormatUnit(unit));
  }
  return FormatNumberToParts(cx, nf, value.number, ToNumberFormatUnit(unit));
}

static bool FormatDurationValue(JSContext* cx, mozilla::intl::NumberFormat* nf,
                                TemporalUnit unit, const DurationValue& value,
                                bool formatToParts,
                                MutableHandle<Value> result) {
  if (!formatToParts) {
    auto* str = FormatDurationValueToString(cx, nf, value);
    if (!str) {
      return false;
    }
    result.setString(str);
  } else {
    auto* parts = FormatDurationValueToParts(cx, nf, value, unit);
    if (!parts) {
      return false;
    }
    result.setObject(*parts);
  }
  return true;
}

/**
 * FormatNumericHours ( durationFormat, hoursValue, signDisplayed )
 *
 * FormatNumericMinutes ( durationFormat, minutesValue, hoursDisplayed,
 * signDisplayed )
 *
 * FormatNumericSeconds ( durationFormat, secondsValue, minutesDisplayed,
 * signDisplayed )
 */
static bool FormatNumericHoursOrMinutesOrSeconds(
    JSContext* cx, Handle<DurationFormatObject*> durationFormat,
    TemporalUnit unit, const DurationValue& value, bool formatToParts,
    MutableHandle<Value> result) {
  MOZ_ASSERT(TemporalUnit::Hour <= unit && unit <= TemporalUnit::Second);

  // FormatNumericHours, steps 1-10.
  // FormatNumericMinutes, steps 1-11.
  // FormatNumericSeconds, steps 1-15.
  auto* nf = GetOrCreateNumericFormatter(cx, durationFormat, unit);
  if (!nf) {
    return false;
  }

  // FormatNumericHours, steps 11-13.
  // FormatNumericMinutes, steps 12-14.
  // FormatNumericSeconds, steps 16-18.
  return FormatDurationValue(cx, nf, unit, value, formatToParts, result);
}

static PlainObject* NewLiteralPart(JSContext* cx, JSString* value) {
  Rooted<IdValueVector> properties(cx, cx);
  if (!properties.emplaceBack(NameToId(cx->names().type),
                              StringValue(cx->names().literal))) {
    return nullptr;
  }
  if (!properties.emplaceBack(NameToId(cx->names().value),
                              StringValue(value))) {
    return nullptr;
  }

  return NewPlainObjectWithUniqueNames(cx, properties);
}

/**
 * FormatNumericUnits ( durationFormat, duration, firstNumericUnit,
 * signDisplayed )
 */
static bool FormatNumericUnits(JSContext* cx,
                               Handle<DurationFormatObject*> durationFormat,
                               const temporal::Duration& duration,
                               TemporalUnit firstNumericUnit,
                               bool signDisplayed, bool formatToParts,
                               MutableHandle<Value> result) {
  auto options = durationFormat->getOptions();

  Rooted<Value> formattedValue(cx);

  // Step 1.
  MOZ_ASSERT(TemporalUnit::Hour <= firstNumericUnit &&
             firstNumericUnit <= TemporalUnit::Second);

  // Step 2.
  using FormattedNumericUnitsVector = JS::GCVector<Value, 3>;
  Rooted<FormattedNumericUnitsVector> numericPartsList(cx, cx);
  if (!numericPartsList.reserve(3)) {
    return false;
  }

  // Step 3.
  auto hoursValue = DurationValue{duration.hours};

  // Step 4.
  auto hoursDisplay = GetUnitOptions(options, TemporalUnit::Hour).display;

  // Step 5.
  auto minutesValue = DurationValue{duration.minutes};

  // Step 6.
  auto minutesDisplay = GetUnitOptions(options, TemporalUnit::Minute).display;

  // Step 7-8.
  auto secondsValue = ComputeFractionalDigits(duration, TemporalUnit::Second);

  // Step 9.
  auto secondsDisplay = GetUnitOptions(options, TemporalUnit::Second).display;

  // Step 10.
  bool hoursFormatted = false;

  // Step 11.
  if (firstNumericUnit == TemporalUnit::Hour) {
    // Step 11.a.
    hoursFormatted =
        !hoursValue.isZero() || hoursDisplay == DurationDisplay::Always;
  }

  // Steps 12-13.
  bool secondsFormatted =
      !secondsValue.isZero() || secondsDisplay == DurationDisplay::Always;

  // Step 14.
  bool minutesFormatted = false;

  // Step 15.
  if (firstNumericUnit == TemporalUnit::Hour ||
      firstNumericUnit == TemporalUnit::Minute) {
    // Steps 15.a-b.
    minutesFormatted = (hoursFormatted && secondsFormatted) ||
                       !minutesValue.isZero() ||
                       minutesDisplay == DurationDisplay::Always;
  }

  // Return early when no units are displayed.
  if (!hoursFormatted && !minutesFormatted && !secondsFormatted) {
    result.setUndefined();
    return true;
  }

  // Step 16.
  if (hoursFormatted) {
    // Step 16.a.
    if (signDisplayed) {
      if (hoursValue.isZero() && temporal::DurationSign(duration) < 0) {
        hoursValue = DurationValue{-0.0};
      }
    } else {
      // Use the absolute value to avoid changing number-format sign display.
      hoursValue = hoursValue.abs();
    }

    // Step 16.b.
    if (!FormatNumericHoursOrMinutesOrSeconds(cx, durationFormat,
                                              TemporalUnit::Hour, hoursValue,
                                              formatToParts, &formattedValue)) {
      return false;
    }

    // Step 16.c.
    numericPartsList.infallibleAppend(formattedValue);

    // Step 16.d.
    signDisplayed = false;
  }

  // Step 17.
  if (minutesFormatted) {
    // Step 17.a.
    if (signDisplayed) {
      if (minutesValue.isZero() && temporal::DurationSign(duration) < 0) {
        minutesValue = DurationValue{-0.0};
      }
    } else {
      // Use the absolute value to avoid changing number-format sign display.
      minutesValue = minutesValue.abs();
    }

    // Step 17.b.
    if (!FormatNumericHoursOrMinutesOrSeconds(
            cx, durationFormat, TemporalUnit::Minute, minutesValue,
            formatToParts, &formattedValue)) {
      return false;
    }

    // Step 17.c.
    numericPartsList.infallibleAppend(formattedValue);

    // Step 17.d.
    signDisplayed = false;
  }

  // Step 18.
  if (secondsFormatted) {
    // Step 18.a.
    if (!signDisplayed) {
      // Use the absolute value to avoid changing number-format sign display.
      secondsValue = secondsValue.abs();
    }
    if (!FormatNumericHoursOrMinutesOrSeconds(
            cx, durationFormat, TemporalUnit::Second, secondsValue,
            formatToParts, &formattedValue)) {
      return false;
    }

    // Step 18.b.
    numericPartsList.infallibleAppend(formattedValue);
  }

  MOZ_ASSERT(numericPartsList.length() > 0);

  // Step 19.
  if (numericPartsList.length() <= 1) {
    result.set(numericPartsList[0]);
    return true;
  }

  Rooted<JSString*> timeSeparator(cx, GetTimeSeparator(cx, durationFormat));
  if (!timeSeparator) {
    return false;
  }

  // Combine the individual parts into a single result.
  if (!formatToParts) {
    // Perform string concatenation when not formatting to parts.

    Rooted<JSString*> string(cx, numericPartsList[0].toString());
    Rooted<JSString*> nextString(cx);
    for (size_t i = 1; i < numericPartsList.length(); i++) {
      // Add the time separator between all elements.
      string = ConcatStrings<CanGC>(cx, string, timeSeparator);
      if (!string) {
        return false;
      }

      // Concatenate the formatted parts.
      nextString = numericPartsList[i].toString();
      string = ConcatStrings<CanGC>(cx, string, nextString);
      if (!string) {
        return false;
      }
    }

    result.setString(string);
  } else {
    // Append all formatted parts into a new array when formatting to parts.

    // First compute the final length of the result array.
    size_t length = 0;
    for (size_t i = 0; i < numericPartsList.length(); i++) {
      length += numericPartsList[i].toObject().as<ArrayObject>().length();
    }

    // Account for the time separator parts.
    length += numericPartsList.length() - 1;

    Rooted<ArrayObject*> array(cx, NewDenseFullyAllocatedArray(cx, length));
    if (!array) {
      return false;
    }
    array->ensureDenseInitializedLength(0, length);

    size_t index = 0;
    for (size_t i = 0; i < numericPartsList.length(); i++) {
      // Add the time separator between all elements.
      if (i > 0) {
        auto* timeSeparatorPart = NewLiteralPart(cx, timeSeparator);
        if (!timeSeparatorPart) {
          return false;
        }
        array->initDenseElement(index++, ObjectValue(*timeSeparatorPart));
      }

      auto* part = &numericPartsList[i].toObject().as<ArrayObject>();
      MOZ_ASSERT(IsPackedArray(part));

      // Append the formatted parts from |part|.
      for (size_t j = 0; j < part->length(); j++) {
        array->initDenseElement(index++, part->getDenseElement(j));
      }
    }
    MOZ_ASSERT(index == length);

    result.setObject(*array);
  }
  return true;
}

static auto ToListFormatStyle(DurationBaseStyle style) {
  MOZ_USING_ENUM(mozilla::intl::ListFormat::Style, Long, Short, Narrow);
  switch (style) {
    case DurationBaseStyle::Long:
      return Long;
    case DurationBaseStyle::Short:
      return Short;
    case DurationBaseStyle::Narrow:
      return Narrow;
    case DurationBaseStyle::Digital:
      return Short;
  }
  MOZ_CRASH("invalid duration format base style");
}

static mozilla::intl::ListFormat* NewDurationListFormat(
    JSContext* cx, Handle<DurationFormatObject*> durationFormat) {
  if (!ResolveLocale(cx, durationFormat)) {
    return nullptr;
  }
  auto dfOptions = durationFormat->getOptions();

  auto locale = EncodeLocale(cx, durationFormat->getLocale());
  if (!locale) {
    return nullptr;
  }

  mozilla::intl::ListFormat::Options options = {
      .mType = mozilla::intl::ListFormat::Type::Unit,
      .mStyle = ToListFormatStyle(dfOptions.style),
  };

  auto result = mozilla::intl::ListFormat::TryCreate(
      mozilla::MakeStringSpan(locale.get()), options);
  if (result.isErr()) {
    ReportInternalError(cx, result.unwrapErr());
    return nullptr;
  }
  return result.unwrap().release();
}

static mozilla::intl::ListFormat* GetOrCreateListFormat(
    JSContext* cx, Handle<DurationFormatObject*> durationFormat) {
  // Obtain a cached mozilla::intl::ListFormat object.
  if (auto* lf = durationFormat->getListFormat()) {
    return lf;
  }

  auto* lf = NewDurationListFormat(cx, durationFormat);
  if (!lf) {
    return nullptr;
  }
  durationFormat->setListFormat(lf);

  AddICUCellMemory(durationFormat, ListFormatObject::EstimatedMemoryUse);
  return lf;
}

// Stack space must be large enough to hold all ten duration values.
static constexpr size_t FormattedDurationValueVectorCapacity = 10;

using FormattedDurationValueVector =
    JS::GCVector<JS::Value, FormattedDurationValueVectorCapacity>;

/**
 * ListFormatParts ( durationFormat, partitionedPartsList )
 */
static bool ListFormatParts(
    JSContext* cx, Handle<DurationFormatObject*> durationFormat,
    Handle<FormattedDurationValueVector> partitionedPartsList,
    bool formatToParts, MutableHandle<Value> result) {
  // Steps 1-6.
  auto* lf = GetOrCreateListFormat(cx, durationFormat);
  if (!lf) {
    return false;
  }

  // <https://unicode.org/reports/tr35/tr35-general.html#ListPatterns> requires
  // that the list patterns are sorted, for example "{1} and {0}" isn't a valid
  // pattern, because "{1}" appears before "{0}". This requirement also means
  // all entries appear in order in the formatted result.

  // Step 7.
  Vector<UniqueTwoByteChars, mozilla::intl::DEFAULT_LIST_LENGTH> strings(cx);
  mozilla::intl::ListFormat::StringList stringList{};

  // Step 8.
  Rooted<JSString*> string(cx);
  Rooted<JSString*> nextString(cx);
  Rooted<ArrayObject*> parts(cx);
  Rooted<NativeObject*> part(cx);
  Rooted<Value> value(cx);
  for (size_t i = 0; i < partitionedPartsList.length(); i++) {
    if (!formatToParts) {
      string = partitionedPartsList[i].toString();
    } else {
      parts = &partitionedPartsList[i].toObject().as<ArrayObject>();
      MOZ_ASSERT(IsPackedArray(parts));

      // Combine the individual number-formatted parts into a single string.
      string = cx->emptyString();
      for (size_t j = 0; j < parts->length(); j++) {
        part = &parts->getDenseElement(j).toObject().as<NativeObject>();
        MOZ_ASSERT(part->containsPure(cx->names().type) &&
                       part->containsPure(cx->names().value),
                   "part is a number-formatted element");

        if (!GetProperty(cx, part, part, cx->names().value, &value)) {
          return false;
        }
        MOZ_ASSERT(value.isString());

        nextString = value.toString();
        string = ConcatStrings<CanGC>(cx, string, nextString);
        if (!string) {
          return false;
        }
      }
    }

    auto* linear = string->ensureLinear(cx);
    if (!linear) {
      return false;
    }

    size_t linearLength = linear->length();

    auto chars = cx->make_pod_array<char16_t>(linearLength);
    if (!chars) {
      return false;
    }
    CopyChars(chars.get(), *linear);

    if (!strings.append(std::move(chars))) {
      return false;
    }

    if (!stringList.emplaceBack(strings[i].get(), linearLength)) {
      return false;
    }
  }

  FormatBuffer<char16_t, INITIAL_CHAR_BUFFER_SIZE> buffer(cx);
  mozilla::intl::ListFormat::PartVector partVector{};

  // Step 9.
  auto formatResult = formatToParts
                          ? lf->FormatToParts(stringList, buffer, partVector)
                          : lf->Format(stringList, buffer);
  if (formatResult.isErr()) {
    ReportInternalError(cx, formatResult.unwrapErr());
    return false;
  }

  Rooted<JSLinearString*> overallResult(cx, buffer.toString(cx));
  if (!overallResult) {
    return false;
  }

  // Directly return the string result when not formatting to parts.
  if (!formatToParts) {
    result.setString(overallResult);
    return true;
  }

  // Step 10.
  size_t partitionedPartsIndex = 0;

  // Step 11. (Not applicable in our implementation.)

  // Compute the final length of the result array.
  size_t flattenedLength = 0;
  for (size_t i = 0; i < partitionedPartsList.length(); i++) {
    auto* parts = &partitionedPartsList[i].toObject().as<ArrayObject>();
    flattenedLength += parts->length();
  }
  for (const auto& part : partVector) {
    if (part.first == mozilla::intl::ListFormat::PartType::Literal) {
      flattenedLength += 1;
    }
  }

  // Step 12.
  Rooted<ArrayObject*> flattenedPartsList(
      cx, NewDenseFullyAllocatedArray(cx, flattenedLength));
  if (!flattenedPartsList) {
    return false;
  }
  flattenedPartsList->ensureDenseInitializedLength(0, flattenedLength);

  // Step 13.
  size_t flattenedPartsIndex = 0;
  size_t partBeginIndex = 0;
  for (const auto& part : partVector) {
    // Steps 13.a-b.
    if (part.first == mozilla::intl::ListFormat::PartType::Element) {
      // Step 13.a.i.
      MOZ_ASSERT(partitionedPartsIndex < partitionedPartsList.length(),
                 "partitionedPartsIndex is an index into result");

      // Step 13.a.ii.
      auto* parts = &partitionedPartsList[partitionedPartsIndex]
                         .toObject()
                         .as<ArrayObject>();
      MOZ_ASSERT(IsPackedArray(parts));

      // Step 13.a.iii.
      //
      // Replace the "element" parts with the number-formatted result.
      for (size_t i = 0; i < parts->length(); i++) {
        flattenedPartsList->initDenseElement(flattenedPartsIndex++,
                                             parts->getDenseElement(i));
      }

      // Step 13.a.iv.
      partitionedPartsIndex += 1;
    } else {
      // Step 13.b.i.
      //
      // Append "literal" parts as-is.
      MOZ_ASSERT(part.first == mozilla::intl::ListFormat::PartType::Literal);

      // Step 13.b.ii.
      MOZ_ASSERT(part.second >= partBeginIndex);
      auto* partStr = NewDependentString(cx, overallResult, partBeginIndex,
                                         part.second - partBeginIndex);
      if (!partStr) {
        return false;
      }

      auto* literalPart = NewLiteralPart(cx, partStr);
      if (!literalPart) {
        return false;
      }

      flattenedPartsList->initDenseElement(flattenedPartsIndex++,
                                           ObjectValue(*literalPart));
    }

    partBeginIndex = part.second;
  }

  MOZ_ASSERT(partitionedPartsIndex == partitionedPartsList.length(),
             "all number-formatted parts handled");
  MOZ_ASSERT(flattenedPartsIndex == flattenedLength,
             "flattened array length miscomputed");

  // Step 14.
  result.setObject(*flattenedPartsList);
  return true;
}

/**
 * PartitionDurationFormatPattern ( durationFormat, duration )
 */
static bool PartitionDurationFormatPattern(
    JSContext* cx, Handle<DurationFormatObject*> durationFormat,
    Handle<Value> durationLike, bool formatToParts,
    MutableHandle<Value> result) {
  temporal::Duration duration;
  if (!ToTemporalDuration(cx, durationLike, &duration)) {
    return false;
  }

  // Normalize -0 to +0 by adding zero.
  duration.years += +0.0;
  duration.months += +0.0;
  duration.weeks += +0.0;
  duration.days += +0.0;
  duration.hours += +0.0;
  duration.minutes += +0.0;
  duration.seconds += +0.0;
  duration.milliseconds += +0.0;
  duration.microseconds += +0.0;
  duration.nanoseconds += +0.0;

  static_assert(durationUnits.size() == FormattedDurationValueVectorCapacity,
                "inline stack capacity large enough for all duration units");

  auto options = durationFormat->getOptions();

  Rooted<Value> formattedValue(cx);

  // Step 1.
  Rooted<FormattedDurationValueVector> formattedValues(cx, cx);
  if (!formattedValues.reserve(FormattedDurationValueVectorCapacity)) {
    return false;
  }

  // Step 2.
  bool signDisplayed = true;

  // Step 3.
  bool numericUnitFound = false;

  // Step 4.
  for (auto unit : durationUnits) {
    if (numericUnitFound) {
      break;
    }

    // Step 4.a. (Moved below)

    // Step 4.b.
    auto unitOptions = GetUnitOptions(options, unit);

    // Step 4.c.
    auto style = unitOptions.style;

    // Step 4.d.
    auto display = unitOptions.display;

    // Steps 4.e-f. (Not applicable in our implementation.)

    // Steps 4.g-h.
    if (style == DurationStyle::Numeric || style == DurationStyle::TwoDigit) {
      // Step 4.g.i.
      if (!FormatNumericUnits(cx, durationFormat, duration, unit, signDisplayed,
                              formatToParts, &formattedValue)) {
        return false;
      }

      // Step 4.g.ii.
      if (!formattedValue.isUndefined()) {
        formattedValues.infallibleAppend(formattedValue);
      }

      // Step 4.g.iii.
      numericUnitFound = true;
    } else {
      // Step 4.a.
      auto value = ToDurationValue(duration, unit);

      // Step 4.h.i. (Performed in NewNumberFormat)

      // Step 4.h.ii.
      if (NextUnitFractional(durationFormat, unit)) {
        // Step 4.h.ii.1.
        value = ComputeFractionalDigits(duration, unit);

        // Steps 4.h.ii.2-5. (Performed in NewNumberFormat)

        // Step 4.h.ii.6.
        numericUnitFound = true;
      }

      // Step 4.h.iii. (Condition inverted to reduce indentation.)
      if (display == DurationDisplay::Auto && value.isZero()) {
        continue;
      }

      // Steps 4.h.iii.2-3.
      if (signDisplayed) {
        // Step 4.h.iii.2.a.
        signDisplayed = false;

        // Step 4.h.iii.2.b.
        if (value.isZero() && temporal::DurationSign(duration) < 0) {
          value = DurationValue{-0.0};
        }
      } else {
        // Use the absolute value to avoid changing number-format sign display.
        value = value.abs();
      }

      // Steps 4.h.iii.1, 4.h.iii.4-7.
      auto* nf = GetOrCreateNumberFormat(cx, durationFormat, unit, style);
      if (!nf) {
        return false;
      }

      // Steps 4.h.iii.8-10.
      if (!FormatDurationValue(cx, nf, unit, value, formatToParts,
                               &formattedValue)) {
        return false;
      }

      // Step 4.h.iii.11.
      formattedValues.infallibleAppend(formattedValue);
    }
  }

  // Step 5.
  return ListFormatParts(cx, durationFormat, formattedValues, formatToParts,
                         result);
}

static bool IsDurationFormat(Handle<JS::Value> v) {
  return v.isObject() && v.toObject().is<DurationFormatObject>();
}

/**
 * Intl.DurationFormat.prototype.format ( durationLike )
 */
static bool durationFormat_format(JSContext* cx, const JS::CallArgs& args) {
  Rooted<DurationFormatObject*> durationFormat(
      cx, &args.thisv().toObject().as<DurationFormatObject>());
  return PartitionDurationFormatPattern(
      cx, durationFormat, args.get(0), /* formatToParts= */ false, args.rval());
}

/**
 * Intl.DurationFormat.prototype.format ( durationLike )
 */
static bool durationFormat_format(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsDurationFormat, durationFormat_format>(cx,
                                                                       args);
}

/**
 * Intl.DurationFormat.prototype.formatToParts ( durationLike )
 */
static bool durationFormat_formatToParts(JSContext* cx,
                                         const JS::CallArgs& args) {
  Rooted<DurationFormatObject*> durationFormat(
      cx, &args.thisv().toObject().as<DurationFormatObject>());
  return PartitionDurationFormatPattern(cx, durationFormat, args.get(0),
                                        /* formatToParts= */ true, args.rval());
}

/**
 * Intl.DurationFormat.prototype.formatToParts ( durationLike )
 */
static bool durationFormat_formatToParts(JSContext* cx, unsigned argc,
                                         Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsDurationFormat, durationFormat_formatToParts>(
      cx, args);
}

/**
 * Intl.DurationFormat.prototype.resolvedOptions ( )
 */
static bool durationFormat_resolvedOptions(JSContext* cx,
                                           const JS::CallArgs& args) {
  Rooted<DurationFormatObject*> durationFormat(
      cx, &args.thisv().toObject().as<DurationFormatObject>());

  if (!ResolveLocale(cx, durationFormat)) {
    return false;
  }
  auto dfOptions = durationFormat->getOptions();

  // Step 3.
  Rooted<IdValueVector> options(cx, cx);

  // Step 4.
  if (!options.emplaceBack(NameToId(cx->names().locale),
                           StringValue(durationFormat->getLocale()))) {
    return false;
  }

  auto* numberingSystem = ResolveNumberingSystem(cx, durationFormat);
  if (!numberingSystem) {
    return false;
  }
  if (!options.emplaceBack(NameToId(cx->names().numberingSystem),
                           StringValue(numberingSystem))) {
    return false;
  }

  auto* style = NewStringCopy<CanGC>(cx, BaseStyleToString(dfOptions.style));
  if (!style) {
    return false;
  }
  if (!options.emplaceBack(NameToId(cx->names().style), StringValue(style))) {
    return false;
  }

  for (auto unit : durationUnits) {
    auto unitOptions = GetUnitOptions(dfOptions, unit);

    auto* style =
        NewStringCopy<CanGC>(cx, DurationStyleToString(unitOptions.style));
    if (!style) {
      return false;
    }
    if (!options.emplaceBack(NameToId(DurationStyleName(unit, cx)),
                             StringValue(style))) {
      return false;
    }

    auto* display =
        NewStringCopy<CanGC>(cx, DisplayToString(unitOptions.display));
    if (!display) {
      return false;
    }
    if (!options.emplaceBack(NameToId(DurationDisplayName(unit, cx)),
                             StringValue(display))) {
      return false;
    }
  }

  if (dfOptions.fractionalDigits >= 0) {
    MOZ_ASSERT(dfOptions.fractionalDigits <= 9);
    if (!options.emplaceBack(NameToId(cx->names().fractionalDigits),
                             Int32Value(dfOptions.fractionalDigits))) {
      return false;
    }
  }

  // Step 5.
  auto* result = NewPlainObjectWithUniqueNames(cx, options);
  if (!result) {
    return false;
  }
  args.rval().setObject(*result);
  return true;
}

/**
 * Intl.DurationFormat.prototype.resolvedOptions ( )
 */
static bool durationFormat_resolvedOptions(JSContext* cx, unsigned argc,
                                           Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsDurationFormat, durationFormat_resolvedOptions>(
      cx, args);
}

/**
 * Intl.DurationFormat.supportedLocalesOf ( locales [ , options ] )
 */
static bool durationFormat_supportedLocalesOf(JSContext* cx, unsigned argc,
                                              Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Steps 1-3.
  auto* array = SupportedLocalesOf(cx, AvailableLocaleKind::DurationFormat,
                                   args.get(0), args.get(1));
  if (!array) {
    return false;
  }
  args.rval().setObject(*array);
  return true;
}

bool js::intl::TemporalDurationToLocaleString(JSContext* cx,
                                              const JS::CallArgs& args) {
  MOZ_ASSERT(args.thisv().isObject());
  MOZ_ASSERT(args.thisv().toObject().is<temporal::DurationObject>());

  Rooted<DurationFormatObject*> durationFormat(
      cx, NewBuiltinClassInstance<DurationFormatObject>(cx));
  if (!durationFormat) {
    return false;
  }

  if (!InitializeDurationFormat(cx, durationFormat, args)) {
    return false;
  }

  return PartitionDurationFormatPattern(cx, durationFormat, args.thisv(),
                                        /* formatToParts= */ false,
                                        args.rval());
}

#undef FOR_EACH_DURATION_UNIT
