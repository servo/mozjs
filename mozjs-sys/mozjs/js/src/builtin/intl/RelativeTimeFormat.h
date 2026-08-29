/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_intl_RelativeTimeFormat_h
#define builtin_intl_RelativeTimeFormat_h

#include "mozilla/intl/NumberPart.h"

#include <stddef.h>
#include <stdint.h>

#include "gc/Barrier.h"
#include "js/Class.h"
#include "vm/NativeObject.h"
#include "vm/StringType.h"

namespace mozilla::intl {
class RelativeTimeFormat;
}

namespace js::intl {

struct RelativeTimeFormatOptions;

class RelativeTimeFormatObject : public NativeObject {
 public:
  static const JSClass class_;
  static const JSClass& protoClass_;

  static constexpr uint32_t LOCALE = 0;
  static constexpr uint32_t NUMBERING_SYSTEM = 1;
  static constexpr uint32_t OPTIONS = 2;
  static constexpr uint32_t URELATIVE_TIME_FORMAT_SLOT = 3;
  static constexpr uint32_t SLOT_COUNT = 4;

  // Estimated memory use for URelativeDateTimeFormatter (see IcuMemoryUsage).
  static constexpr size_t EstimatedMemoryUse = 8188;

  bool isLocaleResolved() const { return getFixedSlot(LOCALE).isString(); }

  JSObject* getRequestedLocales() const {
    const auto& slot = getFixedSlot(LOCALE);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return &slot.toObject();
  }

  void setRequestedLocales(JSObject* requestedLocales) {
    setFixedSlot(LOCALE, ObjectValue(*requestedLocales));
  }

  JSLinearString* getLocale() const {
    const auto& slot = getFixedSlot(LOCALE);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return &slot.toString()->asLinear();
  }

  void setLocale(JSLinearString* locale) {
    setFixedSlot(LOCALE, StringValue(locale));
  }

  JSLinearString* getNumberingSystem() const {
    const auto& slot = getFixedSlot(NUMBERING_SYSTEM);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return &slot.toString()->asLinear();
  }

  void setNumberingSystem(JSLinearString* numberingSystem) {
    setFixedSlot(NUMBERING_SYSTEM, StringValue(numberingSystem));
  }

  RelativeTimeFormatOptions getOptions() const;

  void setOptions(const RelativeTimeFormatOptions& options);

  mozilla::intl::RelativeTimeFormat* getRelativeTimeFormatter() const {
    const auto& slot = getFixedSlot(URELATIVE_TIME_FORMAT_SLOT);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return static_cast<mozilla::intl::RelativeTimeFormat*>(slot.toPrivate());
  }

  void setRelativeTimeFormatter(mozilla::intl::RelativeTimeFormat* rtf) {
    setFixedSlot(URELATIVE_TIME_FORMAT_SLOT, PrivateValue(rtf));
  }

 private:
  static const JSClassOps classOps_;
  static const ClassSpec classSpec_;

  static void finalize(JS::GCContext* gcx, JSObject* obj);
};

enum class NumberFormatUnit;

[[nodiscard]] bool FormattedRelativeTimeToParts(
    JSContext* cx, Handle<JSString*> str,
    const mozilla::intl::NumberPartVector& parts,
    NumberFormatUnit numberFormatUnit, MutableHandle<JS::Value> result);

}  // namespace js::intl

#endif /* builtin_intl_RelativeTimeFormat_h */
