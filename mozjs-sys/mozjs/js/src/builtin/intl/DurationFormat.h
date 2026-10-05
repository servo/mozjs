/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_intl_DurationFormat_h
#define builtin_intl_DurationFormat_h

#include "mozilla/Assertions.h"

#include <stdint.h>

#include "builtin/temporal/TemporalUnit.h"
#include "js/Class.h"
#include "js/Value.h"
#include "vm/NativeObject.h"
#include "vm/StringType.h"

namespace mozilla::intl {
class ListFormat;
class NumberFormat;
}  // namespace mozilla::intl

namespace js::intl {

struct DurationFormatOptions;

class DurationFormatObject : public NativeObject {
 public:
  static const JSClass class_;
  static const JSClass& protoClass_;

  static constexpr uint32_t LOCALE_SLOT = 0;
  static constexpr uint32_t NUMBERING_SYSTEM = 1;
  static constexpr uint32_t NUMBER_FORMAT_YEARS_SLOT = 2;
  static constexpr uint32_t NUMBER_FORMAT_MONTHS_SLOT = 3;
  static constexpr uint32_t NUMBER_FORMAT_WEEKS_SLOT = 4;
  static constexpr uint32_t NUMBER_FORMAT_DAYS_SLOT = 5;
  static constexpr uint32_t NUMBER_FORMAT_HOURS_SLOT = 6;
  static constexpr uint32_t NUMBER_FORMAT_MINUTES_SLOT = 7;
  static constexpr uint32_t NUMBER_FORMAT_SECONDS_SLOT = 8;
  static constexpr uint32_t NUMBER_FORMAT_MILLISECONDS_SLOT = 9;
  static constexpr uint32_t NUMBER_FORMAT_MICROSECONDS_SLOT = 10;
  static constexpr uint32_t NUMBER_FORMAT_NANOSECONDS_SLOT = 11;
  static constexpr uint32_t LIST_FORMAT_SLOT = 12;
  static constexpr uint32_t OPTIONS_SLOT = 13;
  static constexpr uint32_t TIME_SEPARATOR_SLOT = 14;
  static constexpr uint32_t SLOT_COUNT = 15;

 private:
  static constexpr uint32_t numberFormatSlot(temporal::TemporalUnit unit) {
    MOZ_ASSERT(temporal::TemporalUnit::Year <= unit &&
               unit <= temporal::TemporalUnit::Nanosecond);

    static_assert(uint32_t(temporal::TemporalUnit::Year) ==
                  NUMBER_FORMAT_YEARS_SLOT);
    static_assert(uint32_t(temporal::TemporalUnit::Nanosecond) ==
                  NUMBER_FORMAT_NANOSECONDS_SLOT);

    return uint32_t(unit);
  }

 public:
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
    const auto& slot = getFixedSlot(NUMBERING_SYSTEM);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return &slot.toString()->asLinear();
  }

  void setNumberingSystem(JSLinearString* numberingSystem) {
    setFixedSlot(NUMBERING_SYSTEM, JS::StringValue(numberingSystem));
  }

  DurationFormatOptions getOptions() const;

  void setOptions(const DurationFormatOptions& options);

  mozilla::intl::NumberFormat* getNumberFormat(
      temporal::TemporalUnit unit) const {
    const auto& slot = getFixedSlot(numberFormatSlot(unit));
    if (slot.isUndefined()) {
      return nullptr;
    }
    return static_cast<mozilla::intl::NumberFormat*>(slot.toPrivate());
  }

  void setNumberFormat(temporal::TemporalUnit unit,
                       mozilla::intl::NumberFormat* numberFormat) {
    setFixedSlot(numberFormatSlot(unit), JS::PrivateValue(numberFormat));
  }

  mozilla::intl::ListFormat* getListFormat() const {
    const auto& slot = getFixedSlot(LIST_FORMAT_SLOT);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return static_cast<mozilla::intl::ListFormat*>(slot.toPrivate());
  }

  void setListFormat(mozilla::intl::ListFormat* listFormat) {
    setFixedSlot(LIST_FORMAT_SLOT, JS::PrivateValue(listFormat));
  }

  JSString* getTimeSeparator() const {
    const auto& slot = getFixedSlot(TIME_SEPARATOR_SLOT);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return slot.toString();
  }

  void setTimeSeparator(JSString* timeSeparator) {
    setFixedSlot(TIME_SEPARATOR_SLOT, JS::StringValue(timeSeparator));
  }

 private:
  static const JSClassOps classOps_;
  static const ClassSpec classSpec_;

  static void finalize(JS::GCContext* gcx, JSObject* obj);
};

/**
 * `toLocaleString` implementation for Temporal.Duration objects.
 */
[[nodiscard]] extern bool TemporalDurationToLocaleString(
    JSContext* cx, const JS::CallArgs& args);

}  // namespace js::intl

#endif /* builtin_intl_DurationFormat_h */
