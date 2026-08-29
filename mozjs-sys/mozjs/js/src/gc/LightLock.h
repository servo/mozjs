/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef gc_LightLock_h
#define gc_LightLock_h

#include "mozilla/Atomics.h"
#include "mozilla/ThreadLocal.h"

#include "js/TypeDecls.h"
#include "threading/ConditionVariable.h"
#include "threading/Mutex.h"
#include "threading/ThreadId.h"

namespace js {

#ifdef MOZ_TSAN
extern void TSANMemoryAcquireFence(JSRuntime* runtime);
extern void TSANMemoryReleaseFence(JSRuntime* runtime);
#endif

#ifdef DEBUG
extern MOZ_THREAD_LOCAL(bool) TlsLightLockHeld;
#endif

// A lightweight lock modelled after rust's parking lot mutex.
//
// WARNING: This is not a general purpose mutex! There are the following
// restrictions on use:
//
//  - only two threads are supported
//  - a thread may hold at most one lock at a time
//
// This is intended for use in concurrent marking which only requires the
// current feature set. This greatly simplifies the implementation.
//
// The fast path uses an atomic compare and swap so should not present much
// overhead if there's no contention. The lock object itself has a small
// footprint and can be easily embedded in other objects.
//
// The rust implementation can be found here:
// https://docs.rs/parking_lot/latest/src/parking_lot/raw_mutex.rs.html
class LightLock {
  enum StateBits : uint32_t {
    // Whether the mutex is locked. Set and cleared by the locking thread.
    IsLocked = Bit(0),

    // Whether a thread is waiting on the mutex. Set and cleared by the waiting
    // thread.
    HasWaiter = Bit(1),
  };

  static constexpr uint32_t UnlockedState = 0;
  static constexpr uint32_t LockedState = IsLocked;
  static constexpr uint32_t LockedWithWaiterState = IsLocked | HasWaiter;

  mozilla::Atomic<uint32_t, mozilla::Relaxed> state;

#ifdef DEBUG
  ThreadId holdingThread_;
#endif

 public:
  void lock(JSRuntime* runtime) {
    MOZ_ASSERT(!TlsLightLockHeld.get());
    MOZ_ASSERT(holdingThread_ != ThreadId::ThisThreadId());
    if (MOZ_UNLIKELY(!state.compareExchange(UnlockedState, LockedState))) {
      lockSlow(runtime);
    }
    // A separate fence is required because Atomic::compareExchange doesn't
    // support separate memory order for success and failure.
    acquireFence(runtime);
#ifdef DEBUG
    MOZ_ASSERT(isLocked());
    MOZ_ASSERT(holdingThread_ == ThreadId());
    holdingThread_ = ThreadId::ThisThreadId();
    TlsLightLockHeld.set(true);
#endif
  }
  void lockSlow(JSRuntime* runtime);

  void unlock(JSRuntime* runtime) {
#ifdef DEBUG
    MOZ_ASSERT(isLocked());
    MOZ_ASSERT(TlsLightLockHeld.get());
    MOZ_ASSERT(holdingThread_ == ThreadId::ThisThreadId());
    holdingThread_ = ThreadId();
#endif
    // A separate fence is required because Atomic::compareExchange doesn't
    // support separate memory order for success and failure.
    releaseFence(runtime);
    if (MOZ_UNLIKELY(!state.compareExchange(LockedState, UnlockedState))) {
      unlockSlow(runtime);
    }
#ifdef DEBUG
    TlsLightLockHeld.set(false);
#endif
  }
  void unlockSlow(JSRuntime* runtime);

  bool isLocked() const { return state & IsLocked; }
  bool hasWaiter() const { return state & HasWaiter; }

 private:
  bool spin(uint32_t& counter);
  bool tryBlockUntilWoken(JSRuntime* runtime);
  void wakeOtherThread(JSRuntime* runtime, const LockGuard<Mutex>& lock);

  void acquireFence(JSRuntime* runtime) {
    std::atomic_thread_fence(std::memory_order_acquire);
#ifdef MOZ_TSAN
    TSANMemoryAcquireFence(runtime);
#endif
  }
  void releaseFence(JSRuntime* runtime) {
    std::atomic_thread_fence(std::memory_order_release);
#ifdef MOZ_TSAN
    TSANMemoryReleaseFence(runtime);
#endif
  }
};

class LightLockRuntime {
  Mutex mutex;
  ConditionVariable condVar;
  bool* waitingPtr = nullptr;
  friend class LightLock;

 public:
  LightLockRuntime();
  static LightLockRuntime* from(JSRuntime* runtime);
};

}  // namespace js

#endif  // gc_LightLock_h
