/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_intl_Collator_h
#define builtin_intl_Collator_h

#include <stddef.h>
#include <stdint.h>

#include "js/Class.h"
#include "js/Value.h"
#include "vm/NativeObject.h"
#include "vm/StringType.h"

namespace mozilla::intl {
class Collator;
}

namespace js::intl {

struct CollatorOptions;

class CollatorObject : public NativeObject {
 public:
  static const JSClass class_;
  static const JSClass& protoClass_;

  static constexpr uint32_t LOCALE_SLOT = 0;
  static constexpr uint32_t COLLATION_SLOT = 1;
  static constexpr uint32_t OPTIONS_SLOT = 2;
  static constexpr uint32_t INTL_COLLATOR_SLOT = 3;
  static constexpr uint32_t BOUND_COMPARE_SLOT = 4;
  static constexpr uint32_t SLOT_COUNT = 5;

  // Box<CollatorBorrowed> causes a request for an allocation of 72,
  // which is rounded up to 80 inside the allocator.
  static constexpr size_t EstimatedMemoryUse = 80;

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

  JSLinearString* getCollation() const {
    const auto& slot = getFixedSlot(COLLATION_SLOT);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return &slot.toString()->asLinear();
  }

  void setCollation(JSLinearString* collation) {
    setFixedSlot(COLLATION_SLOT, JS::StringValue(collation));
  }

  CollatorOptions getOptions() const;

  void setOptions(const CollatorOptions& options);

  mozilla::intl::Collator* getCollator() const {
    const auto& slot = getFixedSlot(INTL_COLLATOR_SLOT);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return static_cast<mozilla::intl::Collator*>(slot.toPrivate());
  }

  void setCollator(mozilla::intl::Collator* collator) {
    setFixedSlot(INTL_COLLATOR_SLOT, JS::PrivateValue(collator));
  }

  JSObject* getBoundCompare() const {
    const auto& slot = getFixedSlot(BOUND_COMPARE_SLOT);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return &slot.toObject();
  }

  void setBoundCompare(JSObject* boundCompare) {
    setFixedSlot(BOUND_COMPARE_SLOT, JS::ObjectValue(*boundCompare));
  }

 private:
  static const JSClassOps classOps_;
  static const ClassSpec classSpec_;

  static void finalize(JS::GCContext* gcx, JSObject* obj);
};

/**
 * Returns a new instance of the standard built-in Collator constructor.
 */
[[nodiscard]] extern CollatorObject* CreateCollator(
    JSContext* cx, JS::Handle<JS::Value> locales,
    JS::Handle<JS::Value> options);

/**
 * Returns a possibly cached instance of the standard built-in Collator
 * constructor.
 */
[[nodiscard]] extern CollatorObject* GetOrCreateCollator(
    JSContext* cx, JS::Handle<JS::Value> locales,
    JS::Handle<JS::Value> options);

/**
 * Compares x and y, and returns a number less than 0 if x < y, 0 if x = y, or a
 * number greater than 0 if x > y according to the sort order for the locale and
 * collation options of the given Collator.
 */
[[nodiscard]] extern bool CompareStrings(JSContext* cx,
                                         JS::Handle<CollatorObject*> collator,
                                         JS::Handle<JSString*> str1,
                                         JS::Handle<JSString*> str2,
                                         JS::MutableHandle<JS::Value> result);

}  // namespace js::intl

#endif /* builtin_intl_Collator_h */
