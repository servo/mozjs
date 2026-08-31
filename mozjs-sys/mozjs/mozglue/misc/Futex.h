/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_Futex_h
#define mozilla_Futex_h

#include <atomic>
#include <cstdint>

#include "mozilla/TimeStamp.h"

namespace mozilla {

template <typename T>
struct FutexImpl {
  using ValueType = T;

  std::atomic<T> mValue{0};

  // Waits until the value changes from aExpected, the futex is woken, or
  // (when aTimeout is non-null) the timeout elapses. Returns false on timeout,
  // true otherwise (including spurious wake-ups!).
  [[nodiscard]] bool wait(T aExpected, const TimeDuration* aTimeout = nullptr);
  void wake();
  void wakeAll();
};

using Futex = FutexImpl<uint32_t>;
#ifdef XP_WIN
using SmallFutex = FutexImpl<uint8_t>;
#else
// On Linux the futex word must be a 32-bit value.
using SmallFutex = Futex;
#endif

}  // namespace mozilla

#endif
