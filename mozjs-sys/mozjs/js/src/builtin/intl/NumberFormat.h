/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_intl_NumberFormat_h
#define builtin_intl_NumberFormat_h

#include <stddef.h>
#include <stdint.h>
#include <string_view>

#include "js/Class.h"
#include "vm/NativeObject.h"
#include "vm/StringType.h"

namespace mozilla::intl {
class NumberFormat;
class NumberRangeFormat;
}  // namespace mozilla::intl

namespace js {
class ArrayObject;
}

namespace js::intl {

struct NumberFormatOptions;

class NumberFormatObject : public NativeObject {
 public:
  static const JSClass class_;
  static const JSClass& protoClass_;

  static constexpr uint32_t LOCALE_SLOT = 0;
  static constexpr uint32_t NUMBERING_SYSTEM_SLOT = 1;
  static constexpr uint32_t OPTIONS_SLOT = 2;
  static constexpr uint32_t DIGITS_OPTIONS_SLOT = 3;
  static constexpr uint32_t UNUMBER_FORMATTER_SLOT = 4;
  static constexpr uint32_t UNUMBER_RANGE_FORMATTER_SLOT = 5;
  static constexpr uint32_t BOUND_FORMAT_SLOT = 6;
  static constexpr uint32_t SLOT_COUNT = 7;

  // Estimated memory use for UNumberFormatter and UFormattedNumber
  // (see IcuMemoryUsage).
  static constexpr size_t EstimatedMemoryUse = 972;

  // Estimated memory use for UNumberRangeFormatter and UFormattedNumberRange
  // (see IcuMemoryUsage).
  static constexpr size_t EstimatedRangeFormatterMemoryUse = 19894;

  bool isLocaleResolved() const { return getFixedSlot(LOCALE_SLOT).isString(); }

  JSObject* getRequestedLocales() const {
    const auto& slot = getFixedSlot(LOCALE_SLOT);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return &slot.toObject();
  }

  void setRequestedLocales(JSObject* requestedLocales) {
    setFixedSlot(LOCALE_SLOT, JS::ObjectValue(*requestedLocales));
  }

  JSLinearString* getLocale() const {
    const auto& slot = getFixedSlot(LOCALE_SLOT);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return &slot.toString()->asLinear();
  }

  void setLocale(JSLinearString* locale) {
    setFixedSlot(LOCALE_SLOT, JS::StringValue(locale));
  }

  JSLinearString* getNumberingSystem() const {
    const auto& slot = getFixedSlot(NUMBERING_SYSTEM_SLOT);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return &slot.toString()->asLinear();
  }

  void setNumberingSystem(JSLinearString* numberingSystem) {
    setFixedSlot(NUMBERING_SYSTEM_SLOT, JS::StringValue(numberingSystem));
  }

  NumberFormatOptions getOptions() const;

  void setOptions(const NumberFormatOptions& options);

  mozilla::intl::NumberFormat* getNumberFormatter() const {
    const auto& slot = getFixedSlot(UNUMBER_FORMATTER_SLOT);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return static_cast<mozilla::intl::NumberFormat*>(slot.toPrivate());
  }

  void setNumberFormatter(mozilla::intl::NumberFormat* formatter) {
    setFixedSlot(UNUMBER_FORMATTER_SLOT, PrivateValue(formatter));
  }

  mozilla::intl::NumberRangeFormat* getNumberRangeFormatter() const {
    const auto& slot = getFixedSlot(UNUMBER_RANGE_FORMATTER_SLOT);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return static_cast<mozilla::intl::NumberRangeFormat*>(slot.toPrivate());
  }

  void setNumberRangeFormatter(mozilla::intl::NumberRangeFormat* formatter) {
    setFixedSlot(UNUMBER_RANGE_FORMATTER_SLOT, PrivateValue(formatter));
  }

  JSObject* getBoundFormat() const {
    const auto& slot = getFixedSlot(BOUND_FORMAT_SLOT);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return &slot.toObject();
  }

  void setBoundFormat(JSObject* boundFormat) {
    setFixedSlot(BOUND_FORMAT_SLOT, JS::ObjectValue(*boundFormat));
  }

 private:
  static const JSClassOps classOps_;
  static const ClassSpec classSpec_;

  static void finalize(JS::GCContext* gcx, JSObject* obj);
};

enum class NumberFormatUnit {
  Year,
  Quarter,
  Month,
  Week,
  Day,
  Hour,
  Minute,
  Second,
  Millisecond,
  Microsecond,
  Nanosecond,
};

/**
 * Returns a new instance of the standard built-in NumberFormat constructor.
 */
[[nodiscard]] extern NumberFormatObject* CreateNumberFormat(
    JSContext* cx, JS::Handle<JS::Value> locales,
    JS::Handle<JS::Value> options);

/**
 * Returns a possibly cached instance of the standard built-in NumberFormat
 * constructor.
 */
[[nodiscard]] extern NumberFormatObject* GetOrCreateNumberFormat(
    JSContext* cx, JS::Handle<JS::Value> locales,
    JS::Handle<JS::Value> options);

/**
 * Returns a string representing the number x according to the effective locale
 * and the formatting options of the given NumberFormat.
 */
[[nodiscard]] extern JSString* FormatNumber(
    JSContext* cx, Handle<NumberFormatObject*> numberFormat, double x);

/**
 * Returns a string representing the BigInt x according to the effective locale
 * and the formatting options of the given NumberFormat.
 */
[[nodiscard]] extern JSString* FormatBigInt(
    JSContext* cx, Handle<NumberFormatObject*> numberFormat, Handle<BigInt*> x);

[[nodiscard]] extern JSLinearString* FormatNumber(
    JSContext* cx, mozilla::intl::NumberFormat* numberFormat, double x);

[[nodiscard]] extern JSLinearString* FormatNumber(
    JSContext* cx, mozilla::intl::NumberFormat* numberFormat,
    std::string_view x);

[[nodiscard]] extern ArrayObject* FormatNumberToParts(
    JSContext* cx, mozilla::intl::NumberFormat* numberFormat, double x,
    NumberFormatUnit numberFormatUnit);

[[nodiscard]] extern ArrayObject* FormatNumberToParts(
    JSContext* cx, mozilla::intl::NumberFormat* numberFormat,
    std::string_view x, NumberFormatUnit numberFormatUnit);

}  // namespace js::intl

#endif /* builtin_intl_NumberFormat_h */
