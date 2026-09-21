/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef intl_components_Collator_h_
#define intl_components_Collator_h_

#ifndef JS_STANDALONE
#  include "gtest/MozGtestFriend.h"
#endif

#include <type_traits>

#include "mozilla/intl/collator_glue.h"

#include "mozilla/intl/ICU4CGlue.h"
#include "mozilla/intl/ICUError.h"
#include "mozilla/Result.h"
#include "mozilla/Span.h"

namespace mozilla::intl {

/**
 * Collator for backing Web APIs.
 *
 * Use `mozilla::intl::AppCollator` when you need to collate from C++
 * in app UI code.
 */
class Collator final {
 public:
  /**
   * Attempt to initialize a new collator.
   *
   * Fails if aLocale is empty or does not parse according to UTS 35 and
   * succeeds otherwise. The collation-relevant locale extension keys that are
   * honored by ECMA-402 are honored (unless overridden by aOptions) and the
   * others are not.
   *
   * Notably, this succeeds for unknown locales (with the root collation) as
   * long as `aLocale` is well-formed according to UTS 35.
   */
  static Result<UniquePtr<Collator>, ICUError> TryCreate(
      mozilla::Span<const char> aLocale, CollatorOptions aOptions) {
    Collator* ptr = mozilla_collator_glue_collator_try_new(
        aLocale.Elements(), aLocale.Length(), aOptions);
    if (!ptr) {
      // TODO: Mint a better error
      return Err(ICUError::InternalError);
    }
    return UniquePtr<Collator>(ptr);
  }

  ~Collator() = default;
  static void operator delete(void* aCollator) {
    mozilla_collator_glue_collator_free(reinterpret_cast<Collator*>(aCollator));
  }

  /**
   * Returns the resolved options of this collator.
   */
  CollatorOptions ResolvedOptions() {
    return mozilla_collator_glue_collator_resolved_options(this);
  }

  // If you have UTF-8, it's trivial to add `CompareUTF-8` that calls the
  // `compare_utf8` on the Rust collator. Do not convert from UTF-8 to UTF-16
  // and call this method!
  int32_t CompareUTF16(Span<const char16_t> aLeft,
                       Span<const char16_t> aRight) const {
    return mozilla_collator_glue_collator_compare_utf16(
        this, reinterpret_cast<const uint16_t*>(aLeft.Elements()),
        aLeft.Length(), reinterpret_cast<const uint16_t*>(aRight.Elements()),
        aRight.Length());
  }

  int32_t CompareLatin1(Span<const unsigned char> aLeft,
                        Span<const unsigned char> aRight) const {
    return mozilla_collator_glue_collator_compare_latin1(
        this, aLeft.Elements(), aLeft.Length(), aRight.Elements(),
        aRight.Length());
  }

  int32_t CompareLatin1UTF16(Span<const unsigned char> aLeft,
                             Span<const char16_t> aRight) const {
    return mozilla_collator_glue_collator_compare_latin1_utf16(
        this, aLeft.Elements(), aLeft.Length(),
        reinterpret_cast<const uint16_t*>(aRight.Elements()), aRight.Length());
  }

  /**
   * `true` iff the collation (which must not be "standard" or "search")
   * is supported by locale. (Currently, subtags of the locale other than
   * language are ignored, since there are no collations attaching to
   * something more specific.)
   */
  static bool IsSupportedCollation(Span<const char> aLocale,
                                   Span<const char> aCollation) {
    auto locale = AsBytes(aLocale);
    auto collation = AsBytes(aCollation);
    return mozilla_collator_glue_is_supported_collation(
        locale.Elements(), locale.Length(), collation.Elements(),
        collation.Length());
  }

  /**
   * Returns an iterator over all possible collator locale extensions.
   * These extensions can be used in BCP 47 locales. For instance this
   * iterator could return "phonebk" and could be appled to the German locale
   * "de" as "de-u-co-phonebk" for a phonebook-style collation.
   *
   * The collation extensions can be found here:
   * https://unicode.org/reports/tr35/#Key_Type_Definitions
   */
  static auto GetBcp47KeywordValues() {
    return ICU4XEnumeration<mozilla::intl::CollationList,
                            mozilla_collator_glue_collation_list_len,
                            mozilla_collator_glue_collation_list_item,
                            mozilla_collator_glue_collation_list_new,
                            mozilla_collator_glue_collation_list_free>();
  }

  /**
   * Returns an iterator over all supported collator locales.
   *
   * The returned strings are a subset of Unicode BCP 47 locale
   * identifiers. See
   * https://unicode-org.github.io/icu4x/rustdoc/icu/locale/struct.DataLocale.html
   *
   * At the time of writing only language, script, and region occur
   * in practice since the occurrence of the upstream-occurring variant
   * (POSIX) is deliberately excluded and subdivision does not occur
   * in collation data at the time of writing.
   */
  static auto GetAvailableLocales() {
    return ICU4XEnumeration<mozilla::intl::CollatorLocaleList,
                            mozilla_collator_glue_locale_list_len,
                            mozilla_collator_glue_locale_list_item,
                            mozilla_collator_glue_locale_list_new,
                            mozilla_collator_glue_locale_list_free>();
  }

  Collator() = delete;
  Collator(const Collator&) = delete;
  Collator& operator=(const Collator&) = delete;
};

static_assert(std::is_empty_v<Collator>);

}  // namespace mozilla::intl

#endif
