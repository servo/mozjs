/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Futex.h"

#include <windows.h>

namespace mozilla {

static DWORD ConvertTimeout(const TimeDuration* aTimeout) {
  if (!aTimeout || *aTimeout == TimeDuration::Forever()) {
    return INFINITE;
  }
  double msecd = aTimeout->ToMilliseconds();
  if (msecd < 0.0) {
    return 0;
  }
  if (msecd > UINT32_MAX) {
    return INFINITE;
  }
  DWORD msec = static_cast<DWORD>(msecd);
  // Round sub-millisecond waits up to 1ms (bug 1437167 comment 6).
  if (msec == 0 && !aTimeout->IsZero()) {
    msec = 1;
  }
  return msec;
}

template <typename T>
bool FutexImpl<T>::wait(T aExpected, const TimeDuration* aTimeout) {
  return ::WaitOnAddress(reinterpret_cast<volatile void*>(&mValue), &aExpected,
                         sizeof(aExpected), ConvertTimeout(aTimeout));
}

template <typename T>
void FutexImpl<T>::wake() {
  ::WakeByAddressSingle(reinterpret_cast<void*>(&mValue));
}

template <typename T>
void FutexImpl<T>::wakeAll() {
  ::WakeByAddressAll(reinterpret_cast<void*>(&mValue));
}

template struct FutexImpl<uint32_t>;
template struct FutexImpl<uint8_t>;

}  // namespace mozilla
