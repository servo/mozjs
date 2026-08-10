/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_intl_Locale_h
#define builtin_intl_Locale_h

#include <stdint.h>

#include "js/Class.h"
#include "vm/NativeObject.h"
#include "vm/StringType.h"

namespace js::intl {

class LocaleObject : public NativeObject {
 public:
  static const JSClass class_;
  static const JSClass protoClass_;

  static constexpr uint32_t LANGUAGE_TAG_SLOT = 0;
  static constexpr uint32_t BASENAME_SLOT = 1;
  static constexpr uint32_t UNICODE_EXTENSION_SLOT = 2;
  static constexpr uint32_t SLOT_COUNT = 3;

  void initialize(JSLinearString* languageTag, JSLinearString* baseName,
                  JSLinearString* unicodeExtension) {
    initFixedSlot(LANGUAGE_TAG_SLOT, JS::StringValue(languageTag));
    initFixedSlot(BASENAME_SLOT, JS::StringValue(baseName));
    if (unicodeExtension) {
      initFixedSlot(UNICODE_EXTENSION_SLOT, JS::StringValue(unicodeExtension));
    } else {
      MOZ_ASSERT(getFixedSlot(UNICODE_EXTENSION_SLOT).isUndefined());
    }
  }

  /**
   * Returns the complete language tag, including any extensions and privateuse
   * subtags.
   */
  JSLinearString* getLanguageTag() const {
    return &getFixedSlot(LANGUAGE_TAG_SLOT).toString()->asLinear();
  }

  /**
   * Returns the basename subtags, i.e. excluding any extensions and privateuse
   * subtags.
   */
  JSLinearString* getBaseName() const {
    return &getFixedSlot(BASENAME_SLOT).toString()->asLinear();
  }

  /**
   * Returns Unicode locale extension subtag or `nullptr` if no Unicode
   * extension sequence is present.
   */
  JSLinearString* getUnicodeExtension() const {
    const auto& slot = getFixedSlot(UNICODE_EXTENSION_SLOT);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return &slot.toString()->asLinear();
  }

 private:
  static const ClassSpec classSpec_;
};

}  // namespace js::intl

#endif /* builtin_intl_Locale_h */
