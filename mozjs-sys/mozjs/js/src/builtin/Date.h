/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * JS Date class interface.
 */

#ifndef builtin_Date_h
#define builtin_Date_h

#include "js/Date.h"

#include "jstypes.h"

#include "js/RootingAPI.h"
#include "js/TypeDecls.h"

class JSLinearString;

namespace js {

class JSOffThreadAtom;

/*
 * These functions provide a C interface to the date/time object
 */

/*
 * Construct a new Date Object from a time value given in milliseconds UTC
 * since the epoch.
 */
extern JSObject* NewDateObjectMsec(JSContext* cx, JS::ClippedTime t,
                                   JS::HandleObject proto = nullptr);

/*
 * Returns the current time in milliseconds since the epoch.
 */
JS::ClippedTime DateNow(JSContext* cx);

/**
 * Returns the result of calling |Date.parse|.
 */
JS::ClippedTime DateParse(JSContext* cx, const JSLinearString* str);

struct ParsedDate final {
  /**
   * Parsed date in milliseconds since the epoch.
   */
  int64_t date;

  /**
   * `true` if |date| is a local time. Otherwise `false` for UTC time.
   */
  bool isLocalTime;
};

/**
 * Returns the result of calling |Date.parse|.
 */
bool DateParse(const JSOffThreadAtom* str, ParsedDate* result);

/**
 * Convert from local time to UTC time.
 */
JS::ClippedTime LocalTimeToUTC(JSContext* cx, int64_t localTime);

/**
 * Convert from UTC time to local time.
 */
int64_t UTCToLocalTime(JSContext* cx, int64_t utcTime);

bool date_valueOf(JSContext* cx, unsigned argc, JS::Value* vp);

bool date_toPrimitive(JSContext* cx, unsigned argc, JS::Value* vp);

struct YearMonthDay {
  // Signed year in the range [-271821, 275760].
  int32_t year;

  // 0-indexed month, i.e. 0 is January, 1 is February, ..., 11 is December.
  int32_t month;

  // 1-indexed day of month.
  int32_t day;
};

/*
 * Split an epoch milliseconds value into year-month-day parts.
 */
YearMonthDay ToYearMonthDay(int64_t time);

struct HourMinuteSecond {
  // Hours from 0 to 23.
  int32_t hour;

  // Minutes from 0 to 59.
  int32_t minute;

  // Seconds from 0 to 59.
  int32_t second;
};

/*
 * Split an epoch milliseconds value into hour-minute-second parts.
 */
HourMinuteSecond ToHourMinuteSecond(int64_t epochMilliseconds);

} /* namespace js */

#endif /* builtin_Date_h */
