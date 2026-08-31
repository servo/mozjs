/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_PlatformMutex_h
#define mozilla_PlatformMutex_h

#include "mozilla/Types.h"

#if defined(XP_WIN)
#  include "mozilla/Futex.h"
#elif !defined(__wasi__)
#  include <pthread.h>
#endif

namespace mozilla {

namespace detail {

class ConditionVariableImpl;

class MutexImpl {
 public:
  explicit MFBT_API MutexImpl();
  MFBT_API ~MutexImpl();

 protected:
  MFBT_API void lock();
  MFBT_API void unlock();
  // We have a separate, forwarding API so internal uses don't have to go
  // through the PLT.
  MFBT_API bool tryLock();

 private:
  MutexImpl(const MutexImpl&) = delete;
  void operator=(const MutexImpl&) = delete;
  MutexImpl(MutexImpl&&) = delete;
  void operator=(MutexImpl&&) = delete;
  bool operator==(const MutexImpl& rhs) = delete;

  void mutexLock();
  bool mutexTryLock();

#if defined(XP_WIN)
  SmallFutex mFutex;
#elif !defined(__wasi__)
  pthread_mutex_t mMutex;
#endif

  friend class mozilla::detail::ConditionVariableImpl;
};

}  // namespace detail

}  // namespace mozilla
#endif  // mozilla_PlatformMutex_h
