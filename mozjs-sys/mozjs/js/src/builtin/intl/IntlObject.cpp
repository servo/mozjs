/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Implementation of the Intl object and its non-constructor properties. */

#include "builtin/intl/IntlObject.h"

#include "mozilla/Assertions.h"
#include "mozilla/intl/Calendar.h"
#include "mozilla/intl/Collator.h"
#include "mozilla/intl/Currency.h"
#include "mozilla/intl/TimeZone.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <string_view>

#include "builtin/Array.h"
#include "builtin/intl/CommonFunctions.h"
#include "builtin/intl/LocaleNegotiation.h"
#include "builtin/intl/MeasureUnitGenerated.h"
#include "builtin/intl/NumberingSystemsGenerated.h"
#include "builtin/intl/SharedIntlData.h"
#include "js/Class.h"
#include "js/experimental/Intl.h"
#include "js/friend/ErrorMessages.h"  // js::GetErrorMessage, JSMSG_*
#include "js/GCAPI.h"
#include "js/GCVector.h"
#include "js/PropertyAndElement.h"
#include "js/PropertySpec.h"
#include "vm/GlobalObject.h"
#include "vm/JSAtomUtils.h"  // ClassName
#include "vm/JSContext.h"
#include "vm/PlainObject.h"  // js::PlainObject
#include "vm/StringType.h"

#include "vm/JSObject-inl.h"
#include "vm/NativeObject-inl.h"

using namespace js;
using namespace js::intl;

/******************** Mozilla Intl extensions ********************/

/**
 * Returns a plain object with calendar information for a single valid locale
 * (callers must perform this validation).  The object will have these
 * properties:
 *
 *   firstDayOfWeek
 *     an integer in the range 1=Monday to 7=Sunday indicating the day
 *     considered the first day of the week in calendars, e.g. 7 for en-US,
 *     1 for en-GB, 7 for bn-IN
 *   minDays
 *     an integer in the range of 1 to 7 indicating the minimum number
 *     of days required in the first week of the year, e.g. 1 for en-US,
 *     4 for de
 *   weekend
 *     an array with values in the range 1=Monday to 7=Sunday indicating the
 *     days of the week considered as part of the weekend, e.g. [6, 7] for en-US
 *     and en-GB, [7] for bn-IN (note that "weekend" is *not* necessarily two
 *     days)
 *
 * NOTE: "calendar" and "locale" properties are *not* added to the object.
 */
static PlainObject* GetCalendarInfo(JSContext* cx,
                                    Handle<JSLinearString*> loc) {
  auto locale = EncodeLocale(cx, loc);
  if (!locale) {
    return nullptr;
  }

  auto result = mozilla::intl::Calendar::TryCreate(locale.get());
  if (result.isErr()) {
    ReportInternalError(cx, result.unwrapErr());
    return nullptr;
  }
  auto calendar = result.unwrap();

  Rooted<IdValueVector> properties(cx, cx);

  if (!properties.emplaceBack(NameToId(cx->names().locale), StringValue(loc))) {
    return nullptr;
  }

  auto type = calendar->GetBcp47Type();
  if (type.isErr()) {
    ReportInternalError(cx, type.unwrapErr());
    return nullptr;
  }

  auto* calendarType = NewStringCopy<CanGC>(cx, type.unwrap());
  if (!calendarType) {
    return nullptr;
  }

  if (!properties.emplaceBack(NameToId(cx->names().calendar),
                              StringValue(calendarType))) {
    return nullptr;
  }

  if (!properties.emplaceBack(
          NameToId(cx->names().firstDayOfWeek),
          Int32Value(static_cast<int32_t>(calendar->GetFirstDayOfWeek())))) {
    return nullptr;
  }

  if (!properties.emplaceBack(
          NameToId(cx->names().minDays),
          Int32Value(calendar->GetMinimalDaysInFirstWeek()))) {
    return nullptr;
  }

  auto weekend = calendar->GetWeekend();
  if (weekend.isErr()) {
    ReportInternalError(cx, weekend.unwrapErr());
    return nullptr;
  }
  auto weekendSet = weekend.unwrap();

  auto* weekendArray = NewDenseFullyAllocatedArray(cx, weekendSet.size());
  if (!weekendArray) {
    return nullptr;
  }
  weekendArray->setDenseInitializedLength(weekendSet.size());

  size_t index = 0;
  for (auto day : weekendSet) {
    weekendArray->initDenseElement(index++,
                                   Int32Value(static_cast<int32_t>(day)));
  }
  MOZ_ASSERT(index == weekendSet.size());

  if (!properties.emplaceBack(NameToId(cx->names().weekend),
                              ObjectValue(*weekendArray))) {
    return nullptr;
  }

  return NewPlainObjectWithUniqueNames(cx, properties);
}

/**
 * This function is a custom function in the style of the standard Intl.*
 * functions, that isn't part of any spec or proposal yet.
 *
 * Returns an object with the following properties:
 *   locale:
 *     The actual resolved locale.
 *
 *   calendar:
 *     The default calendar of the resolved locale.
 *
 *   firstDayOfWeek:
 *     The first day of the week for the resolved locale.
 *
 *   minDays:
 *     The minimum number of days in a week for the resolved locale.
 *
 *   weekend:
 *     The days of the week considered as the weekend for the resolved locale.
 *
 * Days are encoded as integers in the range 1=Monday to 7=Sunday.
 */
static bool intl_getCalendarInfo(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // 1. Let requestedLocales be ? CanonicalizeLocaleList(locales).
  Rooted<ArrayObject*> requestedLocales(
      cx, CanonicalizeLocaleList(cx, args.get(0)));
  if (!requestedLocales) {
    return false;
  }

  // 2. Let localeOptions be a new Record.
  // 3. Set localeOptions.[[localeMatcher]] to "best fit".
  Rooted<LocaleOptions> localeOptions(cx);

  // 4. Let r be ResolveLocale(%DateTimeFormat%.[[availableLocales]],
  //    requestedLocales, localeOpt).
  auto localeData = LocaleData::Default;
  mozilla::EnumSet<UnicodeExtensionKey> relevantExtensionKeys{
      UnicodeExtensionKey::Calendar,
  };

  Rooted<ResolvedLocale> resolved(cx);
  if (!ResolveLocale(cx, AvailableLocaleKind::DateTimeFormat, requestedLocales,
                     localeOptions, relevantExtensionKeys, localeData,
                     &resolved)) {
    return false;
  }

  Rooted<JSLinearString*> locale(cx, resolved.toLocale(cx));
  if (!locale) {
    return false;
  }

  // 5. Let result be GetCalendarInfo(r.[[locale]]).
  auto* result = GetCalendarInfo(cx, locale);
  if (!result) {
    return false;
  }

  // 6. Return result.
  args.rval().setObject(*result);
  return true;
}

static const JSFunctionSpec intl_extensions[] = {
    JS_FN("getCalendarInfo", intl_getCalendarInfo, 1, 0),
    JS_FS_END,
};

bool JS::AddMozGetCalendarInfo(JSContext* cx, Handle<JSObject*> intl) {
  return JS_DefineFunctions(cx, intl, intl_extensions);
}

/******************** Intl ********************/

/**
 * Create an array from a sorted list of strings.
 */
template <size_t N>
static ArrayObject* CreateArrayFromSortedList(
    JSContext* cx, const std::array<const char*, N>& list) {
  // Ensure the list is sorted and doesn't contain duplicates.
  MOZ_ASSERT(std::adjacent_find(std::begin(list), std::end(list),
                                [](const auto& a, const auto& b) {
                                  return std::strcmp(a, b) >= 0;
                                }) == std::end(list));

  size_t length = std::size(list);

  Rooted<ArrayObject*> array(cx, NewDenseFullyAllocatedArray(cx, length));
  if (!array) {
    return nullptr;
  }
  array->ensureDenseInitializedLength(0, length);

  for (size_t i = 0; i < length; ++i) {
    auto* str = NewStringCopyZ<CanGC>(cx, list[i]);
    if (!str) {
      return nullptr;
    }
    array->initDenseElement(i, StringValue(str));
  }
  return array;
}

/**
 * Create an array from an intl::Enumeration.
 */
template <const auto& unsupported>
static bool EnumerationIntoList(JSContext* cx, auto values,
                                MutableHandle<StringList> list) {
  for (auto value : values) {
    if (value.isErr()) {
      ReportInternalError(cx);
      return false;
    }
    auto span = value.unwrap();

    // Skip over known, unsupported values.
    std::string_view sv(span.data(), span.size());
    if (std::any_of(std::begin(unsupported), std::end(unsupported),
                    [sv](const auto& e) { return sv == e; })) {
      continue;
    }

    auto* string = NewStringCopy<CanGC>(cx, span);
    if (!string) {
      return false;
    }
    if (!list.append(string)) {
      return false;
    }
  }

  return true;
}

/**
 * Create an array from an intl::ICU4XEnumeration.
 */
static bool ICU4XEnumerationIntoList(JSContext* cx, auto& values,
                                     MutableHandle<StringList> list) {
  for (mozilla::Span<const char> value : values) {
    auto* string = NewStringCopy<CanGC>(cx, value);
    if (!string) {
      return false;
    }
    if (!list.append(string)) {
      return false;
    }
  }

  return true;
}

/**
 * Returns the list of calendar types which mustn't be returned by
 * |Intl.supportedValuesOf()|.
 */
static constexpr auto UnsupportedCalendars() {
  return std::array{
      "islamic",
      "islamic-rgsa",
  };
}

/**
 * AvailableCalendars ( )
 */
static ArrayObject* AvailableCalendars(JSContext* cx) {
  Rooted<StringList> list(cx, StringList(cx));

  {
    // Hazard analysis complains that the mozilla::Result destructor calls a
    // GC function, which is unsound when returning an unrooted value. Work
    // around this issue by restricting the lifetime of |keywords| to a
    // separate block.
    auto keywords = mozilla::intl::Calendar::GetBcp47KeywordValuesForLocale("");
    if (keywords.isErr()) {
      ReportInternalError(cx, keywords.unwrapErr());
      return nullptr;
    }

    static constexpr auto unsupported = UnsupportedCalendars();

    if (!EnumerationIntoList<unsupported>(cx, keywords.unwrap(), &list)) {
      return nullptr;
    }
  }

  return CreateSortedArrayFromList(cx, &list);
}

/**
 * AvailableCollations ( )
 */
static ArrayObject* AvailableCollations(JSContext* cx) {
  Rooted<StringList> list(cx, StringList(cx));

  auto keywords = mozilla::intl::Collator::GetBcp47KeywordValues();

  if (!ICU4XEnumerationIntoList(cx, keywords, &list)) {
    return nullptr;
  }

  return CreateSortedArrayFromList(cx, &list);
}

/**
 * Returns a list of known, unsupported currencies which are returned by
 * |Currency::GetISOCurrencies()|.
 */
static constexpr auto UnsupportedCurrencies() {
  // "MVP" is also marked with "questionable, remove?" in ucurr.cpp, but only
  // this single currency code isn't supported by |Intl.DisplayNames| and
  // therefore must be excluded by |Intl.supportedValuesOf|.
  return std::array{
      "LSM",  // https://unicode-org.atlassian.net/browse/ICU-21687
  };
}

/**
 * AvailableCurrencies ( )
 */
static ArrayObject* AvailableCurrencies(JSContext* cx) {
  Rooted<StringList> list(cx, StringList(cx));

  {
    // Hazard analysis complains that the mozilla::Result destructor calls a
    // GC function, which is unsound when returning an unrooted value. Work
    // around this issue by restricting the lifetime of |currencies| to a
    // separate block.
    auto currencies = mozilla::intl::Currency::GetISOCurrencies();
    if (currencies.isErr()) {
      ReportInternalError(cx, currencies.unwrapErr());
      return nullptr;
    }

    static constexpr auto unsupported = UnsupportedCurrencies();

    if (!EnumerationIntoList<unsupported>(cx, currencies.unwrap(), &list)) {
      return nullptr;
    }
  }

  return CreateSortedArrayFromList(cx, &list);
}

/**
 * AvailableNumberingSystems ( )
 */
static ArrayObject* AvailableNumberingSystems(JSContext* cx) {
  static constexpr std::array numberingSystems = {
      NUMBERING_SYSTEMS_WITH_SIMPLE_DIGIT_MAPPINGS};

  return CreateArrayFromSortedList(cx, numberingSystems);
}

/**
 * AvailableTimeZones ( )
 */
static ArrayObject* AvailableTimeZones(JSContext* cx) {
  // Unsorted list of canonical time zone names, possibly containing duplicates.
  Rooted<StringList> timeZones(cx, StringList(cx));

  auto& sharedIntlData = cx->runtime()->sharedIntlData.ref();
  auto iterResult = sharedIntlData.availableTimeZonesIteration(cx);
  if (iterResult.isErr()) {
    return nullptr;
  }
  auto iter = iterResult.unwrap();

  Rooted<JSAtom*> validatedTimeZone(cx);
  for (; !iter.done(); iter.next()) {
    validatedTimeZone = iter.get();

    // Canonicalize the time zone before adding it to the result array.
    auto* timeZone = sharedIntlData.canonicalizeTimeZone(cx, validatedTimeZone);
    if (!timeZone) {
      return nullptr;
    }

    if (!timeZones.append(timeZone)) {
      return nullptr;
    }
  }

  return CreateSortedArrayFromList(cx, &timeZones);
}

template <size_t N>
constexpr auto MeasurementUnitNames(const SimpleMeasureUnit (&units)[N]) {
  std::array<const char*, N> array = {};
  for (size_t i = 0; i < N; ++i) {
    array[i] = units[i].name;
  }
  return array;
}

/**
 * AvailableUnits ( )
 */
static ArrayObject* AvailableUnits(JSContext* cx) {
  static constexpr auto simpleMeasureUnitNames =
      MeasurementUnitNames(simpleMeasureUnits);

  return CreateArrayFromSortedList(cx, simpleMeasureUnitNames);
}

/**
 * Intl.getCanonicalLocales ( locales )
 */
static bool intl_getCanonicalLocales(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Steps 1-2.
  auto* array = CanonicalizeLocaleList(cx, args.get(0));
  if (!array) {
    return false;
  }
  args.rval().setObject(*array);
  return true;
}

/**
 * Intl.supportedValuesOf ( key )
 */
static bool intl_supportedValuesOf(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Step 1.
  auto* key = ToString(cx, args.get(0));
  if (!key) {
    return false;
  }

  auto* linearKey = key->ensureLinear(cx);
  if (!linearKey) {
    return false;
  }

  // Steps 2-8.
  ArrayObject* list;
  if (StringEqualsLiteral(linearKey, "calendar")) {
    list = AvailableCalendars(cx);
  } else if (StringEqualsLiteral(linearKey, "collation")) {
    list = AvailableCollations(cx);
  } else if (StringEqualsLiteral(linearKey, "currency")) {
    list = AvailableCurrencies(cx);
  } else if (StringEqualsLiteral(linearKey, "numberingSystem")) {
    list = AvailableNumberingSystems(cx);
  } else if (StringEqualsLiteral(linearKey, "timeZone")) {
    list = AvailableTimeZones(cx);
  } else if (StringEqualsLiteral(linearKey, "unit")) {
    list = AvailableUnits(cx);
  } else {
    if (UniqueChars chars = QuoteString(cx, linearKey, '"')) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_INVALID_KEY,
                                chars.get());
    }
    return false;
  }
  if (!list) {
    return false;
  }

  // Step 9.
  args.rval().setObject(*list);
  return true;
}

static bool intl_toSource(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  args.rval().setString(cx->names().Intl);
  return true;
}

static const JSFunctionSpec intl_static_methods[] = {
    JS_FN("toSource", intl_toSource, 0, 0),
    JS_FN("getCanonicalLocales", intl_getCanonicalLocales, 1, 0),
    JS_FN("supportedValuesOf", intl_supportedValuesOf, 1, 0),
    JS_FS_END,
};

static const JSPropertySpec intl_static_properties[] = {
    JS_STRING_SYM_PS(toStringTag, "Intl", JSPROP_READONLY),
    JS_PS_END,
};

static JSObject* CreateIntlObject(JSContext* cx, JSProtoKey key) {
  Rooted<JSObject*> proto(cx, &cx->global()->getObjectPrototype());

  // The |Intl| object is just a plain object with some "static" function
  // properties and some constructor properties.
  return NewTenuredObjectWithGivenProto(cx, &IntlClass, proto);
}

/**
 * Initializes the Intl Object and its standard built-in properties.
 * Spec: ECMAScript Internationalization API Specification, 8.0, 8.1
 */
static bool IntlClassFinish(JSContext* cx, Handle<JSObject*> intl,
                            Handle<JSObject*> proto) {
  // Add the constructor properties.
  Rooted<JS::PropertyKey> ctorId(cx);
  Rooted<JS::Value> ctorValue(cx);
  for (const auto& protoKey : {
           JSProto_Collator,
           JSProto_DateTimeFormat,
           JSProto_DisplayNames,
           JSProto_DurationFormat,
           JSProto_ListFormat,
           JSProto_Locale,
           JSProto_NumberFormat,
           JSProto_PluralRules,
           JSProto_RelativeTimeFormat,
           JSProto_Segmenter,
       }) {
    if (GlobalObject::skipDeselectedConstructor(cx, protoKey)) {
      continue;
    }

    JSObject* ctor = GlobalObject::getOrCreateConstructor(cx, protoKey);
    if (!ctor) {
      return false;
    }

    ctorId = NameToId(ClassName(protoKey, cx));
    ctorValue.setObject(*ctor);
    if (!DefineDataProperty(cx, intl, ctorId, ctorValue, 0)) {
      return false;
    }
  }

  return true;
}

static const ClassSpec IntlClassSpec = {
    CreateIntlObject, nullptr, intl_static_methods, intl_static_properties,
    nullptr,          nullptr, IntlClassFinish,
};

const JSClass js::intl::IntlClass = {
    "Intl",
    JSCLASS_HAS_CACHED_PROTO(JSProto_Intl),
    JS_NULL_CLASS_OPS,
    &IntlClassSpec,
};
