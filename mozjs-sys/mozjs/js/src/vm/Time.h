/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_Time_h
#define vm_Time_h

#include "mozilla/TimeStamp.h"

#include <stddef.h>
#include <stdint.h>

namespace js {

#if !JS_HAS_INTL_API
/*
 * Broken down form of 64 bit time value.
 */
struct PRMJTime {
  int32_t tm_usec; /* microseconds of second (0-999999) */
  int8_t tm_sec;   /* seconds of minute (0-59) */
  int8_t tm_min;   /* minutes of hour (0-59) */
  int8_t tm_hour;  /* hour of day (0-23) */
  int8_t tm_mday;  /* day of month (1-31) */
  int8_t tm_mon;   /* month of year (0-11) */
  int8_t tm_wday;  /* 0=sunday, 1=monday, ... */
  int32_t tm_year; /* absolute year, AD */
  int16_t tm_yday; /* day of year (0 to 365) */
  int8_t tm_isdst; /* non-zero if DST in effect */
};
#endif

/* Some handy constants */
constexpr inline int64_t PRMJ_USEC_PER_SEC = 1000000;
constexpr inline int64_t PRMJ_USEC_PER_MSEC = 1000;
constexpr inline int64_t PRMJ_NSEC_PER_USEC = 1000;

/* Return the current local time in micro-seconds */
extern int64_t PRMJ_Now();

#if !JS_HAS_INTL_API
/* Format a time value into a buffer. Same semantics as strftime() */
extern size_t PRMJ_FormatTime(char* buf, size_t buflen, const char* fmt,
                              const PRMJTime* tm, int timeZoneYear,
                              int offsetInSeconds);
#endif

class MOZ_RAII AutoIncrementalTimer {
  mozilla::TimeStamp startTime;
  mozilla::TimeDuration& output;

 public:
  AutoIncrementalTimer(const AutoIncrementalTimer&) = delete;
  AutoIncrementalTimer& operator=(const AutoIncrementalTimer&) = delete;

  explicit AutoIncrementalTimer(mozilla::TimeDuration& output_)
      : output(output_) {
    startTime = mozilla::TimeStamp::Now();
  }

  ~AutoIncrementalTimer() { output += mozilla::TimeStamp::Now() - startTime; }
};

}  // namespace js

#endif /* vm_Time_h */
