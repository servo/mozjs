/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Intl.DisplayNames implementation. */

#include "builtin/intl/DisplayNames.h"

#include "mozilla/Assertions.h"
#include "mozilla/intl/DisplayNames.h"
#include "mozilla/Span.h"
#include "mozilla/UsingEnum.h"

#include "jspubtd.h"

#include "builtin/intl/CommonFunctions.h"
#include "builtin/intl/FormatBuffer.h"
#include "builtin/intl/LocaleNegotiation.h"
#include "builtin/intl/Packed.h"
#include "builtin/intl/ParameterNegotiation.h"
#include "builtin/Number.h"
#include "gc/AllocKind.h"
#include "gc/GCContext.h"
#include "js/CallArgs.h"
#include "js/Class.h"
#include "js/experimental/Intl.h"     // JS::AddMozDisplayNamesConstructor
#include "js/friend/ErrorMessages.h"  // js::GetErrorMessage, JSMSG_*
#include "js/Printer.h"
#include "js/PropertyAndElement.h"  // JS_DefineFunctions, JS_DefineProperties
#include "js/PropertyDescriptor.h"
#include "js/PropertySpec.h"
#include "js/RootingAPI.h"
#include "js/TypeDecls.h"
#include "js/Utility.h"
#include "vm/GlobalObject.h"
#include "vm/JSContext.h"
#include "vm/JSObject.h"
#include "vm/Runtime.h"
#include "vm/SelfHosting.h"
#include "vm/Stack.h"
#include "vm/StringType.h"

#include "vm/JSObject-inl.h"
#include "vm/NativeObject-inl.h"

using namespace js;
using namespace js::intl;

const JSClassOps DisplayNamesObject::classOps_ = {
    .finalize = DisplayNamesObject::finalize,
};

const JSClass DisplayNamesObject::class_ = {
    "Intl.DisplayNames",
    JSCLASS_HAS_RESERVED_SLOTS(DisplayNamesObject::SLOT_COUNT) |
        JSCLASS_HAS_CACHED_PROTO(JSProto_DisplayNames) |
        JSCLASS_BACKGROUND_FINALIZE,
    &DisplayNamesObject::classOps_,
    &DisplayNamesObject::classSpec_,
};

const JSClass& DisplayNamesObject::protoClass_ = PlainObject::class_;

static bool displayNames_supportedLocalesOf(JSContext* cx, unsigned argc,
                                            Value* vp);

static bool displayNames_of(JSContext* cx, unsigned argc, Value* vp);

static bool displayNames_resolvedOptions(JSContext* cx, unsigned argc,
                                         Value* vp);

static bool displayNames_toSource(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  args.rval().setString(cx->names().DisplayNames);
  return true;
}

static const JSFunctionSpec displayNames_static_methods[] = {
    JS_FN("supportedLocalesOf", displayNames_supportedLocalesOf, 1, 0),
    JS_FS_END,
};

static const JSFunctionSpec displayNames_methods[] = {
    JS_FN("of", displayNames_of, 1, 0),
    JS_FN("resolvedOptions", displayNames_resolvedOptions, 0, 0),
    JS_FN("toSource", displayNames_toSource, 0, 0),
    JS_FS_END,
};

static const JSPropertySpec displayNames_properties[] = {
    JS_STRING_SYM_PS(toStringTag, "Intl.DisplayNames", JSPROP_READONLY),
    JS_PS_END,
};

static bool DisplayNames(JSContext* cx, unsigned argc, Value* vp);

const ClassSpec DisplayNamesObject::classSpec_ = {
    GenericCreateConstructor<DisplayNames, 2, gc::AllocKind::FUNCTION>,
    GenericCreatePrototype<DisplayNamesObject>,
    displayNames_static_methods,
    nullptr,
    displayNames_methods,
    displayNames_properties,
    nullptr,
    ClassSpec::DontDefineConstructor,
};

struct js::intl::DisplayNamesOptions {
  using Style = mozilla::intl::DisplayNames::Style;
  Style style = Style::Long;

  enum class Type : int8_t {
    Language,
    Region,
    Script,
    Currency,
    Calendar,
    DateTimeField,
    Weekday,
    Month,
    Quarter,
    DayPeriod
  };
  Type type = Type::Language;

  using Fallback = mozilla::intl::DisplayNames::Fallback;
  Fallback fallback = Fallback::Code;

  using LanguageDisplay = mozilla::intl::DisplayNames::LanguageDisplay;
  LanguageDisplay languageDisplay = LanguageDisplay::Dialect;

  bool mozExtensions = false;
};

struct PackedDisplayNamesOptions {
  using RawValue = uint32_t;

  using StyleField =
      packed::EnumField<RawValue, DisplayNamesOptions::Style::Narrow,
                        DisplayNamesOptions::Style::Abbreviated>;

  using TypeField =
      packed::EnumField<StyleField, DisplayNamesOptions::Type::Language,
                        DisplayNamesOptions::Type::DayPeriod>;

  using FallbackField =
      packed::EnumField<TypeField, DisplayNamesOptions::Fallback::None,
                        DisplayNamesOptions::Fallback::Code>;

  using LanguageDisplayField =
      packed::EnumField<FallbackField,
                        DisplayNamesOptions::LanguageDisplay::Standard,
                        DisplayNamesOptions::LanguageDisplay::Dialect>;

  using MozExtensionsField = packed::BooleanField<LanguageDisplayField>;

  using PackedValue = packed::PackedValue<MozExtensionsField>;

  static auto pack(const DisplayNamesOptions& options) {
    RawValue rawValue = StyleField::pack(options.style) |
                        TypeField::pack(options.type) |
                        FallbackField::pack(options.fallback) |
                        LanguageDisplayField::pack(options.languageDisplay) |
                        MozExtensionsField::pack(options.mozExtensions);
    return PackedValue::toValue(rawValue);
  }

  static auto unpack(JS::Value value) {
    RawValue rawValue = PackedValue::fromValue(value);
    return DisplayNamesOptions{
        .style = StyleField::unpack(rawValue),
        .type = TypeField::unpack(rawValue),
        .fallback = FallbackField::unpack(rawValue),
        .languageDisplay = LanguageDisplayField::unpack(rawValue),
        .mozExtensions = MozExtensionsField::unpack(rawValue),
    };
  }
};

DisplayNamesOptions js::intl::DisplayNamesObject::getOptions() const {
  const auto& slot = getFixedSlot(OPTIONS);
  if (slot.isUndefined()) {
    return {};
  }
  return PackedDisplayNamesOptions::unpack(slot);
}

void js::intl::DisplayNamesObject::setOptions(
    const DisplayNamesOptions& options) {
  setFixedSlot(OPTIONS, PackedDisplayNamesOptions::pack(options));
}

static constexpr std::string_view DisplayNamesStyleToString(
    DisplayNamesOptions::Style style) {
  MOZ_USING_ENUM(DisplayNamesOptions::Style, Long, Short, Narrow, Abbreviated);
  switch (style) {
    case Long:
      return "long";
    case Short:
      return "short";
    case Narrow:
      return "narrow";
    case Abbreviated:
      return "abbreviated";
  }
  MOZ_CRASH("invalid display names style");
}

static constexpr std::string_view DisplayNamesTypeToString(
    DisplayNamesOptions::Type type) {
  MOZ_USING_ENUM(DisplayNamesOptions::Type, Language, Region, Script, Currency,
                 Calendar, DateTimeField, Weekday, Month, Quarter, DayPeriod);
  switch (type) {
    case Language:
      return "language";
    case Region:
      return "region";
    case Script:
      return "script";
    case Currency:
      return "currency";
    case Calendar:
      return "calendar";
    case DateTimeField:
      return "dateTimeField";
    case Weekday:
      return "weekday";
    case Month:
      return "month";
    case Quarter:
      return "quarter";
    case DayPeriod:
      return "dayPeriod";
  }
  MOZ_CRASH("invalid display names type");
}

static constexpr std::string_view FallbackToString(
    DisplayNamesOptions::Fallback fallback) {
  MOZ_USING_ENUM(DisplayNamesOptions::Fallback, Code, None);
  switch (fallback) {
    case Code:
      return "code";
    case None:
      return "none";
  }
  MOZ_CRASH("invalid display names fallback");
}

static constexpr std::string_view LanguageDisplayToString(
    DisplayNamesOptions::LanguageDisplay languageDisplay) {
  MOZ_USING_ENUM(DisplayNamesOptions::LanguageDisplay, Dialect, Standard);
  switch (languageDisplay) {
    case Dialect:
      return "dialect";
    case Standard:
      return "standard";
  }
  MOZ_CRASH("invalid display names language display");
}

enum class DisplayNamesKind {
  Standard,

  // Calendar display names are no longer available with the current spec
  // proposal text, but may be re-enabled in the future. For our internal use
  // we still need to have them present, so use a feature guard for now.
  EnableMozExtensions,
};

/**
 * Intl.DisplayNames ( locales, options )
 */
static bool DisplayNames(JSContext* cx, const CallArgs& args,
                         DisplayNamesKind kind) {
  // Step 1.
  if (!ThrowIfNotConstructing(cx, args, "Intl.DisplayNames")) {
    return false;
  }

  // Step 2 (Inlined 9.1.14, OrdinaryCreateFromConstructor).
  Rooted<JSObject*> proto(cx);
  if (kind == DisplayNamesKind::Standard) {
    if (!GetPrototypeFromBuiltinConstructor(cx, args, JSProto_DisplayNames,
                                            &proto)) {
      return false;
    }
  } else {
    Rooted<JSObject*> newTarget(cx, &args.newTarget().toObject());
    if (!GetPrototypeFromConstructor(cx, newTarget, JSProto_Null, &proto)) {
      return false;
    }
  }

  Rooted<DisplayNamesObject*> displayNames(cx);
  displayNames = NewObjectWithClassProto<DisplayNamesObject>(cx, proto);
  if (!displayNames) {
    return false;
  }

  // Step 3. (Inlined ResolveOptions)

  // ResolveOptions, step 1.
  auto* requestedLocales = CanonicalizeLocaleList(cx, args.get(0));
  if (!requestedLocales) {
    return false;
  }
  displayNames->setRequestedLocales(requestedLocales);

  DisplayNamesOptions dnOptions{};
  dnOptions.mozExtensions = kind == DisplayNamesKind::EnableMozExtensions;

  // ResolveOptions, steps 2-3.
  Rooted<JSObject*> options(
      cx, RequireObjectArg(cx, "options", "Intl.DisplayNames", args.get(1)));
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
  if (kind == DisplayNamesKind::EnableMozExtensions) {
    Rooted<JSLinearString*> calendar(cx);
    if (!GetUnicodeExtensionOption(cx, options, UnicodeExtensionKey::Calendar,
                                   &calendar)) {
      return false;
    }
    if (calendar) {
      displayNames->setCalendar(calendar);
    }
  }

  // ResolveOptions, step 7. (Not applicable)

  // ResolveOptions, step 8. (Performed in ResolveLocale)

  // ResolveOptions, step 9. (Return)

  // Step 4. (Not applicable when ResolveOptions is inlined.)

  // Step 5. (Performed in ResolveRelativeTimeFormat)

  // Steps 6-7.
  if (kind == DisplayNamesKind::Standard) {
    static constexpr auto styles = MapOptions<DisplayNamesStyleToString>(
        DisplayNamesOptions::Style::Long, DisplayNamesOptions::Style::Short,
        DisplayNamesOptions::Style::Narrow);
    if (!GetStringOption(cx, options, cx->names().style, styles,
                         DisplayNamesOptions::Style::Long, &dnOptions.style)) {
      return false;
    }
  } else {
    static constexpr auto styles = MapOptions<DisplayNamesStyleToString>(
        DisplayNamesOptions::Style::Long, DisplayNamesOptions::Style::Short,
        DisplayNamesOptions::Style::Narrow,
        DisplayNamesOptions::Style::Abbreviated);
    if (!GetStringOption(cx, options, cx->names().style, styles,
                         DisplayNamesOptions::Style::Long, &dnOptions.style)) {
      return false;
    }
  }

  // Step 8.
  mozilla::Maybe<DisplayNamesOptions::Type> type{};
  if (kind == DisplayNamesKind::EnableMozExtensions) {
    static constexpr auto types = MapOptions<DisplayNamesTypeToString>(
        DisplayNamesOptions::Type::Language, DisplayNamesOptions::Type::Region,
        DisplayNamesOptions::Type::Script, DisplayNamesOptions::Type::Currency,
        DisplayNamesOptions::Type::Calendar,
        DisplayNamesOptions::Type::DateTimeField,
        DisplayNamesOptions::Type::Weekday, DisplayNamesOptions::Type::Month,
        DisplayNamesOptions::Type::Quarter,
        DisplayNamesOptions::Type::DayPeriod);
    if (!GetStringOption(cx, options, cx->names().type, types, &type)) {
      return false;
    }
  } else {
    static constexpr auto types = MapOptions<DisplayNamesTypeToString>(
        DisplayNamesOptions::Type::Language, DisplayNamesOptions::Type::Region,
        DisplayNamesOptions::Type::Script, DisplayNamesOptions::Type::Currency,
        DisplayNamesOptions::Type::Calendar,
        DisplayNamesOptions::Type::DateTimeField);
    if (!GetStringOption(cx, options, cx->names().type, types, &type)) {
      return false;
    }
  }

  // Step 9.
  if (type.isNothing()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_UNDEFINED_TYPE);
    return false;
  }

  // Step 10
  dnOptions.type = *type;

  // Steps 11-12.
  static constexpr auto fallbacks = MapOptions<FallbackToString>(
      DisplayNamesOptions::Fallback::Code, DisplayNamesOptions::Fallback::None);
  if (!GetStringOption(cx, options, cx->names().fallback, fallbacks,
                       DisplayNamesOptions::Fallback::Code,
                       &dnOptions.fallback)) {
    return false;
  }

  // Steps 13-14. (Performed in ResolveDisplayNames)

  // Steps 17 and 20.a.
  static constexpr auto languageDisplays = MapOptions<LanguageDisplayToString>(
      DisplayNamesOptions::LanguageDisplay::Dialect,
      DisplayNamesOptions::LanguageDisplay::Standard);
  if (!GetStringOption(cx, options, cx->names().languageDisplay,
                       languageDisplays,
                       DisplayNamesOptions::LanguageDisplay::Dialect,
                       &dnOptions.languageDisplay)) {
    return false;
  }

  // Assign the options to |displayNames|.
  displayNames->setOptions(dnOptions);

  // Steps 15-16, 18-19, 20.b-c, and 21-23. (Not applicable)

  // Step 24.
  args.rval().setObject(*displayNames);
  return true;
}

static bool DisplayNames(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return DisplayNames(cx, args, DisplayNamesKind::Standard);
}

static bool MozDisplayNames(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return DisplayNames(cx, args, DisplayNamesKind::EnableMozExtensions);
}

void js::intl::DisplayNamesObject::finalize(JS::GCContext* gcx, JSObject* obj) {
  auto* dn = &obj->as<DisplayNamesObject>();

  if (auto* displayNames = dn->getDisplayNames()) {
    RemoveICUCellMemory(gcx, obj, DisplayNamesObject::EstimatedMemoryUse);
    delete displayNames;
  }
}

bool JS::AddMozDisplayNamesConstructor(JSContext* cx, Handle<JSObject*> intl) {
  Rooted<JSObject*> ctor(
      cx, GlobalObject::createConstructor(cx, MozDisplayNames,
                                          cx->names().DisplayNames, 2));
  if (!ctor) {
    return false;
  }

  Rooted<JSObject*> proto(
      cx, GlobalObject::createBlankPrototype<PlainObject>(cx, cx->global()));
  if (!proto) {
    return false;
  }

  if (!LinkConstructorAndPrototype(cx, ctor, proto)) {
    return false;
  }

  if (!JS_DefineFunctions(cx, ctor, displayNames_static_methods)) {
    return false;
  }

  if (!JS_DefineFunctions(cx, proto, displayNames_methods)) {
    return false;
  }

  if (!JS_DefineProperties(cx, proto, displayNames_properties)) {
    return false;
  }

  Rooted<JS::Value> ctorValue(cx, ObjectValue(*ctor));
  return DefineDataProperty(cx, intl, cx->names().DisplayNames, ctorValue, 0);
}

/**
 * Resolve the actual locale to finish initialization of the DisplayNames.
 */
static bool ResolveLocale(JSContext* cx,
                          Handle<DisplayNamesObject*> displayNames) {
  // Return if the locale was already resolved.
  if (displayNames->isLocaleResolved()) {
    return true;
  }

  bool mozExtensions = displayNames->getOptions().mozExtensions;

  Rooted<ArrayObject*> requestedLocales(
      cx, &displayNames->getRequestedLocales()->as<ArrayObject>());

  // %Intl.DisplayNames%.[[RelevantExtensionKeys]] is « ».
  mozilla::EnumSet<UnicodeExtensionKey> relevantExtensionKeys{};

  // MozDisplayNames supports the "ca" Unicode extension.
  if (mozExtensions) {
    relevantExtensionKeys += UnicodeExtensionKey::Calendar;
  }

  // Initialize locale options from constructor arguments.
  Rooted<LocaleOptions> localeOptions(cx);
  if (mozExtensions) {
    if (auto* ca = displayNames->getCalendar()) {
      localeOptions.setUnicodeExtension(UnicodeExtensionKey::Calendar, ca);
    }
  }

  // Use the default locale data.
  auto localeData = LocaleData::Default;

  // Resolve the actual locale.
  Rooted<ResolvedLocale> resolved(cx);
  if (!ResolveLocale(cx, AvailableLocaleKind::DisplayNames, requestedLocales,
                     localeOptions, relevantExtensionKeys, localeData,
                     &resolved)) {
    return false;
  }

  // Finish initialization by setting the actual locale and calendar.
  auto* locale = resolved.toLocale(cx);
  if (!locale) {
    return false;
  }
  displayNames->setLocale(locale);

  if (mozExtensions) {
    if (auto ca = resolved.extension(UnicodeExtensionKey::Calendar)) {
      displayNames->setCalendar(ca);
    } else {
      displayNames->setCalendar(cx->names().default_);
    }
  }

  MOZ_ASSERT(displayNames->isLocaleResolved(), "locale successfully resolved");
  return true;
}

static JSLinearString* ResolveCalendar(
    JSContext* cx, Handle<DisplayNamesObject*> displayNames) {
  MOZ_ASSERT(displayNames->isLocaleResolved());

  auto* calendar = displayNames->getCalendar();
  if (calendar == cx->names().default_) {
    calendar = DefaultCalendar(cx, displayNames->getLocale());
    if (!calendar) {
      return nullptr;
    }
    displayNames->setCalendar(calendar);
  }
  return calendar;
}

static mozilla::intl::DisplayNames* NewDisplayNames(
    JSContext* cx, Handle<DisplayNamesObject*> displayNames) {
  if (!ResolveLocale(cx, displayNames)) {
    return nullptr;
  }
  auto dnOptions = displayNames->getOptions();

  auto locale = EncodeLocale(cx, displayNames->getLocale());
  if (!locale) {
    return nullptr;
  }

  mozilla::intl::DisplayNames::Options options = {
      .style = dnOptions.style,
      .languageDisplay = dnOptions.languageDisplay,
  };

  auto result = mozilla::intl::DisplayNames::TryCreate(locale.get(), options);
  if (result.isErr()) {
    ReportInternalError(cx, result.unwrapErr());
    return nullptr;
  }
  return result.unwrap().release();
}

static mozilla::intl::DisplayNames* GetOrCreateDisplayNames(
    JSContext* cx, Handle<DisplayNamesObject*> displayNames) {
  // Obtain a cached mozilla::intl::DisplayNames object.
  if (auto* dn = displayNames->getDisplayNames()) {
    return dn;
  }

  auto* dn = NewDisplayNames(cx, displayNames);
  if (!dn) {
    return nullptr;
  }
  displayNames->setDisplayNames(dn);

  AddICUCellMemory(displayNames, DisplayNamesObject::EstimatedMemoryUse);
  return dn;
}

// The "code" is usually a small ASCII string, so try to avoid an allocation
// by copying it to the stack. Unfortunately we can't pass a string span of
// the JSString directly to the unified DisplayNames API, as the
// intl::FormatBuffer will be written to. This writing can trigger a GC and
// invalidate the span, creating a nogc rooting hazard.
class StringUtf8Chars final {
  size_t length_ = 0;
  char inlineChars_[32] = {};
  UniqueChars allocChars_ = nullptr;

 public:
  bool init(JSContext* cx, Handle<JSLinearString*> string) {
    length_ = string->length();
    if (length_ < 32 && StringIsAscii(string)) {
      CopyChars(reinterpret_cast<Latin1Char*>(inlineChars_), *string);
    } else {
      allocChars_ = JS_EncodeStringToUTF8(cx, string);
      if (!allocChars_) {
        return false;
      }
    }
    return true;
  }

  const char* chars() const {
    return allocChars_ ? allocChars_.get() : inlineChars_;
  }

  size_t length() const { return length_; }

  operator mozilla::Span<const char>() const { return {chars(), length()}; }
};

static void ReportInvalidOptionError(JSContext* cx,
                                     DisplayNamesOptions::Type type,
                                     Handle<JSLinearString*> option) {
  auto sv = DisplayNamesTypeToString(type);
  MOZ_ASSERT(sv.data()[sv.length()] == '\0', "string must be zero-terminated");

  if (auto str = QuoteString(cx, option, '"')) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_INVALID_OPTION_VALUE, sv.data(), str.get());
  }
}

static void ReportInvalidOptionError(JSContext* cx,
                                     DisplayNamesOptions::Type type,
                                     double option) {
  ToCStringBuf cbuf;
  const char* str = NumberToCString(&cbuf, option);
  MOZ_ASSERT(str);
  JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                            JSMSG_INVALID_DIGITS_VALUE, str);
}

/**
 * Return the display name for the requested code or undefined if no applicable
 * display name was found.
 */
static bool ComputeDisplayName(JSContext* cx,
                               Handle<DisplayNamesObject*> displayNames,
                               Handle<JSLinearString*> code,
                               MutableHandle<JS::Value> rvalue) {
  auto* dn = GetOrCreateDisplayNames(cx, displayNames);
  if (!dn) {
    return false;
  }
  auto dnOptions = displayNames->getOptions();
  auto type = dnOptions.type;
  auto fallback = dnOptions.fallback;

  FormatBuffer<char16_t, INITIAL_CHAR_BUFFER_SIZE> buffer(cx);
  mozilla::Result<mozilla::Ok, mozilla::intl::DisplayNamesError> result =
      mozilla::Ok{};

  switch (type) {
    case DisplayNamesOptions::Type::Language: {
      StringUtf8Chars codeChars{};
      if (!codeChars.init(cx, code)) {
        return false;
      }
      result = dn->GetLanguage(buffer, codeChars, fallback);
      break;
    }
    case DisplayNamesOptions::Type::Region: {
      StringUtf8Chars codeChars{};
      if (!codeChars.init(cx, code)) {
        return false;
      }
      result = dn->GetRegion(buffer, codeChars, fallback);
      break;
    }
    case DisplayNamesOptions::Type::Script: {
      StringUtf8Chars codeChars{};
      if (!codeChars.init(cx, code)) {
        return false;
      }
      result = dn->GetScript(buffer, codeChars, fallback);
      break;
    }
    case DisplayNamesOptions::Type::Currency: {
      StringUtf8Chars codeChars{};
      if (!codeChars.init(cx, code)) {
        return false;
      }
      result = dn->GetCurrency(buffer, codeChars, fallback);
      break;
    }
    case DisplayNamesOptions::Type::Calendar: {
      StringUtf8Chars codeChars{};
      if (!codeChars.init(cx, code)) {
        return false;
      }
      result = dn->GetCalendar(buffer, codeChars, fallback);
      break;
    }
    case DisplayNamesOptions::Type::DateTimeField: {
      mozilla::intl::DateTimeField field;
      if (StringEqualsLiteral(code, "era")) {
        field = mozilla::intl::DateTimeField::Era;
      } else if (StringEqualsLiteral(code, "year")) {
        field = mozilla::intl::DateTimeField::Year;
      } else if (StringEqualsLiteral(code, "quarter")) {
        field = mozilla::intl::DateTimeField::Quarter;
      } else if (StringEqualsLiteral(code, "month")) {
        field = mozilla::intl::DateTimeField::Month;
      } else if (StringEqualsLiteral(code, "weekOfYear")) {
        field = mozilla::intl::DateTimeField::WeekOfYear;
      } else if (StringEqualsLiteral(code, "weekday")) {
        field = mozilla::intl::DateTimeField::Weekday;
      } else if (StringEqualsLiteral(code, "day")) {
        field = mozilla::intl::DateTimeField::Day;
      } else if (StringEqualsLiteral(code, "dayPeriod")) {
        field = mozilla::intl::DateTimeField::DayPeriod;
      } else if (StringEqualsLiteral(code, "hour")) {
        field = mozilla::intl::DateTimeField::Hour;
      } else if (StringEqualsLiteral(code, "minute")) {
        field = mozilla::intl::DateTimeField::Minute;
      } else if (StringEqualsLiteral(code, "second")) {
        field = mozilla::intl::DateTimeField::Second;
      } else if (StringEqualsLiteral(code, "timeZoneName")) {
        field = mozilla::intl::DateTimeField::TimeZoneName;
      } else {
        ReportInvalidOptionError(cx, type, code);
        return false;
      }

      auto locale = EncodeLocale(cx, displayNames->getLocale());
      if (!locale) {
        return false;
      }

      auto& sharedIntlData = cx->runtime()->sharedIntlData.ref();
      auto* dtpgen =
          sharedIntlData.getDateTimePatternGenerator(cx, locale.get());
      if (!dtpgen) {
        return false;
      }

      result = dn->GetDateTimeField(buffer, field, *dtpgen, fallback);
      break;
    }
    case DisplayNamesOptions::Type::Weekday: {
      double d = LinearStringToNumber(code);
      if (!IsInteger(d) || d < 1 || d > 7) {
        ReportInvalidOptionError(cx, type, d);
        return false;
      }

      auto* calendar = ResolveCalendar(cx, displayNames);
      if (!calendar) {
        return false;
      }

      auto calendarChars = EncodeAscii(cx, calendar);
      if (!calendarChars) {
        return false;
      }

      result = dn->GetWeekday(buffer, static_cast<mozilla::intl::Weekday>(d),
                              mozilla::MakeStringSpan(calendarChars.get()),
                              fallback);
      break;
    }
    case DisplayNamesOptions::Type::Month: {
      double d = LinearStringToNumber(code);
      if (!IsInteger(d) || d < 1 || d > 13) {
        ReportInvalidOptionError(cx, type, d);
        return false;
      }

      auto calendarChars = EncodeAscii(cx, displayNames->getCalendar());
      if (!calendarChars) {
        return false;
      }

      result =
          dn->GetMonth(buffer, static_cast<mozilla::intl::Month>(d),
                       mozilla::MakeStringSpan(calendarChars.get()), fallback);
      break;
    }
    case DisplayNamesOptions::Type::Quarter: {
      double d = LinearStringToNumber(code);

      // Inlined implementation of `IsValidQuarterCode ( quarter )`.
      if (!IsInteger(d) || d < 1 || d > 4) {
        ReportInvalidOptionError(cx, type, d);
        return false;
      }

      auto calendarChars = EncodeAscii(cx, displayNames->getCalendar());
      if (!calendarChars) {
        return false;
      }

      result = dn->GetQuarter(buffer, static_cast<mozilla::intl::Quarter>(d),
                              mozilla::MakeStringSpan(calendarChars.get()),
                              fallback);
      break;
    }
    case DisplayNamesOptions::Type::DayPeriod: {
      mozilla::intl::DayPeriod dayPeriod;
      if (StringEqualsLiteral(code, "am")) {
        dayPeriod = mozilla::intl::DayPeriod::AM;
      } else if (StringEqualsLiteral(code, "pm")) {
        dayPeriod = mozilla::intl::DayPeriod::PM;
      } else {
        ReportInvalidOptionError(cx, type, code);
        return false;
      }

      auto calendarChars = EncodeAscii(cx, displayNames->getCalendar());
      if (!calendarChars) {
        return false;
      }

      result = dn->GetDayPeriod(buffer, dayPeriod,
                                mozilla::MakeStringSpan(calendarChars.get()),
                                fallback);
      break;
    }
  }

  if (result.isErr()) {
    switch (result.unwrapErr()) {
      case mozilla::intl::DisplayNamesError::InternalError:
        ReportInternalError(cx);
        break;
      case mozilla::intl::DisplayNamesError::OutOfMemory:
        ReportOutOfMemory(cx);
        break;
      case mozilla::intl::DisplayNamesError::InvalidOption:
        ReportInvalidOptionError(cx, type, code);
        break;
      case mozilla::intl::DisplayNamesError::DuplicateVariantSubtag:
        JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                  JSMSG_DUPLICATE_VARIANT_SUBTAG);
        break;
      case mozilla::intl::DisplayNamesError::InvalidLanguageTag:
        JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                  JSMSG_INVALID_LANGUAGE_TAG);
        break;
    }
    return false;
  }

  auto* str = buffer.toString(cx);
  if (!str) {
    return false;
  }

  if (str->empty()) {
    rvalue.setUndefined();
  } else {
    rvalue.setString(str);
  }
  return true;
}

static bool IsDisplayNames(Handle<JS::Value> v) {
  return v.isObject() && v.toObject().is<DisplayNamesObject>();
}

/**
 * Intl.DisplayNames.prototype.of ( code )
 */
static bool displayNames_of(JSContext* cx, const CallArgs& args) {
  Rooted<DisplayNamesObject*> displayNames(
      cx, &args.thisv().toObject().as<DisplayNamesObject>());

  // Step 3.
  auto* str = JS::ToString(cx, args.get(0));
  if (!str) {
    return false;
  }

  Rooted<JSLinearString*> code(cx, str->ensureLinear(cx));
  if (!code) {
    return false;
  }

  // Steps 4-8.
  return ComputeDisplayName(cx, displayNames, code, args.rval());
}

/**
 * Intl.DisplayNames.prototype.of ( code )
 */
static bool displayNames_of(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsDisplayNames, displayNames_of>(cx, args);
}

/**
 * Intl.DisplayNames.prototype.resolvedOptions ( )
 */
static bool displayNames_resolvedOptions(JSContext* cx, const CallArgs& args) {
  Rooted<DisplayNamesObject*> displayNames(
      cx, &args.thisv().toObject().as<DisplayNamesObject>());

  if (!ResolveLocale(cx, displayNames)) {
    return false;
  }
  auto dnOptions = displayNames->getOptions();

  // Step 3.
  Rooted<IdValueVector> options(cx, cx);

  // Step 4.
  if (!options.emplaceBack(NameToId(cx->names().locale),
                           StringValue(displayNames->getLocale()))) {
    return false;
  }

  auto* style =
      NewStringCopy<CanGC>(cx, DisplayNamesStyleToString(dnOptions.style));
  if (!style) {
    return false;
  }
  if (!options.emplaceBack(NameToId(cx->names().style), StringValue(style))) {
    return false;
  }

  auto* type =
      NewStringCopy<CanGC>(cx, DisplayNamesTypeToString(dnOptions.type));
  if (!type) {
    return false;
  }
  if (!options.emplaceBack(NameToId(cx->names().type), StringValue(type))) {
    return false;
  }

  auto* fallback =
      NewStringCopy<CanGC>(cx, FallbackToString(dnOptions.fallback));
  if (!fallback) {
    return false;
  }
  if (!options.emplaceBack(NameToId(cx->names().fallback),
                           StringValue(fallback))) {
    return false;
  }

  // languageDisplay is only present for language display names.
  if (dnOptions.type == DisplayNamesOptions::Type::Language) {
    auto* languageDisplay = NewStringCopy<CanGC>(
        cx, LanguageDisplayToString(dnOptions.languageDisplay));
    if (!languageDisplay) {
      return false;
    }
    if (!options.emplaceBack(NameToId(cx->names().languageDisplay),
                             StringValue(languageDisplay))) {
      return false;
    }
  }

  if (dnOptions.mozExtensions) {
    auto* calendar = ResolveCalendar(cx, displayNames);
    if (!calendar) {
      return false;
    }

    if (!options.emplaceBack(NameToId(cx->names().calendar),
                             StringValue(calendar))) {
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
 * Intl.DisplayNames.prototype.resolvedOptions ( )
 */
static bool displayNames_resolvedOptions(JSContext* cx, unsigned argc,
                                         Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsDisplayNames, displayNames_resolvedOptions>(
      cx, args);
}

/**
 * Intl.DisplayNames.supportedLocalesOf ( locales [ , options ] )
 */
static bool displayNames_supportedLocalesOf(JSContext* cx, unsigned argc,
                                            Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Steps 1-3.
  auto* array = SupportedLocalesOf(cx, AvailableLocaleKind::DisplayNames,
                                   args.get(0), args.get(1));
  if (!array) {
    return false;
  }
  args.rval().setObject(*array);
  return true;
}
