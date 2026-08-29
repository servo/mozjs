/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Assertions.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>

#if defined(XP_DARWIN)
#  include <pthread_spis.h>
#endif

#include "mozilla/PlatformMutex.h"

#define REPORT_PTHREADS_ERROR(result, msg) \
  {                                        \
    errno = result;                        \
    perror(msg);                           \
    MOZ_CRASH(msg);                        \
  }

#define TRY_CALL_PTHREADS(call, msg)      \
  {                                       \
    int result = (call);                  \
    if (result != 0) {                    \
      REPORT_PTHREADS_ERROR(result, msg); \
    }                                     \
  }

mozilla::detail::MutexImpl::MutexImpl() {
  pthread_mutexattr_t* attrp = nullptr;

#if defined(DEBUG)
#  define MUTEX_KIND PTHREAD_MUTEX_ERRORCHECK
// Linux with glibc, FreeBSD and macOS 10.14+ support adaptive mutexes that
// spin for a short number of tries before sleeping.  NSPR's locks did this,
// too, and it seems like a reasonable thing to do.
#elif (defined(__linux__) && defined(__GLIBC__)) || defined(__FreeBSD__)
#  define MUTEX_KIND PTHREAD_MUTEX_ADAPTIVE_NP
#elif defined(XP_DARWIN)
#  if defined(PTHREAD_MUTEX_POLICY_FIRSTFIT_NP)
#    define POLICY_KIND PTHREAD_MUTEX_POLICY_FIRSTFIT_NP
#  else
#    define POLICY_KIND (3)  // The definition is missing in old SDKs
#  endif
#endif

#if defined(MUTEX_KIND) || defined(POLICY_KIND)
#  define ATTR_REQUIRED
#endif

#if defined(ATTR_REQUIRED)
  pthread_mutexattr_t attr;

  TRY_CALL_PTHREADS(
      pthread_mutexattr_init(&attr),
      "mozilla::detail::MutexImpl::MutexImpl: pthread_mutexattr_init failed");

#  if defined(MUTEX_KIND)
  TRY_CALL_PTHREADS(pthread_mutexattr_settype(&attr, MUTEX_KIND),
                    "mozilla::detail::MutexImpl::MutexImpl: "
                    "pthread_mutexattr_settype failed");
#  elif defined(POLICY_KIND)
  TRY_CALL_PTHREADS(pthread_mutexattr_setpolicy_np(&attr, POLICY_KIND),
                    "mozilla::detail::MutexImpl::MutexImpl: "
                    "pthread_mutexattr_setpolicy_np failed");
#  endif
  attrp = &attr;
#endif

  TRY_CALL_PTHREADS(
      pthread_mutex_init(&mMutex, attrp),
      "mozilla::detail::MutexImpl::MutexImpl: pthread_mutex_init failed");

#if defined(ATTR_REQUIRED)
  TRY_CALL_PTHREADS(pthread_mutexattr_destroy(&attr),
                    "mozilla::detail::MutexImpl::MutexImpl: "
                    "pthread_mutexattr_destroy failed");
#endif
}

mozilla::detail::MutexImpl::~MutexImpl() {
  TRY_CALL_PTHREADS(
      pthread_mutex_destroy(&mMutex),
      "mozilla::detail::MutexImpl::~MutexImpl: pthread_mutex_destroy failed");
}

inline void mozilla::detail::MutexImpl::mutexLock() {
  TRY_CALL_PTHREADS(
      pthread_mutex_lock(&mMutex),
      "mozilla::detail::MutexImpl::mutexLock: pthread_mutex_lock failed");
}

bool mozilla::detail::MutexImpl::tryLock() { return mutexTryLock(); }

bool mozilla::detail::MutexImpl::mutexTryLock() {
  int result = pthread_mutex_trylock(&mMutex);
  if (result == 0) {
    return true;
  }

  if (result == EBUSY) {
    return false;
  }

  REPORT_PTHREADS_ERROR(
      result,
      "mozilla::detail::MutexImpl::mutexTryLock: pthread_mutex_trylock failed");
}

void mozilla::detail::MutexImpl::lock() { mutexLock(); }

void mozilla::detail::MutexImpl::unlock() {
  TRY_CALL_PTHREADS(
      pthread_mutex_unlock(&mMutex),
      "mozilla::detail::MutexImpl::unlock: pthread_mutex_unlock failed");
}

#undef TRY_CALL_PTHREADS
