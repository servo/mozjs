/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "prtime.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PRBool debug_mode = PR_TRUE;

static char* dayOfWeek[] = {"Sun", "Mon", "Tue", "Wed",
                            "Thu", "Fri", "Sat", "???"};
static char* month[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul",
                        "Aug", "Sep", "Oct", "Nov", "Dec", "???"};

static void PrintExplodedTime(const PRExplodedTime* et) {
  PRInt32 totalOffset;
  PRInt32 hourOffset, minOffset;
  const char* sign;

  /* Print day of the week, month, day, hour, minute, and second */
  if (debug_mode)
    printf("%s %s %ld %02ld:%02ld:%02ld ", dayOfWeek[et->tm_wday],
           month[et->tm_month], et->tm_mday, et->tm_hour, et->tm_min,
           et->tm_sec);

  /* Print time zone */
  totalOffset = et->tm_params.tp_gmt_offset + et->tm_params.tp_dst_offset;
  if (totalOffset == 0) {
    if (debug_mode) {
      printf("UTC ");
    }
  } else {
    sign = "+";
    if (totalOffset < 0) {
      totalOffset = -totalOffset;
      sign = "-";
    }
    hourOffset = totalOffset / 3600;
    minOffset = (totalOffset % 3600) / 60;
    if (debug_mode) {
      printf("%s%02ld%02ld ", sign, hourOffset, minOffset);
    }
  }

  /* Print year */
  if (debug_mode) {
    printf("%hd", et->tm_year);
  }
}

static PRStatus KnownAnswerTest(void) {
  static const struct {
    const char* string;
    PRBool default_to_gmt;
    PRTime expected;
  } tests[] = {
      {"Mon, 15 Oct 2007 19:45:00 GMT", PR_FALSE, PR_INT64(1192477500000000)},
      {"15 Oct 07 19:45 GMT", PR_FALSE, PR_INT64(1192477500000000)},
      {"Mon Oct 15 12:45 PDT 2007", PR_FALSE, PR_INT64(1192477500000000)},
      {"16 Oct 2007 4:45-JST (Tuesday)", PR_FALSE, PR_INT64(1192477500000000)},
      // Not normalized.
      {"Mon Oct 15 12:44:60 PDT 2007", PR_FALSE, PR_INT64(1192477500000000)},
      // Not normalized.
      {"Sun Oct 14 36:45 PDT 2007", PR_FALSE, PR_INT64(1192477500000000)},
      {"Mon, 15 Oct 2007 19:45:23 GMT", PR_FALSE, PR_INT64(1192477523000000)},
  };

  PRBool failed = PR_FALSE;

  for (int i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
    PRTime result = 0;
    if (PR_ParseTimeString(tests[i].string, tests[i].default_to_gmt, &result) !=
        PR_SUCCESS) {
      printf("PR_ParseTimeString(\"%s\", %s, &result) failed\n",
             tests[i].string, tests[i].default_to_gmt ? "PR_TRUE" : "PR_FALSE");
      failed = PR_TRUE;
      continue;
    }
    if (result != tests[i].expected) {
      printf(
          "PR_ParseTimeString(\"%s\", %s, &result) returns %lld, expected "
          "%lld\n",
          tests[i].string, tests[i].default_to_gmt ? "PR_TRUE" : "PR_FALSE",
          (long long)result, (long long)tests[i].expected);
      failed = PR_TRUE;
    }
  }

  return failed ? PR_FAILURE : PR_SUCCESS;
}

int main(int argc, char** argv) {
  /*
   * 1. Verify that PR_ParseTimeString doesn't crash on an out-of-range time
   * string (bug 480740).
   */
  PRTime ct;
  PRExplodedTime et;
  PRStatus rv;
  char* sp1 = "Sat, 1 Jan 3001 00:00:00";  /* no time zone */
  char* sp2 = "Fri, 31 Dec 3000 23:59:60"; /* no time zone, not normalized */

#if _MSC_VER >= 1400 && !defined(WINCE)
  /* Run this test in the US Pacific Time timezone. */
  _putenv_s("TZ", "PST8PDT");
  _tzset();
#endif

  rv = PR_ParseTimeString(sp1, PR_FALSE, &ct);
  printf("rv = %d\n", rv);
  PR_ExplodeTime(ct, PR_GMTParameters, &et);
  PrintExplodedTime(&et);
  printf("\n");

  rv = PR_ParseTimeString(sp2, PR_FALSE, &ct);
  printf("rv = %d\n", rv);
  PR_ExplodeTime(ct, PR_GMTParameters, &et);
  PrintExplodedTime(&et);
  printf("\n");

  /* 2. Run a full-blown test for PR_ParseTimeString. */
  if (KnownAnswerTest() != PR_SUCCESS) {
    printf("FAIL\n");
    return 1;
  }

  printf("PASS\n");
  return 0;
}
