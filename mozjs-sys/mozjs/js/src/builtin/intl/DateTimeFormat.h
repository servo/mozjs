/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_intl_DateTimeFormat_h
#define builtin_intl_DateTimeFormat_h

#include <stddef.h>
#include <stdint.h>

#include "builtin/temporal/Calendar.h"
#include "js/Class.h"
#include "vm/NativeObject.h"
#include "vm/StringType.h"

namespace mozilla::intl {
class DateTimeFormat;
class DateIntervalFormat;
}  // namespace mozilla::intl

namespace js::intl {

struct DateTimeFormatOptions;

enum class DateTimeValueKind {
  Number,
  TemporalDate,
  TemporalTime,
  TemporalDateTime,
  TemporalYearMonth,
  TemporalMonthDay,
  TemporalZonedDateTime,
  TemporalInstant,
};

class DateTimeFormatObject : public NativeObject {
 public:
  static const JSClass class_;
  static const JSClass& protoClass_;

  static constexpr uint32_t LOCALE_SLOT = 0;
  static constexpr uint32_t NUMBERING_SYSTEM_SLOT = 1;
  static constexpr uint32_t CALENDAR_SLOT = 2;
  static constexpr uint32_t TIMEZONE_SLOT = 3;
  static constexpr uint32_t OPTIONS_SLOT = 4;
  static constexpr uint32_t PATTERN_SLOT = 5;
  static constexpr uint32_t CALENDAR_VALUE_SLOT = 6;
  static constexpr uint32_t DATE_FORMAT_SLOT = 7;
  static constexpr uint32_t DATE_INTERVAL_FORMAT_SLOT = 8;
  static constexpr uint32_t DATE_TIME_VALUE_KIND_SLOT = 9;
  static constexpr uint32_t BOUND_FORMAT_SLOT = 10;
  static constexpr uint32_t SLOT_COUNT = 11;

  // Estimated memory use for UDateFormat (see IcuMemoryUsage).
  static constexpr size_t UDateFormatEstimatedMemoryUse = 72440;

  // Estimated memory use for UDateIntervalFormat (see IcuMemoryUsage).
  static constexpr size_t UDateIntervalFormatEstimatedMemoryUse = 175646;

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

  JSLinearString* getCalendar() const {
    const auto& slot = getFixedSlot(CALENDAR_SLOT);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return &slot.toString()->asLinear();
  }

  void setCalendar(JSLinearString* calendar) {
    setFixedSlot(CALENDAR_SLOT, JS::StringValue(calendar));
  }

  JSLinearString* getTimeZone() const {
    const auto& slot = getFixedSlot(TIMEZONE_SLOT);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return &slot.toString()->asLinear();
  }

  void setTimeZone(JSLinearString* timeZone) {
    setFixedSlot(TIMEZONE_SLOT, JS::StringValue(timeZone));
  }

  DateTimeFormatOptions getOptions() const;

  void setOptions(const DateTimeFormatOptions& options);

  JSString* getPattern() const {
    const auto& slot = getFixedSlot(PATTERN_SLOT);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return slot.toString();
  }

  void setPattern(JSString* pattern) {
    setFixedSlot(PATTERN_SLOT, JS::StringValue(pattern));
  }

  temporal::CalendarValue getCalendarValue() const {
    const auto& slot = getFixedSlot(CALENDAR_VALUE_SLOT);
    if (slot.isUndefined()) {
      return temporal::CalendarValue();
    }
    return temporal::CalendarValue(slot);
  }

  void setCalendarValue(const temporal::CalendarValue& calendar) {
    setFixedSlot(CALENDAR_VALUE_SLOT, calendar.toSlotValue());
  }

  mozilla::intl::DateTimeFormat* getDateFormat() const {
    const auto& slot = getFixedSlot(DATE_FORMAT_SLOT);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return static_cast<mozilla::intl::DateTimeFormat*>(slot.toPrivate());
  }

  void setDateFormat(mozilla::intl::DateTimeFormat* dateFormat) {
    setFixedSlot(DATE_FORMAT_SLOT, JS::PrivateValue(dateFormat));
  }

  mozilla::intl::DateIntervalFormat* getDateIntervalFormat() const {
    const auto& slot = getFixedSlot(DATE_INTERVAL_FORMAT_SLOT);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return static_cast<mozilla::intl::DateIntervalFormat*>(slot.toPrivate());
  }

  void setDateIntervalFormat(
      mozilla::intl::DateIntervalFormat* dateIntervalFormat) {
    setFixedSlot(DATE_INTERVAL_FORMAT_SLOT,
                 JS::PrivateValue(dateIntervalFormat));
  }

  DateTimeValueKind getDateTimeValueKind() const {
    const auto& slot = getFixedSlot(DATE_TIME_VALUE_KIND_SLOT);
    if (slot.isUndefined()) {
      return DateTimeValueKind::Number;
    }
    return static_cast<DateTimeValueKind>(slot.toInt32());
  }

  void setDateTimeValueKind(DateTimeValueKind kind) {
    setFixedSlot(DATE_TIME_VALUE_KIND_SLOT,
                 JS::Int32Value(static_cast<int32_t>(kind)));
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

  void maybeClearCache(DateTimeValueKind kind);

 private:
  static const JSClassOps classOps_;
  static const ClassSpec classSpec_;

  static void finalize(JS::GCContext* gcx, JSObject* obj);
};

enum class DateTimeFormatKind {
  /**
   * Call CreateDateTimeFormat with `required = Any` and `defaults = All`.
   */
  All,

  /**
   * Call CreateDateTimeFormat with `required = Date` and `defaults = Date`.
   */
  Date,

  /**
   * Call CreateDateTimeFormat with `required = Time` and `defaults = Time`.
   */
  Time,
};

/**
 * Returns a new instance of the standard built-in DateTimeFormat constructor.
 */
[[nodiscard]] extern DateTimeFormatObject* CreateDateTimeFormat(
    JSContext* cx, JS::Handle<JS::Value> locales, JS::Handle<JS::Value> options,
    DateTimeFormatKind kind);

/**
 * Returns a possibly cached instance of the standard built-in DateTimeFormat
 * constructor.
 */
[[nodiscard]] extern DateTimeFormatObject* GetOrCreateDateTimeFormat(
    JSContext* cx, JS::Handle<JS::Value> locales, JS::Handle<JS::Value> options,
    DateTimeFormatKind kind);

/**
 * Returns a String value representing |millis| (which must be a valid time
 * value) according to the effective locale and the formatting options of the
 * given DateTimeFormat.
 */
[[nodiscard]] extern bool FormatDateTime(
    JSContext* cx, JS::Handle<DateTimeFormatObject*> dateTimeFormat,
    double millis, JS::MutableHandle<JS::Value> result);

/**
 * Shared `toLocaleString` implementation for Temporal objects.
 */
[[nodiscard]] extern bool TemporalObjectToLocaleString(
    JSContext* cx, const JS::CallArgs& args, DateTimeFormatKind formatKind,
    JS::Handle<JSLinearString*> toLocaleStringTimeZone = nullptr);

}  // namespace js::intl

#endif /* builtin_intl_DateTimeFormat_h */
