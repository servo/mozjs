/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_temporal_Era_h
#define builtin_temporal_Era_h

#include "mozilla/Assertions.h"

#include <initializer_list>
#include <string_view>

#include "jstypes.h"

#include "builtin/temporal/Calendar.h"

namespace js::temporal {

enum class EraCode {
  // The standard era of a calendar.
  Standard,

  // The era before the standard era of a calendar.
  Inverse,

  // Named Japanese eras.
  Meiji,
  Taisho,
  Showa,
  Heisei,
  Reiwa,
};

// static variables in constexpr functions requires C++23 support, so we can't
// declare the eras directly in CalendarEras.
namespace eras {
inline constexpr auto Standard = {EraCode::Standard};

inline constexpr auto StandardInverse = {EraCode::Standard, EraCode::Inverse};

inline constexpr auto Japanese = {
    EraCode::Standard, EraCode::Inverse,

    EraCode::Meiji,    EraCode::Taisho,  EraCode::Showa,
    EraCode::Heisei,   EraCode::Reiwa,
};

// https://tc39.es/proposal-intl-era-monthcode/#table-eras
//
// Calendars which don't use eras were omitted.
namespace names {
using namespace std::literals;

// Empty placeholder.
inline constexpr auto Empty = {
    ""sv,
};

inline constexpr auto Buddhist = {
    "be"sv,
};

inline constexpr auto Coptic = {
    "am"sv,
};

inline constexpr auto EthiopianAmeteAlem = {
    "aa"sv,
};

inline constexpr auto Ethiopian = {
    "am"sv,
};

inline constexpr auto Gregorian = {
    "ce"sv,
    "ad"sv,
};

inline constexpr auto GregorianInverse = {
    "bce"sv,
    "bc"sv,
};

inline constexpr auto Hebrew = {
    "am"sv,
};

inline constexpr auto Indian = {
    "shaka"sv,
};

inline constexpr auto Islamic = {
    "ah"sv,
};

inline constexpr auto IslamicInverse = {
    "bh"sv,
};

inline constexpr auto JapaneseMeiji = {
    "meiji"sv,
};

inline constexpr auto JapaneseTaisho = {
    "taisho"sv,
};

inline constexpr auto JapaneseShowa = {
    "showa"sv,
};

inline constexpr auto JapaneseHeisei = {
    "heisei"sv,
};

inline constexpr auto JapaneseReiwa = {
    "reiwa"sv,
};

inline constexpr auto Persian = {
    "ap"sv,
};

inline constexpr auto ROC = {
    "roc"sv,
};

inline constexpr auto ROCInverse = {
    "broc"sv,
};
}  // namespace names
}  // namespace eras

constexpr auto& CalendarEras(CalendarId calendar) {
  switch (calendar) {
    case CalendarId::ISO8601:
    case CalendarId::Buddhist:
    case CalendarId::Chinese:
    case CalendarId::Coptic:
    case CalendarId::Dangi:
    case CalendarId::EthiopianAmeteAlem:
    case CalendarId::Hebrew:
    case CalendarId::Indian:
    case CalendarId::Persian:
      return eras::Standard;

    case CalendarId::Ethiopian:
    case CalendarId::Gregorian:
    case CalendarId::IslamicCivil:
    case CalendarId::IslamicTabular:
    case CalendarId::IslamicUmmAlQura:
    case CalendarId::ROC:
      return eras::StandardInverse;

    case CalendarId::Japanese:
      return eras::Japanese;
  }
  MOZ_CRASH("invalid calendar id");
}

/**
 * CalendarSupportsEra ( calendar )
 */
constexpr bool CalendarSupportsEra(CalendarId calendar) {
  switch (calendar) {
    case CalendarId::ISO8601:
    case CalendarId::Chinese:
    case CalendarId::Dangi:
      return false;

    case CalendarId::Buddhist:
    case CalendarId::Coptic:
    case CalendarId::Ethiopian:
    case CalendarId::EthiopianAmeteAlem:
    case CalendarId::Hebrew:
    case CalendarId::Indian:
    case CalendarId::Persian:
    case CalendarId::Gregorian:
    case CalendarId::IslamicCivil:
    case CalendarId::IslamicTabular:
    case CalendarId::IslamicUmmAlQura:
    case CalendarId::ROC:
    case CalendarId::Japanese:
      return true;
  }
  MOZ_CRASH("invalid calendar id");
}

constexpr auto& CalendarEraNames(CalendarId calendar, EraCode era) {
  switch (calendar) {
    case CalendarId::ISO8601:
    case CalendarId::Chinese:
    case CalendarId::Dangi:
      MOZ_ASSERT(era == EraCode::Standard);
      return eras::names::Empty;

    case CalendarId::Buddhist:
      MOZ_ASSERT(era == EraCode::Standard);
      return eras::names::Buddhist;

    case CalendarId::Coptic:
      MOZ_ASSERT(era == EraCode::Standard);
      return eras::names::Coptic;

    case CalendarId::Ethiopian:
      MOZ_ASSERT(era == EraCode::Standard || era == EraCode::Inverse);
      return era == EraCode::Standard ? eras::names::Ethiopian
                                      : eras::names::EthiopianAmeteAlem;

    case CalendarId::EthiopianAmeteAlem:
      MOZ_ASSERT(era == EraCode::Standard);
      return eras::names::EthiopianAmeteAlem;

    case CalendarId::Hebrew:
      MOZ_ASSERT(era == EraCode::Standard);
      return eras::names::Hebrew;

    case CalendarId::Indian:
      MOZ_ASSERT(era == EraCode::Standard);
      return eras::names::Indian;

    case CalendarId::Persian:
      MOZ_ASSERT(era == EraCode::Standard);
      return eras::names::Persian;

    case CalendarId::Gregorian: {
      MOZ_ASSERT(era == EraCode::Standard || era == EraCode::Inverse);
      return era == EraCode::Standard ? eras::names::Gregorian
                                      : eras::names::GregorianInverse;
    }

    case CalendarId::IslamicCivil:
    case CalendarId::IslamicTabular:
    case CalendarId::IslamicUmmAlQura: {
      MOZ_ASSERT(era == EraCode::Standard || era == EraCode::Inverse);
      return era == EraCode::Standard ? eras::names::Islamic
                                      : eras::names::IslamicInverse;
    }

    case CalendarId::Japanese: {
      switch (era) {
        case EraCode::Standard:
          return eras::names::Gregorian;
        case EraCode::Inverse:
          return eras::names::GregorianInverse;
        case EraCode::Meiji:
          return eras::names::JapaneseMeiji;
        case EraCode::Taisho:
          return eras::names::JapaneseTaisho;
        case EraCode::Showa:
          return eras::names::JapaneseShowa;
        case EraCode::Heisei:
          return eras::names::JapaneseHeisei;
        case EraCode::Reiwa:
          return eras::names::JapaneseReiwa;
      }
      break;
    }

    case CalendarId::ROC: {
      MOZ_ASSERT(era == EraCode::Standard || era == EraCode::Inverse);
      return era == EraCode::Standard ? eras::names::ROC
                                      : eras::names::ROCInverse;
    }
  }
  MOZ_CRASH("invalid era");
}

constexpr auto CalendarEraName(CalendarId calendar, EraCode era) {
  const auto& names = CalendarEraNames(calendar, era);
  MOZ_ASSERT(names.size() > 0);
  return *names.begin();
}

/**
 * CalendarHasMidYearEras ( calendar )
 */
constexpr bool CalendarHasMidYearEras(CalendarId calendar) {
  // Steps 1-2.
  //
  // Japanese eras can start in the middle of the year. All other calendars
  // start their eras at year boundaries. (Or don't have eras at all.)
  return calendar == CalendarId::Japanese;
}

}  // namespace js::temporal

#endif /* builtin_temporal_Era_h */
