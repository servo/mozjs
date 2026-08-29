/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "util/DefaultLocale.h"

#include "mozilla/Assertions.h"
#if JS_HAS_INTL_API
#  include "mozilla/intl/Locale.h"
#endif
#include "mozilla/Span.h"

#include <clocale>
#include <cstring>
#include <string_view>

#include "js/GCAPI.h"
#include "util/LanguageId.h"

using namespace js;

static std::string_view SystemDefaultLocale() {
#ifdef JS_HAS_INTL_API
  // Use ICU if available to retrieve the default locale, this ensures ICU's
  // default locale matches our default locale.
  return mozilla::intl::Locale::GetDefaultLocale();
#else
  const char* loc = std::setlocale(LC_ALL, nullptr);

  // Convert to a well-formed BCP 47 language tag.
  if (!loc || !std::strcmp(loc, "C")) {
    loc = "und";
  }

  std::string_view locale{loc};

  // Remove optional code page from the locale string.
  return locale.substr(0, locale.find('.'));
#endif
}

#ifdef JS_HAS_INTL_API
/**
 * Create a LanguageId from a `mozilla::intl::Locale`.
 */
static LanguageId ToLanguageId(const mozilla::intl::Locale& locale) {
  MOZ_ASSERT(locale.Language().Length() <= 3, "unexpected overlong language");

  auto toStringView = [](const auto& subtag) -> std::string_view {
    auto span = subtag.Span();
    return {span.data(), span.size()};
  };

  auto language = toStringView(locale.Language());
  auto script = toStringView(locale.Script());
  auto region = toStringView(locale.Region());

  return LanguageId::fromParts(language, script, region);
}

/**
 * Canonicalize a LanguageId using `mozilla::intl::Locale`.
 */
static auto CanonicalizeLocale(LanguageId langId) {
  mozilla::intl::LanguageSubtag language{mozilla::Span{langId.language()}};
  mozilla::intl::ScriptSubtag script{mozilla::Span{langId.script()}};
  mozilla::intl::RegionSubtag region{mozilla::Span{langId.region()}};

  mozilla::intl::Locale locale{};
  locale.SetLanguage(language);
  locale.SetScript(script);
  locale.SetRegion(region);

  auto result = locale.CanonicalizeBaseName();
  MOZ_RELEASE_ASSERT(
      result.isOk(),
      "canonicalization is infallible when no variant subtags are present");

  return ToLanguageId(locale);
}
#endif

LanguageId js::SystemDefaultLocale() {
  // Tell the analysis this function can't GC. (bug 1588528)
  JS::AutoSuppressGCAnalysis nogc;

  auto parsed = LanguageId::fromId(::SystemDefaultLocale());

  // Ignore any subtags after the (language, script, region) subtags triple.
  if (parsed) {
#ifdef JS_HAS_INTL_API
    // Return canonicalized locale if Intl API is available.
    return CanonicalizeLocale(parsed->first);
#else
    return parsed->first;
#endif
  }

  // Unknow system default locale.
  return LanguageId::und();
}

LanguageId js::DefaultLocaleFrom(std::string_view localeId) {
  // Tell the analysis this function can't GC. (bug 1588528)
  JS::AutoSuppressGCAnalysis nogc;

  auto parsed = LanguageId::fromBcp49(localeId);

#ifdef JS_HAS_INTL_API
  // Handle the common case first:
  // 1. The language, script, and region subtags are in canonical case.
  // 2. No additional variant or extension subtags are present.
  if (parsed && parsed->second == 0) {
    // Return canonicalized locale if Intl API is available.
    return CanonicalizeLocale(parsed->first);
  }

  // Slow path: Use the LocaleParser to parse and validate the complete input.
  mozilla::intl::Locale locale;
  bool canParseLocale = mozilla::intl::LocaleParser::TryParse(
                            mozilla::Span<const char>{localeId}, locale)
                            .isOk();
  if (canParseLocale) {
    // Remove variant subtags, because no available ICU locale contains any.
    locale.ClearVariants();

    auto result = locale.CanonicalizeBaseName();
    MOZ_RELEASE_ASSERT(
        result.isOk(),
        "canonicalization is infallible when no variant subtags are present");

    // Reject overlong language subtags which don't fit into `LanguageId`.
    if (locale.Language().Length() <= 3) {
      return ToLanguageId(locale);
    }
  }
#else
  // We don't perform any additional validation when the Intl API is disabled.
  if (parsed) {
    return parsed->first;
  }
#endif

  // Unparseable Unicode BCP 47 locale identifier.
  return LanguageId::und();
}
