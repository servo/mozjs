/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef util_LanguageId_h
#define util_LanguageId_h

#include "mozilla/Assertions.h"
#include "mozilla/HashFunctions.h"
#include "mozilla/Maybe.h"
#include "mozilla/Span.h"
#include "mozilla/TextUtils.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdint.h>
#include <string_view>
#include <utility>

namespace js {

class LanguageIdString;

/**
 * Compact representation of language identifiers.
 *
 * Language identifiers have the following limitations when compared to Unicode
 * BCP 47 locale identifiers:
 * - Language subtags can have at most three letters.
 * - Variant and extension subtags are not supported.
 *
 * In other words, language identifiers contain only language, script, and
 * region subtags.
 *
 * All locales supported by ICU4C can be represented as language identifiers,
 * except for "en_US_POSIX". "en_US_POSIX" canonicalizes to "en-US-u-va-posix",
 * which contains a Unicode extension sequence, so it's not a valid available
 * ECMA-402 locale, see also <https://tc39.es/ecma402/#available-locales-list>.
 *
 * Features:
 * - Fixed-length fields to avoid any heap allocations.
 * - Minimal size to allow efficient storing in other data structures.
 * - Fast comparison support for prefix-based locale lookup operations.
 * - Methods optimized for fast generated assembly code. Verified by inspecting
 *   the (x86) assembly code for Clang with optimization level O3 and ensuring
 *   all methods generate only basic assembly instructions and don't require
 *   calls to other built-ins.
 *
 * References:
 * https://tc39.es/ecma402/#sec-language-tags
 * https://unicode-org.github.io/icu/userguide/locale/
 * https://unicode.org/reports/tr35/tr35.html#Unicode_Language_and_Locale_Identifiers
 */
class LanguageId final {
  static constexpr size_t LanguageLength = 3;
  static constexpr size_t ScriptLength = 4;
  static constexpr size_t RegionLength = 3;
  static constexpr size_t Length = LanguageLength + ScriptLength + RegionLength;

  static constexpr size_t LanguageIndex = 0;
  static constexpr size_t ScriptIndex = LanguageIndex + LanguageLength;
  static constexpr size_t RegionIndex = ScriptIndex + ScriptLength;

  // GCC 10 doesn't support defaulted equality operators for plain arrays
  // (<https://gcc.gnu.org/bugzilla/show_bug.cgi?id=93480>). So we can't write
  // this:
  //
  // char language_[3] = {};
  // char script_[4] = {};
  // char region_[3] = {};
  //
  // In addition to that GCC bug, Clang sometimes (!) generates worse code for
  // comparisons when separate arrays are used.
  std::array<char, Length> chars_{};

  constexpr auto as_span() { return mozilla::Span<char, Length>{chars_}; }
  constexpr auto language_span() {
    return as_span().Subspan<LanguageIndex, LanguageLength>();
  }
  constexpr auto script_span() {
    return as_span().Subspan<ScriptIndex, ScriptLength>();
  }
  constexpr auto region_span() {
    return as_span().Subspan<RegionIndex, RegionLength>();
  }

  constexpr auto as_span() const {
    return mozilla::Span<const char, Length>{chars_};
  }
  constexpr auto language_span() const {
    return as_span().Subspan<LanguageIndex, LanguageLength>();
  }
  constexpr auto script_span() const {
    return as_span().Subspan<ScriptIndex, ScriptLength>();
  }
  constexpr auto region_span() const {
    return as_span().Subspan<RegionIndex, RegionLength>();
  }

  friend class LanguageIdString;

  /**
   * Return true if |language| is a language subtag in canonical case.
   *
   * Canonical case of language subtags is lower-case.
   */
  template <typename CharT>
  static constexpr bool IsValidLanguage(
      std::basic_string_view<CharT> language) {
    return (language.length() == 2 || language.length() == 3) &&
           std::all_of(language.begin(), language.end(),
                       mozilla::IsAsciiLowercaseAlpha<CharT>);
  }

  /**
   * Return true if |script| is a script subtag in canonical case.
   *
   * Canonical case of script subtags is title-case.
   */
  template <typename CharT>
  static constexpr bool IsValidScript(std::basic_string_view<CharT> script) {
    return script.length() == 4 && mozilla::IsAsciiUppercaseAlpha(script[0]) &&
           std::all_of(std::next(script.begin()), script.end(),
                       mozilla::IsAsciiLowercaseAlpha<CharT>);
  }

  /**
   * Return true if |region| is a alpha region subtag in canonical case.
   *
   * Canonical case of region subtags is upper-case.
   */
  template <typename CharT>
  static constexpr bool IsValidAlphaRegion(
      std::basic_string_view<CharT> region) {
    return region.length() == 2 &&
           std::all_of(region.begin(), region.end(),
                       mozilla::IsAsciiUppercaseAlpha<CharT>);
  }

  /**
   * Return true if |region| is a digit region subtag.
   */
  template <typename CharT>
  static constexpr bool IsValidDigitRegion(
      std::basic_string_view<CharT> region) {
    return region.length() == 3 && std::all_of(region.begin(), region.end(),
                                               mozilla::IsAsciiDigit<CharT>);
  }

  /**
   * Return true if |region| is a region subtag.
   */
  template <typename CharT>
  static constexpr bool IsValidRegion(std::basic_string_view<CharT> region) {
    return IsValidAlphaRegion(region) || IsValidDigitRegion(region);
  }

  constexpr LanguageId() = default;

 public:
  constexpr bool operator==(const LanguageId&) const = default;

  /**
   * Language subtag of this language identifier.
   */
  constexpr auto language() const {
    // Language subtags are two or three characters long.
    size_t length = 2 + (language_span()[2] != '\0');
    return std::string_view{std::data(language_span()), length};
  }

  /**
   * Script subtag of this language identifier or empty if no script subtag is
   * present.
   */
  constexpr auto script() const {
    // Script subtags are always four characters long.
    size_t length = hasScript() ? 4 : 0;
    return std::string_view{std::data(script_span()), length};
  }

  /**
   * Region subtag of this language identifier or empty if no region subtag is
   * present.
   */
  constexpr auto region() const {
    // Region subtags are two or three characters long.
    size_t length = hasRegion() ? (2 + (region_span()[2] != '\0')) : 0;
    return std::string_view{std::data(region_span()), length};
  }

  /**
   * Return true if this language identifier has a script subtag.
   */
  constexpr bool hasScript() const {
    // CDT indexer doesn't like `script_span()[0]`, so for now directly access
    // through `chars_`. "LanguageId.h" is indirectly included in almost all
    // files, so breaking the indexer here leads to making everything
    // non-indexable. :-/
    // return script_span()[0] != '\0';
    return chars_[ScriptIndex] != '\0';
  }

  /**
   * Return true if this language identifier has a region subtag.
   */
  constexpr bool hasRegion() const {
    // CDT indexer doesn't like `region_span()[0]`, see `hasScript`.
    // return region_span()[0] != '\0';
    return chars_[RegionIndex] != '\0';
  }

  /**
   * Hash number of this language identifier.
   */
  auto hash() const {
    auto [lead_span, trail_span] = as_span().SplitAt<8>();

    uint64_t lead = 0;
    std::memcpy(&lead, std::data(lead_span), std::size(lead_span));

    uint32_t trail = 0;
    std::memcpy(&trail, std::data(trail_span), std::size(trail_span));

    // Using HashGeneric is much faster than for example HashStringKnownLength.
    return mozilla::HashGeneric(lead, trail);
  }

 private:
  template <char... separators, typename CharT>
  static constexpr mozilla::Maybe<std::pair<LanguageId, size_t>> from(
      std::basic_string_view<CharT> localeId) {
    // Return true iff |sv| starts with a subtag of length |len|.
    auto hasSubtag = [](std::basic_string_view<CharT> sv, size_t len) {
      if (sv.length() == len) {
        return true;
      }
      if (sv.length() > len) {
        auto ch = sv[len];
        return (... || (separators == ch));
      }
      return false;
    };

    // Copy the subtag |tag| to |dest| and then removed the processed prefix
    // from |localeId|.
    auto copyAndRemovePrefix = [&](auto dest,
                                   std::basic_string_view<CharT> tag) {
      MOZ_ASSERT(localeId.starts_with(tag), "tag is a prefix");
      MOZ_ASSERT(std::size(dest) >= tag.length(), "dest is large enough");

      std::copy_n(tag.data(), tag.length(), std::data(dest));
      localeId.remove_prefix(tag.length() + (localeId.length() > tag.length()));
    };

    LanguageId result{};

    // NB: Two and three letter language tags handled in separate branches to
    // ensure the compiler treats |lang.length()| as a compile-time constant.
    // This leads to smaller and faster generated assembly code, because memcpy
    // calls with a constant length can inlined.
    if (hasSubtag(localeId, 2)) {
      auto lang = localeId.substr(0, 2);
      if (!IsValidLanguage(lang)) [[unlikely]] {
        return mozilla::Nothing();
      }
      copyAndRemovePrefix(result.language_span(), lang);
    } else if (hasSubtag(localeId, 3)) {
      auto lang = localeId.substr(0, 3);
      if (!IsValidLanguage(lang)) [[unlikely]] {
        return mozilla::Nothing();
      }
      copyAndRemovePrefix(result.language_span(), lang);
    } else [[unlikely]] {
      return mozilla::Nothing();
    }

    // Optional script subtag.
    if (hasSubtag(localeId, 4)) {
      auto script = localeId.substr(0, 4);
      if (IsValidScript(script)) [[likely]] {
        copyAndRemovePrefix(result.script_span(), script);
      }
    }

    // Optional region subtag.
    if (hasSubtag(localeId, 2)) {
      auto region = localeId.substr(0, 2);
      if (IsValidAlphaRegion(region)) [[likely]] {
        copyAndRemovePrefix(result.region_span(), region);
      }
    } else if (hasSubtag(localeId, 3)) {
      auto region = localeId.substr(0, 3);
      if (IsValidDigitRegion(region)) [[likely]] {
        copyAndRemovePrefix(result.region_span(), region);
      }
    }

    return mozilla::Some(std::pair{result, localeId.length()});
  }

 public:
  /**
   * Create a language identifier from an ICU or Unicode locale identifier.
   * Returns the language identifier and the number of unprocessed characters
   * (trailing subtags or unparseable characters). Return Nothing if the input
   * doesn't start with a language subtag.
   *
   * The language, script, and region subtags must be in canonical case.
   *
   * Subtags in ICU and Unicode locale identifiers are separated by "-" or "_".
   */
  static constexpr auto fromId(std::string_view localeId) {
    return from<'-', '_'>(localeId);
  }

  /**
   * Create a language identifier from an ICU or Unicode locale identifier.
   * Returns the language identifier and the number of unprocessed characters
   * (trailing subtags or unparseable characters). Return Nothing if the input
   * doesn't start with a language subtag.
   *
   * The language, script, and region subtags must be in canonical case.
   *
   * Subtags in ICU and Unicode locale identifiers are separated by "-" or "_".
   */
  static constexpr auto fromId(mozilla::Span<const char> localeId) {
    return fromId(std::string_view{localeId.data(), localeId.size()});
  }

  /**
   * Create a language identifier from a Unicode BCP 47 locale identifier.
   * Returns the language identifier and the number of unprocessed characters
   * (trailing subtags or unparseable characters). Return Nothing if the input
   * doesn't start with a language subtag.
   *
   * The language, script, and region subtags must be in canonical case.
   *
   * Subtags in BCP 47 locale identifiers are separated by "-".
   */
  static constexpr auto fromBcp49(std::string_view localeId) {
    return from<'-'>(localeId);
  }

  /**
   * Create a language identifier from a Unicode BCP 47 locale identifier.
   * Returns the language identifier and the number of unprocessed characters
   * (trailing subtags or unparseable characters). Return Nothing if the input
   * doesn't start with a language subtag.
   *
   * The language, script, and region subtags must be in canonical case.
   *
   * Subtags in BCP 47 locale identifiers are separated by "-".
   */
  static constexpr auto fromBcp49(std::u16string_view localeId) {
    return from<u'-'>(localeId);
  }

  /**
   * Create a language identifier from a Unicode BCP 47 locale identifier.
   * Returns the language identifier and the number of unprocessed characters
   * (trailing subtags or unparseable characters). Return Nothing if the input
   * doesn't start with a language subtag.
   *
   * The language, script, and region subtags must be in canonical case.
   *
   * Subtags in BCP 47 locale identifiers are separated by "-".
   */
  template <typename CharT>
  static constexpr auto fromBcp49(mozilla::Span<const CharT> localeId) {
    return fromBcp49(std::basic_string_view{localeId.data(), localeId.size()});
  }

  /**
   * Create a language identifier from a valid Unicode BCP 47 locale identifier.
   *
   * The language, script, and region subtags must be in canonical case.
   *
   * Subtags in BCP 47 locale identifiers are separated by "-".
   */
  static consteval auto fromValidBcp49(std::string_view localeId) {
    return fromBcp49(localeId)->first;
  }

  /**
   * Create a language identifier from a valid subtags.
   *
   * The language, script, and region subtags must be in canonical case.
   */
  static constexpr auto fromParts(std::string_view language,
                                  std::string_view script,
                                  std::string_view region) {
    MOZ_ASSERT(IsValidLanguage(language));
    MOZ_ASSERT_IF(!script.empty(), IsValidScript(script));
    MOZ_ASSERT_IF(!region.empty(), IsValidRegion(region));

    LanguageId result{};
    language.copy(std::data(result.language_span()), language.length());
    script.copy(std::data(result.script_span()), script.length());
    region.copy(std::data(result.region_span()), region.length());

    return result;
  }

  /**
   * Return the language identifier for the undetermined locale "und".
   */
  static constexpr auto und() {
    constexpr LanguageId locale = fromValidBcp49("und");
    return locale;
  }

  /**
   * Return the language identifier with any script subtag removed.
   */
  constexpr auto withoutScript() const {
    LanguageId result = *this;

    // mozilla::Span requires that the _same_ span is used for iteration.
    auto script = result.script_span();

    std::fill(std::begin(script), std::end(script), '\0');
    return result;
  }

  /**
   * Return the language identifier with any region subtag removed.
   */
  constexpr auto withoutRegion() const {
    LanguageId result = *this;

    // mozilla::Span requires that the _same_ span is used for iteration.
    auto region = result.region_span();

    std::fill(std::begin(region), std::end(region), '\0');
    return result;
  }

  /**
   * Return the parent language identifier or "und" if this language identifier
   * consists of a single language subtag.
   */
  constexpr auto parentLocale() const {
    if (hasRegion()) {
      return withoutRegion();
    }
    if (hasScript()) {
      return withoutScript();
    }
    return und();
  }

  /**
   * Return `true` if this language identifier is a prefix of `other`.
   *
   * Examples:
   * - "en" is a prefix of "en", "en-Latn", "en-US", and "en-Latn-US".
   * - "en-Latn" is a prefix of "en-Latn" and "en-Latn-US".
   * - "en-US" is a prefix of "en-US".
   * - "en-US" is not a prefix of "en-Latn-US".
   * - "en-Latn-US" is a prefix "en-Latn-US".
   */
  constexpr bool isPrefixOf(LanguageId other) const {
    if (!hasRegion()) {
      // Remove region subtag if this language identifier has no region.
      other = other.withoutRegion();

      if (!hasScript()) {
        // Remove script subtag if this language identifier has no script.
        other = other.withoutScript();
      }
    }

    return *this == other;
  }

  /**
   * Return the language identifier string.
   */
  constexpr auto toString() const;
};
static_assert(sizeof(LanguageId) == 10,
              "LanguageId uses a compact language identifier representation");

/**
 * String representation of a language identifier as a Unicode BCP 47 locale
 * identifier.
 */
class LanguageIdString final {
  // Language subtag: 2-3 characters
  // Script subtag: 4 characters
  // Region subtag: 2-3 characters
  // Subtag separator: 1 character ("-")
  //
  // Total: 12 + 1 (null terminated for ICU4C).
  std::array<char, 12 + 1> chars_ = {};

  // String length can't exceed 12 characters, so it fits into uint8_t.
  uint8_t length_ = 0;

  friend class LanguageId;

  constexpr explicit LanguageIdString(const LanguageId& langId) {
    static_assert(
        decltype(std::declval<LanguageId>().as_span())::extent +
                3 /* two subtag separators and a trailing NUL character */
            <= std::tuple_size_v<decltype(LanguageIdString::chars_)>,
        "LanguageIdString::chars_ is large enough to hold all subtags");

    auto out = std::begin(chars_);

    // Copy the language subtag.
    //
    // Intentionally use `std::copy[_n]()` instead of `string_view::copy()` here
    // and below to copy a compile-time constant number of characters. This may
    // include a trailing NUL character, which will be overwritten if necessary.
    auto language = langId.language();
    MOZ_ASSERT(!language.empty(), "language subtag is never empty");

    // Generated assembly code of this constructor is 25% larger when calling
    // `std::copy` on a mozilla::Span instead of `std::copy_n`. `std::span`
    // generates the same assembly for `std::copy` and `std::copy_n`.
    auto language_span = langId.language_span();
    std::copy_n(std::data(language_span), std::size(language_span), out);
    out += language.length();

    // Copy the script subtag, if present.
    if (auto script = langId.script(); !script.empty()) {
      auto script_span = langId.script_span();

      *out++ = '-';
      std::copy_n(std::data(script_span), std::size(script_span), out);
      out += script.length();
    }

    // Copy the region subtag, if present.
    if (auto region = langId.region(); !region.empty()) {
      auto region_span = langId.region_span();

      *out++ = '-';
      std::copy_n(std::data(region_span), std::size(region_span), out);
      out += region.length();
    }

    length_ = std::distance(std::begin(chars_), out);

    MOZ_ASSERT(chars_[length_] == '\0', "chars_ is null-terminated");
  }

 public:
  /**
   * Auto-converts into a `std::string_view`.
   */
  constexpr operator std::string_view() const {
    return std::string_view{std::data(chars_), length_};
  }

  /**
   * Auto-converts into a `mozilla::Span`.
   */
  constexpr operator mozilla::Span<const char>() const {
    return mozilla::Span{std::data(chars_), length_};
  }

  /**
   * Return the length of the language identifier string.
   */
  constexpr size_t length() const { return length_; }

  /**
   * Return a pointer to the language identifier string's characters.
   */
  constexpr const char* data() const { return std::data(chars_); }

  /**
   * Return a pointer to a null-terminated character array.
   *
   * Prefer this method over calling `data()` when passing the language
   * identifier string as a null-terminated string, because it gives stronger
   * signal that the characters are null-terminated.
   *
   * The method name is borrowed from `std::string::c_str()`.
   */
  constexpr const char* c_str() const { return std::data(chars_); }
};
static_assert(sizeof(LanguageIdString) <= 2 * sizeof(uint64_t),
              "LanguageIdString fits into two 64-bit registers");

constexpr auto LanguageId::toString() const { return LanguageIdString{*this}; }

}  // namespace js

#endif /* util_LanguageId_h */
