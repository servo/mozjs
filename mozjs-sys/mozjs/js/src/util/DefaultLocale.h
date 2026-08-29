/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef util_DefaultLocale_h
#define util_DefaultLocale_h

#include <string_view>

namespace js {
class LanguageId;

/**
 * Return the system default locale as a language identifier, or, on failure,
 * the undetermined locale "und".
 */
LanguageId SystemDefaultLocale();

/**
 * Parse `localeId` as a Unicode BCP 47 locale identifier, canonicalize the
 * parsed locale, and then return it as a `LanguageId`. Return the undetermined
 * locale "und" if the input is not parseable or canonicalization failed.
 */
LanguageId DefaultLocaleFrom(std::string_view localeId);
}  // namespace js

#endif /* util_DefaultLocale_h */
