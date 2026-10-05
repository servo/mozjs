/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Intl.RelativeTimeFormat implementation. */

#include "builtin/intl/RelativeTimeFormat.h"

#include "mozilla/Assertions.h"
#include "mozilla/intl/RelativeTimeFormat.h"
#include "mozilla/UsingEnum.h"

#include "builtin/intl/CommonFunctions.h"
#include "builtin/intl/FormatBuffer.h"
#include "builtin/intl/LanguageTag.h"
#include "builtin/intl/LocaleNegotiation.h"
#include "builtin/intl/NumberFormat.h"
#include "builtin/intl/Packed.h"
#include "builtin/intl/ParameterNegotiation.h"
#include "gc/GCContext.h"
#include "js/friend/ErrorMessages.h"  // js::GetErrorMessage, JSMSG_*
#include "js/Printer.h"
#include "js/PropertySpec.h"
#include "vm/GlobalObject.h"
#include "vm/JSContext.h"
#include "vm/PlainObject.h"
#include "vm/StringType.h"

#include "vm/NativeObject-inl.h"

using namespace js;
using namespace js::intl;

/**************** RelativeTimeFormat *****************/

const JSClassOps RelativeTimeFormatObject::classOps_ = {
    .finalize = RelativeTimeFormatObject::finalize,
};

const JSClass RelativeTimeFormatObject::class_ = {
    "Intl.RelativeTimeFormat",
    JSCLASS_HAS_RESERVED_SLOTS(RelativeTimeFormatObject::SLOT_COUNT) |
        JSCLASS_HAS_CACHED_PROTO(JSProto_RelativeTimeFormat) |
        JSCLASS_BACKGROUND_FINALIZE,
    &RelativeTimeFormatObject::classOps_,
    &RelativeTimeFormatObject::classSpec_,
};

const JSClass& RelativeTimeFormatObject::protoClass_ = PlainObject::class_;

static bool relativeTimeFormat_supportedLocalesOf(JSContext* cx, unsigned argc,
                                                  Value* vp);

static bool relativeTimeFormat_format(JSContext* cx, unsigned argc, Value* vp);

static bool relativeTimeFormat_formatToParts(JSContext* cx, unsigned argc,
                                             Value* vp);

static bool relativeTimeFormat_resolvedOptions(JSContext* cx, unsigned argc,
                                               Value* vp);

static bool relativeTimeFormat_toSource(JSContext* cx, unsigned argc,
                                        Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  args.rval().setString(cx->names().RelativeTimeFormat);
  return true;
}

static const JSFunctionSpec relativeTimeFormat_static_methods[] = {
    JS_FN("supportedLocalesOf", relativeTimeFormat_supportedLocalesOf, 1, 0),
    JS_FS_END,
};

static const JSFunctionSpec relativeTimeFormat_methods[] = {
    JS_FN("resolvedOptions", relativeTimeFormat_resolvedOptions, 0, 0),
    JS_FN("format", relativeTimeFormat_format, 2, 0),
    JS_FN("formatToParts", relativeTimeFormat_formatToParts, 2, 0),
    JS_FN("toSource", relativeTimeFormat_toSource, 0, 0),
    JS_FS_END,
};

static const JSPropertySpec relativeTimeFormat_properties[] = {
    JS_STRING_SYM_PS(toStringTag, "Intl.RelativeTimeFormat", JSPROP_READONLY),
    JS_PS_END,
};

static bool RelativeTimeFormat(JSContext* cx, unsigned argc, Value* vp);

const ClassSpec RelativeTimeFormatObject::classSpec_ = {
    GenericCreateConstructor<RelativeTimeFormat, 0, gc::AllocKind::FUNCTION>,
    GenericCreatePrototype<RelativeTimeFormatObject>,
    relativeTimeFormat_static_methods,
    nullptr,
    relativeTimeFormat_methods,
    relativeTimeFormat_properties,
    nullptr,
    ClassSpec::DontDefineConstructor,
};

struct js::intl::RelativeTimeFormatOptions {
  using Style = mozilla::intl::RelativeTimeFormatOptions::Style;
  Style style = Style::Long;

  using Numeric = mozilla::intl::RelativeTimeFormatOptions::Numeric;
  Numeric numeric = Numeric::Always;
};

struct PackedRelativeTimeFormatOptions {
  using RawValue = uint32_t;

  using StyleField =
      packed::EnumField<RawValue, RelativeTimeFormatOptions::Style::Short,
                        RelativeTimeFormatOptions::Style::Long>;

  using NumericField =
      packed::EnumField<StyleField, RelativeTimeFormatOptions::Numeric::Always,
                        RelativeTimeFormatOptions::Numeric::Auto>;

  using PackedValue = packed::PackedValue<NumericField>;

  static auto pack(const RelativeTimeFormatOptions& options) {
    RawValue rawValue =
        StyleField::pack(options.style) | NumericField::pack(options.numeric);
    return PackedValue::toValue(rawValue);
  }

  static auto unpack(JS::Value value) {
    RawValue rawValue = PackedValue::fromValue(value);
    return RelativeTimeFormatOptions{
        .style = StyleField::unpack(rawValue),
        .numeric = NumericField::unpack(rawValue),
    };
  }
};

RelativeTimeFormatOptions js::intl::RelativeTimeFormatObject::getOptions()
    const {
  const auto& slot = getFixedSlot(OPTIONS);
  if (slot.isUndefined()) {
    return {};
  }
  return PackedRelativeTimeFormatOptions::unpack(slot);
}

void js::intl::RelativeTimeFormatObject::setOptions(
    const RelativeTimeFormatOptions& options) {
  setFixedSlot(OPTIONS, PackedRelativeTimeFormatOptions::pack(options));
}

static constexpr std::string_view RelativeTimeStyleToString(
    RelativeTimeFormatOptions::Style style) {
  MOZ_USING_ENUM(RelativeTimeFormatOptions::Style, Long, Short, Narrow);
  switch (style) {
    case Long:
      return "long";
    case Short:
      return "short";
    case Narrow:
      return "narrow";
  }
  MOZ_CRASH("invalid relative time format style");
}

static constexpr std::string_view NumericToString(
    RelativeTimeFormatOptions::Numeric numeric) {
  MOZ_USING_ENUM(RelativeTimeFormatOptions::Numeric, Always, Auto);
  switch (numeric) {
    case Always:
      return "always";
    case Auto:
      return "auto";
  }
  MOZ_CRASH("invalid relative time format numeric");
}

/**
 * Intl.RelativeTimeFormat ( [ locales [ , options ] ] )
 */
static bool RelativeTimeFormat(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Step 1.
  if (!ThrowIfNotConstructing(cx, args, "Intl.RelativeTimeFormat")) {
    return false;
  }

  // Step 2 (Inlined 9.1.14, OrdinaryCreateFromConstructor).
  Rooted<JSObject*> proto(cx);
  if (!GetPrototypeFromBuiltinConstructor(cx, args, JSProto_RelativeTimeFormat,
                                          &proto)) {
    return false;
  }

  Rooted<RelativeTimeFormatObject*> relativeTimeFormat(cx);
  relativeTimeFormat =
      NewObjectWithClassProto<RelativeTimeFormatObject>(cx, proto);
  if (!relativeTimeFormat) {
    return false;
  }

  // Step 3. (Inlined ResolveOptions)

  // ResolveOptions, step 1.
  auto* requestedLocales = CanonicalizeLocaleList(cx, args.get(0));
  if (!requestedLocales) {
    return false;
  }
  relativeTimeFormat->setRequestedLocales(requestedLocales);

  RelativeTimeFormatOptions rtfOptions{};

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
      relativeTimeFormat->setNumberingSystem(numberingSystem);
    }

    // ResolveOptions, step 7. (Not applicable)

    // ResolveOptions, step 8. (Performed in ResolveRelativeTimeFormat)

    // ResolveOptions, step 9. (Return)

    // Step 4. (Not applicable when ResolveOptions is inlined.)

    // Steps 5-9. (Performed in ResolveLocale)

    // Steps 10-11.
    static constexpr auto styles = MapOptions<RelativeTimeStyleToString>(
        RelativeTimeFormatOptions::Style::Long,
        RelativeTimeFormatOptions::Style::Short,
        RelativeTimeFormatOptions::Style::Narrow);
    if (!GetStringOption(cx, options, cx->names().style, styles,
                         RelativeTimeFormatOptions::Style::Long,
                         &rtfOptions.style)) {
      return false;
    }

    // Steps 12-13.
    static constexpr auto numerics =
        MapOptions<NumericToString>(RelativeTimeFormatOptions::Numeric::Always,
                                    RelativeTimeFormatOptions::Numeric::Auto);
    if (!GetStringOption(cx, options, cx->names().numeric, numerics,
                         RelativeTimeFormatOptions::Numeric::Always,
                         &rtfOptions.numeric)) {
      return false;
    }
  }
  relativeTimeFormat->setOptions(rtfOptions);

  // Steps 14-17. (Not applicable in our implementation.)

  // Step 18.
  args.rval().setObject(*relativeTimeFormat);
  return true;
}

void js::intl::RelativeTimeFormatObject::finalize(JS::GCContext* gcx,
                                                  JSObject* obj) {
  auto* rtf = &obj->as<RelativeTimeFormatObject>();

  if (auto* formatter = rtf->getRelativeTimeFormatter()) {
    RemoveICUCellMemory(gcx, obj, RelativeTimeFormatObject::EstimatedMemoryUse);

    // This was allocated using `new` in mozilla::intl::RelativeTimeFormat,
    // so we delete here.
    delete formatter;
  }
}

/**
 * Resolve the actual locale to finish initialization of the RelativeTimeFormat.
 */
static bool ResolveLocale(
    JSContext* cx, Handle<RelativeTimeFormatObject*> relativeTimeFormat) {
  // Return if the locale was already resolved.
  if (relativeTimeFormat->isLocaleResolved()) {
    return true;
  }

  Rooted<ArrayObject*> requestedLocales(
      cx, &relativeTimeFormat->getRequestedLocales()->as<ArrayObject>());

  // %Intl.RelativeTimeFormat%.[[RelevantExtensionKeys]] is « "nu" ».
  mozilla::EnumSet<UnicodeExtensionKey> relevantExtensionKeys{
      UnicodeExtensionKey::NumberingSystem,
  };

  // Initialize locale options from constructor arguments.
  Rooted<LocaleOptions> localeOptions(cx);
  if (auto* nu = relativeTimeFormat->getNumberingSystem()) {
    localeOptions.setUnicodeExtension(UnicodeExtensionKey::NumberingSystem, nu);
  }

  // Use the default locale data.
  auto localeData = LocaleData::Default;

  // Resolve the actual locale.
  Rooted<ResolvedLocale> resolved(cx);
  if (!ResolveLocale(cx, AvailableLocaleKind::RelativeTimeFormat,
                     requestedLocales, localeOptions, relevantExtensionKeys,
                     localeData, &resolved)) {
    return false;
  }

  // Finish initialization by setting the actual locale and numbering system.
  auto* locale = resolved.toLocale(cx);
  if (!locale) {
    return false;
  }
  relativeTimeFormat->setLocale(locale);

  if (auto nu = resolved.extension(UnicodeExtensionKey::NumberingSystem)) {
    relativeTimeFormat->setNumberingSystem(nu);
  } else {
    relativeTimeFormat->setNumberingSystem(cx->names().default_);
  }

  MOZ_ASSERT(relativeTimeFormat->isLocaleResolved(),
             "locale successfully resolved");
  return true;
}

static JSLinearString* ResolveNumberingSystem(
    JSContext* cx, Handle<RelativeTimeFormatObject*> relativeTimeFormat) {
  MOZ_ASSERT(relativeTimeFormat->isLocaleResolved());

  auto* numberingSystem = relativeTimeFormat->getNumberingSystem();
  if (numberingSystem == cx->names().default_) {
    numberingSystem =
        DefaultNumberingSystem(cx, relativeTimeFormat->getLocale());
    if (!numberingSystem) {
      return nullptr;
    }
    relativeTimeFormat->setNumberingSystem(numberingSystem);
  }
  return numberingSystem;
}

/**
 * Returns a new URelativeDateTimeFormatter with the locale and options of the
 * given RelativeTimeFormatObject.
 */
static mozilla::intl::RelativeTimeFormat* NewRelativeTimeFormatter(
    JSContext* cx, Handle<RelativeTimeFormatObject*> relativeTimeFormat) {
  if (!ResolveLocale(cx, relativeTimeFormat)) {
    return nullptr;
  }
  auto rtfOptions = relativeTimeFormat->getOptions();

  // ICU expects numberingSystem as a Unicode locale extensions on locale.
  //
  // We don't add any Unicode extension keywords when the default values can be
  // used, because ICU optimizes for this case.

  JS::RootedVector<UnicodeExtensionKeyword> keywords(cx);

  auto* numberingSystem = relativeTimeFormat->getNumberingSystem();
  if (numberingSystem != cx->names().default_) {
    if (!keywords.emplaceBack("nu", numberingSystem)) {
      return nullptr;
    }
  }

  Rooted<JSLinearString*> localeStr(cx, relativeTimeFormat->getLocale());
  auto locale = FormatLocale(cx, localeStr, keywords);
  if (!locale) {
    return nullptr;
  }

  mozilla::intl::RelativeTimeFormatOptions options = {
      .style = rtfOptions.style,
      .numeric = rtfOptions.numeric,
  };

  auto result =
      mozilla::intl::RelativeTimeFormat::TryCreate(locale.get(), options);
  if (result.isErr()) {
    ReportInternalError(cx, result.unwrapErr());
    return nullptr;
  }
  return result.unwrap().release();
}

static mozilla::intl::RelativeTimeFormat* GetOrCreateRelativeTimeFormat(
    JSContext* cx, Handle<RelativeTimeFormatObject*> relativeTimeFormat) {
  // Obtain a cached RelativeDateTimeFormatter object.
  auto* rtf = relativeTimeFormat->getRelativeTimeFormatter();
  if (rtf) {
    return rtf;
  }

  rtf = NewRelativeTimeFormatter(cx, relativeTimeFormat);
  if (!rtf) {
    return nullptr;
  }
  relativeTimeFormat->setRelativeTimeFormatter(rtf);

  AddICUCellMemory(relativeTimeFormat,
                   RelativeTimeFormatObject::EstimatedMemoryUse);
  return rtf;
}

/**
 * SingularRelativeTimeUnit ( unit )
 */
static bool SingularRelativeTimeUnit(
    JSContext* cx, Handle<JSString*> string,
    mozilla::intl::RelativeTimeFormat::FormatUnit* result) {
  using FormatUnit = mozilla::intl::RelativeTimeFormat::FormatUnit;

  auto* unit = string->ensureLinear(cx);
  if (!unit) {
    return false;
  }

  // Steps 1-10.
  if (StringEqualsLiteral(unit, "second") ||
      StringEqualsLiteral(unit, "seconds")) {
    *result = FormatUnit::Second;
  } else if (StringEqualsLiteral(unit, "minute") ||
             StringEqualsLiteral(unit, "minutes")) {
    *result = FormatUnit::Minute;
  } else if (StringEqualsLiteral(unit, "hour") ||
             StringEqualsLiteral(unit, "hours")) {
    *result = FormatUnit::Hour;
  } else if (StringEqualsLiteral(unit, "day") ||
             StringEqualsLiteral(unit, "days")) {
    *result = FormatUnit::Day;
  } else if (StringEqualsLiteral(unit, "week") ||
             StringEqualsLiteral(unit, "weeks")) {
    *result = FormatUnit::Week;
  } else if (StringEqualsLiteral(unit, "month") ||
             StringEqualsLiteral(unit, "months")) {
    *result = FormatUnit::Month;
  } else if (StringEqualsLiteral(unit, "quarter") ||
             StringEqualsLiteral(unit, "quarters")) {
    *result = FormatUnit::Quarter;
  } else if (StringEqualsLiteral(unit, "year") ||
             StringEqualsLiteral(unit, "years")) {
    *result = FormatUnit::Year;
  } else {
    if (auto unitChars = QuoteString(cx, unit, '"')) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_INVALID_OPTION_VALUE, "unit",
                                unitChars.get());
    }
    return false;
  }
  return true;
}

static auto ToNumberFormatUnit(
    mozilla::intl::RelativeTimeFormat::FormatUnit unit) {
  MOZ_USING_ENUM(mozilla::intl::RelativeTimeFormat::FormatUnit, Second, Minute,
                 Hour, Day, Week, Month, Quarter, Year);
  switch (unit) {
    case Second:
      return NumberFormatUnit::Second;
    case Minute:
      return NumberFormatUnit::Minute;
    case Hour:
      return NumberFormatUnit::Hour;
    case Day:
      return NumberFormatUnit::Day;
    case Week:
      return NumberFormatUnit::Week;
    case Month:
      return NumberFormatUnit::Month;
    case Quarter:
      return NumberFormatUnit::Quarter;
    case Year:
      return NumberFormatUnit::Year;
  }
  MOZ_CRASH("invalid format unit");
}

/**
 * FormatRelativeTime ( relativeTimeFormat, value, unit )
 * FormatRelativeTimeToParts ( relativeTimeFormat, value, unit )
 * PartitionRelativeTimePattern ( relativeTimeFormat, value, unit )
 *
 * Returns a relative time as a string formatted according to the effective
 * locale and the formatting options of the given RelativeTimeFormat.
 */
static bool FormatRelativeTime(
    JSContext* cx, Handle<RelativeTimeFormatObject*> relativeTimeFormat,
    double value, Handle<JSString*> unit, bool formatToParts,
    MutableHandle<JS::Value> rvalue) {
  // PartitionRelativeTimePattern, step 1.
  if (!std::isfinite(value)) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_DATE_NOT_FINITE, "RelativeTimeFormat",
                              formatToParts ? "formatToParts" : "format");
    return false;
  }

  // PartitionRelativeTimePattern, step 2.
  mozilla::intl::RelativeTimeFormat::FormatUnit relTimeUnit;
  if (!SingularRelativeTimeUnit(cx, unit, &relTimeUnit)) {
    return false;
  }

  auto* rtf = GetOrCreateRelativeTimeFormat(cx, relativeTimeFormat);
  if (!rtf) {
    return false;
  }

  // PartitionRelativeTimePattern, steps 3-14.
  // FormatRelativeTimeToParts, steps 2-5.
  if (formatToParts) {
    mozilla::intl::NumberPartVector parts;
    auto result = rtf->formatToParts(value, relTimeUnit, parts);
    if (result.isErr()) {
      ReportInternalError(cx, result.unwrapErr());
      return false;
    }

    Rooted<JSString*> str(cx, NewStringCopy<CanGC>(cx, result.unwrap()));
    if (!str) {
      return false;
    }

    auto numberUnit = ToNumberFormatUnit(relTimeUnit);
    return FormattedRelativeTimeToParts(cx, str, parts, numberUnit, rvalue);
  }

  // PartitionRelativeTimePattern, steps 3-14.
  // FormatRelativeTime, steps 2-4.
  FormatBuffer<char16_t, INITIAL_CHAR_BUFFER_SIZE> buffer(cx);
  auto result = rtf->format(value, relTimeUnit, buffer);
  if (result.isErr()) {
    ReportInternalError(cx, result.unwrapErr());
    return false;
  }

  auto* str = buffer.toString(cx);
  if (!str) {
    return false;
  }

  rvalue.setString(str);
  return true;
}

static bool IsRelativeTimeFormat(Handle<JS::Value> v) {
  return v.isObject() && v.toObject().is<RelativeTimeFormatObject>();
}

/**
 * Intl.RelativeTimeFormat.prototype.format ( value, unit )
 */
static bool relativeTimeFormat_format(JSContext* cx, const CallArgs& args) {
  Rooted<RelativeTimeFormatObject*> relativeTimeFormat(
      cx, &args.thisv().toObject().as<RelativeTimeFormatObject>());

  // Step 3.
  double value;
  if (!JS::ToNumber(cx, args.get(0), &value)) {
    return false;
  }

  // Step 4.
  Rooted<JSString*> unit(cx, JS::ToString(cx, args.get(1)));
  if (!unit) {
    return false;
  }

  // Step 5.
  return FormatRelativeTime(cx, relativeTimeFormat, value, unit,
                            /* formatToParts= */ false, args.rval());
}

/**
 * Intl.RelativeTimeFormat.prototype.format ( value, unit )
 */
static bool relativeTimeFormat_format(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsRelativeTimeFormat, relativeTimeFormat_format>(
      cx, args);
}

/**
 * Intl.RelativeTimeFormat.prototype.formatToParts ( value, unit )
 */
static bool relativeTimeFormat_formatToParts(JSContext* cx,
                                             const CallArgs& args) {
  Rooted<RelativeTimeFormatObject*> relativeTimeFormat(
      cx, &args.thisv().toObject().as<RelativeTimeFormatObject>());

  // Step 3.
  double value;
  if (!JS::ToNumber(cx, args.get(0), &value)) {
    return false;
  }

  // Step 4.
  Rooted<JSString*> unit(cx, JS::ToString(cx, args.get(1)));
  if (!unit) {
    return false;
  }

  // Step 5.
  return FormatRelativeTime(cx, relativeTimeFormat, value, unit,
                            /* formatToParts= */ true, args.rval());
}

/**
 * Intl.RelativeTimeFormat.prototype.formatToParts ( value, unit )
 */
static bool relativeTimeFormat_formatToParts(JSContext* cx, unsigned argc,
                                             Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsRelativeTimeFormat,
                              relativeTimeFormat_formatToParts>(cx, args);
}

/**
 * Intl.RelativeTimeFormat.prototype.resolvedOptions ( )
 */
static bool relativeTimeFormat_resolvedOptions(JSContext* cx,
                                               const CallArgs& args) {
  Rooted<RelativeTimeFormatObject*> relativeTimeFormat(
      cx, &args.thisv().toObject().as<RelativeTimeFormatObject>());

  if (!ResolveLocale(cx, relativeTimeFormat)) {
    return false;
  }
  auto rtfOptions = relativeTimeFormat->getOptions();

  // Step 3.
  Rooted<IdValueVector> options(cx, cx);

  // Step 4.
  if (!options.emplaceBack(NameToId(cx->names().locale),
                           StringValue(relativeTimeFormat->getLocale()))) {
    return false;
  }

  auto* style =
      NewStringCopy<CanGC>(cx, RelativeTimeStyleToString(rtfOptions.style));
  if (!style) {
    return false;
  }
  if (!options.emplaceBack(NameToId(cx->names().style), StringValue(style))) {
    return false;
  }

  auto* numeric = NewStringCopy<CanGC>(cx, NumericToString(rtfOptions.numeric));
  if (!numeric) {
    return false;
  }
  if (!options.emplaceBack(NameToId(cx->names().numeric),
                           StringValue(numeric))) {
    return false;
  }

  auto* numberingSystem = ResolveNumberingSystem(cx, relativeTimeFormat);
  if (!numberingSystem) {
    return false;
  }
  if (!options.emplaceBack(NameToId(cx->names().numberingSystem),
                           StringValue(numberingSystem))) {
    return false;
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
 * Intl.RelativeTimeFormat.prototype.resolvedOptions ( )
 */
static bool relativeTimeFormat_resolvedOptions(JSContext* cx, unsigned argc,
                                               Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsRelativeTimeFormat,
                              relativeTimeFormat_resolvedOptions>(cx, args);
}

/**
 * Intl.RelativeTimeFormat.supportedLocalesOf ( locales [ , options ] )
 */
static bool relativeTimeFormat_supportedLocalesOf(JSContext* cx, unsigned argc,
                                                  Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Steps 1-3.
  auto* array = SupportedLocalesOf(cx, AvailableLocaleKind::RelativeTimeFormat,
                                   args.get(0), args.get(1));
  if (!array) {
    return false;
  }
  args.rval().setObject(*array);
  return true;
}
