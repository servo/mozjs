/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_intl_ParameterNegotiation_h
#define builtin_intl_ParameterNegotiation_h

#include "mozilla/Assertions.h"
#include "mozilla/Maybe.h"
#include "mozilla/Span.h"
#include "mozilla/Variant.h"

#include <array>
#include <stddef.h>
#include <stdint.h>
#include <string_view>
#include <utility>

#include "js/friend/ErrorMessages.h"
#include "js/RootingAPI.h"
#include "js/TypeDecls.h"
#include "vm/StringType.h"

namespace js::intl {

/**
 * Pair representing options and their corresponding names.
 */
template <typename Option, size_t N>
using OptionValues =
    std::pair<std::array<Option, N>, std::array<std::string_view, N>>;

/**
 * Apply the function `F` on each element of `args` and then return the inputs
 * and results as a pair of arrays.
 */
template <auto F>
constexpr auto MapOptions(auto... args) {
  return std::pair{std::array{(args)...}, std::array{F(args)...}};
}

namespace detail {
/**
 * GetOption ( options, property, type, values, default )
 *
 * Read the property `property' from `options`, convert it to a string, and then
 * compare this string against `values`. If the string was found in `values`,
 * return its index. Otherwise return an index larger than the size of `values`.
 */
bool GetStringOption(JSContext* cx, JS::Handle<JSObject*> options,
                     JS::Handle<PropertyName*> property,
                     mozilla::Span<const std::string_view> values,
                     JSErrNum errorNumber, size_t* result);
}  // namespace detail

/**
 * GetOption ( options, property, type, values, default )
 *
 * Read the property `property' from `options`, convert it to a string, and then
 * compare it against the option values in `values`. If no matching option was
 * found, return `defaultValue`.
 */
template <typename Option, size_t N>
inline bool GetStringOption(JSContext* cx, JS::Handle<JSObject*> options,
                            JS::Handle<PropertyName*> property,
                            const OptionValues<Option, N>& values,
                            Option defaultValue, JSErrNum errorNumber,
                            Option* result) {
  size_t index;
  if (!detail::GetStringOption(cx, options, property, values.second,
                               errorNumber, &index)) {
    return false;
  }
  if (index < N) {
    *result = values.first[index];
  } else {
    *result = defaultValue;
  }
  return true;
}

/**
 * GetOption ( options, property, type, values, default )
 *
 * Read the property `property' from `options`, convert it to a string, and then
 * compare it against the option values in `values`. If no matching option was
 * found, return `defaultValue`.
 */
template <typename Option, size_t N>
inline bool GetStringOption(JSContext* cx, JS::Handle<JSObject*> options,
                            JS::Handle<PropertyName*> property,
                            const OptionValues<Option, N>& values,
                            Option defaultValue, Option* result) {
  return GetStringOption(cx, options, property, values, defaultValue,
                         JSMSG_INVALID_OPTION_VALUE, result);
}

/**
 * GetOption ( options, property, type, values, default )
 *
 * Read the property `property' from `options`, convert it to a string, and then
 * compare it against the option values in `values`. If no matching option was
 * found, return `mozilla::Nothing()`.
 */
template <typename Option, size_t N>
inline bool GetStringOption(JSContext* cx, JS::Handle<JSObject*> options,
                            JS::Handle<PropertyName*> property,
                            const OptionValues<Option, N>& values,
                            mozilla::Maybe<Option>* result) {
  size_t index;
  if (!detail::GetStringOption(cx, options, property, values.second,
                               JSMSG_INVALID_OPTION_VALUE, &index)) {
    return false;
  }
  if (index < N) {
    *result = mozilla::Some(values.first[index]);
  } else {
    *result = mozilla::Nothing();
  }
  return true;
}

/**
 * GetOption ( options, property, type, values, default )
 *
 * Read the property `property' from `options` and convert it to a string. If
 * the option is not present, return `nullptr`.
 */
bool GetStringOption(JSContext* cx, JS::Handle<JSObject*> options,
                     JS::Handle<PropertyName*> property,
                     JS::MutableHandle<JSString*> result);

/**
 * GetOption ( options, property, type, values, default )
 *
 * Read the property `property' from `options` and convert it to a string. If
 * the option is not present, return `nullptr`.
 */
bool GetStringOption(JSContext* cx, JS::Handle<JSObject*> options,
                     JS::Handle<PropertyName*> property,
                     JS::MutableHandle<JSLinearString*> result);

/**
 * GetOption ( options, property, type, values, default )
 *
 * Read the property `property' from `options` and convert it to a boolean. If
 * the option is not present, return `mozilla::Nothing`.
 */
bool GetBooleanOption(JSContext* cx, JS::Handle<JSObject*> options,
                      JS::Handle<PropertyName*> property,
                      mozilla::Maybe<bool>* result);

namespace detail {
/**
 * GetBooleanOrStringNumberFormatOption ( options, property, stringValues,
 * fallback )
 *
 * Read the property `property' from `options`, convert it to a boolean or a
 * string, and then compare it against the option values in `stringValues`.
 */
bool GetBooleanOrStringNumberFormatOption(
    JSContext* cx, JS::Handle<JSObject*> options,
    JS::Handle<PropertyName*> property,
    mozilla::Span<const std::string_view> stringValues,
    mozilla::Variant<bool, size_t>* result);
}  // namespace detail

/**
 * GetBooleanOrStringNumberFormatOption ( options, property, stringValues,
 * fallback )
 *
 * Read the property `property' from `options`, convert it to a boolean or a
 * string, and then compare it against the option values in `stringValues`. If
 * no matching option was found, return `fallback`.
 */
template <typename Option, size_t N>
inline bool GetBooleanOrStringNumberFormatOption(
    JSContext* cx, JS::Handle<JSObject*> options,
    JS::Handle<PropertyName*> property,
    const OptionValues<Option, N>& stringValues, Option fallback,
    mozilla::Variant<bool, Option>* result) {
  mozilla::Variant<bool, size_t> boolOrIndex{false};
  if (!detail::GetBooleanOrStringNumberFormatOption(
          cx, options, property, stringValues.second, &boolOrIndex)) {
    return false;
  }
  if (boolOrIndex.is<bool>()) {
    *result = mozilla::AsVariant(boolOrIndex.extract<bool>());
  } else {
    size_t index = boolOrIndex.extract<size_t>();
    if (index < N) {
      *result = mozilla::AsVariant(stringValues.first[index]);
    } else {
      *result = mozilla::AsVariant(fallback);
    }
  }
  return true;
}

/**
 * DefaultNumberOption ( value, minimum, maximum, fallback )
 *
 * If |value| in not undefined, convert it to a number and then validate against
 * the given range. Otherwise return `mozilla::Nothing`.
 */
bool DefaultNumberOption(JSContext* cx, JS::Handle<JS::Value> value,
                         int32_t minimum, int32_t maximum,
                         mozilla::Maybe<int32_t>* result);

/**
 * DefaultNumberOption ( value, minimum, maximum, fallback )
 *
 * If |value| in not undefined, convert it to a number and then validate against
 * the given range. Otherwise return `fallback`.
 */
inline bool DefaultNumberOption(JSContext* cx, JS::Handle<JS::Value> value,
                                int32_t minimum, int32_t maximum,
                                int32_t fallback, int32_t* result) {
  MOZ_ASSERT(minimum <= fallback && fallback <= maximum);

  mozilla::Maybe<int32_t> r;
  if (!DefaultNumberOption(cx, value, minimum, maximum, &r)) {
    return false;
  }

  *result = r.valueOr(fallback);
  return true;
}

/**
 * GetNumberOption ( options, property, minimum, maximum, fallback )
 *
 * Read the property `property' from `options`, convert it to a number and then
 * validate against the given range. Otherwise return `mozilla::Nothing`.
 */
bool GetNumberOption(JSContext* cx, JS::Handle<JSObject*> options,
                     JS::Handle<PropertyName*> property, int32_t minimum,
                     int32_t maximum, mozilla::Maybe<int32_t>* result);

/**
 * GetNumberOption ( options, property, minimum, maximum, fallback )
 *
 * Read the property `property' from `options`, convert it to a number and then
 * validate against the given range. Otherwise return `fallback`.
 */
inline bool GetNumberOption(JSContext* cx, JS::Handle<JSObject*> options,
                            JS::Handle<PropertyName*> property, int32_t minimum,
                            int32_t maximum, int32_t fallback,
                            int32_t* result) {
  MOZ_ASSERT(minimum <= fallback && fallback <= maximum);

  mozilla::Maybe<int32_t> r;
  if (!GetNumberOption(cx, options, property, minimum, maximum, &r)) {
    return false;
  }

  *result = r.valueOr(fallback);
  return true;
}

enum class LocaleMatcher { BestFit, Lookup };

/**
 * Get the "localeMatcher" option from `options`.
 */
bool GetLocaleMatcherOption(JSContext* cx, JS::Handle<JSObject*> options,
                            JSErrNum errorNumber, LocaleMatcher* result);

/**
 * Get the "localeMatcher" option from `options`.
 */
inline bool GetLocaleMatcherOption(JSContext* cx, JS::Handle<JSObject*> options,
                                   LocaleMatcher* result) {
  return GetLocaleMatcherOption(cx, options, JSMSG_INVALID_OPTION_VALUE,
                                result);
}

enum class UnicodeExtensionKey : uint8_t;

/**
 * Get a Unicode extension key option from `options`. If the option is present,
 * validate and canonicalize the option value.
 */
bool GetUnicodeExtensionOption(JSContext* cx, JS::Handle<JSObject*> options,
                               UnicodeExtensionKey key,
                               JS::MutableHandle<JSLinearString*> result);

/**
 * Validate and canonicalize the option value.
 */
JSLinearString* GetUnicodeExtensionOption(JSContext* cx,
                                          UnicodeExtensionKey key,
                                          JS::Handle<JSLinearString*> option);

}  // namespace js::intl

#endif /* builtin_intl_ParameterNegotiation_h */
