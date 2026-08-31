/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "builtin/temporal/Calendar.h"

#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/Casting.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/EnumSet.h"
#include "mozilla/FloatingPoint.h"
#include "mozilla/intl/Locale.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/Maybe.h"
#include "mozilla/Result.h"
#include "mozilla/ResultVariant.h"
#include "mozilla/Span.h"
#include "mozilla/TextUtils.h"
#include "mozilla/UniquePtr.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stddef.h>
#include <stdint.h>

#include "jstypes.h"
#include "NamespaceImports.h"

#include "builtin/Number.h"
#include "builtin/temporal/CalendarFields.h"
#include "builtin/temporal/Duration.h"
#include "builtin/temporal/Era.h"
#include "builtin/temporal/MonthCode.h"
#include "builtin/temporal/PlainDate.h"
#include "builtin/temporal/PlainDateTime.h"
#include "builtin/temporal/PlainMonthDay.h"
#include "builtin/temporal/PlainTime.h"
#include "builtin/temporal/PlainYearMonth.h"
#include "builtin/temporal/Temporal.h"
#include "builtin/temporal/TemporalParser.h"
#include "builtin/temporal/TemporalRoundingMode.h"
#include "builtin/temporal/TemporalTypes.h"
#include "builtin/temporal/TemporalUnit.h"
#include "builtin/temporal/ZonedDateTime.h"
#include "gc/Barrier.h"
#include "gc/GCEnum.h"
#include "icu4x/Calendar.hpp"
#include "icu4x/Date.hpp"
#include "icu4x/diplomat_runtime.hpp"
#include "icu4x/IsoDate.hpp"
#include "js/AllocPolicy.h"
#include "js/ErrorReport.h"
#include "js/friend/ErrorMessages.h"
#include "js/Printer.h"
#include "js/RootingAPI.h"
#include "js/TracingAPI.h"
#include "js/Value.h"
#include "js/Vector.h"
#include "util/Text.h"
#include "vm/BytecodeUtil.h"
#include "vm/Compartment.h"
#include "vm/JSAtomState.h"
#include "vm/JSContext.h"
#include "vm/StringType.h"

#include "vm/Compartment-inl.h"
#include "vm/JSContext-inl.h"
#include "vm/JSObject-inl.h"
#include "vm/ObjectOperations-inl.h"

// diplomat_simple_write isn't defined in C++ headers, but we have to use it to
// avoid memory allocation.
// (https://github.com/rust-diplomat/diplomat/issues/866)
namespace diplomat::capi {
extern "C" icu4x::diplomat::capi::DiplomatWrite diplomat_simple_write(
    char* buf, size_t buf_size);
}

using namespace js;
using namespace js::temporal;

void js::temporal::CalendarValue::trace(JSTracer* trc) {
  TraceRoot(trc, &value_, "CalendarValue::value");
}

bool js::temporal::WrapCalendarValue(JSContext* cx,
                                     MutableHandle<JS::Value> calendar) {
  MOZ_ASSERT(calendar.isInt32());
  return cx->compartment()->wrap(cx, calendar);
}

/**
 * IsISOLeapYear ( year )
 */
static constexpr bool IsISOLeapYear(int32_t year) {
  // Steps 1-5.
  int32_t d = (year % 100 != 0) ? 4 : 16;
  return (year & (d - 1)) == 0;
}

/**
 * ISODaysInYear ( year )
 */
static int32_t ISODaysInYear(int32_t year) {
  // Steps 1-3.
  return IsISOLeapYear(year) ? 366 : 365;
}

/**
 * ISODaysInMonth ( year, month )
 */
static constexpr int32_t ISODaysInMonth(int32_t year, int32_t month) {
  MOZ_ASSERT(1 <= month && month <= 12);

  constexpr uint8_t daysInMonth[2][13] = {
      {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
      {0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}};

  // Steps 1-4.
  return daysInMonth[IsISOLeapYear(year)][month];
}

/**
 * ISODaysInMonth ( year, month )
 */
int32_t js::temporal::ISODaysInMonth(int32_t year, int32_t month) {
  return ::ISODaysInMonth(year, month);
}

/**
 * 21.4.1.6 Week Day
 *
 * Compute the week day from |day| without first expanding |day| into a full
 * date through |MakeDate(day, 0)|:
 *
 *   WeekDay(MakeDate(day, 0))
 * = WeekDay(day × msPerDay + 0)
 * = WeekDay(day × msPerDay)
 * = 𝔽(ℝ(Day(day × msPerDay) + 4𝔽) modulo 7)
 * = 𝔽(ℝ(𝔽(floor(ℝ((day × msPerDay) / msPerDay))) + 4𝔽) modulo 7)
 * = 𝔽(ℝ(𝔽(floor(ℝ(day))) + 4𝔽) modulo 7)
 * = 𝔽(ℝ(𝔽(day) + 4𝔽) modulo 7)
 */
static int32_t WeekDay(int32_t day) {
  int32_t result = (day + 4) % 7;
  if (result < 0) {
    result += 7;
  }
  return result;
}

/**
 * ISODayOfWeek ( isoDate )
 */
static int32_t ISODayOfWeek(const ISODate& isoDate) {
  MOZ_ASSERT(IsValidISODate(isoDate));

  // Step 1.
  int32_t day = MakeDay(isoDate);

  // Step 2.
  int32_t dayOfWeek = WeekDay(day);

  // Steps 3-4.
  return dayOfWeek != 0 ? dayOfWeek : 7;
}

static constexpr auto FirstDayOfMonth(int32_t year) {
  // The following array contains the day of year for the first day of each
  // month, where index 0 is January, and day 0 is January 1.
  std::array<int32_t, 13> days = {};
  for (int32_t month = 1; month <= 12; ++month) {
    days[month] = days[month - 1] + ::ISODaysInMonth(year, month);
  }
  return days;
}

/**
 * ISODayOfYear ( isoDate )
 */
static int32_t ISODayOfYear(const ISODate& isoDate) {
  MOZ_ASSERT(IsValidISODate(isoDate));

  const auto& [year, month, day] = isoDate;

  // First day of month arrays for non-leap and leap years.
  constexpr decltype(FirstDayOfMonth(0)) firstDayOfMonth[2] = {
      FirstDayOfMonth(1), FirstDayOfMonth(0)};

  // Steps 1-2.
  //
  // Instead of first computing the date and then using DayWithinYear to map the
  // date to the day within the year, directly lookup the first day of the month
  // and then add the additional days.
  return firstDayOfMonth[IsISOLeapYear(year)][month - 1] + day;
}

static int32_t FloorDiv(int32_t dividend, int32_t divisor) {
  MOZ_ASSERT(divisor > 0);

  int32_t quotient = dividend / divisor;
  int32_t remainder = dividend % divisor;
  if (remainder < 0) {
    quotient -= 1;
  }
  return quotient;
}

/**
 * 21.4.1.3 Year Number, DayFromYear
 */
static int32_t DayFromYear(int32_t year) {
  return 365 * (year - 1970) + FloorDiv(year - 1969, 4) -
         FloorDiv(year - 1901, 100) + FloorDiv(year - 1601, 400);
}

/**
 * 21.4.1.11 MakeTime ( hour, min, sec, ms )
 */
static int64_t MakeTime(const Time& time) {
  MOZ_ASSERT(IsValidTime(time));

  // Step 1 (Not applicable).

  // Step 2.
  int64_t h = time.hour;

  // Step 3.
  int64_t m = time.minute;

  // Step 4.
  int64_t s = time.second;

  // Step 5.
  int64_t milli = time.millisecond;

  // Steps 6-7.
  return h * ToMilliseconds(TemporalUnit::Hour) +
         m * ToMilliseconds(TemporalUnit::Minute) +
         s * ToMilliseconds(TemporalUnit::Second) + milli;
}

/**
 * 21.4.1.12 MakeDay ( year, month, date )
 */
int32_t js::temporal::MakeDay(const ISODate& date) {
  MOZ_ASSERT(IsValidISODate(date));

  return DayFromYear(date.year) + ISODayOfYear(date) - 1;
}

/**
 * 21.4.1.13 MakeDate ( day, time )
 */
int64_t js::temporal::MakeDate(const ISODateTime& dateTime) {
  MOZ_ASSERT(IsValidISODateTime(dateTime));

  // Step 1 (Not applicable).

  // Steps 2-3.
  int64_t tv = MakeDay(dateTime.date) * ToMilliseconds(TemporalUnit::Day) +
               MakeTime(dateTime.time);

  // Step 4.
  return tv;
}

struct YearWeek final {
  int32_t year = 0;
  int32_t week = 0;
};

/**
 * ISOWeekOfYear ( isoDate )
 */
static YearWeek ISOWeekOfYear(const ISODate& isoDate) {
  MOZ_ASSERT(IsValidISODate(isoDate));

  // Step 1.
  int32_t year = isoDate.year;

  // Step 2-7. (Not applicable in our implementation.)

  // Steps 8-9.
  int32_t dayOfYear = ISODayOfYear(isoDate);
  int32_t dayOfWeek = ISODayOfWeek(isoDate);

  // Step 10.
  int32_t week = (10 + dayOfYear - dayOfWeek) / 7;
  MOZ_ASSERT(0 <= week && week <= 53);

  // An ISO year has 53 weeks if the year starts on a Thursday or if it's a
  // leap year which starts on a Wednesday.
  auto isLongYear = [](int32_t year) {
    int32_t startOfYear = ISODayOfWeek({year, 1, 1});
    return startOfYear == 4 || (startOfYear == 3 && IsISOLeapYear(year));
  };

  // Step 11.
  //
  // Part of last year's last week, which is either week 52 or week 53.
  if (week == 0) {
    return {year - 1, 52 + int32_t(isLongYear(year - 1))};
  }

  // Step 12.
  //
  // Part of next year's first week if the current year isn't a long year.
  if (week == 53 && !isLongYear(year)) {
    return {year + 1, 1};
  }

  // Step 13.
  return {year, week};
}

/**
 * ToTemporalCalendarIdentifier ( calendarSlotValue )
 */
std::string_view js::temporal::CalendarIdentifier(CalendarId calendarId) {
  switch (calendarId) {
    case CalendarId::ISO8601:
      return "iso8601";
    case CalendarId::Buddhist:
      return "buddhist";
    case CalendarId::Chinese:
      return "chinese";
    case CalendarId::Coptic:
      return "coptic";
    case CalendarId::Dangi:
      return "dangi";
    case CalendarId::Ethiopian:
      return "ethiopic";
    case CalendarId::EthiopianAmeteAlem:
      return "ethioaa";
    case CalendarId::Gregorian:
      return "gregory";
    case CalendarId::Hebrew:
      return "hebrew";
    case CalendarId::Indian:
      return "indian";
    case CalendarId::IslamicCivil:
      return "islamic-civil";
    case CalendarId::IslamicTabular:
      return "islamic-tbla";
    case CalendarId::IslamicUmmAlQura:
      return "islamic-umalqura";
    case CalendarId::Japanese:
      return "japanese";
    case CalendarId::Persian:
      return "persian";
    case CalendarId::ROC:
      return "roc";
  }
  MOZ_CRASH("invalid calendar id");
}

class MOZ_STACK_CLASS AsciiLowerCaseChars final {
  static constexpr size_t InlineCapacity = 24;

  Vector<char, InlineCapacity> chars_;

 public:
  explicit AsciiLowerCaseChars(JSContext* cx) : chars_(cx) {}

  operator mozilla::Span<const char>() const {
    return mozilla::Span<const char>{chars_};
  }

  [[nodiscard]] bool init(JSLinearString* str) {
    MOZ_ASSERT(StringIsAscii(str));

    if (!chars_.resize(str->length())) {
      return false;
    }

    CopyChars(reinterpret_cast<JS::Latin1Char*>(chars_.begin()), *str);

    mozilla::intl::AsciiToLowerCase(chars_.begin(), chars_.length(),
                                    chars_.begin());

    return true;
  }
};

/**
 * CanonicalizeCalendar ( id )
 */
bool js::temporal::CanonicalizeCalendar(JSContext* cx, Handle<JSString*> id,
                                        MutableHandle<CalendarValue> result) {
  Rooted<JSLinearString*> linear(cx, id->ensureLinear(cx));
  if (!linear) {
    return false;
  }

  // Steps 1-3.
  do {
    if (!StringIsAscii(linear) || linear->empty()) {
      break;
    }

    AsciiLowerCaseChars lowerCaseChars(cx);
    if (!lowerCaseChars.init(linear)) {
      return false;
    }
    mozilla::Span<const char> id = lowerCaseChars;

    // Reject invalid types before trying to resolve aliases.
    if (mozilla::intl::LocaleParser::CanParseUnicodeExtensionType(id).isErr()) {
      break;
    }

    // Resolve calendar aliases.
    static constexpr auto key = mozilla::MakeStringSpan("ca");
    if (const char* replacement =
            mozilla::intl::Locale::ReplaceUnicodeExtensionType(key, id)) {
      id = mozilla::MakeStringSpan(replacement);
    }

    // Step 1.
    static constexpr const auto& calendars = AvailableCalendars();

    // Steps 2-3.
    for (auto identifier : calendars) {
      if (id == mozilla::Span{CalendarIdentifier(identifier)}) {
        result.set(CalendarValue(identifier));
        return true;
      }
    }
  } while (false);

  if (auto chars = QuoteString(cx, linear)) {
    JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr,
                             JSMSG_TEMPORAL_CALENDAR_INVALID_ID, chars.get());
  }
  return false;
}

template <typename T, typename... Ts>
static bool ToTemporalCalendar(JSContext* cx, Handle<JSObject*> object,
                               MutableHandle<CalendarValue> result) {
  if (auto* unwrapped = object->maybeUnwrapIf<T>()) {
    result.set(unwrapped->calendar());
    return result.wrap(cx);
  }

  if constexpr (sizeof...(Ts) > 0) {
    return ToTemporalCalendar<Ts...>(cx, object, result);
  }

  result.set(CalendarValue());
  return true;
}

/**
 * ToTemporalCalendarSlotValue ( temporalCalendarLike )
 */
bool js::temporal::ToTemporalCalendar(JSContext* cx,
                                      Handle<Value> temporalCalendarLike,
                                      MutableHandle<CalendarValue> result) {
  // Step 1.
  if (temporalCalendarLike.isObject()) {
    Rooted<JSObject*> obj(cx, &temporalCalendarLike.toObject());

    // Step 1.a.
    Rooted<CalendarValue> calendar(cx);
    if (!::ToTemporalCalendar<PlainDateObject, PlainDateTimeObject,
                              PlainMonthDayObject, PlainYearMonthObject,
                              ZonedDateTimeObject>(cx, obj, &calendar)) {
      return false;
    }
    if (calendar) {
      result.set(calendar);
      return true;
    }
  }

  // Step 2.
  if (!temporalCalendarLike.isString()) {
    ReportValueError(cx, JSMSG_UNEXPECTED_TYPE, JSDVG_IGNORE_STACK,
                     temporalCalendarLike, nullptr, "not a string");
    return false;
  }
  Rooted<JSString*> str(cx, temporalCalendarLike.toString());

  // Step 3.
  Rooted<JSLinearString*> id(cx, ParseTemporalCalendarString(cx, str));
  if (!id) {
    return false;
  }

  // Step 4.
  return CanonicalizeCalendar(cx, id, result);
}

/**
 * GetTemporalCalendarSlotValueWithISODefault ( item )
 */
bool js::temporal::GetTemporalCalendarWithISODefault(
    JSContext* cx, Handle<JSObject*> item,
    MutableHandle<CalendarValue> result) {
  // Step 1.
  Rooted<CalendarValue> calendar(cx);
  if (!::ToTemporalCalendar<PlainDateObject, PlainDateTimeObject,
                            PlainMonthDayObject, PlainYearMonthObject,
                            ZonedDateTimeObject>(cx, item, &calendar)) {
    return false;
  }
  if (calendar) {
    result.set(calendar);
    return true;
  }

  // Step 2.
  Rooted<Value> calendarValue(cx);
  if (!GetProperty(cx, item, item, cx->names().calendar, &calendarValue)) {
    return false;
  }

  // Step 3.
  if (calendarValue.isUndefined()) {
    result.set(CalendarValue(CalendarId::ISO8601));
    return true;
  }

  // Step 4.
  return ToTemporalCalendar(cx, calendarValue, result);
}

static inline int32_t OrdinalMonth(const icu4x::capi::Date* date) {
  int32_t month = icu4x::capi::icu4x_Date_ordinal_month_mv1(date);
  MOZ_ASSERT(month > 0);
  return month;
}

static inline int32_t DayOfMonth(const icu4x::capi::Date* date) {
  int32_t dayOfMonth = icu4x::capi::icu4x_Date_day_of_month_mv1(date);
  MOZ_ASSERT(dayOfMonth > 0);
  return dayOfMonth;
}

static inline int32_t DayOfYear(const icu4x::capi::Date* date) {
  int32_t dayOfYear = icu4x::capi::icu4x_Date_day_of_year_mv1(date);
  MOZ_ASSERT(dayOfYear > 0);
  return dayOfYear;
}

static inline int32_t DaysInMonth(const icu4x::capi::Date* date) {
  int32_t daysInMonth = icu4x::capi::icu4x_Date_days_in_month_mv1(date);
  MOZ_ASSERT(daysInMonth > 0);
  return daysInMonth;
}

static inline int32_t DaysInYear(const icu4x::capi::Date* date) {
  int32_t daysInYear = icu4x::capi::icu4x_Date_days_in_year_mv1(date);
  MOZ_ASSERT(daysInYear > 0);
  return daysInYear;
}

static inline int32_t MonthsInYear(const icu4x::capi::Date* date) {
  int32_t monthsInYear = icu4x::capi::icu4x_Date_months_in_year_mv1(date);
  MOZ_ASSERT(monthsInYear > 0);
  return monthsInYear;
}

static auto ToAnyCalendarKind(CalendarId id) {
  switch (id) {
    case CalendarId::ISO8601:
      return icu4x::capi::CalendarKind_Iso;
    case CalendarId::Buddhist:
      return icu4x::capi::CalendarKind_Buddhist;
    case CalendarId::Chinese:
      return icu4x::capi::CalendarKind_Chinese;
    case CalendarId::Coptic:
      return icu4x::capi::CalendarKind_Coptic;
    case CalendarId::Dangi:
      return icu4x::capi::CalendarKind_Dangi;
    case CalendarId::Ethiopian:
      return icu4x::capi::CalendarKind_Ethiopian;
    case CalendarId::EthiopianAmeteAlem:
      return icu4x::capi::CalendarKind_EthiopianAmeteAlem;
    case CalendarId::Gregorian:
      return icu4x::capi::CalendarKind_Gregorian;
    case CalendarId::Hebrew:
      return icu4x::capi::CalendarKind_Hebrew;
    case CalendarId::Indian:
      return icu4x::capi::CalendarKind_Indian;
    case CalendarId::IslamicCivil:
      return icu4x::capi::CalendarKind_HijriTabularTypeIIFriday;
    case CalendarId::IslamicTabular:
      return icu4x::capi::CalendarKind_HijriTabularTypeIIThursday;
    case CalendarId::IslamicUmmAlQura:
      return icu4x::capi::CalendarKind_HijriUmmAlQura;
    case CalendarId::Japanese:
      return icu4x::capi::CalendarKind_Japanese;
    case CalendarId::Persian:
      return icu4x::capi::CalendarKind_Persian;
    case CalendarId::ROC:
      return icu4x::capi::CalendarKind_Roc;
  }
  MOZ_CRASH("invalid calendar id");
}

class ICU4XCalendarDeleter {
 public:
  void operator()(icu4x::capi::Calendar* ptr) {
    icu4x::capi::icu4x_Calendar_destroy_mv1(ptr);
  }
};

using UniqueICU4XCalendar =
    mozilla::UniquePtr<icu4x::capi::Calendar, ICU4XCalendarDeleter>;

static UniqueICU4XCalendar CreateICU4XCalendar(CalendarId id) {
  auto* result = icu4x::capi::icu4x_Calendar_create_mv1(ToAnyCalendarKind(id));
  MOZ_ASSERT(result, "unexpected null-pointer result");
  return UniqueICU4XCalendar{result};
}

// Passing values near INT32_{MIN,MAX} triggers ICU4X assertions, so we have to
// handle large input years early.
static constexpr uint32_t MaximumYear = 300'000;

static void ReportCalendarFieldOverflow(JSContext* cx, const char* name,
                                        double num) {
  ToCStringBuf numCbuf;
  const char* numStr = NumberToCString(&numCbuf, num);

  JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                            JSMSG_TEMPORAL_CALENDAR_OVERFLOW_FIELD, name,
                            numStr);
}

class ICU4XDateDeleter {
 public:
  void operator()(icu4x::capi::Date* ptr) {
    icu4x::capi::icu4x_Date_destroy_mv1(ptr);
  }
};

using UniqueICU4XDate = mozilla::UniquePtr<icu4x::capi::Date, ICU4XDateDeleter>;

static UniqueICU4XDate CreateICU4XDate(JSContext* cx, const ISODate& date,
                                       CalendarId calendarId,
                                       const icu4x::capi::Calendar* calendar) {
  if (mozilla::Abs(date.year) > MaximumYear) {
    ReportCalendarFieldOverflow(cx, "year", date.year);
    return nullptr;
  }

  auto result = icu4x::capi::icu4x_Date_from_iso_in_calendar_mv1(
      date.year, date.month, date.day, calendar);
  if (!result.is_ok) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_TEMPORAL_CALENDAR_INTERNAL_ERROR);
    return nullptr;
  }
  return UniqueICU4XDate{result.ok};
}

class ICU4XIsoDateDeleter {
 public:
  void operator()(icu4x::capi::IsoDate* ptr) {
    icu4x::capi::icu4x_IsoDate_destroy_mv1(ptr);
  }
};

using UniqueICU4XIsoDate =
    mozilla::UniquePtr<icu4x::capi::IsoDate, ICU4XIsoDateDeleter>;

static constexpr size_t EraNameMaxLength() {
  size_t length = 0;
  for (auto calendar : AvailableCalendars()) {
    for (auto era : CalendarEras(calendar)) {
      for (auto name : CalendarEraNames(calendar, era)) {
        length = std::max(length, name.length());
      }
    }
  }
  return length;
}

/**
 * CanonicalizeEraInCalendar ( calendar, era )
 */
static mozilla::Maybe<EraCode> CanonicalizeEraInCalendar(
    CalendarId calendar, JSLinearString* string) {
  MOZ_ASSERT(CalendarSupportsEra(calendar));

  // Note: Assigning MaxLength to EraNameMaxLength() breaks the CDT indexer.
  constexpr size_t MaxLength = 8;
  static_assert(MaxLength >= EraNameMaxLength(),
                "Storage size is at least as large as the largest known era");

  if (string->length() > MaxLength || !StringIsAscii(string)) {
    return mozilla::Nothing();
  }

  char chars[MaxLength] = {};
  CopyChars(reinterpret_cast<JS::Latin1Char*>(chars), *string);

  auto stringView = std::string_view{chars, string->length()};

  for (auto era : CalendarEras(calendar)) {
    for (auto name : CalendarEraNames(calendar, era)) {
      if (name == stringView) {
        return mozilla::Some(era);
      }
    }
  }
  return mozilla::Nothing();
}

static constexpr std::string_view IcuEraName(CalendarId calendar, EraCode era) {
  switch (calendar) {
    // https://docs.rs/icu/latest/icu/calendar/cal/struct.Iso.html#era-codes
    case CalendarId::ISO8601: {
      MOZ_ASSERT(era == EraCode::Standard);
      return "default";
    }

    // https://docs.rs/icu/latest/icu/calendar/cal/struct.Buddhist.html#era-codes
    case CalendarId::Buddhist: {
      MOZ_ASSERT(era == EraCode::Standard);
      return "be";
    }

    // https://docs.rs/icu/latest/icu/calendar/cal/east_asian_traditional/struct.EastAsianTraditional.html#year-and-era-codes
    case CalendarId::Chinese: {
      MOZ_ASSERT(era == EraCode::Standard);
      return "";
    }

    // https://docs.rs/icu/latest/icu/calendar/cal/struct.Coptic.html#era-codes
    case CalendarId::Coptic: {
      MOZ_ASSERT(era == EraCode::Standard);
      return "am";
    }

    // https://docs.rs/icu/latest/icu/calendar/cal/east_asian_traditional/struct.EastAsianTraditional.html#year-and-era-codes
    case CalendarId::Dangi: {
      MOZ_ASSERT(era == EraCode::Standard);
      return "";
    }

    // https://docs.rs/icu/latest/icu/calendar/cal/struct.Ethiopian.html#era-codes
    case CalendarId::Ethiopian: {
      MOZ_ASSERT(era == EraCode::Standard || era == EraCode::Inverse);
      return era == EraCode::Standard ? "am" : "aa";
    }

    // https://docs.rs/icu/latest/icu/calendar/cal/struct.Ethiopian.html#era-codes
    case CalendarId::EthiopianAmeteAlem: {
      MOZ_ASSERT(era == EraCode::Standard);
      return "aa";
    }

    // https://docs.rs/icu/latest/icu/calendar/cal/struct.Gregorian.html#era-codes
    case CalendarId::Gregorian: {
      MOZ_ASSERT(era == EraCode::Standard || era == EraCode::Inverse);
      return era == EraCode::Standard ? "ce" : "bce";
    }

    // https://docs.rs/icu/latest/icu/calendar/cal/struct.Hebrew.html#era-codes
    case CalendarId::Hebrew: {
      MOZ_ASSERT(era == EraCode::Standard);
      return "am";
    }

    // https://docs.rs/icu/latest/icu/calendar/cal/struct.Indian.html#era-codes
    case CalendarId::Indian: {
      MOZ_ASSERT(era == EraCode::Standard);
      return "shaka";
    }

    // https://docs.rs/icu/latest/icu/calendar/cal/struct.Hijri.html#era-codes
    case CalendarId::IslamicCivil:
    case CalendarId::IslamicTabular:
    case CalendarId::IslamicUmmAlQura: {
      MOZ_ASSERT(era == EraCode::Standard || era == EraCode::Inverse);
      return era == EraCode::Standard ? "ah" : "bh";
    }

    // https://docs.rs/icu/latest/icu/calendar/cal/struct.Persian.html#era-codes
    case CalendarId::Persian: {
      MOZ_ASSERT(era == EraCode::Standard);
      return "ap";
    }

    // https://docs.rs/icu/latest/icu/calendar/cal/struct.Japanese.html#era-codes
    case CalendarId::Japanese: {
      switch (era) {
        case EraCode::Standard:
          return "ce";
        case EraCode::Inverse:
          return "bce";
        case EraCode::Meiji:
          return "meiji";
        case EraCode::Taisho:
          return "taisho";
        case EraCode::Showa:
          return "showa";
        case EraCode::Heisei:
          return "heisei";
        case EraCode::Reiwa:
          return "reiwa";
      }
      break;
    }

    // https://docs.rs/icu/latest/icu/calendar/cal/struct.Roc.html#era-codes
    case CalendarId::ROC: {
      MOZ_ASSERT(era == EraCode::Standard || era == EraCode::Inverse);
      return era == EraCode::Standard ? "roc" : "broc";
    }
  }
  MOZ_CRASH("invalid era");
}

enum class CalendarError {
  // Catch-all kind for all other error types.
  Generic,

  // https://docs.rs/icu/latest/icu/calendar/enum.DateError.html#variant.Range
  OutOfRange,

  // https://docs.rs/icu/latest/icu/calendar/enum.DateError.html#variant.UnknownEra
  UnknownEra,

  // https://docs.rs/icu/latest/icu/calendar/enum.DateError.html#variant.UnknownMonthCode
  UnknownMonthCode,
};

#ifdef DEBUG
static auto CalendarErasAsEnumSet(CalendarId calendarId) {
  // `mozilla::EnumSet<EraCode>(CalendarEras(calendarId))` doesn't work in old
  // GCC versions, so add all era codes manually to the enum set.
  mozilla::EnumSet<EraCode> eras{};
  for (auto era : CalendarEras(calendarId)) {
    eras += era;
  }
  return eras;
}
#endif

struct EraYear {
  EraCode era = EraCode::Standard;
  int32_t year = 0;
};

static mozilla::Result<UniqueICU4XDate, CalendarError> CreateDateFromCodes(
    CalendarId calendarId, const icu4x::capi::Calendar* calendar,
    EraYear eraYear, MonthCode monthCode, int32_t day) {
  MOZ_ASSERT(calendarId != CalendarId::ISO8601);
  MOZ_ASSERT(icu4x::capi::icu4x_Calendar_kind_mv1(calendar) ==
             ToAnyCalendarKind(calendarId));
  MOZ_ASSERT(CalendarErasAsEnumSet(calendarId).contains(eraYear.era));
  MOZ_ASSERT(mozilla::Abs(eraYear.year) <= MaximumYear);
  MOZ_ASSERT(IsValidMonthCodeForCalendar(calendarId, monthCode));
  MOZ_ASSERT(day > 0);
  MOZ_ASSERT(day <= CalendarDaysInMonth(calendarId).second);

  auto era = IcuEraName(calendarId, eraYear.era);
  auto monthCodeView = std::string_view{monthCode};
  auto date = icu4x::capi::icu4x_Date_from_codes_in_calendar_mv1(
      icu4x::diplomat::capi::DiplomatStringView{era.data(), era.length()},
      eraYear.year,
      icu4x::diplomat::capi::DiplomatStringView{monthCodeView.data(),
                                                monthCodeView.length()},
      day, calendar);
  if (date.is_ok) {
    return UniqueICU4XDate{date.ok};
  }

  // Map possible calendar errors.
  //
  // Calendar error codes which can't happen for `create_from_codes_in_calendar`
  // are mapped to `CalendarError::Generic`.
  switch (date.err) {
    case icu4x::capi::CalendarError_OutOfRange:
      return mozilla::Err(CalendarError::OutOfRange);
    case icu4x::capi::CalendarError_UnknownEra:
      return mozilla::Err(CalendarError::UnknownEra);
    case icu4x::capi::CalendarError_UnknownMonthCode:
      return mozilla::Err(CalendarError::UnknownMonthCode);
    default:
      return mozilla::Err(CalendarError::Generic);
  }
}

static mozilla::Result<UniqueICU4XDate, CalendarError> CreateDateFromCodes(
    CalendarId calendarId, const icu4x::capi::Calendar* calendar, int32_t year,
    MonthCode monthCode, int32_t day) {
  return CreateDateFromCodes(calendarId, calendar,
                             EraYear{EraCode::Standard, year}, monthCode, day);
}

/**
 * ConstrainMonthCode ( calendar, arithmeticYear, monthCode, overflow )
 */
static bool ConstrainMonthCode(JSContext* cx, CalendarId calendar,
                               MonthCode monthCode, TemporalOverflow overflow,
                               MonthCode* result) {
  // Step 1.
  MOZ_ASSERT(IsValidMonthCodeForCalendar(calendar, monthCode));

  // Steps 2 and 4.
  MOZ_ASSERT(CalendarHasLeapMonths(calendar));
  MOZ_ASSERT(monthCode.isLeapMonth());

  // Step 3.
  if (overflow == TemporalOverflow::Reject) {
    // Ensure the month code is null-terminated.
    char code[5] = {};
    auto monthCodeView = std::string_view{monthCode};
    monthCodeView.copy(code, monthCodeView.length());

    JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr,
                             JSMSG_TEMPORAL_CALENDAR_INVALID_MONTHCODE, code);
    return false;
  }

  // Steps 5-6.
  bool skipBackward =
      calendar == CalendarId::Chinese || calendar == CalendarId::Dangi;

  // Step 7.
  if (skipBackward) {
    // Step 7.a.
    *result = MonthCode{monthCode.ordinal()};
    return true;
  }

  // Step 8.a
  MOZ_ASSERT(calendar == CalendarId::Hebrew);
  MOZ_ASSERT(monthCode.code() == MonthCode::Code::M05L);

  // Step 8.b
  *result = MonthCode{6};
  return true;
}

static UniqueICU4XDate CreateDateFromCodes(
    JSContext* cx, CalendarId calendarId, const icu4x::capi::Calendar* calendar,
    EraYear eraYear, MonthCode monthCode, int32_t day,
    TemporalOverflow overflow) {
  MOZ_ASSERT(IsValidMonthCodeForCalendar(calendarId, monthCode));
  MOZ_ASSERT(day > 0);
  MOZ_ASSERT(day <= CalendarDaysInMonth(calendarId).second);

  // Constrain day to the maximum possible day for the input month.
  //
  // Special cases like February 29 in leap years of the Gregorian calendar are
  // handled below.
  int32_t daysInMonth = CalendarDaysInMonth(calendarId, monthCode).second;
  if (overflow == TemporalOverflow::Constrain) {
    day = std::min(day, daysInMonth);
  } else {
    MOZ_ASSERT(overflow == TemporalOverflow::Reject);

    if (day > daysInMonth) {
      ReportCalendarFieldOverflow(cx, "day", day);
      return nullptr;
    }
  }

  // ICU4X doesn't support large dates, so we have to handle this case early.
  if (mozilla::Abs(eraYear.year) > MaximumYear) {
    ReportCalendarFieldOverflow(cx, "year", eraYear.year);
    return nullptr;
  }

  auto result =
      CreateDateFromCodes(calendarId, calendar, eraYear, monthCode, day);
  if (result.isOk()) {
    return result.unwrap();
  }

  switch (result.inspectErr()) {
    case CalendarError::UnknownMonthCode: {
      // We've asserted above that |monthCode| is valid for this calendar, so
      // any unknown month code must be for a leap month which doesn't happen in
      // the current year.
      MonthCode constrained;
      if (!ConstrainMonthCode(cx, calendarId, monthCode, overflow,
                              &constrained)) {
        return nullptr;
      }
      MOZ_ASSERT(!constrained.isLeapMonth());

      // Retry as non-leap month when we're allowed to constrain.
      return CreateDateFromCodes(cx, calendarId, calendar, eraYear, constrained,
                                 day, overflow);
    }

    case CalendarError::OutOfRange: {
      // ICU4X throws an out-of-range error if:
      // 1. month > monthsInYear(year), or
      // 2. days > daysInMonthOf(year, month).

      // Case 1 can't happen for month-codes, so it doesn't apply here.
      // Case 2 can only happen when |day| is larger than the minimum number
      // of days in the month.
      MOZ_ASSERT(day > CalendarDaysInMonth(calendarId, monthCode).first);

      if (overflow == TemporalOverflow::Reject) {
        ReportCalendarFieldOverflow(cx, "day", day);
        return nullptr;
      }

      auto firstDayOfMonth = CreateDateFromCodes(
          cx, calendarId, calendar, eraYear, monthCode, 1, overflow);
      if (!firstDayOfMonth) {
        return nullptr;
      }

      int32_t daysInMonth = DaysInMonth(firstDayOfMonth.get());
      MOZ_ASSERT(day > daysInMonth);
      return CreateDateFromCodes(cx, calendarId, calendar, eraYear, monthCode,
                                 daysInMonth, overflow);
    }

    case CalendarError::UnknownEra:
      MOZ_ASSERT(false, "unexpected calendar error");
      break;

    case CalendarError::Generic:
      break;
  }

  JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                            JSMSG_TEMPORAL_CALENDAR_INTERNAL_ERROR);
  return nullptr;
}

static UniqueICU4XDate CreateDateFromCodes(
    JSContext* cx, CalendarId calendarId, const icu4x::capi::Calendar* calendar,
    int32_t year, MonthCode monthCode, int32_t day, TemporalOverflow overflow) {
  return CreateDateFromCodes(cx, calendarId, calendar,
                             EraYear{EraCode::Standard, year}, monthCode, day,
                             overflow);
}

static UniqueICU4XDate CreateDateFrom(JSContext* cx, CalendarId calendarId,
                                      const icu4x::capi::Calendar* calendar,
                                      EraYear eraYear, int32_t month,
                                      int32_t day, TemporalOverflow overflow) {
  MOZ_ASSERT(calendarId != CalendarId::ISO8601);
  MOZ_ASSERT(month > 0);
  MOZ_ASSERT(day > 0);
  MOZ_ASSERT(month <= CalendarMonthsPerYear(calendarId));
  MOZ_ASSERT(day <= CalendarDaysInMonth(calendarId).second);

  switch (calendarId) {
    case CalendarId::ISO8601:
    case CalendarId::Buddhist:
    case CalendarId::Coptic:
    case CalendarId::Ethiopian:
    case CalendarId::EthiopianAmeteAlem:
    case CalendarId::Gregorian:
    case CalendarId::Indian:
    case CalendarId::IslamicCivil:
    case CalendarId::IslamicTabular:
    case CalendarId::IslamicUmmAlQura:
    case CalendarId::Japanese:
    case CalendarId::Persian:
    case CalendarId::ROC: {
      MOZ_ASSERT(!CalendarHasLeapMonths(calendarId));

      // Use the month-code corresponding to the ordinal month number for
      // calendar systems without leap months.
      auto date = CreateDateFromCodes(cx, calendarId, calendar, eraYear,
                                      MonthCode{month}, day, overflow);
      if (!date) {
        return nullptr;
      }
      MOZ_ASSERT_IF(!CalendarHasMidYearEras(calendarId),
                    OrdinalMonth(date.get()) == month);
      return date;
    }

    case CalendarId::Chinese:
    case CalendarId::Dangi:
    case CalendarId::Hebrew: {
      static_assert(CalendarHasLeapMonths(CalendarId::Chinese));
      static_assert(CalendarMonthsPerYear(CalendarId::Chinese) == 13);
      static_assert(CalendarHasLeapMonths(CalendarId::Dangi));
      static_assert(CalendarMonthsPerYear(CalendarId::Dangi) == 13);
      static_assert(CalendarHasLeapMonths(CalendarId::Hebrew));
      static_assert(CalendarMonthsPerYear(CalendarId::Hebrew) == 13);

      MOZ_ASSERT(1 <= month && month <= 13);

      // Constrain |day| when overflow is "reject" to avoid rejecting too large
      // day values in CreateDateFromCodes.
      //
      // For example in the Hebrew calendar, when month = 10 and day = 30 and
      // the input year is a leap year. We first try month code "M10", but since
      // "M10" can have at most 29 days, we need to constrain the days value
      // before calling CreateDateFromCodes.
      int32_t constrainedDay = day;
      if (overflow == TemporalOverflow::Reject) {
        auto daysInMonth = CalendarDaysInMonth(calendarId);
        if (day > daysInMonth.first && day <= daysInMonth.second) {
          constrainedDay = daysInMonth.first;
        }
      }

      // Return after a date with a matching ordinal month was found, but the
      // date was constructed using a constrained day.
      auto returnForOrdinalMonthMatch = [&](auto date,
                                            auto monthCode) -> UniqueICU4XDate {
        MOZ_ASSERT(OrdinalMonth(date.get()) == month);

        // If |day| was constrained, check if the actual input days value
        // exceeds the number of days in the resolved month.
        if (constrainedDay < day) {
          MOZ_ASSERT(overflow == TemporalOverflow::Reject);

          if (day > CalendarDaysInMonth(calendarId, monthCode).second) {
            ReportCalendarFieldOverflow(cx, "day", day);
            return nullptr;
          }
          return CreateDateFromCodes(cx, calendarId, calendar, eraYear,
                                     monthCode, day, overflow);
        }
        return date;
      };

      // Create date with month number replaced by month-code.
      auto monthCode = MonthCode{std::min(month, 12)};
      auto date = CreateDateFromCodes(cx, calendarId, calendar, eraYear,
                                      monthCode, constrainedDay, overflow);
      if (!date) {
        return nullptr;
      }

      // If the ordinal month of |date| matches the input month, no additional
      // changes are necessary and we can directly return |date|.
      int32_t ordinal = OrdinalMonth(date.get());
      if (ordinal == month) {
        return returnForOrdinalMonthMatch(std::move(date), monthCode);
      }

      // Otherwise adjust the month code according to the calendar.
      MonthCode adjustedMonthCode{};
      if (calendarId == CalendarId::Hebrew) {
        // We need to handle two cases:
        // 1. The input year contains a leap month and we need to adjust the
        //    month-code.
        // 2. The thirteenth month of a year without leap months was requested.
        if (ordinal > month) {
          MOZ_ASSERT(1 < month && month <= 12);

          // This case can only happen in leap years.
          MOZ_ASSERT(MonthsInYear(date.get()) == 13);

          // Leap months can occur between M05 and M06 in the Hebrew calendar.
          //
          // Month code:     M01  M02  M03  M04  M05  M05L  M06 ...
          // Ordinal month:  1    2    3    4    5    6     7

          // The month can be off by exactly one.
          MOZ_ASSERT((ordinal - month) == 1);
        } else {
          MOZ_ASSERT(month == 13);
          MOZ_ASSERT(ordinal == 12);
          MOZ_ASSERT(MonthsInYear(date.get()) != 13);
          MOZ_ASSERT(day == constrainedDay ||
                     overflow == TemporalOverflow::Reject);

          if (overflow == TemporalOverflow::Reject) {
            ReportCalendarFieldOverflow(cx, "month", month);
            return nullptr;
          }
          return date;
        }

        // The previous month is the leap month Adar I iff |month| is six.
        bool isLeapMonth = month == 6;
        adjustedMonthCode = MonthCode{month - 1, isLeapMonth};
      } else {
        // We need to handle three cases:
        // 1. The input year contains a leap month and we need to adjust the
        //    month-code.
        // 2. The thirteenth month of a year without leap months was requested.
        // 3. The thirteenth month of a year with leap months was requested.
        if (ordinal > month) {
          MOZ_ASSERT(1 < month && month <= 12);

          // This case can only happen in leap years.
          MOZ_ASSERT(MonthsInYear(date.get()) == 13);

          // Leap months can occur after any month in the Chinese calendar.
          //
          // Example when the fourth month is a leap month between M03 and M04.
          //
          // Month code:     M01  M02  M03  M03L  M04  M05  M06 ...
          // Ordinal month:  1    2    3    4     5    6    7

          // The month can be off by exactly one.
          MOZ_ASSERT((ordinal - month) == 1);

          // First try the case when the previous month isn't a leap month. This
          // case can only occur when |month > 2|, because otherwise we know
          // that "M01L" is the correct answer.
          if (month > 2) {
            auto previousMonthCode = MonthCode{month - 1};
            date = CreateDateFromCodes(cx, calendarId, calendar, eraYear,
                                       previousMonthCode, constrainedDay,
                                       overflow);
            if (!date) {
              return nullptr;
            }

            int32_t ordinal = OrdinalMonth(date.get());
            if (ordinal == month) {
              return returnForOrdinalMonthMatch(std::move(date),
                                                previousMonthCode);
            }
          }

          // Fall-through when the previous month is a leap month.
        } else {
          MOZ_ASSERT(month == 13);
          MOZ_ASSERT(ordinal == 12);
          MOZ_ASSERT(day == constrainedDay ||
                     overflow == TemporalOverflow::Reject);

          // Years with leap months contain thirteen months.
          if (MonthsInYear(date.get()) != 13) {
            if (overflow == TemporalOverflow::Reject) {
              ReportCalendarFieldOverflow(cx, "month", month);
              return nullptr;
            }
            return date;
          }

          // Fall-through to return leap month "M12L" at the end of the year.
        }

        // Finally handle the case when the previous month is a leap month.
        adjustedMonthCode = MonthCode{month - 1, /* isLeapMonth= */ true};
      }

      date = CreateDateFromCodes(cx, calendarId, calendar, eraYear,
                                 adjustedMonthCode, day, overflow);
      if (!date) {
        return nullptr;
      }
      MOZ_ASSERT(OrdinalMonth(date.get()) == month, "unexpected ordinal month");
      return date;
    }
  }
  MOZ_CRASH("invalid calendar id");
}

static UniqueICU4XDate CreateDateFrom(JSContext* cx, CalendarId calendarId,
                                      const icu4x::capi::Calendar* calendar,
                                      int32_t year, int32_t month, int32_t day,
                                      TemporalOverflow overflow) {
  return CreateDateFrom(cx, calendarId, calendar,
                        EraYear{EraCode::Standard, year}, month, day, overflow);
}

static constexpr size_t ICUEraNameMaxLength() {
  size_t length = 0;
  for (auto calendar : AvailableCalendars()) {
    for (auto era : CalendarEras(calendar)) {
      auto name = IcuEraName(calendar, era);
      length = std::max(length, name.length());
    }
  }
  return length;
}

class EraName {
  // Note: Assigning MaxLength to ICUEraNameMaxLength() breaks the CDT indexer.
  static constexpr size_t MaxLength = 7;

// Disable tautological-value-range-compare to avoid a bogus Clang warning.
// See bug 1956918 and bug 1936626.
#ifdef __clang__
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wtautological-value-range-compare"
#endif

  static_assert(MaxLength >= ICUEraNameMaxLength(),
                "Storage size is at least as large as the largest known era");

#ifdef __clang__
#  pragma clang diagnostic pop
#endif

  // Storage for the largest known era string and the terminating NUL-character.
  char buf[MaxLength + 1] = {};
  size_t length = 0;

 public:
  explicit EraName(const icu4x::capi::Date* date) {
    auto writable = diplomat::capi::diplomat_simple_write(buf, std::size(buf));

    icu4x::capi::icu4x_Date_era_mv1(date, &writable);
    MOZ_ASSERT(writable.buf == buf, "unexpected buffer relocation");

    length = writable.len;
  }

  bool operator==(std::string_view sv) const {
    return std::string_view{buf, length} == sv;
  }

  bool operator!=(std::string_view sv) const { return !(*this == sv); }
};

/**
 * Retrieve the era code from |date| and then map the returned ICU4X era code to
 * the corresponding |EraCode| member.
 */
static bool CalendarDateEra(JSContext* cx, CalendarId calendar,
                            const icu4x::capi::Date* date, EraCode* result) {
  MOZ_ASSERT(calendar != CalendarId::ISO8601);

  auto eraName = EraName(date);

  // Map from era name to era code.
  for (auto era : CalendarEras(calendar)) {
    if (eraName == IcuEraName(calendar, era)) {
      *result = era;
      return true;
    }
  }

  // Invalid/Unknown era name.
  JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                            JSMSG_TEMPORAL_CALENDAR_INTERNAL_ERROR);
  return false;
}

/**
 * Return the extended (non-era) year from |date|.
 */
static int32_t CalendarDateYear(CalendarId calendar,
                                const icu4x::capi::Date* date) {
  MOZ_ASSERT(calendar != CalendarId::ISO8601);

  return icu4x::capi::icu4x_Date_extended_year_mv1(date);
}

/**
 * Retrieve the month code from |date| and then map the returned ICU4X month
 * code to the corresponding |MonthCode| member.
 */
static MonthCode CalendarDateMonthCode(CalendarId calendar,
                                       const icu4x::capi::Date* date) {
  MOZ_ASSERT(calendar != CalendarId::ISO8601);

  // Valid month codes are "M01".."M13" and "M01L".."M12L".
  constexpr size_t MaxLength =
      std::string_view{MonthCode::maxLeapMonth()}.length();
  static_assert(
      MaxLength > std::string_view{MonthCode::maxNonLeapMonth()}.length(),
      "string representation of max-leap month is larger");

  // Storage for the largest valid month code and the terminating NUL-character.
  char buf[MaxLength + 1] = {};
  auto writable = diplomat::capi::diplomat_simple_write(buf, std::size(buf));

  icu4x::capi::icu4x_Date_month_code_mv1(date, &writable);
  MOZ_ASSERT(writable.buf == buf, "unexpected buffer relocation");

  auto view = std::string_view{writable.buf, writable.len};

  MOZ_ASSERT(view.length() >= 3);
  MOZ_ASSERT(view[0] == 'M');
  MOZ_ASSERT(mozilla::IsAsciiDigit(view[1]));
  MOZ_ASSERT(mozilla::IsAsciiDigit(view[2]));
  MOZ_ASSERT_IF(view.length() > 3, view[3] == 'L');

  int32_t ordinal =
      AsciiDigitToNumber(view[1]) * 10 + AsciiDigitToNumber(view[2]);
  bool isLeapMonth = view.length() > 3;
  auto monthCode = MonthCode{ordinal, isLeapMonth};

  // The month code must be valid for this calendar.
  MOZ_ASSERT(IsValidMonthCodeForCalendar(calendar, monthCode));

  return monthCode;
}

class MonthCodeString {
  // Zero-terminated month code string.
  char str_[4 + 1] = {};

 public:
  explicit MonthCodeString(MonthCodeField field) {
    str_[0] = 'M';
    str_[1] = char('0' + (field.ordinal() / 10));
    str_[2] = char('0' + (field.ordinal() % 10));
    str_[3] = field.isLeapMonth() ? 'L' : '\0';
    str_[4] = '\0';
  }

  const char* toCString() const { return str_; }
};

/**
 * CalendarResolveFields ( calendar, fields, type )
 */
static bool ISOCalendarResolveMonth(JSContext* cx,
                                    Handle<CalendarFields> fields,
                                    double* result) {
  double month = fields.month();
  MOZ_ASSERT_IF(fields.has(CalendarField::Month),
                IsInteger(month) && month > 0);

  // CalendarResolveFields, steps 1.e.
  if (!fields.has(CalendarField::MonthCode)) {
    MOZ_ASSERT(fields.has(CalendarField::Month));

    *result = month;
    return true;
  }

  auto monthCode = fields.monthCode();

  // CalendarResolveFields, steps 1.f-k.
  int32_t ordinal = monthCode.ordinal();
  if (ordinal < 1 || ordinal > 12 || monthCode.isLeapMonth()) {
    JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr,
                             JSMSG_TEMPORAL_CALENDAR_INVALID_MONTHCODE,
                             MonthCodeString{monthCode}.toCString());
    return false;
  }

  // CalendarResolveFields, steps 1.l-m.
  if (fields.has(CalendarField::Month) && month != ordinal) {
    ToCStringBuf cbuf;
    const char* monthStr = NumberToCString(&cbuf, month);

    JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr,
                             JSMSG_TEMPORAL_CALENDAR_INCOMPATIBLE_MONTHCODE,
                             MonthCodeString{monthCode}.toCString(), monthStr);
    return false;
  }

  // CalendarResolveFields, steps 1.n.
  *result = ordinal;
  return true;
}

struct EraYears {
  // Year starting from the calendar epoch.
  mozilla::Maybe<EraYear> fromEpoch;

  // Year starting from a specific calendar era.
  mozilla::Maybe<EraYear> fromEra;
};

/**
 * CalendarResolveFields ( calendar, fields, type )
 * CalendarDateToISO ( calendar, fields, overflow )
 * CalendarMonthDayToISOReferenceDate ( calendar, fields, overflow )
 *
 * Extract `year` and `eraYear` from |fields| and perform some initial
 * validation to ensure the values are valid for the requested calendar.
 */
static bool CalendarFieldYear(JSContext* cx, CalendarId calendar,
                              Handle<CalendarFields> fields, EraYears* result) {
  MOZ_ASSERT(fields.has(CalendarField::Year) ||
             fields.has(CalendarField::EraYear));

  // |eraYear| is to be ignored when not relevant for |calendar| per
  // CalendarResolveFields.
  bool supportsEra =
      fields.has(CalendarField::Era) && CalendarSupportsEra(calendar);
  MOZ_ASSERT_IF(fields.has(CalendarField::Era), CalendarSupportsEra(calendar));

  // Case 1: |year| field is present.
  mozilla::Maybe<EraYear> fromEpoch;
  if (fields.has(CalendarField::Year)) {
    double year = fields.year();
    MOZ_ASSERT(IsInteger(year));

    int32_t intYear;
    if (!mozilla::NumberEqualsInt32(year, &intYear) ||
        mozilla::Abs(intYear) > MaximumYear) {
      ReportCalendarFieldOverflow(cx, "year", year);
      return false;
    }

    fromEpoch = mozilla::Some(EraYear{EraCode::Standard, intYear});
  } else {
    MOZ_ASSERT(supportsEra);
  }

  // Case 2: |era| and |eraYear| fields are present and relevant for |calendar|.
  mozilla::Maybe<EraYear> fromEra;
  if (supportsEra) {
    MOZ_ASSERT(fields.has(CalendarField::Era));
    MOZ_ASSERT(fields.has(CalendarField::EraYear));

    auto era = fields.era();
    MOZ_ASSERT(era);

    double eraYear = fields.eraYear();
    MOZ_ASSERT(IsInteger(eraYear));

    auto* linearEra = era->ensureLinear(cx);
    if (!linearEra) {
      return false;
    }

    // Ensure the requested era is valid for |calendar|.
    auto eraCode = CanonicalizeEraInCalendar(calendar, linearEra);
    if (!eraCode) {
      if (auto code = QuoteString(cx, era)) {
        JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr,
                                 JSMSG_TEMPORAL_CALENDAR_INVALID_ERA,
                                 code.get());
      }
      return false;
    }

    int32_t intEraYear;
    if (!mozilla::NumberEqualsInt32(eraYear, &intEraYear) ||
        mozilla::Abs(intEraYear) > MaximumYear) {
      ReportCalendarFieldOverflow(cx, "eraYear", eraYear);
      return false;
    }

    fromEra = mozilla::Some(EraYear{*eraCode, intEraYear});
  }

  *result = {fromEpoch, fromEra};
  return true;
}

struct Month {
  // Month code.
  MonthCode code;

  // Ordinal month number.
  int32_t ordinal = 0;
};

/**
 * NonISOCalendarDateToISO ( calendar, fields, overflow )
 * NonISOMonthDayToISOReferenceDate ( calendar, fields, overflow )
 *
 * Extract `monthCode` and `month` from |fields| and perform some initial
 * validation to ensure the values are valid for the requested calendar.
 */
static bool CalendarFieldMonth(JSContext* cx, CalendarId calendar,
                               Handle<CalendarFields> fields,
                               TemporalOverflow overflow, Month* result) {
  MOZ_ASSERT(fields.has(CalendarField::MonthCode) ||
             fields.has(CalendarField::Month));

  // Case 1: |monthCode| field is present.
  MonthCode fromMonthCode;
  if (fields.has(CalendarField::MonthCode)) {
    auto monthCode = fields.monthCode();
    int32_t ordinal = monthCode.ordinal();
    bool isLeapMonth = monthCode.isLeapMonth();

    constexpr int32_t minMonth = MonthCode{1}.ordinal();
    constexpr int32_t maxNonLeapMonth = MonthCode::maxNonLeapMonth().ordinal();
    constexpr int32_t maxLeapMonth = MonthCode::maxLeapMonth().ordinal();

    // Minimum month number is 1. Maximum month is 12 (or 13 when the calendar
    // uses epagomenal months).
    const int32_t maxMonth = isLeapMonth ? maxLeapMonth : maxNonLeapMonth;
    if (minMonth <= ordinal && ordinal <= maxMonth) {
      fromMonthCode = MonthCode{ordinal, isLeapMonth};
    }

    // Ensure the month code is valid for this calendar.
    if (!IsValidMonthCodeForCalendar(calendar, fromMonthCode)) {
      JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr,
                               JSMSG_TEMPORAL_CALENDAR_INVALID_MONTHCODE,
                               MonthCodeString{monthCode}.toCString());
      return false;
    }
  }

  // Case 2: |month| field is present.
  int32_t intMonth = 0;
  if (fields.has(CalendarField::Month)) {
    double month = fields.month();
    MOZ_ASSERT(IsInteger(month) && month > 0);

    if (!mozilla::NumberEqualsInt32(month, &intMonth)) {
      intMonth = 0;
    }

    const int32_t monthsPerYear = CalendarMonthsPerYear(calendar);
    if (intMonth < 1 || intMonth > monthsPerYear) {
      if (overflow == TemporalOverflow::Reject) {
        ReportCalendarFieldOverflow(cx, "month", month);
        return false;
      }
      MOZ_ASSERT(overflow == TemporalOverflow::Constrain);

      // An invalid month can't be equal to any valid month code.
      if (fields.has(CalendarField::MonthCode)) {
        ToCStringBuf cbuf;
        const char* monthStr = NumberToCString(&cbuf, month);

        JS_ReportErrorNumberUTF8(
            cx, GetErrorMessage, nullptr,
            JSMSG_TEMPORAL_CALENDAR_INCOMPATIBLE_MONTHCODE,
            MonthCodeString{fields.monthCode()}.toCString(), monthStr);
        return false;
      }

      // Constrain to largest allowed month value.
      intMonth = monthsPerYear;
    }

    MOZ_ASSERT(intMonth > 0);
  }

  *result = {fromMonthCode, intMonth};
  return true;
}

/**
 * CalendarResolveFields ( calendar, fields, type )
 * CalendarDateToISO ( calendar, fields, overflow )
 * CalendarMonthDayToISOReferenceDate ( calendar, fields, overflow )
 *
 * Extract `day` from |fields| and perform some initial validation to ensure the
 * value is valid for the requested calendar.
 */
static bool CalendarFieldDay(JSContext* cx, CalendarId calendar,
                             Handle<CalendarFields> fields,
                             TemporalOverflow overflow, int32_t* result) {
  MOZ_ASSERT(fields.has(CalendarField::Day));

  double day = fields.day();
  MOZ_ASSERT(IsInteger(day) && day > 0);

  int32_t intDay;
  if (!mozilla::NumberEqualsInt32(day, &intDay)) {
    intDay = 0;
  }

  // Constrain to a valid day value in this calendar.
  int32_t daysPerMonth = CalendarDaysInMonth(calendar).second;
  if (intDay < 1 || intDay > daysPerMonth) {
    if (overflow == TemporalOverflow::Reject) {
      ReportCalendarFieldOverflow(cx, "day", day);
      return false;
    }
    MOZ_ASSERT(overflow == TemporalOverflow::Constrain);

    intDay = daysPerMonth;
  }

  *result = intDay;
  return true;
}

/**
 * CalendarResolveFields ( calendar, fields, type )
 *
 * > The operation throws a TypeError exception if the properties of fields are
 * > internally inconsistent within the calendar [...]. For example:
 * >
 * > [...] The values for "era" and "eraYear" do not together identify the same
 * > year as the value for "year".
 */
static bool CalendarFieldEraYearMatchesYear(JSContext* cx, CalendarId calendar,
                                            Handle<CalendarFields> fields,
                                            const icu4x::capi::Date* date) {
  MOZ_ASSERT(fields.has(CalendarField::EraYear));
  MOZ_ASSERT(fields.has(CalendarField::Year));

  double year = fields.year();
  MOZ_ASSERT(IsInteger(year));

  int32_t intYear;
  MOZ_ALWAYS_TRUE(mozilla::NumberEqualsInt32(year, &intYear));

  int32_t yearFromEraYear = CalendarDateYear(calendar, date);

  // The user requested year must match the actual (extended/epoch) year.
  if (intYear != yearFromEraYear) {
    Int32ToCStringBuf yearCbuf;
    const char* yearStr = Int32ToCString(&yearCbuf, intYear);

    Int32ToCStringBuf fromEraCbuf;
    const char* fromEraStr = Int32ToCString(&fromEraCbuf, yearFromEraYear);

    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_TEMPORAL_CALENDAR_INCOMPATIBLE_YEAR,
                              yearStr, fromEraStr);
    return false;
  }
  return true;
}

/**
 * CalendarResolveFields ( calendar, fields, type )
 *
 * > The operation throws a TypeError exception if the properties of fields are
 * > internally inconsistent within the calendar [...]. For example:
 * >
 * > If "month" and "monthCode" in the calendar [...] do not identify the same
 * > month.
 */
static bool CalendarFieldMonthCodeMatchesMonth(JSContext* cx,
                                               Handle<CalendarFields> fields,
                                               const icu4x::capi::Date* date,
                                               int32_t month) {
  int32_t ordinal = OrdinalMonth(date);

  // The user requested month must match the actual ordinal month.
  if (month != ordinal) {
    ToCStringBuf cbuf;
    const char* monthStr = NumberToCString(&cbuf, fields.month());

    JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr,
                             JSMSG_TEMPORAL_CALENDAR_INCOMPATIBLE_MONTHCODE,
                             MonthCodeString{fields.monthCode()}.toCString(),
                             monthStr);
    return false;
  }
  return true;
}

static ISODate ToISODate(const icu4x::capi::Date* date) {
  UniqueICU4XIsoDate isoDate{icu4x::capi::icu4x_Date_to_iso_mv1(date)};
  MOZ_ASSERT(isoDate, "unexpected null-pointer result");

  int32_t isoYear = icu4x::capi::icu4x_IsoDate_year_mv1(isoDate.get());

  int32_t isoMonth = icu4x::capi::icu4x_IsoDate_month_mv1(isoDate.get());
  MOZ_ASSERT(1 <= isoMonth && isoMonth <= 12);

  int32_t isoDay = icu4x::capi::icu4x_IsoDate_day_of_month_mv1(isoDate.get());
  MOZ_ASSERT(1 <= isoDay && isoDay <= ::ISODaysInMonth(isoYear, isoMonth));

  return {isoYear, isoMonth, isoDay};
}

static UniqueICU4XDate CreateDateFrom(JSContext* cx, CalendarId calendar,
                                      const icu4x::capi::Calendar* cal,
                                      const EraYears& eraYears,
                                      const Month& month, int32_t day,
                                      Handle<CalendarFields> fields,
                                      TemporalOverflow overflow) {
  // Use |eraYear| if present, so we can more easily check for consistent
  // |year| and |eraYear| fields.
  auto eraYear = eraYears.fromEra ? *eraYears.fromEra : *eraYears.fromEpoch;

  UniqueICU4XDate date;
  if (month.code != MonthCode{}) {
    date = CreateDateFromCodes(cx, calendar, cal, eraYear, month.code, day,
                               overflow);
  } else {
    date = CreateDateFrom(cx, calendar, cal, eraYear, month.ordinal, day,
                          overflow);
  }
  if (!date) {
    return nullptr;
  }

  // |year| and |eraYear| must be consistent.
  if (eraYears.fromEpoch && eraYears.fromEra) {
    if (!CalendarFieldEraYearMatchesYear(cx, calendar, fields, date.get())) {
      return nullptr;
    }
  }

  // |month| and |monthCode| must be consistent.
  if (month.code != MonthCode{} && month.ordinal > 0) {
    if (!CalendarFieldMonthCodeMatchesMonth(cx, fields, date.get(),
                                            month.ordinal)) {
      return nullptr;
    }
  }

  return date;
}

/**
 * RegulateISODate ( year, month, day, overflow )
 */
static bool RegulateISODate(JSContext* cx, int32_t year, double month,
                            double day, TemporalOverflow overflow,
                            ISODate* result) {
  MOZ_ASSERT(IsInteger(month));
  MOZ_ASSERT(IsInteger(day));

  // Step 1.
  if (overflow == TemporalOverflow::Constrain) {
    // Step 1.a.
    int32_t m = int32_t(std::clamp(month, 1.0, 12.0));

    // Step 1.b.
    double daysInMonth = double(::ISODaysInMonth(year, m));

    // Step 1.c.
    int32_t d = int32_t(std::clamp(day, 1.0, daysInMonth));

    // Step 3. (Inlined call to CreateISODateRecord.)
    *result = {year, m, d};
    return true;
  }

  // Step 2.a.
  MOZ_ASSERT(overflow == TemporalOverflow::Reject);

  // Step 2.b.
  if (!ThrowIfInvalidISODate(cx, year, month, day)) {
    return false;
  }

  // Step 3. (Inlined call to CreateISODateRecord.)
  *result = {year, int32_t(month), int32_t(day)};
  return true;
}

/**
 * NonISOCalendarDateToISO ( calendar, fields, overflow )
 */
static bool NonISOCalendarDateToISO(JSContext* cx, CalendarId calendar,
                                    Handle<CalendarFields> fields,
                                    TemporalOverflow overflow,
                                    ISODate* result) {
  EraYears eraYears;
  if (!CalendarFieldYear(cx, calendar, fields, &eraYears)) {
    return false;
  }

  Month month;
  if (!CalendarFieldMonth(cx, calendar, fields, overflow, &month)) {
    return false;
  }

  int32_t day;
  if (!CalendarFieldDay(cx, calendar, fields, overflow, &day)) {
    return false;
  }

  auto cal = CreateICU4XCalendar(calendar);
  auto date = CreateDateFrom(cx, calendar, cal.get(), eraYears, month, day,
                             fields, overflow);
  if (!date) {
    return false;
  }

  *result = ToISODate(date.get());
  return true;
}

/**
 * CalendarDateToISO ( calendar, fields, overflow )
 */
static bool CalendarDateToISO(JSContext* cx, CalendarId calendar,
                              Handle<CalendarFields> fields,
                              TemporalOverflow overflow, ISODate* result) {
  // Step 1.
  if (calendar == CalendarId::ISO8601) {
    // Step 1.a.
    MOZ_ASSERT(fields.has(CalendarField::Year));
    MOZ_ASSERT(fields.has(CalendarField::Month) ||
               fields.has(CalendarField::MonthCode));
    MOZ_ASSERT(fields.has(CalendarField::Day));

    // Remaining steps from CalendarResolveFields to resolve the month.
    double month;
    if (!ISOCalendarResolveMonth(cx, fields, &month)) {
      return false;
    }

    int32_t intYear;
    if (!mozilla::NumberEqualsInt32(fields.year(), &intYear)) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_TEMPORAL_PLAIN_DATE_INVALID);
      return false;
    }

    // Step 1.b.
    return RegulateISODate(cx, intYear, month, fields.day(), overflow, result);
  }

  // Step 2.
  return NonISOCalendarDateToISO(cx, calendar, fields, overflow, result);
}

/**
 * NonISOMonthDayToISOReferenceDate ( calendar, fields, overflow )
 *
 * Return the reference ISO year for "chinese" and "dangi" calendars. Return
 * zero if no reference ISO year can be determined.
 */
static int32_t EastAsianCalendarReferenceISOYear(CalendarId calendar,
                                                 MonthCode monthCode,
                                                 int32_t day) {
  MOZ_ASSERT(calendar == CalendarId::Chinese || calendar == CalendarId::Dangi);
  MOZ_ASSERT(day > 0);

  if (day < 30) {
    switch (monthCode.code()) {
      case MonthCode::Code::M01:
      case MonthCode::Code::M02:
      case MonthCode::Code::M03:
      case MonthCode::Code::M04:
      case MonthCode::Code::M05:
      case MonthCode::Code::M06:
      case MonthCode::Code::M07:
      case MonthCode::Code::M08:
      case MonthCode::Code::M09:
      case MonthCode::Code::M10:
      case MonthCode::Code::M11:
      case MonthCode::Code::M12:
        return 1972;

      case MonthCode::Code::M01L:
        return 0;
      case MonthCode::Code::M02L:
        return 1947;
      case MonthCode::Code::M03L:
        return 1966;
      case MonthCode::Code::M04L:
        return 1963;
      case MonthCode::Code::M05L:
        return 1971;
      case MonthCode::Code::M06L:
        return 1960;
      case MonthCode::Code::M07L:
        return 1968;
      case MonthCode::Code::M08L:
        return 1957;
      case MonthCode::Code::M09L:
        return 2014;
      case MonthCode::Code::M10L:
        return 1984;
      case MonthCode::Code::M11L:
        return day <= 10 ? 2033 : 2034;
      case MonthCode::Code::M12L:
        return 0;

      case MonthCode::Code::Invalid:
      case MonthCode::Code::M13:
        break;
    }
  } else {
    switch (monthCode.code()) {
      case MonthCode::Code::M01:
        return 1970;
      case MonthCode::Code::M02:
        return 1972;
      case MonthCode::Code::M03:
        return calendar == CalendarId::Chinese ? 1966 : 1968;
      case MonthCode::Code::M04:
        return 1970;
      case MonthCode::Code::M05:
        return 1972;
      case MonthCode::Code::M06:
        return 1971;
      case MonthCode::Code::M07:
        return 1972;
      case MonthCode::Code::M08:
        return 1971;
      case MonthCode::Code::M09:
        return 1972;
      case MonthCode::Code::M10:
        return 1972;
      case MonthCode::Code::M11:
        return 1970;
      case MonthCode::Code::M12:
        return 1972;

      case MonthCode::Code::M01L:
        return 0;
      case MonthCode::Code::M02L:
        return 0;
      case MonthCode::Code::M03L:
        return 1955;
      case MonthCode::Code::M04L:
        return 1944;
      case MonthCode::Code::M05L:
        return 1952;
      case MonthCode::Code::M06L:
        return 1941;
      case MonthCode::Code::M07L:
        return 1938;
      case MonthCode::Code::M08L:
        return 0;
      case MonthCode::Code::M09L:
        return 0;
      case MonthCode::Code::M10L:
        return 0;
      case MonthCode::Code::M11L:
        return 0;
      case MonthCode::Code::M12L:
        return 0;

      case MonthCode::Code::Invalid:
      case MonthCode::Code::M13:
        break;
    }
  }
  MOZ_CRASH("unexpected month code");
}

/**
 * CalendarMonthDayToISOReferenceDate ( calendar, fields, overflow )
 */
static bool NonISOMonthDayToISOReferenceDate(JSContext* cx, CalendarId calendar,
                                             icu4x::capi::Calendar* cal,
                                             ISODate startISODate,
                                             ISODate endISODate,
                                             MonthCode monthCode, int32_t day,
                                             UniqueICU4XDate* resultDate) {
  MOZ_ASSERT(startISODate != endISODate);

  int32_t direction = startISODate > endISODate ? -1 : 1;

  auto fromIsoDate = CreateICU4XDate(cx, startISODate, calendar, cal);
  if (!fromIsoDate) {
    return false;
  }

  auto toIsoDate = CreateICU4XDate(cx, endISODate, calendar, cal);
  if (!toIsoDate) {
    return false;
  }

  // Find the calendar year for the ISO start date.
  int32_t calendarYear = CalendarDateYear(calendar, fromIsoDate.get());

  // Find the calendar year for the ISO end date.
  int32_t toCalendarYear = CalendarDateYear(calendar, toIsoDate.get());

  while (direction < 0 ? calendarYear >= toCalendarYear
                       : calendarYear <= toCalendarYear) {
    // This loop can run for a long time.
    if (!CheckForInterrupt(cx)) {
      return false;
    }

    auto result =
        CreateDateFromCodes(calendar, cal, calendarYear, monthCode, day);
    if (result.isOk()) {
      auto isoDate = ToISODate(result.inspect().get());

      // Make sure the resolved date is before |startISODate|.
      if (direction < 0 ? isoDate > startISODate : isoDate < startISODate) {
        calendarYear += direction;
        continue;
      }

      // Stop searching if |endISODate| was reached.
      if (direction < 0 ? isoDate < endISODate : isoDate > endISODate) {
        *resultDate = nullptr;
        return true;
      }

      *resultDate = result.unwrap();
      return true;
    }

    switch (result.inspectErr()) {
      case CalendarError::UnknownMonthCode: {
        MOZ_ASSERT(CalendarHasLeapMonths(calendar));
        MOZ_ASSERT(monthCode.isLeapMonth());

        // Try the next candidate year if the requested leap month doesn't
        // occur in the current year.
        calendarYear += direction;
        continue;
      }

      case CalendarError::OutOfRange: {
        // ICU4X throws an out-of-range error when:
        // 1. month > monthsInYear(year), or
        // 2. days > daysInMonthOf(year, month).
        //
        // Case 1 can't happen for month-codes, so it doesn't apply here.
        // Case 2 can only happen when |day| is larger than the minimum number
        // of days in the month.
        MOZ_ASSERT(day > CalendarDaysInMonth(calendar, monthCode).first);

        // Try next candidate year to find an earlier year which can fulfill
        // the input request.
        calendarYear += direction;
        continue;
      }

      case CalendarError::UnknownEra:
        MOZ_ASSERT(false, "unexpected calendar error");
        break;

      case CalendarError::Generic:
        break;
    }

    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_TEMPORAL_CALENDAR_INTERNAL_ERROR);
    return false;
  }

  *resultDate = nullptr;
  return true;
}

/**
 * NonISOMonthDayToISOReferenceDate ( calendar, fields, overflow )
 */
static bool NonISOMonthDayToISOReferenceDate(JSContext* cx, CalendarId calendar,
                                             Handle<CalendarFields> fields,
                                             TemporalOverflow overflow,
                                             ISODate* result) {
  EraYears eraYears;
  if (fields.has(CalendarField::Year) || fields.has(CalendarField::EraYear)) {
    if (!CalendarFieldYear(cx, calendar, fields, &eraYears)) {
      return false;
    }
  } else {
    MOZ_ASSERT(fields.has(CalendarField::MonthCode));
  }

  Month month;
  if (!CalendarFieldMonth(cx, calendar, fields, overflow, &month)) {
    return false;
  }

  int32_t day;
  if (!CalendarFieldDay(cx, calendar, fields, overflow, &day)) {
    return false;
  }

  auto cal = CreateICU4XCalendar(calendar);

  // We first have to compute the month-code if it wasn't provided to us.
  auto monthCode = month.code;
  if (fields.has(CalendarField::Year) || fields.has(CalendarField::EraYear)) {
    auto date = CreateDateFrom(cx, calendar, cal.get(), eraYears, month, day,
                               fields, overflow);
    if (!date) {
      return false;
    }

    // This operation throws a RangeError if the ISO 8601 year corresponding to
    // `fields.[[Year]]` is outside the valid limits.
    auto isoDate = ToISODate(date.get());
    if (!ISODateWithinLimits(isoDate)) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_TEMPORAL_PLAIN_DATE_INVALID);
      return false;
    }

    if (!fields.has(CalendarField::MonthCode)) {
      monthCode = CalendarDateMonthCode(calendar, date.get());
    }
    MOZ_ASSERT(monthCode != MonthCode{});

    if (overflow == TemporalOverflow::Constrain) {
      // Call into ICU4X if `day` exceeds the minimum number of days.
      int32_t minDaysInMonth = CalendarDaysInMonth(calendar, monthCode).first;
      if (day > minDaysInMonth) {
        day = DayOfMonth(date.get());
      }
    } else {
      MOZ_ASSERT(overflow == TemporalOverflow::Reject);
      MOZ_ASSERT(day == DayOfMonth(date.get()));
    }
  } else {
    MOZ_ASSERT(monthCode != MonthCode{});

    // Constrain `day` to maximum possible day of the input month.
    int32_t maxDaysInMonth = CalendarDaysInMonth(calendar, monthCode).second;
    if (overflow == TemporalOverflow::Constrain) {
      day = std::min(day, maxDaysInMonth);
    } else {
      MOZ_ASSERT(overflow == TemporalOverflow::Reject);

      if (day > maxDaysInMonth) {
        ReportCalendarFieldOverflow(cx, "day", day);
        return false;
      }
    }
  }

  if (calendar == CalendarId::Chinese || calendar == CalendarId::Dangi) {
    int32_t referenceYear =
        EastAsianCalendarReferenceISOYear(calendar, monthCode, day);
    if (referenceYear == 0) {
      if (overflow == TemporalOverflow::Reject) {
        ReportCalendarFieldOverflow(cx, "day", day);
        return false;
      }
      monthCode = MonthCode{monthCode.ordinal()};
    }
  }

  constexpr ISODate candidates[][2] = {
      // The reference date is the latest ISO 8601 date corresponding to the
      // calendar date that is between January 1, 1900 and December 31, 1972
      // inclusive.
      {ISODate{1972, 12, 31}, ISODate{1900, 1, 1}},

      // If there is no such date, it is the earliest ISO 8601 date
      // corresponding to the calendar date between January 1, 1973 and
      // December 31, 2035.
      {ISODate{1973, 1, 1}, ISODate{2035, 12, 31}},

      // If there is still no such date, it is the latest ISO 8601 date
      // corresponding to the calendar date on or before December 31, 1899.
      //
      // Year 1600 is sufficient to find all possible month-days, even for
      // rare cases like `{calendar: "chinese", monthCode: "M08L", day: 30}`.
      {ISODate{1899, 12, 31}, ISODate{1600, 1, 1}},
  };

  UniqueICU4XDate date;
  for (const auto& [start, end] : candidates) {
    if (!NonISOMonthDayToISOReferenceDate(cx, calendar, cal.get(), start, end,
                                          monthCode, day, &date)) {
      return false;
    }
    if (date) {
      break;
    }
  }

  // We shouldn't end up here, but just in case still handle a missing date and
  // report an error.
  if (!date) {
    ReportCalendarFieldOverflow(cx, "day", day);
    return false;
  }

  *result = ToISODate(date.get());

  MOZ_ASSERT_IF(
      calendar == CalendarId::Chinese || calendar == CalendarId::Dangi,
      result->year ==
          EastAsianCalendarReferenceISOYear(calendar, monthCode, day));
  return true;
}

/**
 * CalendarMonthDayToISOReferenceDate ( calendar, fields, overflow )
 */
static bool CalendarMonthDayToISOReferenceDate(JSContext* cx,
                                               CalendarId calendar,
                                               Handle<CalendarFields> fields,
                                               TemporalOverflow overflow,
                                               ISODate* result) {
  // Step 1.
  if (calendar == CalendarId::ISO8601) {
    // Step 1.a.
    MOZ_ASSERT(fields.has(CalendarField::Month) ||
               fields.has(CalendarField::MonthCode));
    MOZ_ASSERT(fields.has(CalendarField::Day));

    // Remaining steps from CalendarResolveFields to resolve the month.
    double month;
    if (!ISOCalendarResolveMonth(cx, fields, &month)) {
      return false;
    }

    // Step 1.b.
    int32_t referenceISOYear = 1972;

    // Step 1.c.
    double year =
        !fields.has(CalendarField::Year) ? referenceISOYear : fields.year();

    int32_t intYear;
    if (!mozilla::NumberEqualsInt32(year, &intYear)) {
      // Calendar cycles repeat every 400 years in the Gregorian calendar.
      intYear = int32_t(std::fmod(year, 400));
    }

    // Step 1.d.
    ISODate regulated;
    if (!RegulateISODate(cx, intYear, month, fields.day(), overflow,
                         &regulated)) {
      return false;
    }

    // Step 1.e.
    *result = {referenceISOYear, regulated.month, regulated.day};
    return true;
  }

  // Step 2.
  return NonISOMonthDayToISOReferenceDate(cx, calendar, fields, overflow,
                                          result);
}

enum class FieldType { Date, YearMonth, MonthDay };

/**
 * NonISOResolveFields ( calendar, fields, type )
 */
static bool NonISOResolveFields(JSContext* cx, CalendarId calendar,
                                Handle<CalendarFields> fields, FieldType type) {
  // Date and Month-Day require |day| to be present.
  bool requireDay = type == FieldType::Date || type == FieldType::MonthDay;

  // Date and Year-Month require |year| (or |eraYear|) to be present.
  // Month-Day requires |year| (or |eraYear|) if |monthCode| is absent.
  // Month-Day requires |year| (or |eraYear|) if |month| is present, even if
  // |monthCode| is also present.
  bool requireYear = type == FieldType::Date || type == FieldType::YearMonth ||
                     !fields.has(CalendarField::MonthCode) ||
                     fields.has(CalendarField::Month);

  // Determine if any calendar fields are missing.
  const char* missingField = nullptr;
  if (!fields.has(CalendarField::MonthCode) &&
      !fields.has(CalendarField::Month)) {
    // |monthCode| or |month| must be present.
    missingField = "monthCode";
  } else if (requireDay && !fields.has(CalendarField::Day)) {
    missingField = "day";
  } else if (!CalendarSupportsEra(calendar)) {
    if (requireYear && !fields.has(CalendarField::Year)) {
      missingField = "year";
    }
  } else {
    if (fields.has(CalendarField::Era) != fields.has(CalendarField::EraYear)) {
      // |era| and |eraYear| must either both be present or both absent.
      missingField = fields.has(CalendarField::Era) ? "eraYear" : "era";
    } else if (requireYear && !fields.has(CalendarField::EraYear) &&
               !fields.has(CalendarField::Year)) {
      missingField = "eraYear";
    }
  }

  if (missingField) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_TEMPORAL_CALENDAR_MISSING_FIELD,
                              missingField);
    return false;
  }

  return true;
}

/**
 * CalendarResolveFields ( calendar, fields, type )
 */
static bool CalendarResolveFields(JSContext* cx, CalendarId calendar,
                                  Handle<CalendarFields> fields,
                                  FieldType type) {
  // Step 1.
  if (calendar == CalendarId::ISO8601) {
    // Steps 1.a-e.
    const char* missingField = nullptr;
    if ((type == FieldType::Date || type == FieldType::YearMonth) &&
        !fields.has(CalendarField::Year)) {
      missingField = "year";
    } else if ((type == FieldType::Date || type == FieldType::MonthDay) &&
               !fields.has(CalendarField::Day)) {
      missingField = "day";
    } else if (!fields.has(CalendarField::MonthCode) &&
               !fields.has(CalendarField::Month)) {
      missingField = "month";
    }

    if (missingField) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_TEMPORAL_CALENDAR_MISSING_FIELD,
                                missingField);
      return false;
    }

    // Steps 1.f-n. (Handled in ISOCalendarResolveMonth.)

    return true;
  }

  // Step 2.
  return NonISOResolveFields(cx, calendar, fields, type);
}

/**
 * CalendarISOToDate ( calendar, isoDate )
 * NonISOCalendarISOToDate ( calendar, isoDate )
 * CalendarDateEra ( calendar, date )
 *
 * Return the Calendar Date Record's [[Era]] field.
 */
bool js::temporal::CalendarEra(JSContext* cx, Handle<CalendarValue> calendar,
                               const ISODate& date,
                               MutableHandle<Value> result) {
  auto calendarId = calendar.identifier();

  // Step 1.
  if (calendarId == CalendarId::ISO8601) {
    result.setUndefined();
    return true;
  }

  // Step 2.
  if (!CalendarSupportsEra(calendarId)) {
    result.setUndefined();
    return true;
  }

  // TODO: Remove when we update to the next ICU4X release.
  // See: https://github.com/unicode-org/icu4x/pull/7503
  if (calendarId == CalendarId::Japanese && date.year <= 1872) {
    calendarId = CalendarId::Gregorian;
  }

  auto era = EraCode::Standard;

  // Call into ICU4X if the calendar has more than one era.
  auto eras = CalendarEras(calendarId);
  if (eras.size() > 1) {
    auto cal = CreateICU4XCalendar(calendarId);
    auto dt = CreateICU4XDate(cx, date, calendarId, cal.get());
    if (!dt) {
      return false;
    }

    if (!CalendarDateEra(cx, calendarId, dt.get(), &era)) {
      return false;
    }
  } else {
    MOZ_ASSERT(*eras.begin() == EraCode::Standard,
               "single era calendars use only the standard era");
  }

  auto* str = NewStringCopy<CanGC>(cx, CalendarEraName(calendarId, era));
  if (!str) {
    return false;
  }

  result.setString(str);
  return true;
}

/**
 * CalendarISOToDate ( calendar, isoDate )
 * NonISOCalendarISOToDate ( calendar, isoDate )
 * CalendarDateEraYear ( calendar, date )
 *
 * Return the Calendar Date Record's [[EraYear]] field.
 */
bool js::temporal::CalendarEraYear(JSContext* cx,
                                   Handle<CalendarValue> calendar,
                                   const ISODate& date,
                                   MutableHandle<Value> result) {
  auto calendarId = calendar.identifier();

  // Step 1.
  if (calendarId == CalendarId::ISO8601) {
    result.setUndefined();
    return true;
  }

  // Step 2.
  if (!CalendarSupportsEra(calendarId)) {
    result.setUndefined();
    return true;
  }

  auto eras = CalendarEras(calendarId);
  if (eras.size() == 1) {
    // Return the calendar year for calendars with a single era.
    return CalendarYear(cx, calendar, date, result);
  }
  MOZ_ASSERT(eras.size() > 1);

  // TODO: Remove when we update to the next ICU4X release.
  // See: https://github.com/unicode-org/icu4x/pull/7503
  if (calendarId == CalendarId::Japanese && date.year <= 1872) {
    calendarId = CalendarId::Gregorian;
  }

  auto cal = CreateICU4XCalendar(calendarId);
  auto dt = CreateICU4XDate(cx, date, calendarId, cal.get());
  if (!dt) {
    return false;
  }

  int32_t year = icu4x::capi::icu4x_Date_era_year_or_related_iso_mv1(dt.get());
  result.setInt32(year);
  return true;
}

/**
 * CalendarISOToDate ( calendar, isoDate )
 * NonISOCalendarISOToDate ( calendar, isoDate )
 * CalendarDateArithmeticYear ( calendar, date )
 *
 * Return the Calendar Date Record's [[Year]] field.
 */
bool js::temporal::CalendarYear(JSContext* cx, Handle<CalendarValue> calendar,
                                const ISODate& date,
                                MutableHandle<Value> result) {
  auto calendarId = calendar.identifier();

  // Step 1.
  if (calendarId == CalendarId::ISO8601) {
    result.setInt32(date.year);
    return true;
  }

  // Step 2.
  auto cal = CreateICU4XCalendar(calendarId);
  auto dt = CreateICU4XDate(cx, date, calendarId, cal.get());
  if (!dt) {
    return false;
  }

  int32_t year = CalendarDateYear(calendarId, dt.get());
  result.setInt32(year);
  return true;
}

/**
 * CalendarISOToDate ( calendar, isoDate )
 * NonISOCalendarISOToDate ( calendar, isoDate )
 *
 * Return the Calendar Date Record's [[Month]] field.
 */
bool js::temporal::CalendarMonth(JSContext* cx, Handle<CalendarValue> calendar,
                                 const ISODate& date,
                                 MutableHandle<Value> result) {
  auto calendarId = calendar.identifier();

  // Step 1.
  if (calendarId == CalendarId::ISO8601) {
    result.setInt32(date.month);
    return true;
  }

  // Step 2.
  auto cal = CreateICU4XCalendar(calendarId);
  auto dt = CreateICU4XDate(cx, date, calendarId, cal.get());
  if (!dt) {
    return false;
  }

  int32_t month = OrdinalMonth(dt.get());
  result.setInt32(month);
  return true;
}

/**
 * CalendarISOToDate ( calendar, isoDate )
 * NonISOCalendarISOToDate ( calendar, isoDate )
 *
 * Return the Calendar Date Record's [[MonthCode]] field.
 */
bool js::temporal::CalendarMonthCode(JSContext* cx,
                                     Handle<CalendarValue> calendar,
                                     const ISODate& date,
                                     MutableHandle<Value> result) {
  auto calendarId = calendar.identifier();

  // Step 1.
  if (calendarId == CalendarId::ISO8601) {
    // Steps 1.a-b.
    auto monthCode = MonthCode{date.month};
    JSString* str = NewStringCopy<CanGC>(cx, std::string_view{monthCode});
    if (!str) {
      return false;
    }

    result.setString(str);
    return true;
  }

  // Step 2.
  auto cal = CreateICU4XCalendar(calendarId);
  auto dt = CreateICU4XDate(cx, date, calendarId, cal.get());
  if (!dt) {
    return false;
  }

  auto monthCode = CalendarDateMonthCode(calendarId, dt.get());
  auto* str = NewStringCopy<CanGC>(cx, std::string_view{monthCode});
  if (!str) {
    return false;
  }

  result.setString(str);
  return true;
}

/**
 * CalendarISOToDate ( calendar, isoDate )
 * NonISOCalendarISOToDate ( calendar, isoDate )
 *
 * Return the Calendar Date Record's [[Day]] field.
 */
bool js::temporal::CalendarDay(JSContext* cx, Handle<CalendarValue> calendar,
                               const ISODate& date,
                               MutableHandle<Value> result) {
  auto calendarId = calendar.identifier();

  // Step 1.
  if (calendarId == CalendarId::ISO8601) {
    result.setInt32(date.day);
    return true;
  }

  // Step 2.
  auto cal = CreateICU4XCalendar(calendarId);
  auto dt = CreateICU4XDate(cx, date, calendarId, cal.get());
  if (!dt) {
    return false;
  }

  int32_t day = DayOfMonth(dt.get());
  result.setInt32(day);
  return true;
}

/**
 * CalendarISOToDate ( calendar, isoDate )
 * NonISOCalendarISOToDate ( calendar, isoDate )
 *
 * Return the Calendar Date Record's [[DayOfWeek]] field.
 */
bool js::temporal::CalendarDayOfWeek(JSContext* cx,
                                     Handle<CalendarValue> calendar,
                                     const ISODate& date,
                                     MutableHandle<Value> result) {
  auto calendarId = calendar.identifier();

  // Step 1.
  if (calendarId == CalendarId::ISO8601) {
    result.setInt32(ISODayOfWeek(date));
    return true;
  }

  // Step 2.
  auto cal = CreateICU4XCalendar(calendarId);
  auto dt = CreateICU4XDate(cx, date, calendarId, cal.get());
  if (!dt) {
    return false;
  }

  // Week day codes are correctly ordered.
  static_assert(icu4x::capi::Weekday_Monday == 1);
  static_assert(icu4x::capi::Weekday_Tuesday == 2);
  static_assert(icu4x::capi::Weekday_Wednesday == 3);
  static_assert(icu4x::capi::Weekday_Thursday == 4);
  static_assert(icu4x::capi::Weekday_Friday == 5);
  static_assert(icu4x::capi::Weekday_Saturday == 6);
  static_assert(icu4x::capi::Weekday_Sunday == 7);

  icu4x::capi::Weekday day = icu4x::capi::icu4x_Date_day_of_week_mv1(dt.get());
  result.setInt32(static_cast<int32_t>(day));
  return true;
}

/**
 * CalendarISOToDate ( calendar, isoDate )
 * NonISOCalendarISOToDate ( calendar, isoDate )
 *
 * Return the Calendar Date Record's [[DayOfYear]] field.
 */
bool js::temporal::CalendarDayOfYear(JSContext* cx,
                                     Handle<CalendarValue> calendar,
                                     const ISODate& date,
                                     MutableHandle<Value> result) {
  auto calendarId = calendar.identifier();

  // Step 1.
  if (calendarId == CalendarId::ISO8601) {
    result.setInt32(ISODayOfYear(date));
    return true;
  }

  // Step 2.
  auto cal = CreateICU4XCalendar(calendarId);
  auto dt = CreateICU4XDate(cx, date, calendarId, cal.get());
  if (!dt) {
    return false;
  }

  int32_t day = DayOfYear(dt.get());
  result.setInt32(day);
  return true;
}

/**
 * CalendarISOToDate ( calendar, isoDate )
 * NonISOCalendarISOToDate ( calendar, isoDate )
 *
 * Return the Calendar Date Record's [[WeekOfYear]].[[Week]] field.
 */
bool js::temporal::CalendarWeekOfYear(JSContext* cx,
                                      Handle<CalendarValue> calendar,
                                      const ISODate& date,
                                      MutableHandle<Value> result) {
  auto calendarId = calendar.identifier();

  // Step 1.
  if (calendarId == CalendarId::ISO8601) {
    result.setInt32(ISOWeekOfYear(date).week);
    return true;
  }

  // Step 2.
  //
  // Non-Gregorian calendars don't get week-of-year support for now.
  //
  // https://github.com/tc39/proposal-temporal/issues/3096
  // https://github.com/tc39/proposal-intl-era-monthcode/issues/15
  result.setUndefined();
  return true;
}

/**
 * CalendarISOToDate ( calendar, isoDate )
 * NonISOCalendarISOToDate ( calendar, isoDate )
 *
 * Return the Calendar Date Record's [[WeekOfYear]].[[Year]] field.
 */
bool js::temporal::CalendarYearOfWeek(JSContext* cx,
                                      Handle<CalendarValue> calendar,
                                      const ISODate& date,
                                      MutableHandle<Value> result) {
  auto calendarId = calendar.identifier();

  // Step 1.
  if (calendarId == CalendarId::ISO8601) {
    result.setInt32(ISOWeekOfYear(date).year);
    return true;
  }

  // Step 2.
  //
  // Non-ISO8601 calendars don't get year-of-week support for now.
  //
  // https://github.com/tc39/proposal-temporal/issues/3096
  // https://github.com/tc39/proposal-intl-era-monthcode/issues/15
  result.setUndefined();
  return true;
}

/**
 * CalendarISOToDate ( calendar, isoDate )
 * NonISOCalendarISOToDate ( calendar, isoDate )
 *
 * Return the Calendar Date Record's [[DaysInWeek]] field.
 */
bool js::temporal::CalendarDaysInWeek(JSContext* cx,
                                      Handle<CalendarValue> calendar,
                                      const ISODate& date,
                                      MutableHandle<Value> result) {
  // All supported ICU4X calendars use a 7-day week and so does the ISO 8601
  // calendar.
  //
  // This function isn't supported through the ICU4X FFI, so we have to
  // hardcode the result.

  // Step 1-2.
  result.setInt32(7);
  return true;
}

/**
 * CalendarISOToDate ( calendar, isoDate )
 * NonISOCalendarISOToDate ( calendar, isoDate )
 *
 * Return the Calendar Date Record's [[DaysInMonth]] field.
 */
bool js::temporal::CalendarDaysInMonth(JSContext* cx,
                                       Handle<CalendarValue> calendar,
                                       const ISODate& date,
                                       MutableHandle<Value> result) {
  auto calendarId = calendar.identifier();

  // Step 1.
  if (calendarId == CalendarId::ISO8601) {
    result.setInt32(::ISODaysInMonth(date.year, date.month));
    return true;
  }

  // Step 2.
  auto cal = CreateICU4XCalendar(calendarId);
  auto dt = CreateICU4XDate(cx, date, calendarId, cal.get());
  if (!dt) {
    return false;
  }

  int32_t days = DaysInMonth(dt.get());
  result.setInt32(days);
  return true;
}

/**
 * CalendarISOToDate ( calendar, isoDate )
 * NonISOCalendarISOToDate ( calendar, isoDate )
 *
 * Return the Calendar Date Record's [[DaysInYear]] field.
 */
bool js::temporal::CalendarDaysInYear(JSContext* cx,
                                      Handle<CalendarValue> calendar,
                                      const ISODate& date,
                                      MutableHandle<Value> result) {
  auto calendarId = calendar.identifier();

  // Step 1.
  if (calendarId == CalendarId::ISO8601) {
    result.setInt32(ISODaysInYear(date.year));
    return true;
  }

  // Step 2.
  auto cal = CreateICU4XCalendar(calendarId);
  auto dt = CreateICU4XDate(cx, date, calendarId, cal.get());
  if (!dt) {
    return false;
  }

  int32_t days = DaysInYear(dt.get());
  result.setInt32(days);
  return true;
}

/**
 * CalendarISOToDate ( calendar, isoDate )
 * NonISOCalendarISOToDate ( calendar, isoDate )
 *
 * Return the Calendar Date Record's [[MonthsInYear]] field.
 */
bool js::temporal::CalendarMonthsInYear(JSContext* cx,
                                        Handle<CalendarValue> calendar,
                                        const ISODate& date,
                                        MutableHandle<Value> result) {
  auto calendarId = calendar.identifier();

  // Step 1.
  if (calendarId == CalendarId::ISO8601) {
    result.setInt32(12);
    return true;
  }

  // Step 2
  auto cal = CreateICU4XCalendar(calendarId);
  auto dt = CreateICU4XDate(cx, date, calendarId, cal.get());
  if (!dt) {
    return false;
  }

  int32_t months = MonthsInYear(dt.get());
  result.setInt32(months);
  return true;
}

/**
 * CalendarISOToDate ( calendar, isoDate )
 * NonISOCalendarISOToDate ( calendar, isoDate )
 *
 * Return the Calendar Date Record's [[InLeapYear]] field.
 */
bool js::temporal::CalendarInLeapYear(JSContext* cx,
                                      Handle<CalendarValue> calendar,
                                      const ISODate& date,
                                      MutableHandle<Value> result) {
  auto calendarId = calendar.identifier();

  // Step 1.
  if (calendarId == CalendarId::ISO8601) {
    result.setBoolean(IsISOLeapYear(date.year));
    return true;
  }

  // Step 2.

  // FIXME: Not supported in ICU4X.
  //
  // https://github.com/unicode-org/icu4x/issues/5654

  auto cal = CreateICU4XCalendar(calendarId);
  auto dt = CreateICU4XDate(cx, date, calendarId, cal.get());
  if (!dt) {
    return false;
  }

  bool inLeapYear = false;
  switch (calendarId) {
    case CalendarId::ISO8601:
    case CalendarId::Buddhist:
    case CalendarId::Gregorian:
    case CalendarId::Japanese:
    case CalendarId::Coptic:
    case CalendarId::Ethiopian:
    case CalendarId::EthiopianAmeteAlem:
    case CalendarId::Indian:
    case CalendarId::Persian:
    case CalendarId::ROC: {
      MOZ_ASSERT(!CalendarHasLeapMonths(calendarId));

      // Solar calendars have either 365 or 366 days per year.
      int32_t days = DaysInYear(dt.get());
      MOZ_ASSERT(days == 365 || days == 366);

      // Leap years have 366 days.
      inLeapYear = days == 366;
      break;
    }

    case CalendarId::IslamicCivil:
    case CalendarId::IslamicTabular:
    case CalendarId::IslamicUmmAlQura: {
      MOZ_ASSERT(!CalendarHasLeapMonths(calendarId));

      // Lunar Islamic calendars have either 354 or 355 days per year.
      //
      // Allow 353 days to workaround
      // <https://github.com/unicode-org/icu4x/issues/4930>.
      int32_t days = DaysInYear(dt.get());
      MOZ_ASSERT(days == 353 || days == 354 || days == 355);

      // Leap years have 355 days.
      inLeapYear = days == 355;
      break;
    }

    case CalendarId::Chinese:
    case CalendarId::Dangi:
    case CalendarId::Hebrew: {
      MOZ_ASSERT(CalendarHasLeapMonths(calendarId));

      // Calendars with separate leap months have either 12 or 13 months per
      // year.
      int32_t months = MonthsInYear(dt.get());
      MOZ_ASSERT(months == 12 || months == 13);

      // Leap years have 13 months.
      inLeapYear = months == 13;
      break;
    }
  }

  result.setBoolean(inLeapYear);
  return true;
}

enum class DateFieldType { Date, YearMonth, MonthDay };

/**
 * ISODateToFields ( calendar, isoDate, type )
 */
static bool ISODateToFields(JSContext* cx, Handle<CalendarValue> calendar,
                            const ISODate& date, DateFieldType type,
                            MutableHandle<CalendarFields> result) {
  auto calendarId = calendar.identifier();

  // Step 1.
  result.set(CalendarFields{});

  // Steps 2-6. (Optimization for the ISO 8601 calendar.)
  if (calendarId == CalendarId::ISO8601) {
    // Step 2. (Not applicable in our implementation.)

    // Step 3.
    result.setMonthCode(MonthCode{date.month});

    // Step 4.
    if (type == DateFieldType::MonthDay || type == DateFieldType::Date) {
      result.setDay(date.day);
    }

    // Step 5.
    if (type == DateFieldType::YearMonth || type == DateFieldType::Date) {
      result.setYear(date.year);
    }

    // Step 6.
    return true;
  }

  // Step 2.
  auto cal = CreateICU4XCalendar(calendarId);
  auto dt = CreateICU4XDate(cx, date, calendarId, cal.get());
  if (!dt) {
    return false;
  }

  // Step 3.
  auto monthCode = CalendarDateMonthCode(calendarId, dt.get());
  result.setMonthCode(monthCode);

  // Step 4.
  if (type == DateFieldType::MonthDay || type == DateFieldType::Date) {
    int32_t day = DayOfMonth(dt.get());
    result.setDay(day);
  }

  // Step 5.
  if (type == DateFieldType::YearMonth || type == DateFieldType::Date) {
    int32_t year = CalendarDateYear(calendarId, dt.get());
    result.setYear(year);
  }

  // Step 6.
  return true;
}

/**
 * ISODateToFields ( calendar, isoDate, type )
 */
bool js::temporal::ISODateToFields(JSContext* cx, Handle<PlainDate> date,
                                   MutableHandle<CalendarFields> result) {
  return ISODateToFields(cx, date.calendar(), date, DateFieldType::Date,
                         result);
}

/**
 * ISODateToFields ( calendar, isoDate, type )
 */
bool js::temporal::ISODateToFields(JSContext* cx,
                                   Handle<PlainDateTime> dateTime,
                                   MutableHandle<CalendarFields> result) {
  return ISODateToFields(cx, dateTime.calendar(), dateTime.date(),
                         DateFieldType::Date, result);
}

/**
 * ISODateToFields ( calendar, isoDate, type )
 */
bool js::temporal::ISODateToFields(JSContext* cx,
                                   Handle<PlainMonthDay> monthDay,
                                   MutableHandle<CalendarFields> result) {
  return ISODateToFields(cx, monthDay.calendar(), monthDay.date(),
                         DateFieldType::MonthDay, result);
}

/**
 * ISODateToFields ( calendar, isoDate, type )
 */
bool js::temporal::ISODateToFields(JSContext* cx,
                                   Handle<PlainYearMonth> yearMonth,
                                   MutableHandle<CalendarFields> result) {
  return ISODateToFields(cx, yearMonth.calendar(), yearMonth.date(),
                         DateFieldType::YearMonth, result);
}

/**
 * CalendarDateFromFields ( calendar, fields, overflow )
 */
bool js::temporal::CalendarDateFromFields(JSContext* cx,
                                          Handle<CalendarValue> calendar,
                                          Handle<CalendarFields> fields,
                                          TemporalOverflow overflow,
                                          MutableHandle<PlainDate> result) {
  auto calendarId = calendar.identifier();

  // Step 1.
  if (!CalendarResolveFields(cx, calendarId, fields, FieldType::Date)) {
    return false;
  }

  // Step 2.
  ISODate date;
  if (!CalendarDateToISO(cx, calendarId, fields, overflow, &date)) {
    return false;
  }

  // Steps 3-4.
  return CreateTemporalDate(cx, date, calendar, result);
}

/**
 * CalendarYearMonthFromFields ( calendar, fields, overflow )
 */
bool js::temporal::CalendarYearMonthFromFields(
    JSContext* cx, Handle<CalendarValue> calendar,
    Handle<CalendarFields> fields, TemporalOverflow overflow,
    MutableHandle<PlainYearMonth> result) {
  auto calendarId = calendar.identifier();

  // Step 2.
  if (!CalendarResolveFields(cx, calendarId, fields, FieldType::YearMonth)) {
    return false;
  }

  // Step 1. (Reordered)
  Rooted<CalendarFields> resolvedFields(cx, CalendarFields{fields});
  resolvedFields.setDay(1);

  // Step 3.
  ISODate date;
  if (!CalendarDateToISO(cx, calendarId, resolvedFields, overflow, &date)) {
    return false;
  }

  // Steps 4-5.
  return CreateTemporalYearMonth(cx, date, calendar, result);
}

/**
 * CalendarMonthDayFromFields ( calendar, fields, overflow )
 */
bool js::temporal::CalendarMonthDayFromFields(
    JSContext* cx, Handle<CalendarValue> calendar,
    Handle<CalendarFields> fields, TemporalOverflow overflow,
    MutableHandle<PlainMonthDay> result) {
  auto calendarId = calendar.identifier();

  // Step 1.
  if (!CalendarResolveFields(cx, calendarId, fields, FieldType::MonthDay)) {
    return false;
  }

  // Step 2.
  ISODate date;
  if (!CalendarMonthDayToISOReferenceDate(cx, calendarId, fields, overflow,
                                          &date)) {
    return false;
  }

  // Step 3-4.
  return CreateTemporalMonthDay(cx, date, calendar, result);
}

/**
 * Mathematical Operations, "modulo" notation.
 */
static int32_t NonNegativeModulo(int64_t x, int32_t y) {
  MOZ_ASSERT(y > 0);

  int32_t result = mozilla::AssertedCast<int32_t>(x % y);
  return (result < 0) ? (result + y) : result;
}

/**
 * RegulateISODate ( year, month, day, overflow )
 *
 * With |overflow = "constrain"|.
 */
static ISODate ConstrainISODate(const ISODate& date) {
  const auto& [year, month, day] = date;

  // Step 1.a.
  int32_t m = std::clamp(month, 1, 12);

  // Step 1.b.
  int32_t daysInMonth = ::ISODaysInMonth(year, m);

  // Step 1.c.
  int32_t d = std::clamp(day, 1, daysInMonth);

  // Step 3.
  return {year, m, d};
}

/**
 * RegulateISODate ( year, month, day, overflow )
 */
static bool RegulateISODate(JSContext* cx, const ISODate& date,
                            TemporalOverflow overflow, ISODate* result) {
  // Step 1.
  if (overflow == TemporalOverflow::Constrain) {
    // Steps 1.a-c and 3.
    *result = ConstrainISODate(date);
    return true;
  }

  // Step 2.a.
  MOZ_ASSERT(overflow == TemporalOverflow::Reject);

  // Step 2.b.
  if (!ThrowIfInvalidISODate(cx, date)) {
    return false;
  }

  // Step 3. (Inlined call to CreateISODateRecord.)
  *result = date;
  return true;
}

struct BalancedYearMonth final {
  int64_t year = 0;
  int32_t month = 0;
};

/**
 * BalanceISOYearMonth ( year, month )
 */
static BalancedYearMonth BalanceISOYearMonth(int64_t year, int64_t month) {
  MOZ_ASSERT(std::abs(year) < (int64_t(1) << 33),
             "year is the addition of plain-date year with duration years");
  MOZ_ASSERT(std::abs(month) < (int64_t(1) << 33),
             "month is the addition of plain-date month with duration months");

  // Step 1. (Not applicable in our implementation.)

  // Step 2.
  int64_t balancedYear = year + temporal::FloorDiv(month - 1, 12);

  // Step 3.
  int32_t balancedMonth = NonNegativeModulo(month - 1, 12) + 1;
  MOZ_ASSERT(1 <= balancedMonth && balancedMonth <= 12);

  // Step 4.
  return {balancedYear, balancedMonth};
}

static BalancedYearMonth BalanceYearMonth(int64_t year, int64_t month,
                                          int32_t monthsPerYear) {
  MOZ_ASSERT(std::abs(year) < (int64_t(1) << 33),
             "year is the addition of plain-date year with duration years");
  MOZ_ASSERT(std::abs(month) < (int64_t(1) << 33),
             "month is the addition of plain-date month with duration months");

  int64_t balancedYear = year + temporal::FloorDiv(month - 1, monthsPerYear);

  int32_t balancedMonth = NonNegativeModulo(month - 1, monthsPerYear) + 1;
  MOZ_ASSERT(1 <= balancedMonth && balancedMonth <= monthsPerYear);

  return {balancedYear, balancedMonth};
}

/**
 * CalendarDateAdd ( calendar, isoDate, duration, overflow )
 */
static bool AddISODate(JSContext* cx, const ISODate& isoDate,
                       const DateDuration& duration, TemporalOverflow overflow,
                       ISODate* result) {
  MOZ_ASSERT(ISODateWithinLimits(isoDate));
  MOZ_ASSERT(IsValidDuration(duration));

  // Step 1.a.
  auto yearMonth = BalanceISOYearMonth(isoDate.year + duration.years,
                                       isoDate.month + duration.months);
  MOZ_ASSERT(1 <= yearMonth.month && yearMonth.month <= 12);

  auto balancedYear = mozilla::CheckedInt<int32_t>(yearMonth.year);
  if (!balancedYear.isValid()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_TEMPORAL_PLAIN_DATE_INVALID);
    return false;
  }

  // Step 1.b.
  ISODate regulated;
  if (!RegulateISODate(cx, {balancedYear.value(), yearMonth.month, isoDate.day},
                       overflow, &regulated)) {
    return false;
  }
  if (!ISODateWithinLimits(regulated)) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_TEMPORAL_PLAIN_DATE_INVALID);
    return false;
  }

  // Step 1.c.
  int64_t days = duration.days + duration.weeks * 7;

  // Step 1.d.
  ISODate balanced;
  if (!BalanceISODate(cx, regulated, days, &balanced)) {
    return false;
  }
  MOZ_ASSERT(IsValidISODate(balanced));

  *result = balanced;
  return true;
}

struct CalendarDate {
  int32_t year = 0;
  MonthCode monthCode;
  int32_t day = 0;
};

struct CalendarDateWithOrdinalMonth {
  int32_t year = 0;
  int32_t month = 0;
  int32_t day = 0;
};

/**
 * CompareISODate adjusted for calendar dates.
 */
static int32_t CompareCalendarDate(const CalendarDate& one,
                                   const CalendarDate& two) {
  if (one.year != two.year) {
    return one.year < two.year ? -1 : 1;
  }
  if (one.monthCode != two.monthCode) {
    return one.monthCode < two.monthCode ? -1 : 1;
  }
  if (one.day != two.day) {
    return one.day < two.day ? -1 : 1;
  }
  return 0;
}

/**
 * CompareISODate adjusted for calendar dates.
 */
static int32_t CompareCalendarDate(const CalendarDateWithOrdinalMonth& one,
                                   const CalendarDateWithOrdinalMonth& two) {
  return CompareISODate(ISODate{one.year, one.month, one.day},
                        ISODate{two.year, two.month, two.day});
}

/**
 * ISODateSurpasses ( sign, baseDate, isoDate2, years, months, weeks, days )
 */
static inline bool ISODateSurpasses(int32_t sign, const ISODate& one,
                                    const ISODate& two) {
  return CompareISODate(one, two) * sign > 0;
}

/**
 * CompareSurpasses ( sign, year, monthOrCode, day, target )
 */
static inline bool CompareSurpasses(int32_t sign, const CalendarDate& one,
                                    const CalendarDate& two) {
  return CompareCalendarDate(one, two) * sign > 0;
}

/**
 * CompareSurpasses ( sign, year, monthOrCode, day, target )
 */
static inline bool CompareSurpasses(int32_t sign,
                                    const CalendarDateWithOrdinalMonth& one,
                                    const CalendarDateWithOrdinalMonth& two) {
  return CompareCalendarDate(one, two) * sign > 0;
}

static CalendarDate ToCalendarDate(CalendarId calendarId,
                                   const icu4x::capi::Date* dt) {
  int32_t year = CalendarDateYear(calendarId, dt);
  auto monthCode = CalendarDateMonthCode(calendarId, dt);
  int32_t day = DayOfMonth(dt);

  return {year, monthCode, day};
}

static CalendarDateWithOrdinalMonth ToCalendarDateWithOrdinalMonth(
    CalendarId calendarId, const icu4x::capi::Date* dt) {
  MOZ_ASSERT(!CalendarHasLeapMonths(calendarId));

  int32_t year = CalendarDateYear(calendarId, dt);
  int32_t month = OrdinalMonth(dt);
  int32_t day = DayOfMonth(dt);

  return {year, month, day};
}

static bool AddYearMonthDuration(
    JSContext* cx, CalendarId calendarId,
    const CalendarDateWithOrdinalMonth& calendarDate,
    const DateDuration& duration, CalendarDate* result) {
  MOZ_ASSERT(!CalendarHasLeapMonths(calendarId));
  MOZ_ASSERT(IsValidDuration(duration));

  auto [year, month, day] = calendarDate;

  // Months per year are fixed, so we can directly compute the final number of
  // years.
  auto yearMonth =
      BalanceYearMonth(year + duration.years, month + duration.months,
                       CalendarMonthsPerYear(calendarId));

  auto balancedYear = mozilla::CheckedInt<int32_t>(yearMonth.year);
  if (!balancedYear.isValid()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_TEMPORAL_PLAIN_DATE_INVALID);
    return false;
  }

  *result = {balancedYear.value(), MonthCode{yearMonth.month}, day};
  return true;
}

static bool AddYearMonthDuration(JSContext* cx, CalendarId calendarId,
                                 const icu4x::capi::Calendar* calendar,
                                 const CalendarDate& calendarDate,
                                 const DateDuration& duration,
                                 TemporalOverflow overflow,
                                 CalendarDate* result) {
  MOZ_ASSERT(CalendarHasLeapMonths(calendarId));
  MOZ_ASSERT(IsValidDuration(duration));

  auto [year, monthCode, day] = calendarDate;

  // Add all duration years.
  auto durationYear = mozilla::CheckedInt<int32_t>(year) + duration.years;
  if (!durationYear.isValid()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_TEMPORAL_PLAIN_DATE_INVALID);
    return false;
  }
  year = durationYear.value();

  // Regulate according to |overflow|.
  auto firstDayOfMonth = CreateDateFromCodes(cx, calendarId, calendar, year,
                                             monthCode, 1, overflow);
  if (!firstDayOfMonth) {
    return false;
  }

  // Months per year are variable, so we have construct a new date for each
  // year to balance the years and months.
  int64_t months = duration.months;
  if (months != 0) {
    if (months > 0) {
      while (true) {
        // Check if adding |months| is still in the current year.
        int32_t month = OrdinalMonth(firstDayOfMonth.get());
        int32_t monthsInYear = MonthsInYear(firstDayOfMonth.get());
        if (month + months <= monthsInYear) {
          break;
        }

        // We've crossed a year boundary. Increase |year| and adjust |months|.
        year += 1;
        months -= (monthsInYear - month + 1);

        // Restart the loop with the first month of the next year.
        firstDayOfMonth = CreateDateFrom(cx, calendarId, calendar, year, 1, 1,
                                         TemporalOverflow::Constrain);
        if (!firstDayOfMonth) {
          return false;
        }
      }
    } else {
      int32_t monthsPerYear = CalendarMonthsPerYear(calendarId);

      while (true) {
        // Check if subtracting |months| is still in the current year.
        int32_t month = OrdinalMonth(firstDayOfMonth.get());
        if (month + months >= 1) {
          break;
        }

        // We've crossed a year boundary. Decrease |year| and adjust |months|.
        year -= 1;
        months += month;

        // Restart the loop with the last month of the previous year.
        firstDayOfMonth =
            CreateDateFrom(cx, calendarId, calendar, year, monthsPerYear, 1,
                           TemporalOverflow::Constrain);
        if (!firstDayOfMonth) {
          return false;
        }
      }
    }
    MOZ_ASSERT(std::abs(months) <= CalendarMonthsPerYear(calendarId));

    // Compute the actual month to find the correct month code.
    int32_t month =
        OrdinalMonth(firstDayOfMonth.get()) + static_cast<int32_t>(months);
    firstDayOfMonth = CreateDateFrom(cx, calendarId, calendar, year, month, 1,
                                     TemporalOverflow::Constrain);
    if (!firstDayOfMonth) {
      return false;
    }

    monthCode = CalendarDateMonthCode(calendarId, firstDayOfMonth.get());
  }

  *result = {year, monthCode, day};
  return true;
}

static bool AddNonISODate(JSContext* cx, CalendarId calendarId,
                          const ISODate& isoDate, const DateDuration& duration,
                          TemporalOverflow overflow, ISODate* result) {
  MOZ_ASSERT(ISODateWithinLimits(isoDate));
  MOZ_ASSERT(IsValidDuration(duration));

  auto cal = CreateICU4XCalendar(calendarId);

  auto dt = CreateICU4XDate(cx, isoDate, calendarId, cal.get());
  if (!dt) {
    return false;
  }

  CalendarDate calendarDate;
  if (!CalendarHasLeapMonths(calendarId)) {
    auto date = ToCalendarDateWithOrdinalMonth(calendarId, dt.get());
    if (!AddYearMonthDuration(cx, calendarId, date, duration, &calendarDate)) {
      return false;
    }
  } else {
    auto date = ToCalendarDate(calendarId, dt.get());
    if (!AddYearMonthDuration(cx, calendarId, cal.get(), date, duration,
                              overflow, &calendarDate)) {
      return false;
    }
  }

  // Regulate according to |overflow|.
  auto regulated =
      CreateDateFromCodes(cx, calendarId, cal.get(), calendarDate.year,
                          calendarDate.monthCode, calendarDate.day, overflow);
  if (!regulated) {
    return false;
  }

  // Compute the corresponding ISO date.
  auto regulatedIso = ToISODate(regulated.get());
  if (!ISODateWithinLimits(regulatedIso)) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_TEMPORAL_PLAIN_DATE_INVALID);
    return false;
  }

  // Add duration days and weeks.
  int64_t days = duration.days + duration.weeks * 7;

  // Adding days isn't calendar-specific, so we can use BalanceISODate.
  ISODate balancedIso;
  if (!BalanceISODate(cx, regulatedIso, days, &balancedIso)) {
    return false;
  }
  MOZ_ASSERT(IsValidISODate(balancedIso));

  *result = balancedIso;
  return true;
}

/**
 * NonISODateAdd ( calendar, isoDate, duration, overflow )
 */
static bool NonISODateAdd(JSContext* cx, CalendarId calendarId,
                          const ISODate& isoDate, const DateDuration& duration,
                          TemporalOverflow overflow, ISODate* result) {
  // ICU4X doesn't yet provide a public API for CalendarDateAdd.
  //
  // https://github.com/unicode-org/icu4x/issues/3964

  // If neither |years| nor |months| are present, just delegate to the ISO 8601
  // calendar version. This works because all supported calendars use a 7-days
  // week.
  if (duration.years == 0 && duration.months == 0) {
    return AddISODate(cx, isoDate, duration, overflow, result);
  }

  switch (calendarId) {
    case CalendarId::ISO8601:
    case CalendarId::Buddhist:
    case CalendarId::Gregorian:
    case CalendarId::Japanese:
    case CalendarId::ROC:
      // Use the ISO 8601 calendar if the calendar system starts its year at the
      // same time as the ISO 8601 calendar and all months exactly match the
      // ISO 8601 calendar months.
      return AddISODate(cx, isoDate, duration, overflow, result);

    case CalendarId::Chinese:
    case CalendarId::Coptic:
    case CalendarId::Dangi:
    case CalendarId::Ethiopian:
    case CalendarId::EthiopianAmeteAlem:
    case CalendarId::Hebrew:
    case CalendarId::Indian:
    case CalendarId::IslamicCivil:
    case CalendarId::IslamicTabular:
    case CalendarId::IslamicUmmAlQura:
    case CalendarId::Persian:
      return AddNonISODate(cx, calendarId, isoDate, duration, overflow, result);
  }
  MOZ_CRASH("invalid calendar id");
}

/**
 * CalendarDateAdd ( calendar, isoDate, duration, overflow )
 */
bool js::temporal::CalendarDateAdd(JSContext* cx,
                                   Handle<CalendarValue> calendar,
                                   const ISODate& isoDate,
                                   const DateDuration& duration,
                                   TemporalOverflow overflow, ISODate* result) {
  MOZ_ASSERT(ISODateWithinLimits(isoDate));
  MOZ_ASSERT(IsValidDuration(duration));

  auto calendarId = calendar.identifier();

  // Steps 1-2.
  if (calendarId == CalendarId::ISO8601) {
    if (!AddISODate(cx, isoDate, duration, overflow, result)) {
      return false;
    }
  } else {
    if (!NonISODateAdd(cx, calendarId, isoDate, duration, overflow, result)) {
      return false;
    }
  }

  // Step 3.
  if (!ISODateWithinLimits(*result)) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_TEMPORAL_PLAIN_DATE_INVALID);
    return false;
  }

  // Step 4.
  return true;
}

/**
 * CalendarDateUntil ( calendar, one, two, largestUnit )
 */
static DateDuration DifferenceISODate(const ISODate& one, const ISODate& two,
                                      TemporalUnit largestUnit) {
  MOZ_ASSERT(one != two);
  MOZ_ASSERT(ISODateWithinLimits(one));
  MOZ_ASSERT(ISODateWithinLimits(two));

  MOZ_ASSERT(TemporalUnit::Year <= largestUnit &&
             largestUnit <= TemporalUnit::Day);

  // Step 3.a.
  int32_t sign = -CompareISODate(one, two);
  MOZ_ASSERT(sign != 0);

  // Step 3.b.
  int32_t years = 0;

  // Step 3.d. (Reordered)
  int32_t months = 0;

  // Steps 3.c and 3.e.
  if (largestUnit == TemporalUnit::Year || largestUnit == TemporalUnit::Month) {
    years = two.year - one.year;
    months = two.month - one.month;

    auto intermediate = ISODate{one.year + years, one.month, one.day};
    if (ISODateSurpasses(sign, intermediate, two)) {
      years -= sign;
      months += 12 * sign;
    }

    intermediate = ISODate{one.year + years, one.month + months, one.day};
    if (intermediate.month > 12) {
      intermediate.month -= 12;
      intermediate.year += 1;
    } else if (intermediate.month < 1) {
      intermediate.month += 12;
      intermediate.year -= 1;
    }
    if (ISODateSurpasses(sign, intermediate, two)) {
      months -= sign;
    }

    if (largestUnit == TemporalUnit::Month) {
      months += years * 12;
      years = 0;
    }
  }

  // Balance intermediate result per ISODateSurpasses.
  auto intermediate = BalanceISOYearMonth(one.year + years, one.month + months);
  auto constrained = ConstrainISODate(
      ISODate{int32_t(intermediate.year), intermediate.month, one.day});

  // Step 3.f.
  int64_t weeks = 0;

  // Steps 3.h-j.
  int64_t days = MakeDay(two) - MakeDay(constrained);

  // Step 3.g. (Weeks computed from days.)
  if (largestUnit == TemporalUnit::Week) {
    weeks = days / 7;
    days %= 7;
  }

  // Step 3.k.
  auto result = DateDuration{
      int64_t(years),
      int64_t(months),
      int64_t(weeks),
      int64_t(days),
  };
  MOZ_ASSERT(IsValidDuration(result));
  return result;
}

/**
 * NonISODateUntil ( calendar, one, two, largestUnit )
 */
static bool DifferenceNonISODate(JSContext* cx, CalendarId calendarId,
                                 const ISODate& one, const ISODate& two,
                                 TemporalUnit largestUnit,
                                 DateDuration* result) {
  MOZ_ASSERT(!CalendarHasLeapMonths(calendarId));
  MOZ_ASSERT(one != two);
  MOZ_ASSERT(ISODateWithinLimits(one));
  MOZ_ASSERT(ISODateWithinLimits(two));

  MOZ_ASSERT(TemporalUnit::Year <= largestUnit &&
             largestUnit <= TemporalUnit::Month);

  // If the months per year are fixed, we can use a modified DifferenceISODate
  // implementation to compute the date duration.
  const int32_t monthsPerYear = CalendarMonthsPerYear(calendarId);

  auto cal = CreateICU4XCalendar(calendarId);

  auto dtOne = CreateICU4XDate(cx, one, calendarId, cal.get());
  if (!dtOne) {
    return false;
  }

  auto dtTwo = CreateICU4XDate(cx, two, calendarId, cal.get());
  if (!dtTwo) {
    return false;
  }

  auto oneDate = ToCalendarDateWithOrdinalMonth(calendarId, dtOne.get());
  auto twoDate = ToCalendarDateWithOrdinalMonth(calendarId, dtTwo.get());

  int32_t sign = -CompareCalendarDate(oneDate, twoDate);
  MOZ_ASSERT(sign != 0);

  int32_t years = twoDate.year - oneDate.year;
  int32_t months = twoDate.month - oneDate.month;

  // If |oneDate + years| surpasses |twoDate|, reduce |years| by one and add
  // |monthsPerYear| to |months|. The next step will balance the intermediate
  // result.
  auto intermediate = CalendarDateWithOrdinalMonth{oneDate.year + years,
                                                   oneDate.month, oneDate.day};
  if (CompareSurpasses(sign, intermediate, twoDate)) {
    years -= sign;
    months += monthsPerYear * sign;
  }

  // Add both |years| and |months| and then balance the intermediate result to
  // ensure its month is within the valid bounds.
  intermediate = CalendarDateWithOrdinalMonth{
      oneDate.year + years, oneDate.month + months, oneDate.day};
  if (intermediate.month > monthsPerYear) {
    intermediate.month -= monthsPerYear;
    intermediate.year += 1;
  } else if (intermediate.month < 1) {
    intermediate.month += monthsPerYear;
    intermediate.year -= 1;
  }

  // If |intermediate| surpasses |twoDate|, reduce |month| by one.
  if (CompareSurpasses(sign, intermediate, twoDate)) {
    months -= sign;
  }

  // Convert years to months if necessary.
  if (largestUnit == TemporalUnit::Month) {
    months += years * monthsPerYear;
    years = 0;
  }

  // Constrain to a proper calendar date.
  auto balanced = BalanceYearMonth(oneDate.year + years, oneDate.month + months,
                                   monthsPerYear);

  auto constrained = CreateDateFrom(
      cx, calendarId, cal.get(), static_cast<int32_t>(balanced.year),
      balanced.month, oneDate.day, TemporalOverflow::Constrain);
  if (!constrained) {
    return false;
  }

  auto constrainedIso = ToISODate(constrained.get());
  MOZ_ASSERT(!ISODateSurpasses(sign, constrainedIso, two),
             "constrained doesn't surpass two");

  int64_t days = MakeDay(two) - MakeDay(constrainedIso);

  *result = DateDuration{
      int64_t(years),
      int64_t(months),
      0,
      int64_t(days),
  };
  MOZ_ASSERT(IsValidDuration(*result));
  return true;
}

/**
 * NonISODateUntil ( calendar, one, two, largestUnit )
 */
static bool DifferenceNonISODateWithLeapMonth(
    JSContext* cx, CalendarId calendarId, const ISODate& one,
    const ISODate& two, TemporalUnit largestUnit, DateDuration* result) {
  MOZ_ASSERT(CalendarHasLeapMonths(calendarId));
  MOZ_ASSERT(one != two);
  MOZ_ASSERT(ISODateWithinLimits(one));
  MOZ_ASSERT(ISODateWithinLimits(two));

  MOZ_ASSERT(TemporalUnit::Year <= largestUnit &&
             largestUnit <= TemporalUnit::Month);

  auto cal = CreateICU4XCalendar(calendarId);

  auto dtOne = CreateICU4XDate(cx, one, calendarId, cal.get());
  if (!dtOne) {
    return false;
  }

  auto dtTwo = CreateICU4XDate(cx, two, calendarId, cal.get());
  if (!dtTwo) {
    return false;
  }

  auto oneDate = ToCalendarDate(calendarId, dtOne.get());
  auto twoDate = ToCalendarDate(calendarId, dtTwo.get());

  int32_t sign = -CompareCalendarDate(oneDate, twoDate);
  MOZ_ASSERT(sign != 0);

  int32_t years = twoDate.year - oneDate.year;

  // If |oneDate + years| surpasses |twoDate|, reduce |years| by one. The next
  // step will balance the intermediate result.
  auto unconstrainedDate =
      CalendarDate{oneDate.year + years, oneDate.monthCode, oneDate.day};
  if (CompareSurpasses(sign, unconstrainedDate, twoDate)) {
    years -= sign;
  }

  auto constrainedStartOfMonth =
      CreateDateFromCodes(cx, calendarId, cal.get(), oneDate.year + years,
                          oneDate.monthCode, 1, TemporalOverflow::Constrain);
  if (!constrainedStartOfMonth) {
    return false;
  }

  auto constrainedDateStartOfMonth =
      ToCalendarDate(calendarId, constrainedStartOfMonth.get());

  auto constrainedDate = CalendarDate{
      .year = constrainedDateStartOfMonth.year,
      .monthCode = constrainedDateStartOfMonth.monthCode,
      .day = oneDate.day,
  };
  if (CompareSurpasses(sign, constrainedDate, twoDate)) {
    years -= sign;
  }

  // Add as many months as possible without surpassing |twoDate|.
  int32_t months = 0;
  while (true) {
    CalendarDate intermediateDate;
    if (!AddYearMonthDuration(cx, calendarId, cal.get(), oneDate,
                              {years, months + sign},
                              TemporalOverflow::Constrain, &intermediateDate)) {
      return false;
    }
    if (CompareSurpasses(sign, intermediateDate, twoDate)) {
      break;
    }
    months += sign;
    constrainedDate = intermediateDate;
  }
  MOZ_ASSERT(std::abs(months) <= CalendarMonthsPerYear(calendarId));

  // Convert years to months if necessary.
  if (largestUnit == TemporalUnit::Month && years != 0) {
    auto monthsUntilEndOfYear = [](const icu4x::capi::Date* date) {
      int32_t month = OrdinalMonth(date);
      int32_t monthsInYear = MonthsInYear(date);
      MOZ_ASSERT(1 <= month && month <= monthsInYear);

      return monthsInYear - month + 1;
    };

    auto monthsSinceStartOfYear = [](const icu4x::capi::Date* date) {
      return OrdinalMonth(date) - 1;
    };

    // Add months until end of year resp. since start of year.
    if (sign > 0) {
      months += monthsUntilEndOfYear(dtOne.get());
    } else {
      months -= monthsSinceStartOfYear(dtOne.get());
    }

    // Months in full year.
    for (int32_t y = sign; y != years; y += sign) {
      auto dt =
          CreateDateFromCodes(cx, calendarId, cal.get(), oneDate.year + y,
                              MonthCode{1}, 1, TemporalOverflow::Constrain);
      if (!dt) {
        return false;
      }
      months += MonthsInYear(dt.get()) * sign;
    }

    // Add months since start of year resp. until end of year.
    auto dt =
        CreateDateFromCodes(cx, calendarId, cal.get(), oneDate.year + years,
                            oneDate.monthCode, 1, TemporalOverflow::Constrain);
    if (!dt) {
      return false;
    }
    if (sign > 0) {
      months += monthsSinceStartOfYear(dt.get());
    } else {
      months -= monthsUntilEndOfYear(dt.get());
    }

    years = 0;
  }

  auto constrained =
      CreateDateFromCodes(cx, calendarId, cal.get(), constrainedDate.year,
                          constrainedDate.monthCode, constrainedDate.day,
                          TemporalOverflow::Constrain);
  if (!constrained) {
    return false;
  }

  auto constrainedIso = ToISODate(constrained.get());
  MOZ_ASSERT(!ISODateSurpasses(sign, constrainedIso, two),
             "constrained doesn't surpass two");

  int64_t days = MakeDay(two) - MakeDay(constrainedIso);

  *result = DateDuration{
      int64_t(years),
      int64_t(months),
      0,
      int64_t(days),
  };
  MOZ_ASSERT(IsValidDuration(*result));
  return true;
}

/**
 * NonISODateUntil ( calendar, one, two, largestUnit )
 */
static bool NonISODateUntil(JSContext* cx, CalendarId calendarId,
                            const ISODate& one, const ISODate& two,
                            TemporalUnit largestUnit, DateDuration* result) {
  // ICU4X doesn't yet provide a public API for CalendarDateUntil.
  //
  // https://github.com/unicode-org/icu4x/issues/3964

  // Delegate to the ISO 8601 calendar for "weeks" and "days". This works
  // because all supported calendars use a 7-days week.
  if (largestUnit >= TemporalUnit::Week) {
    *result = DifferenceISODate(one, two, largestUnit);
    return true;
  }

  switch (calendarId) {
    case CalendarId::ISO8601:
    case CalendarId::Buddhist:
    case CalendarId::Gregorian:
    case CalendarId::Japanese:
    case CalendarId::ROC:
      // Use the ISO 8601 calendar if the calendar system starts its year at the
      // same time as the ISO 8601 calendar and all months exactly match the
      // ISO 8601 calendar months.
      *result = DifferenceISODate(one, two, largestUnit);
      return true;

    case CalendarId::Coptic:
    case CalendarId::Ethiopian:
    case CalendarId::EthiopianAmeteAlem:
    case CalendarId::Indian:
    case CalendarId::IslamicCivil:
    case CalendarId::IslamicTabular:
    case CalendarId::IslamicUmmAlQura:
    case CalendarId::Persian:
      return DifferenceNonISODate(cx, calendarId, one, two, largestUnit,
                                  result);

    case CalendarId::Chinese:
    case CalendarId::Dangi:
    case CalendarId::Hebrew:
      return DifferenceNonISODateWithLeapMonth(cx, calendarId, one, two,
                                               largestUnit, result);
  }
  MOZ_CRASH("invalid calendar id");
}

/**
 * CalendarDateUntil ( calendar, one, two, largestUnit )
 */
bool js::temporal::CalendarDateUntil(JSContext* cx,
                                     Handle<CalendarValue> calendar,
                                     const ISODate& one, const ISODate& two,
                                     TemporalUnit largestUnit,
                                     DateDuration* result) {
  MOZ_ASSERT(ISODateWithinLimits(one));
  MOZ_ASSERT(ISODateWithinLimits(two));
  MOZ_ASSERT(largestUnit <= TemporalUnit::Day);

  // Steps 1-2.
  if (one == two) {
    *result = {};
    return true;
  }

  // Step 3.
  auto calendarId = calendar.identifier();
  if (calendarId == CalendarId::ISO8601) {
    *result = DifferenceISODate(one, two, largestUnit);
    return true;
  }

  // Step 4.
  return NonISODateUntil(cx, calendarId, one, two, largestUnit, result);
}
