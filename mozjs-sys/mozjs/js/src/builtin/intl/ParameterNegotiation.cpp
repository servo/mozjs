/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "builtin/intl/ParameterNegotiation.h"

#include "mozilla/Assertions.h"
#include "mozilla/intl/Locale.h"
#include "mozilla/Span.h"
#include "mozilla/TextUtils.h"
#include "mozilla/UsingEnum.h"

#include <algorithm>
#include <stddef.h>

#include "builtin/intl/LocaleNegotiation.h"
#include "builtin/intl/StringAsciiChars.h"
#include "builtin/String.h"
#include "js/Conversions.h"
#include "js/ErrorReport.h"
#include "js/Printer.h"
#include "js/Value.h"
#include "vm/StringType.h"

#include "vm/JSObject-inl.h"
#include "vm/ObjectOperations-inl.h"

using namespace js;
using namespace js::intl;

static void ReportInvalidOptionValue(
    JSContext* cx, PropertyName* property, JSLinearString* value,
    JSErrNum errorNumber = JSMSG_INVALID_OPTION_VALUE) {
  if (auto propertyChars = EncodeAscii(cx, property)) {
    if (auto chars = QuoteString(cx, value, '"')) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, errorNumber,
                                propertyChars.get(), chars.get());
    }
  }
}

static void ReportInvalidOptionError(JSContext* cx, double number) {
  ToCStringBuf cbuf;
  const char* str = NumberToCString(&cbuf, number);
  MOZ_ASSERT(str);
  JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                            JSMSG_INVALID_DIGITS_VALUE, str);
}

/**
 * GetOption ( options, property, type, values, default )
 */
bool js::intl::detail::GetStringOption(
    JSContext* cx, Handle<JSObject*> options, Handle<PropertyName*> property,
    mozilla::Span<const std::string_view> values, JSErrNum errorNumber,
    size_t* result) {
  // Step 1.
  Rooted<JS::Value> value(cx);
  if (!GetProperty(cx, options, options, property, &value)) {
    return false;
  }

  // Step 2.
  if (value.isUndefined()) {
    *result = values.size();
    return true;
  }

  // Step 3. (Not applicable for String options.)

  // Step 4.
  auto* str = JS::ToString(cx, value);
  if (!str) {
    return false;
  }

  auto* linear = str->ensureLinear(cx);
  if (!linear) {
    return false;
  }

  // Steps 5-6.
  size_t index = 0;
  for (auto allowed : values) {
    if (StringEqualsAscii(linear, allowed.data(), allowed.length())) {
      *result = index;
      return true;
    }
    index++;
  }

  // Step 5.
  ReportInvalidOptionValue(cx, property, linear, errorNumber);
  return false;
}

/**
 * GetOption ( options, property, type, values, default )
 */
bool js::intl::GetStringOption(JSContext* cx, JS::Handle<JSObject*> options,
                               JS::Handle<PropertyName*> property,
                               JS::MutableHandle<JSString*> result) {
  // Step 1.
  Rooted<JS::Value> value(cx);
  if (!GetProperty(cx, options, options, property, &value)) {
    return false;
  }

  // Step 2.
  if (value.isUndefined()) {
    result.set(nullptr);
    return true;
  }

  // Step 3. (Not applicable for String options.)

  // Step 4.
  auto* str = JS::ToString(cx, value);
  if (!str) {
    return false;
  }

  // Step 5. (Not applicable)

  // Step 6.
  result.set(str);
  return true;
}

/**
 * GetOption ( options, property, type, values, default )
 */
bool js::intl::GetStringOption(JSContext* cx, JS::Handle<JSObject*> options,
                               JS::Handle<PropertyName*> property,
                               JS::MutableHandle<JSLinearString*> result) {
  Rooted<JSString*> string(cx);
  if (!GetStringOption(cx, options, property, &string)) {
    return false;
  }
  if (string) {
    auto* linear = string->ensureLinear(cx);
    if (!linear) {
      return false;
    }
    result.set(linear);
  } else {
    result.set(nullptr);
  }
  return true;
}

/**
 * GetOption ( options, property, type, values, default )
 */
bool js::intl::GetBooleanOption(JSContext* cx, Handle<JSObject*> options,
                                Handle<PropertyName*> property,
                                mozilla::Maybe<bool>* result) {
  // Step 1.
  Rooted<JS::Value> value(cx);
  if (!GetProperty(cx, options, options, property, &value)) {
    return false;
  }

  // Step 2.
  if (value.isUndefined()) {
    *result = mozilla::Nothing();
    return true;
  }

  // Step 4. (Not applicable for Boolean options.)

  // Steps 3 and 5.
  *result = mozilla::Some(JS::ToBoolean(value));
  return true;
}

/**
 * GetBooleanOrStringNumberFormatOption ( options, property, stringValues,
 * fallback )
 */
bool js::intl::detail::GetBooleanOrStringNumberFormatOption(
    JSContext* cx, Handle<JSObject*> options, Handle<PropertyName*> property,
    mozilla::Span<const std::string_view> stringValues,
    mozilla::Variant<bool, size_t>* result) {
  // Step 1.
  Rooted<JS::Value> value(cx);
  if (!GetProperty(cx, options, options, property, &value)) {
    return false;
  }

  // Step 2.
  if (value.isUndefined()) {
    *result = mozilla::AsVariant(stringValues.size());
    return true;
  }

  // Steps 3-5.
  if (value.isTrue()) {
    // Step 3.
    *result = mozilla::AsVariant(true);
    return true;
  }

  if (!JS::ToBoolean(value)) {
    // Step 4.
    *result = mozilla::AsVariant(false);
    return true;
  }

  // Step 5.
  auto* str = JS::ToString(cx, value);
  if (!str) {
    return false;
  }

  auto* linear = str->ensureLinear(cx);
  if (!linear) {
    return false;
  }

  // Steps 6-7.
  size_t index = 0;
  for (auto stringValue : stringValues) {
    if (StringEqualsAscii(linear, stringValue.data(), stringValue.length())) {
      *result = mozilla::AsVariant(index);
      return true;
    }
    index++;
  }

  // Step 6.
  ReportInvalidOptionValue(cx, property, linear, JSMSG_INVALID_OPTION_VALUE);
  return false;
}

/**
 * DefaultNumberOption ( value, minimum, maximum, fallback )
 */
bool js::intl::DefaultNumberOption(JSContext* cx, Handle<JS::Value> value,
                                   int32_t minimum, int32_t maximum,
                                   mozilla::Maybe<int32_t>* result) {
  // Step 1.
  if (value.isUndefined()) {
    *result = mozilla::Nothing();
    return true;
  }

  // Fast path for int32 values.
  if (value.isInt32()) {
    // Step 2.
    int32_t num = value.toInt32();

    // Step 3.
    if (num < minimum || num > maximum) {
      ReportInvalidOptionError(cx, num);
      return false;
    }

    // Step 4.
    *result = mozilla::Some(num);
    return true;
  }

  // Step 2.
  double num;
  if (!JS::ToNumber(cx, value, &num)) {
    return false;
  }

  // Step 3.
  if (!std::isfinite(num) || num < double(minimum) || num > double(maximum)) {
    ReportInvalidOptionError(cx, num);
    return false;
  }

  // Step 4.
  *result = mozilla::Some(static_cast<int32_t>(std::floor(num)));
  return true;
}

/**
 * GetNumberOption ( options, property, minimum, maximum, fallback )
 */
bool js::intl::GetNumberOption(JSContext* cx, Handle<JSObject*> options,
                               Handle<PropertyName*> property, int32_t minimum,
                               int32_t maximum,
                               mozilla::Maybe<int32_t>* result) {
  // Step 1.
  Rooted<JS::Value> value(cx);
  if (!GetProperty(cx, options, options, property, &value)) {
    return false;
  }

  // Step 2.
  return DefaultNumberOption(cx, value, minimum, maximum, result);
}

static constexpr std::string_view LocaleMatcherToString(
    LocaleMatcher localeMatcher) {
  MOZ_USING_ENUM(LocaleMatcher, BestFit, Lookup);
  switch (localeMatcher) {
    case BestFit:
      return "best fit";
    case Lookup:
      return "lookup";
  }
  MOZ_CRASH("invalid locale matcher");
}

bool js::intl::GetLocaleMatcherOption(JSContext* cx, Handle<JSObject*> options,
                                      JSErrNum errorNumber,
                                      LocaleMatcher* result) {
  static constexpr auto matchers = MapOptions<LocaleMatcherToString>(
      LocaleMatcher::BestFit, LocaleMatcher::Lookup);
  return GetStringOption(cx, options, cx->names().localeMatcher, matchers,
                         LocaleMatcher::BestFit, errorNumber, result);
}

static auto ToUnicodeKeySpan(UnicodeExtensionKey key) {
  MOZ_USING_ENUM(UnicodeExtensionKey, Calendar, Collation, CollationCaseFirst,
                 CollationNumeric, FirstDayOfWeek, HourCycle, NumberingSystem);
  switch (key) {
    case Calendar:
      return mozilla::MakeStringSpan("ca");
    case Collation:
      return mozilla::MakeStringSpan("co");
    case CollationCaseFirst:
      return mozilla::MakeStringSpan("kf");
    case CollationNumeric:
      return mozilla::MakeStringSpan("kn");
    case FirstDayOfWeek:
      return mozilla::MakeStringSpan("fw");
    case HourCycle:
      return mozilla::MakeStringSpan("hc");
    case NumberingSystem:
      return mozilla::MakeStringSpan("nu");
  }
  MOZ_CRASH("invalid Unicode extension key");
}

static Handle<PropertyName*> ToPropertyName(JSContext* cx,
                                            UnicodeExtensionKey key) {
  MOZ_USING_ENUM(UnicodeExtensionKey, Calendar, Collation, CollationCaseFirst,
                 CollationNumeric, FirstDayOfWeek, HourCycle, NumberingSystem);
  switch (key) {
    case Calendar:
      return cx->names().calendar;
    case Collation:
      return cx->names().collation;
    case CollationCaseFirst:
      return cx->names().caseFirst;
    case CollationNumeric:
      return cx->names().numeric;
    case FirstDayOfWeek:
      return cx->names().firstDayOfWeek;
    case HourCycle:
      return cx->names().hourCycle;
    case NumberingSystem:
      return cx->names().numberingSystem;
  }
  MOZ_CRASH("invalid Unicode extension key");
}

/**
 * Validate `unicodeType` can be matched by the "type" Unicode local nonterminal
 * and then canonicalize the Unicode extension type.
 */
static JSLinearString* ValidateAndCanonicalizeUnicodeExtensionType(
    JSContext* cx, UnicodeExtensionKey key,
    Handle<JSLinearString*> unicodeType) {
  // Empty strings or non-ASCII strings can never match the "type" Unicode
  // locale nonterminal.
  if (unicodeType->empty() || !StringIsAscii(unicodeType)) {
    ReportInvalidOptionValue(cx, ToPropertyName(cx, key), unicodeType);
    return nullptr;
  }

  bool isValid = false;
  const char* replacement = nullptr;
  UniqueChars unicodeTypeChars = nullptr;
  do {
    // NB: GC isn't allowed as long as StringAsciiChars is on the stack, so all
    // error reporting and string allocations have to be moved outside of the
    // current scope.
    StringAsciiChars chars(unicodeType);
    if (!chars.init(cx)) {
      return nullptr;
    }

    // Suppress hazard analysis because it doesn't properly support std::all_of,
    // std::any_of, std::transform.
    JS::AutoSuppressGCAnalysis nogc;

    // Validate the input matches the "type" Unicode local nonterminal.
    isValid =
        mozilla::intl::LocaleParser::CanParseUnicodeExtensionType(chars).isOk();
    if (!isValid) {
      break;
    }

    mozilla::Span<const char> type = chars;

    // Check if any characters in |type| aren't in canonical (= lower) case.
    bool hasUpperCase = std::any_of(type.begin(), type.end(), [](auto ch) {
      return mozilla::IsAsciiUppercaseAlpha(ch);
    });

    // Create a copy if there are any upper-case characters.
    if (hasUpperCase) {
      unicodeTypeChars = cx->make_pod_array<char>(type.size());
      if (!unicodeTypeChars) {
        return nullptr;
      }

      // Convert into canonical case before searching for replacements.
      mozilla::intl::AsciiToLowerCase(type.data(), type.size(),
                                      unicodeTypeChars.get());
      type = {unicodeTypeChars.get(), type.size()};
    }

    // Search if there's a replacement for the current Unicode keyword.
    auto ukey = ToUnicodeKeySpan(key);
    replacement =
        mozilla::intl::Locale::ReplaceUnicodeExtensionType(ukey, type);
  } while (false);

  if (!isValid) {
    ReportInvalidOptionValue(cx, ToPropertyName(cx, key), unicodeType);
    return nullptr;
  }
  if (replacement) {
    return NewStringCopyZ<CanGC>(cx, replacement);
  }
  if (unicodeTypeChars) {
    return NewStringCopyN<CanGC>(cx, unicodeTypeChars.get(),
                                 unicodeType->length());
  }
  return unicodeType;
}

/**
 * GetOption ( options, property, type, values, default )
 */
bool js::intl::GetUnicodeExtensionOption(
    JSContext* cx, JS::Handle<JSObject*> options, UnicodeExtensionKey key,
    JS::MutableHandle<JSLinearString*> result) {
  // Step 1.
  Rooted<JS::Value> value(cx);
  if (!GetProperty(cx, options, options, ToPropertyName(cx, key), &value)) {
    return false;
  }

  // Step 2.
  if (value.isUndefined()) {
    result.set(nullptr);
    return true;
  }

  // Step 3. (Not applicable for String options.)

  // Step 4.
  auto* str = JS::ToString(cx, value);
  if (!str) {
    return false;
  }

  Rooted<JSLinearString*> linear(cx, str->ensureLinear(cx));
  if (!linear) {
    return false;
  }

  // Step 5. (Not applicable)

  // Step 6. (With Unicode extension type validation.)
  auto* unicodeType =
      ValidateAndCanonicalizeUnicodeExtensionType(cx, key, linear);
  if (!unicodeType) {
    return false;
  }

  result.set(unicodeType);
  return true;
}

/**
 * GetOption ( options, property, type, values, default )
 */
JSLinearString* js::intl::GetUnicodeExtensionOption(
    JSContext* cx, UnicodeExtensionKey key,
    JS::Handle<JSLinearString*> option) {
  // Steps 1-5. (Not applicable)

  // Step 6.
  return ValidateAndCanonicalizeUnicodeExtensionType(cx, key, option);
}
