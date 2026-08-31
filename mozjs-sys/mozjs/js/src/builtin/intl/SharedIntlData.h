/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_intl_SharedIntlData_h
#define builtin_intl_SharedIntlData_h

#include "mozilla/MemoryReporting.h"
#include "mozilla/Span.h"
#include "mozilla/UniquePtr.h"

#include <stddef.h>
#include <string_view>

#include "js/AllocPolicy.h"
#include "js/GCAPI.h"
#include "js/GCHashTable.h"
#include "js/Result.h"
#include "js/RootingAPI.h"
#include "js/Utility.h"
#include "util/LanguageId.h"
#include "vm/StringType.h"

namespace mozilla::intl {
class DateTimePatternGenerator;
}  // namespace mozilla::intl

namespace js {

class ArrayObject;

namespace intl {

enum class AvailableLocaleKind {
  Collator,
  DateTimeFormat,
  DisplayNames,
  DurationFormat,
  ListFormat,
  NumberFormat,
  PluralRules,
  RelativeTimeFormat,
  Segmenter,
};

/**
 * This deleter class exists so that mozilla::intl::DateTimePatternGenerator
 * can be a forward declaration, but still be used inside of a UniquePtr.
 */
class DateTimePatternGeneratorDeleter {
 public:
  void operator()(mozilla::intl::DateTimePatternGenerator* ptr);
};

/**
 * Stores Intl data which can be shared across compartments (but not contexts).
 *
 * Used for data which is expensive when computed repeatedly or is not
 * available through ICU.
 */
class SharedIntlData {
  struct LinearStringLookup {
    union {
      const JS::Latin1Char* latin1Chars;
      const char16_t* twoByteChars;
    };
    bool isLatin1;
    size_t length;
    JS::AutoCheckCannotGC nogc;
    HashNumber hash = 0;

    explicit LinearStringLookup(const JSLinearString* string)
        : isLatin1(string->hasLatin1Chars()), length(string->length()) {
      if (isLatin1) {
        latin1Chars = string->latin1Chars(nogc);
      } else {
        twoByteChars = string->twoByteChars(nogc);
      }
    }

    explicit LinearStringLookup(std::string_view string)
        : isLatin1(true), length(string.length()) {
      latin1Chars = reinterpret_cast<const JS::Latin1Char*>(string.data());
    }

    explicit LinearStringLookup(std::u16string_view string)
        : isLatin1(false), length(string.length()) {
      twoByteChars = string.data();
    }
  };

 public:
  /**
   * Information tracking the set of the supported time zone names, derived
   * from the IANA time zone database <https://www.iana.org/time-zones>.
   *
   * There are two kinds of IANA time zone names: Zone and Link (denoted as
   * such in database source files). Zone names are the canonical, preferred
   * name for a time zone, e.g. Asia/Kolkata. Link names simply refer to
   * target Zone names for their meaning, e.g. Asia/Calcutta targets
   * Asia/Kolkata. That a name is a Link doesn't *necessarily* reflect a
   * sense of deprecation: some Link names also exist partly for convenience,
   * e.g. UTC and GMT as Link names targeting the Zone name Etc/UTC.
   *
   * Two data sources determine the time zone names we support: those ICU
   * supports and IANA's zone information.
   *
   * Unfortunately the names ICU and IANA support, and their Link
   * relationships from name to target, aren't identical, so we can't simply
   * implicitly trust ICU's name handling. We must perform various
   * preprocessing of user-provided zone names and post-processing of
   * ICU-provided zone names to implement ECMA-402's IANA-consistent behavior.
   *
   * Also see <https://ssl.icu-project.org/trac/ticket/12044> and
   * <http://unicode.org/cldr/trac/ticket/9892>.
   */

  using TimeZoneName = JSAtom*;

  struct AvailableTimeZoneHasher {
    struct Lookup : LinearStringLookup {
      explicit Lookup(const JSLinearString* timeZone);
      explicit Lookup(std::string_view timeZone);
      explicit Lookup(std::u16string_view timeZone);
    };

    static js::HashNumber hash(const Lookup& lookup) { return lookup.hash; }
    static bool match(TimeZoneName key, const Lookup& lookup);
  };

  struct TimeZoneHasher {
    using Lookup = TimeZoneName;

    static js::HashNumber hash(const Lookup& lookup) { return lookup->hash(); }
    static bool match(TimeZoneName key, const Lookup& lookup) {
      return key == lookup;
    }
  };

  using AvailableTimeZoneSet =
      GCHashSet<TimeZoneName, AvailableTimeZoneHasher, SystemAllocPolicy>;
  using TimeZoneSet =
      GCHashSet<TimeZoneName, TimeZoneHasher, SystemAllocPolicy>;
  using TimeZoneMap =
      GCHashMap<TimeZoneName, TimeZoneName, TimeZoneHasher, SystemAllocPolicy>;

 private:
  /**
   * As a threshold matter, available time zones are those time zones ICU
   * supports, via ucal_openTimeZones. But ICU supports additional non-IANA
   * time zones described in intl/icu/source/tools/tzcode/icuzones (listed in
   * TimeZoneDataGenerated.h's |legacyICUTimeZones|) for its own backwards
   * compatibility purposes. This set consists of ICU's supported time zones,
   * minus all backwards-compatibility time zones.
   */
  AvailableTimeZoneSet availableTimeZones;

  /**
   * IANA treats some time zone names as Zones, that ICU instead treats as
   * Links. For example, IANA considers "America/Indiana/Indianapolis" to be
   * a Zone and "America/Fort_Wayne" a Link that targets it, but ICU
   * considers the former a Link that targets "America/Indianapolis" (which
   * IANA treats as a Link).
   *
   * ECMA-402 requires that we respect IANA data, so if we're asked to
   * canonicalize a time zone name in this set, we must *not* return ICU's
   * canonicalization.
   */
  TimeZoneSet ianaZonesTreatedAsLinksByICU;

  /**
   * IANA treats some time zone names as Links to one target, that ICU
   * instead treats as either Zones, or Links to different targets. An
   * example of the former is "Asia/Calcutta, which IANA assigns the target
   * "Asia/Kolkata" but ICU considers its own Zone. An example of the latter
   * is "US/East-Indiana", which IANA assigns the target
   * "America/Indiana/Indianapolis" but ICU assigns the target
   * "America/Indianapolis".
   *
   * ECMA-402 requires that we respect IANA data, so if we're asked to
   * canonicalize a time zone name that's a key in this map, we *must* return
   * the corresponding value and *must not* return ICU's canonicalization.
   */
  TimeZoneMap ianaLinksCanonicalizedDifferentlyByICU;

  bool timeZoneDataInitialized = false;

  /**
   * Precomputes the available time zone names, because it's too expensive to
   * call ucal_openTimeZones() repeatedly.
   */
  bool ensureTimeZones(JSContext* cx);

  /**
   * Returns the canonical time zone name. |availableTimeZone| must be an
   * available time zone name. If no canonical name was found, returns
   * |nullptr|.
   *
   * This method only handles time zones which are canonicalized differently
   * by ICU when compared to IANA.
   */
  JSAtom* tryCanonicalizeTimeZoneConsistentWithIANA(JSAtom* availableTimeZone);

  /**
   * Returns the canonical time zone name. |availableTimeZone| must be an
   * available time zone name.
   */
  JSAtom* canonicalizeAvailableTimeZone(JSContext* cx,
                                        JS::Handle<JSAtom*> availableTimeZone);

  /**
   * Validates and canonicalizes a time zone name. Returns the case-normalized
   * identifier in |identifier| and its primary time zone in |primary|. If the
   * input time zone isn't a valid IANA time zone name, |identifier| and
   * |primary| both remain unchanged.
   */
  bool validateAndCanonicalizeTimeZone(
      JSContext* cx, const AvailableTimeZoneSet::Lookup& lookup,
      JS::MutableHandle<JSAtom*> identifier,
      JS::MutableHandle<JSAtom*> primary);

 public:
  /**
   * Returns the canonical time zone name. |timeZone| must be a valid time zone
   * name.
   */
  JSLinearString* canonicalizeTimeZone(JSContext* cx,
                                       JS::Handle<JSLinearString*> timeZone);

  /**
   * Validates and canonicalizes a time zone name. Returns the case-normalized
   * identifier in |identifier| and its primary time zone in |primary|. If the
   * input time zone isn't a valid IANA time zone name, |identifier| and
   * |primary| both remain unchanged.
   */
  bool validateAndCanonicalizeTimeZone(JSContext* cx,
                                       JS::Handle<JSLinearString*> timeZone,
                                       JS::MutableHandle<JSAtom*> identifier,
                                       JS::MutableHandle<JSAtom*> primary);

  /**
   * Validates and canonicalizes a time zone name. Returns the case-normalized
   * identifier in |identifier| and its primary time zone in |primary|. If the
   * input time zone isn't a valid IANA time zone name, |identifier| and
   * |primary| both remain unchanged.
   */
  bool validateAndCanonicalizeTimeZone(JSContext* cx,
                                       mozilla::Span<const char> timeZone,
                                       JS::MutableHandle<JSAtom*> identifier,
                                       JS::MutableHandle<JSAtom*> primary);

  /**
   * Returns an iterator over all available time zones supported by ICU. The
   * returned time zone names aren't canonicalized.
   */
  JS::Result<AvailableTimeZoneSet::Iterator> availableTimeZonesIteration(
      JSContext* cx);

 private:
  using Locale = LanguageId;

  struct LocaleHasher {
    using Lookup = Locale;

    static js::HashNumber hash(const Lookup& lookup) { return lookup.hash(); }

    static bool match(Locale key, const Lookup& lookup) {
      return key == lookup;
    }
  };

  using LocaleSet = HashSet<Locale, LocaleHasher, SystemAllocPolicy>;

  // Set of available locales for all Intl service constructors except Collator,
  // which uses its own set.
  //
  // UDateFormat:
  // udat_[count,get]Available() return the same results as their
  // uloc_[count,get]Available() counterparts.
  //
  // UNumberFormatter:
  // unum_[count,get]Available() return the same results as their
  // uloc_[count,get]Available() counterparts.
  //
  // UListFormatter, UPluralRules, and URelativeDateTimeFormatter:
  // We're going to use ULocale availableLocales as per ICU recommendation:
  // https://unicode-org.atlassian.net/browse/ICU-12756
  LocaleSet availableLocales;

  // ucol_[count,get]Available() return different results compared to
  // uloc_[count,get]Available(), we can't use |availableLocales| here.
  LocaleSet collatorAvailableLocales;

  bool availableLocalesInitialized = false;

  // CountAvailable and GetAvailable describe the signatures used for ICU API
  // to determine available locales for various functionality.
  using CountAvailable = int32_t (*)();
  using GetAvailable = const char* (*)(int32_t localeIndex);

  template <class AvailableLocales>
  static bool getAvailableLocales(JSContext* cx, LocaleSet& locales,
                                  const AvailableLocales& availableLocales);

  /**
   * Precomputes the available locales sets.
   */
  bool ensureAvailableLocales(JSContext* cx);

 public:
  /**
   * Sets |available| to true if |locale| is supported by the requested Intl
   * service constructor. Otherwise sets |available| to false.
   */
  [[nodiscard]] bool isAvailableLocale(JSContext* cx, AvailableLocaleKind kind,
                                       LanguageId locale, bool* available);

  /**
   * Returns all available locales for |kind|.
   */
  ArrayObject* availableLocalesOf(JSContext* cx, AvailableLocaleKind kind);

 private:
  using UniqueDateTimePatternGenerator =
      mozilla::UniquePtr<mozilla::intl::DateTimePatternGenerator,
                         DateTimePatternGeneratorDeleter>;

  UniqueDateTimePatternGenerator dateTimePatternGenerator;
  JS::UniqueChars dateTimePatternGeneratorLocale;

 public:
  /**
   * Get a non-owned cached instance of the DateTimePatternGenerator, which is
   * expensive to instantiate.
   *
   * See: https://bugzilla.mozilla.org/show_bug.cgi?id=1549578
   */
  mozilla::intl::DateTimePatternGenerator* getDateTimePatternGenerator(
      JSContext* cx, const char* locale);

 public:
  void destroyInstance();

  void trace(JSTracer* trc);

  size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
};

}  // namespace intl

}  // namespace js

#endif /* builtin_intl_SharedIntlData_h */
