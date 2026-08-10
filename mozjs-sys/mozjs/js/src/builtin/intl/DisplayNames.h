/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_intl_DisplayNames_h
#define builtin_intl_DisplayNames_h

#include <stddef.h>
#include <stdint.h>

#include "jstypes.h"
#include "NamespaceImports.h"

#include "js/Class.h"
#include "js/TypeDecls.h"
#include "js/Value.h"
#include "vm/NativeObject.h"
#include "vm/StringType.h"

namespace mozilla::intl {
class DisplayNames;
}

namespace js {
struct ClassSpec;
}

namespace js::intl {

struct DisplayNamesOptions;

class DisplayNamesObject : public NativeObject {
 public:
  static const JSClass class_;
  static const JSClass& protoClass_;

  static constexpr uint32_t LOCALE = 0;
  static constexpr uint32_t CALENDAR = 1;
  static constexpr uint32_t OPTIONS = 2;
  static constexpr uint32_t LOCALE_DISPLAY_NAMES_SLOT = 3;
  static constexpr uint32_t SLOT_COUNT = 4;

  // Estimated memory use for ULocaleDisplayNames (see IcuMemoryUsage).
  static constexpr size_t EstimatedMemoryUse = 1238;

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

  JSLinearString* getCalendar() const {
    const auto& slot = getFixedSlot(CALENDAR);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return &slot.toString()->asLinear();
  }

  void setCalendar(JSLinearString* calendar) {
    setFixedSlot(CALENDAR, StringValue(calendar));
  }

  DisplayNamesOptions getOptions() const;

  void setOptions(const DisplayNamesOptions& options);

  mozilla::intl::DisplayNames* getDisplayNames() const {
    const auto& slot = getFixedSlot(LOCALE_DISPLAY_NAMES_SLOT);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return static_cast<mozilla::intl::DisplayNames*>(slot.toPrivate());
  }

  void setDisplayNames(mozilla::intl::DisplayNames* displayNames) {
    setFixedSlot(LOCALE_DISPLAY_NAMES_SLOT, PrivateValue(displayNames));
  }

 private:
  static const JSClassOps classOps_;
  static const ClassSpec classSpec_;

  static void finalize(JS::GCContext* gcx, JSObject* obj);
};

}  // namespace js::intl

#endif /* builtin_intl_DisplayNames_h */
