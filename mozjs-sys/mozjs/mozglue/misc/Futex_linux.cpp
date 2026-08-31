/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Futex.h"
#include "mozilla/Assertions.h"
#include "mozilla/CheckedInt.h"

#include <errno.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

namespace mozilla {

// TODO: Maybe share some of this with ConditionVariable_posix?
static const long NanoSecPerSec = 1000000000;
static void AddDurationToTimeSpec(struct timespec* ts,
                                  const TimeDuration& aDuration) {
  MOZ_DIAGNOSTIC_ASSERT(ts->tv_nsec < NanoSecPerSec);
  struct timespec duration;
  // Clamp to zero as time_t is unsigned.
  duration.tv_sec = static_cast<time_t>(std::max(aDuration.ToSeconds(), 0.0));
  duration.tv_nsec = static_cast<uint64_t>(
                         std::max(aDuration.ToMicroseconds(), 0.0) * 1000.0) %
                     NanoSecPerSec;

  // Add nanoseconds. This may wrap, but not above 2 billion.
  ts->tv_nsec += duration.tv_nsec;

  // Add seconds, checking for overflow in the platform specific time_t type.
  CheckedInt<time_t> sec = CheckedInt<time_t>(ts->tv_sec) + duration.tv_sec;

  // If nanoseconds overflowed, carry the result over into seconds.
  if (ts->tv_nsec >= NanoSecPerSec) {
    MOZ_DIAGNOSTIC_ASSERT(ts->tv_nsec < 2 * NanoSecPerSec);
    ts->tv_nsec -= NanoSecPerSec;
    sec += 1;
  }

  // Extracting the value asserts that there was no overflow.
  ts->tv_sec = sec.value();
}

template <>
bool Futex::wait(uint32_t aExpected, const TimeDuration* aTimeout) {
  struct timespec ts;
  struct timespec* tsp = nullptr;
  if (aTimeout && *aTimeout != TimeDuration::Forever()) {
    // FUTEX_WAIT_BITSET takes an absolute timeout measured against
    // CLOCK_MONOTONIC (unless FUTEX_CLOCK_REALTIME is set, which we don't do).
    clock_gettime(CLOCK_MONOTONIC, &ts);
    AddDurationToTimeSpec(&ts, *aTimeout);
    tsp = &ts;
  }
  while (true) {
    // No need to wait if the value already changed.
    if (mValue.load(std::memory_order_relaxed) != aExpected) {
      return true;
    }

    int r = syscall(SYS_futex, &mValue, FUTEX_WAIT_BITSET | FUTEX_PRIVATE_FLAG,
                    aExpected, tsp,
                    /* unused for WAIT_BITSET */ nullptr,
                    /* All bits */ ~0u);
    if (r >= 0) {
      return true;
    }
    if (errno == ETIMEDOUT) {
      return false;
    }
    if (errno == EINTR) {
      continue;
    }
    // EAGAIN means the value already differs from aExpected, which is a valid
    // (non-timeout) wake-up. Any other error is unexpected; treat it as a
    // (spurious) wake-up to avoid hanging.
    return true;
  }
}

template <>
void Futex::wake() {
  syscall(SYS_futex, &mValue, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1);
}

template <>
void Futex::wakeAll() {
  syscall(SYS_futex, &mValue, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, INT32_MAX);
}

template struct FutexImpl<uint32_t>;

}  // namespace mozilla
