/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_intl_ListFormat_h
#define builtin_intl_ListFormat_h

#include <stddef.h>
#include <stdint.h>

#include "js/Class.h"
#include "js/TypeDecls.h"
#include "js/Value.h"
#include "vm/NativeObject.h"
#include "vm/StringType.h"

namespace mozilla::intl {
class ListFormat;
}  // namespace mozilla::intl

namespace js::intl {

struct ListFormatOptions;

class ListFormatObject : public NativeObject {
 public:
  static const JSClass class_;
  static const JSClass& protoClass_;

  static constexpr uint32_t LOCALE = 0;
  static constexpr uint32_t OPTIONS = 1;
  static constexpr uint32_t LIST_FORMAT_SLOT = 2;
  static constexpr uint32_t SLOT_COUNT = 3;

  // Estimated memory use for UListFormatter (see IcuMemoryUsage).
  static constexpr size_t EstimatedMemoryUse = 24;

  bool isLocaleResolved() const { return getFixedSlot(LOCALE).isString(); }

  JSObject* getRequestedLocales() const {
    const auto& slot = getFixedSlot(LOCALE);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return &slot.toObject();
  }

  void setRequestedLocales(JSObject* requestedLocales) {
    setFixedSlot(LOCALE, JS::ObjectValue(*requestedLocales));
  }

  JSLinearString* getLocale() const {
    const auto& slot = getFixedSlot(LOCALE);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return &slot.toString()->asLinear();
  }

  void setLocale(JSLinearString* locale) {
    setFixedSlot(LOCALE, JS::StringValue(locale));
  }

  ListFormatOptions getOptions() const;

  void setOptions(const ListFormatOptions& options);

  mozilla::intl::ListFormat* getListFormatSlot() const {
    const auto& slot = getFixedSlot(LIST_FORMAT_SLOT);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return static_cast<mozilla::intl::ListFormat*>(slot.toPrivate());
  }

  void setListFormatSlot(mozilla::intl::ListFormat* format) {
    setFixedSlot(LIST_FORMAT_SLOT, JS::PrivateValue(format));
  }

 private:
  static const JSClassOps classOps_;
  static const ClassSpec classSpec_;

  static void finalize(JS::GCContext* gcx, JSObject* obj);
};

}  // namespace js::intl

#endif /* builtin_intl_ListFormat_h */
