/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/PlatformMutex.h"

#include <cstdint>

#include "mozilla/Attributes.h"
#include "mozilla/Futex.h"
#include "mozilla/Atomics.h"

namespace {

using State = mozilla::SmallFutex::ValueType;

constexpr State UNLOCKED = 0;
constexpr State LOCKED = 1;     // locked, no other threads waiting.
constexpr State CONTENDED = 2;  // locked, and other threads waiting.

// We only use `load` (and not `swap` or `compare_exchange`) while spinning, to
// be easier on the caches.
State Spin(mozilla::SmallFutex& aFutex) {
  uint32_t spinCount = 100;
  while (true) {
    State state = aFutex.mValue.load(std::memory_order_relaxed);
    // We stop spinning when the mutex is UNLOCKED, but also when it's
    // CONTENDED.
    if (state != LOCKED || spinCount == 0) {
      return state;
    }
    mozilla::cpu_pause();
    --spinCount;
  }
}

MOZ_COLD void LockContended(mozilla::SmallFutex& aFutex) {
  // Spin first to speed things up if the lock is released quickly.
  State state = Spin(aFutex);

  // If it's unlocked now, attempt to take the lock without marking it as
  // contended.
  if (state == UNLOCKED && aFutex.mValue.compare_exchange_strong(
                               state, LOCKED, std::memory_order_acquire,
                               std::memory_order_relaxed)) {
    return;
  }

  while (true) {
    // Put the lock in contended state. We avoid an unnecessary write if it is
    // already CONTENDED, to be friendlier for the caches.
    if (state != CONTENDED &&
        aFutex.mValue.exchange(CONTENDED, std::memory_order_acquire) ==
            UNLOCKED) {
      // We changed it from UNLOCKED to CONTENDED, so we just successfully
      // locked it.
      return;
    }

    // Wait for the futex to change state, assuming it is still CONTENDED.
    (void)aFutex.wait(CONTENDED);

    // Spin again after waking up.
    state = Spin(aFutex);
  }
}

}  // namespace

mozilla::detail::MutexImpl::MutexImpl() = default;
mozilla::detail::MutexImpl::~MutexImpl() = default;

void mozilla::detail::MutexImpl::lock() {
  State expected = UNLOCKED;
  if (!mFutex.mValue.compare_exchange_strong(expected, LOCKED,
                                             std::memory_order_acquire,
                                             std::memory_order_relaxed)) {
    LockContended(mFutex);
  }
}

bool mozilla::detail::MutexImpl::tryLock() { return mutexTryLock(); }

bool mozilla::detail::MutexImpl::mutexTryLock() {
  State expected = UNLOCKED;
  return mFutex.mValue.compare_exchange_strong(
      expected, LOCKED, std::memory_order_acquire, std::memory_order_relaxed);
}

void mozilla::detail::MutexImpl::unlock() {
  if (mFutex.mValue.exchange(UNLOCKED, std::memory_order_release) ==
      CONTENDED) {
    // We only wake up one thread. When that thread locks the mutex, it will
    // mark the mutex as CONTENDED (see LockContended above), which makes sure
    // that any other waiting threads will also be woken up eventually.
    mFutex.wake();
  }
}
