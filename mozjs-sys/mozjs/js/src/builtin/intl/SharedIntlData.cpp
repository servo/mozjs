/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Runtime-wide Intl data shared across compartments. */

#include "builtin/intl/SharedIntlData.h"

#include "mozilla/Assertions.h"
#include "mozilla/HashFunctions.h"
#include "mozilla/intl/Collator.h"
#include "mozilla/intl/DateTimeFormat.h"
#include "mozilla/intl/DateTimePatternGenerator.h"
#include "mozilla/intl/Locale.h"
#include "mozilla/intl/NumberFormat.h"
#include "mozilla/intl/TimeZone.h"
#include "mozilla/Span.h"
#include "mozilla/TextUtils.h"

#include <algorithm>
#include <stdint.h>
#include <string.h>
#include <string_view>
#include <utility>

#include "builtin/Array.h"
#include "builtin/intl/CommonFunctions.h"
#include "builtin/intl/FormatBuffer.h"
#include "builtin/intl/TimeZoneDataGenerated.h"
#include "js/StableStringChars.h"
#include "js/Utility.h"
#include "js/Vector.h"
#include "vm/ArrayObject.h"
#include "vm/JSAtomUtils.h"  // Atomize
#include "vm/JSContext.h"
#include "vm/StringType.h"

#include "vm/NativeObject-inl.h"

using js::HashNumber;

template <typename Char>
static constexpr Char ToUpperASCII(Char c) {
  return mozilla::IsAsciiLowercaseAlpha(c) ? (c - 0x20) : c;
}

static_assert(ToUpperASCII('a') == 'A', "verifying 'a' uppercases correctly");
static_assert(ToUpperASCII('m') == 'M', "verifying 'm' uppercases correctly");
static_assert(ToUpperASCII('z') == 'Z', "verifying 'z' uppercases correctly");
static_assert(ToUpperASCII(u'a') == u'A',
              "verifying u'a' uppercases correctly");
static_assert(ToUpperASCII(u'k') == u'K',
              "verifying u'k' uppercases correctly");
static_assert(ToUpperASCII(u'z') == u'Z',
              "verifying u'z' uppercases correctly");

template <typename Char>
static HashNumber HashStringIgnoreCaseASCII(const Char* s, size_t length) {
  uint32_t hash = 0;
  for (size_t i = 0; i < length; i++) {
    hash = mozilla::AddToHash(hash, ToUpperASCII(s[i]));
  }
  return hash;
}

js::intl::SharedIntlData::AvailableTimeZoneHasher::Lookup::Lookup(
    const JSLinearString* timeZone)
    : js::intl::SharedIntlData::LinearStringLookup(timeZone) {
  if (isLatin1) {
    hash = HashStringIgnoreCaseASCII(latin1Chars, length);
  } else {
    hash = HashStringIgnoreCaseASCII(twoByteChars, length);
  }
}

js::intl::SharedIntlData::AvailableTimeZoneHasher::Lookup::Lookup(
    std::string_view timeZone)
    : js::intl::SharedIntlData::LinearStringLookup(timeZone) {
  hash = HashStringIgnoreCaseASCII(latin1Chars, length);
}

js::intl::SharedIntlData::AvailableTimeZoneHasher::Lookup::Lookup(
    std::u16string_view timeZone)
    : js::intl::SharedIntlData::LinearStringLookup(timeZone) {
  hash = HashStringIgnoreCaseASCII(twoByteChars, length);
}

template <typename Char1, typename Char2>
static bool EqualCharsIgnoreCaseASCII(const Char1* s1, const Char2* s2,
                                      size_t len) {
  for (const Char1* s1end = s1 + len; s1 < s1end; s1++, s2++) {
    if (ToUpperASCII(*s1) != ToUpperASCII(*s2)) {
      return false;
    }
  }
  return true;
}

bool js::intl::SharedIntlData::AvailableTimeZoneHasher::match(
    TimeZoneName key, const Lookup& lookup) {
  if (key->length() != lookup.length) {
    return false;
  }

  // Compare time zone names ignoring ASCII case differences.
  if (key->hasLatin1Chars()) {
    const Latin1Char* keyChars = key->latin1Chars(lookup.nogc);
    if (lookup.isLatin1) {
      return EqualCharsIgnoreCaseASCII(keyChars, lookup.latin1Chars,
                                       lookup.length);
    }
    return EqualCharsIgnoreCaseASCII(keyChars, lookup.twoByteChars,
                                     lookup.length);
  }

  const char16_t* keyChars = key->twoByteChars(lookup.nogc);
  if (lookup.isLatin1) {
    return EqualCharsIgnoreCaseASCII(lookup.latin1Chars, keyChars,
                                     lookup.length);
  }
  return EqualCharsIgnoreCaseASCII(keyChars, lookup.twoByteChars,
                                   lookup.length);
}

static bool IsLegacyICUTimeZone(mozilla::Span<const char> timeZone) {
  std::string_view timeZoneView(timeZone.data(), timeZone.size());
  for (const auto& legacyTimeZone : js::timezone::legacyICUTimeZones) {
    if (timeZoneView == legacyTimeZone) {
      return true;
    }
  }
  return false;
}

bool js::intl::SharedIntlData::ensureTimeZones(JSContext* cx) {
  if (timeZoneDataInitialized) {
    return true;
  }

  // If ensureTimeZones() was called previously, but didn't complete due to
  // OOM, clear all sets/maps and start from scratch.
  availableTimeZones.clearAndCompact();

  auto timeZones = mozilla::intl::TimeZone::GetAvailableTimeZones();
  if (timeZones.isErr()) {
    ReportInternalError(cx, timeZones.unwrapErr());
    return false;
  }

  for (auto timeZoneName : timeZones.unwrap()) {
    if (timeZoneName.isErr()) {
      ReportInternalError(cx);
      return false;
    }
    auto timeZoneSpan = timeZoneName.unwrap();

    // Skip legacy ICU time zone names.
    if (IsLegacyICUTimeZone(timeZoneSpan)) {
      continue;
    }

    JSAtom* timeZone = Atomize(cx, timeZoneSpan.data(), timeZoneSpan.size());
    if (!timeZone) {
      return false;
    }

    auto p =
        availableTimeZones.lookupForAdd(AvailableTimeZoneSet::Lookup{timeZone});

    // ICU shouldn't report any duplicate time zone names, but if it does,
    // just ignore the duplicate name.
    if (!p && !availableTimeZones.add(p, timeZone)) {
      ReportOutOfMemory(cx);
      return false;
    }
  }

  ianaZonesTreatedAsLinksByICU.clearAndCompact();

  for (const char* rawTimeZone : timezone::ianaZonesTreatedAsLinksByICU) {
    MOZ_ASSERT(rawTimeZone != nullptr);
    JSAtom* timeZone = Atomize(cx, rawTimeZone, strlen(rawTimeZone));
    if (!timeZone) {
      return false;
    }

    auto p = ianaZonesTreatedAsLinksByICU.lookupForAdd(
        TimeZoneSet::Lookup{timeZone});
    MOZ_ASSERT(!p, "Duplicate entry in timezone::ianaZonesTreatedAsLinksByICU");

    if (!ianaZonesTreatedAsLinksByICU.add(p, timeZone)) {
      ReportOutOfMemory(cx);
      return false;
    }
  }

  ianaLinksCanonicalizedDifferentlyByICU.clearAndCompact();

  for (const auto& linkAndTarget :
       timezone::ianaLinksCanonicalizedDifferentlyByICU) {
    const char* rawLinkName = linkAndTarget.link;
    const char* rawTarget = linkAndTarget.target;

    MOZ_ASSERT(rawLinkName != nullptr);
    JSAtom* linkName = Atomize(cx, rawLinkName, strlen(rawLinkName));
    if (!linkName) {
      return false;
    }

    MOZ_ASSERT(rawTarget != nullptr);
    JSAtom* target = Atomize(cx, rawTarget, strlen(rawTarget));
    if (!target) {
      return false;
    }

    auto p = ianaLinksCanonicalizedDifferentlyByICU.lookupForAdd(
        TimeZoneMap::Lookup{linkName});
    MOZ_ASSERT(
        !p,
        "Duplicate entry in timezone::ianaLinksCanonicalizedDifferentlyByICU");

    if (!ianaLinksCanonicalizedDifferentlyByICU.add(p, linkName, target)) {
      ReportOutOfMemory(cx);
      return false;
    }
  }

  MOZ_ASSERT(!timeZoneDataInitialized,
             "ensureTimeZones is neither reentrant nor thread-safe");
  timeZoneDataInitialized = true;

  return true;
}

JSLinearString* js::intl::SharedIntlData::canonicalizeTimeZone(
    JSContext* cx, Handle<JSLinearString*> timeZone) {
  if (!ensureTimeZones(cx)) {
    return nullptr;
  }

  auto availablePtr =
      availableTimeZones.lookup(AvailableTimeZoneSet::Lookup{timeZone});
  MOZ_ASSERT(availablePtr.found(), "Invalid time zone name");

  Rooted<JSAtom*> availableTimeZone(cx, *availablePtr);
  return canonicalizeAvailableTimeZone(cx, availableTimeZone);
}

bool js::intl::SharedIntlData::validateAndCanonicalizeTimeZone(
    JSContext* cx, const AvailableTimeZoneSet::Lookup& lookup,
    MutableHandle<JSAtom*> identifier, MutableHandle<JSAtom*> primary) {
  MOZ_ASSERT(timeZoneDataInitialized);

  auto availablePtr = availableTimeZones.lookup(lookup);
  if (!availablePtr) {
    return true;
  }

  Rooted<JSAtom*> availableTimeZone(cx, *availablePtr);
  JSAtom* canonicalTimeZone =
      canonicalizeAvailableTimeZone(cx, availableTimeZone);
  if (!canonicalTimeZone) {
    return false;
  }

  cx->markAtom(availableTimeZone);
  MOZ_ASSERT(AtomIsMarked(cx->zone(), canonicalTimeZone),
             "canonicalizeAvailableTimeZone already marked the atom");

  identifier.set(availableTimeZone);
  primary.set(canonicalTimeZone);
  return true;
}

bool js::intl::SharedIntlData::validateAndCanonicalizeTimeZone(
    JSContext* cx, Handle<JSLinearString*> timeZone,
    MutableHandle<JSAtom*> identifier, MutableHandle<JSAtom*> primary) {
  if (!ensureTimeZones(cx)) {
    return false;
  }
  return validateAndCanonicalizeTimeZone(
      cx, AvailableTimeZoneSet::Lookup{timeZone}, identifier, primary);
}

bool js::intl::SharedIntlData::validateAndCanonicalizeTimeZone(
    JSContext* cx, mozilla::Span<const char> timeZone,
    MutableHandle<JSAtom*> identifier, MutableHandle<JSAtom*> primary) {
  if (!ensureTimeZones(cx)) {
    return false;
  }
  return validateAndCanonicalizeTimeZone(
      cx, AvailableTimeZoneSet::Lookup{{timeZone.data(), timeZone.size()}},
      identifier, primary);
}

JSAtom* js::intl::SharedIntlData::canonicalizeAvailableTimeZone(
    JSContext* cx, Handle<JSAtom*> availableTimeZone) {
  MOZ_ASSERT(timeZoneDataInitialized);
  MOZ_ASSERT(
      availableTimeZones.has(AvailableTimeZoneSet::Lookup{availableTimeZone}),
      "Invalid time zone name");

  // Some time zone names are canonicalized differently by ICU.
  auto* canonicalTimeZone =
      tryCanonicalizeTimeZoneConsistentWithIANA(availableTimeZone);
  if (canonicalTimeZone) {
    cx->markAtom(canonicalTimeZone);
    return canonicalTimeZone;
  }

  JS::AutoStableStringChars stableChars(cx);
  if (!stableChars.initTwoByte(cx, availableTimeZone)) {
    return nullptr;
  }

  using TimeZone = mozilla::intl::TimeZone;

  FormatBuffer<char16_t, TimeZone::TimeZoneIdentifierLength> buffer(cx);
  auto result =
      TimeZone::GetCanonicalTimeZoneID(stableChars.twoByteRange(), buffer);
  if (result.isErr()) {
    ReportInternalError(cx, result.unwrapErr());
    return nullptr;
  }

  std::u16string_view timeZone{buffer.data(), buffer.length()};
  MOZ_ASSERT(timeZone != u"Etc/Unknown", "Invalid canonical time zone");

  auto availablePtr =
      availableTimeZones.lookup(AvailableTimeZoneSet::Lookup{timeZone});
  MOZ_ASSERT(availablePtr, "Invalid time zone name");

  cx->markAtom(*availablePtr);
  return *availablePtr;
}

JSAtom* js::intl::SharedIntlData::tryCanonicalizeTimeZoneConsistentWithIANA(
    JSAtom* availableTimeZone) {
  MOZ_ASSERT(timeZoneDataInitialized);
  MOZ_ASSERT(
      availableTimeZones.has(AvailableTimeZoneSet::Lookup{availableTimeZone}),
      "Invalid time zone name");

  TimeZoneMap::Lookup lookup(availableTimeZone);
  if (TimeZoneMap::Ptr p =
          ianaLinksCanonicalizedDifferentlyByICU.lookup(lookup)) {
    // The effectively supported time zones aren't known at compile time,
    // when
    // 1. SpiderMonkey was compiled with "--with-system-icu".
    // 2. ICU's dynamic time zone data loading feature was used.
    //    (ICU supports loading time zone files at runtime through the
    //    ICU_TIMEZONE_FILES_DIR environment variable.)
    // Ensure ICU supports the new target zone before applying the update.
    TimeZoneName targetTimeZone = p->value();
    if (availableTimeZones.has(AvailableTimeZoneSet::Lookup{targetTimeZone})) {
      return targetTimeZone;
    }
  } else if (TimeZoneSet::Ptr p = ianaZonesTreatedAsLinksByICU.lookup(lookup)) {
    return *p;
  }
  return nullptr;
}

JS::Result<js::intl::SharedIntlData::AvailableTimeZoneSet::Iterator>
js::intl::SharedIntlData::availableTimeZonesIteration(JSContext* cx) {
  if (!ensureTimeZones(cx)) {
    return cx->alreadyReportedError();
  }
  return availableTimeZones.iter();
}

template <class AvailableLocales>
bool js::intl::SharedIntlData::getAvailableLocales(
    JSContext* cx, LocaleSet& locales,
    const AvailableLocales& availableLocales) {
  auto addLocale = [cx, &locales](LanguageId langId) {
    LocaleSet::AddPtr p = locales.lookupForAdd(langId);

    // ICU shouldn't report any duplicate locales, but if it does, just ignore
    // the duplicated locale.
    if (!p && !locales.add(p, langId)) {
      ReportOutOfMemory(cx);
      return false;
    }

    return true;
  };

  if (auto count = availableLocales.Count(); count > 0) {
    if (!locales.reserve(uint32_t(count))) {
      ReportOutOfMemory(cx);
      return false;
    }
  }

  for (auto locale : availableLocales) {
    auto parsedLangId = LanguageId::fromId(locale);

#if !MOZ_SYSTEM_ICU
    MOZ_ASSERT(parsedLangId.isSome(), "unparseable ICU locale identifier");
    MOZ_ASSERT(parsedLangId->second == 0,
               "ICU locale identifier with unexpected subtags");
#else
    // Skip over unexpected locale identifiers when using a system ICU.
    if (parsedLangId.isNothing() || parsedLangId->second > 0) {
      continue;
    }
#endif

    auto lang = parsedLangId->first;

    if (!addLocale(lang)) {
      return false;
    }

    // From <https://tc39.es/ecma402/#sec-internal-slots>:
    //
    // For locales that include a script subtag in addition to language and
    // region, the corresponding locale without a script subtag must also be
    // supported; that is, if an implementation recognizes "zh-Hant-TW", it is
    // also expected to recognize "zh-TW".

    if (lang.hasScript() && lang.hasRegion()) {
      if (!addLocale(lang.withoutScript())) {
        return false;
      }
    }
  }

  // Forcibly add an entry for the last-ditch locale, in case ICU doesn't
  // directly support it (but does support it through fallback, e.g. supporting
  // "en-GB" indirectly using "en" support).
  {
    static constexpr auto lastDitch = LastDitchLocale();
    static_assert(std::string_view{lastDitch.toString()} == "en-GB");

#ifdef DEBUG
    static constexpr auto lastDitchParent = lastDitch.parentLocale();
    static_assert(std::string_view{lastDitchParent.toString()} == "en");

    MOZ_ASSERT(locales.has(lastDitchParent),
               "shouldn't be a need to add every locale implied by the "
               "last-ditch locale, merely just the last-ditch locale");
#endif

    if (!addLocale(lastDitch)) {
      return false;
    }
  }

  return true;
}

#ifdef DEBUG
template <class AvailableLocales1, class AvailableLocales2>
static bool IsSameAvailableLocales(const AvailableLocales1& availableLocales1,
                                   const AvailableLocales2& availableLocales2) {
  return std::equal(
      std::begin(availableLocales1), std::end(availableLocales1),
      std::begin(availableLocales2), std::end(availableLocales2),
      [](mozilla::Span<const char> a, mozilla::Span<const char> b) {
        // Intentionally comparing pointer equivalence.
        return a.Elements() == b.Elements();
      });
}
#endif

bool js::intl::SharedIntlData::ensureAvailableLocales(JSContext* cx) {
  if (availableLocalesInitialized) {
    return true;
  }

  // If ensureAvailableLocales() was called previously, but didn't complete due
  // to OOM, clear all data and start from scratch.
  availableLocales.clearAndCompact();
  collatorAvailableLocales.clearAndCompact();

  if (!getAvailableLocales(cx, availableLocales,
                           mozilla::intl::Locale::GetAvailableLocales())) {
    return false;
  }
  if (!getAvailableLocales(cx, collatorAvailableLocales,
                           mozilla::intl::Collator::GetAvailableLocales())) {
    return false;
  }

  MOZ_ASSERT(IsSameAvailableLocales(
      mozilla::intl::Locale::GetAvailableLocales(),
      mozilla::intl::DateTimeFormat::GetAvailableLocales()));

  MOZ_ASSERT(IsSameAvailableLocales(
      mozilla::intl::Locale::GetAvailableLocales(),
      mozilla::intl::NumberFormat::GetAvailableLocales()));

  MOZ_ASSERT(!availableLocalesInitialized,
             "ensureAvailableLocales is neither reentrant nor thread-safe");
  availableLocalesInitialized = true;

  return true;
}

bool js::intl::SharedIntlData::isAvailableLocale(JSContext* cx,
                                                 AvailableLocaleKind kind,
                                                 LanguageId locale,
                                                 bool* available) {
  if (!ensureAvailableLocales(cx)) {
    return false;
  }

  switch (kind) {
    case AvailableLocaleKind::Collator:
      *available = collatorAvailableLocales.has(locale);
      return true;
    case AvailableLocaleKind::DateTimeFormat:
    case AvailableLocaleKind::DisplayNames:
    case AvailableLocaleKind::DurationFormat:
    case AvailableLocaleKind::ListFormat:
    case AvailableLocaleKind::NumberFormat:
    case AvailableLocaleKind::PluralRules:
    case AvailableLocaleKind::RelativeTimeFormat:
    case AvailableLocaleKind::Segmenter:
      *available = availableLocales.has(locale);
      return true;
  }
  MOZ_CRASH("Invalid Intl constructor");
}

js::ArrayObject* js::intl::SharedIntlData::availableLocalesOf(
    JSContext* cx, AvailableLocaleKind kind) {
  if (!ensureAvailableLocales(cx)) {
    return nullptr;
  }

  LocaleSet* localeSet = nullptr;
  switch (kind) {
    case AvailableLocaleKind::Collator:
      localeSet = &collatorAvailableLocales;
      break;
    case AvailableLocaleKind::DateTimeFormat:
    case AvailableLocaleKind::DisplayNames:
    case AvailableLocaleKind::DurationFormat:
    case AvailableLocaleKind::ListFormat:
    case AvailableLocaleKind::NumberFormat:
    case AvailableLocaleKind::PluralRules:
    case AvailableLocaleKind::RelativeTimeFormat:
    case AvailableLocaleKind::Segmenter:
      localeSet = &availableLocales;
      break;
    default:
      MOZ_CRASH("Invalid Intl constructor");
  }

  const uint32_t count = localeSet->count();
  Rooted<ArrayObject*> result(cx, NewDenseFullyAllocatedArray(cx, count));
  if (!result) {
    return nullptr;
  }
  result->ensureDenseInitializedLength(0, count);

  uint32_t index = 0;
  for (auto range = localeSet->iter(); !range.done(); range.next()) {
    auto langIdStr = range.get().toString();
    auto* locale = NewStringCopy<CanGC>(cx, std::string_view{langIdStr});
    if (!locale) {
      return nullptr;
    }

    result->initDenseElement(index++, StringValue(locale));
  }
  MOZ_ASSERT(index == count);

  return result;
}

void js::intl::DateTimePatternGeneratorDeleter::operator()(
    mozilla::intl::DateTimePatternGenerator* ptr) {
  delete ptr;
}

static bool StringsAreEqual(const char* s1, const char* s2) {
  return !strcmp(s1, s2);
}

mozilla::intl::DateTimePatternGenerator*
js::intl::SharedIntlData::getDateTimePatternGenerator(JSContext* cx,
                                                      const char* locale) {
  // Return the cached instance if the requested locale matches the locale
  // of the cached generator.
  if (dateTimePatternGeneratorLocale &&
      StringsAreEqual(dateTimePatternGeneratorLocale.get(), locale)) {
    return dateTimePatternGenerator.get();
  }

  auto result = mozilla::intl::DateTimePatternGenerator::TryCreate(locale);
  if (result.isErr()) {
    ReportInternalError(cx, result.unwrapErr());
    return nullptr;
  }
  // The UniquePtr needs to be recreated as it's using a different Deleter in
  // order to be able to forward declare DateTimePatternGenerator in
  // SharedIntlData.h.
  UniqueDateTimePatternGenerator gen(result.unwrap().release());

  JS::UniqueChars localeCopy = js::DuplicateString(cx, locale);
  if (!localeCopy) {
    return nullptr;
  }

  dateTimePatternGenerator = std::move(gen);
  dateTimePatternGeneratorLocale = std::move(localeCopy);

  return dateTimePatternGenerator.get();
}

void js::intl::SharedIntlData::destroyInstance() {
  availableTimeZones.clearAndCompact();
  ianaZonesTreatedAsLinksByICU.clearAndCompact();
  ianaLinksCanonicalizedDifferentlyByICU.clearAndCompact();
  availableLocales.clearAndCompact();
  collatorAvailableLocales.clearAndCompact();
}

void js::intl::SharedIntlData::trace(JSTracer* trc) {
  // Atoms are always tenured.
  if (!JS::RuntimeHeapIsMinorCollecting()) {
    availableTimeZones.trace(trc);
    ianaZonesTreatedAsLinksByICU.trace(trc);
    ianaLinksCanonicalizedDifferentlyByICU.trace(trc);
  }
}

size_t js::intl::SharedIntlData::sizeOfExcludingThis(
    mozilla::MallocSizeOf mallocSizeOf) const {
  return availableTimeZones.shallowSizeOfExcludingThis(mallocSizeOf) +
         ianaZonesTreatedAsLinksByICU.shallowSizeOfExcludingThis(mallocSizeOf) +
         ianaLinksCanonicalizedDifferentlyByICU.shallowSizeOfExcludingThis(
             mallocSizeOf) +
         availableLocales.shallowSizeOfExcludingThis(mallocSizeOf) +
         collatorAvailableLocales.shallowSizeOfExcludingThis(mallocSizeOf) +
         mallocSizeOf(dateTimePatternGeneratorLocale.get());
}
