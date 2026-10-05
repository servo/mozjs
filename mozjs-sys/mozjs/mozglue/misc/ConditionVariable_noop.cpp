/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/PlatformConditionVariable.h"
#include "mozilla/PlatformMutex.h"

using mozilla::TimeDuration;

mozilla::detail::ConditionVariableImpl::ConditionVariableImpl() {}

mozilla::detail::ConditionVariableImpl::~ConditionVariableImpl() {}

void mozilla::detail::ConditionVariableImpl::notify_one() {}

void mozilla::detail::ConditionVariableImpl::notify_all() {}

void mozilla::detail::ConditionVariableImpl::wait(MutexImpl&) {
  // On WASI, there are no threads, so we never wait (either the condvar must
  // be ready or there is a deadlock).
}

mozilla::CVStatus mozilla::detail::ConditionVariableImpl::wait_for(
    MutexImpl&, const TimeDuration&) {
  return CVStatus::NoTimeout;
}
