/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <errno.h>

#include "mozilla/PlatformMutex.h"

mozilla::detail::MutexImpl::MutexImpl() = default;

mozilla::detail::MutexImpl::~MutexImpl() = default;

inline void mozilla::detail::MutexImpl::mutexLock() {}

bool mozilla::detail::MutexImpl::tryLock() { return mutexTryLock(); }

bool mozilla::detail::MutexImpl::mutexTryLock() { return true; }

void mozilla::detail::MutexImpl::lock() { mutexLock(); }

void mozilla::detail::MutexImpl::unlock() {}
