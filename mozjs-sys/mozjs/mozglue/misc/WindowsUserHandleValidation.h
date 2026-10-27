/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZGLUE_MISC_WINDOWSUSERHANDLEVALIDATION_H_
#define MOZGLUE_MISC_WINDOWSUSERHANDLEVALIDATION_H_

#include <cstdint>

#include <windows.h>
#include <winternl.h>

#include "mozilla/Assertions.h"

namespace mozilla {

// Bit in Win32ClientInfo user side flags that gates whether user32 APIs like
// GetClientRect/ValidateHwnd/IsWindow route through the kernel's full
// handle-validation path (NtUserValidateHandleSecure).
static const uint32_t kValidateHandlesFlag = 0x20000000;

inline uint32_t& GetWin32UserFlagsFromTeb() {
  static const size_t kX64AndWowWin32ClientInfoOffsetInBytes = 0x800;
  static const size_t kX64AndWowUserFlagsOffsetInBytes = 0x1c;

  auto* teb = reinterpret_cast<uint8_t*>(NtCurrentTeb());
  uint8_t* baseForOffset = teb;
  size_t win32kClientInfoUserFlagsOffset;
#if defined(_WIN64)
  win32kClientInfoUserFlagsOffset =
      kX64AndWowWin32ClientInfoOffsetInBytes + kX64AndWowUserFlagsOffsetInBytes;
#else
  static const size_t kNativeX86Win32ClientInfoOffsetInBytes = 0x6cc;
  static const size_t kNativeX86UserFlagsOffsetInBytes = 0x14;

  BOOL isWow64Process;
  if (!::IsWow64Process(::GetCurrentProcess(), &isWow64Process)) {
    MOZ_CRASH("IsWow64Process failed");
  }

  if (isWow64Process) {
    // On win10+, x86 WOW user32 reads a signed delta at TEB+0xfdc and, if
    // negative, adds it to the base TEB pointer before applying the offset.
    int32_t delta = *reinterpret_cast<int32_t*>(teb + 0xfdc);
    if (delta < 0) {
      baseForOffset += delta;
    }

    win32kClientInfoUserFlagsOffset = kX64AndWowWin32ClientInfoOffsetInBytes +
                                      kX64AndWowUserFlagsOffsetInBytes;
  } else {
    win32kClientInfoUserFlagsOffset = kNativeX86Win32ClientInfoOffsetInBytes +
                                      kNativeX86UserFlagsOffsetInBytes;
  }
#endif

  return *reinterpret_cast<uint32_t*>(baseForOffset +
                                      win32kClientInfoUserFlagsOffset);
}

inline bool JobUILimitsRequireTebFlagClear() {
  // Store result as job is not changed during process lifetime.
  static const bool sClearRequired = [] {
    JOBOBJECT_BASIC_UI_RESTRICTIONS uiRestrictionsInfo{};
    if (!::QueryInformationJobObject(nullptr, JobObjectBasicUIRestrictions,
                                     &uiRestrictionsInfo,
                                     sizeof(uiRestrictionsInfo), nullptr)) {
      // Not in a job or query failed;
      return false;
    }

    // On older Windows the flag gets set incorrectly if any UI restrictions are
    // set and it should only be set if JOB_OBJECT_UILIMIT_HANDLES is set.
    // So, the flag needs clearing if there are restrictions and
    // JOB_OBJECT_UILIMIT_HANDLES is not set.
    return uiRestrictionsInfo.UIRestrictionsClass &&
           !(uiRestrictionsInfo.UIRestrictionsClass &
             JOB_OBJECT_UILIMIT_HANDLES);
  }();
  return sClearRequired;
}

inline void ForceToGuiThreadAndFixTebValidateHandlesFlag() {
  if (!JobUILimitsRequireTebFlagClear()) {
    return;
  }

  BOOL isGuiThread = ::IsGUIThread(/* bConvertToGuiThread */ TRUE);
  if (!isGuiThread) {
    MOZ_CRASH("Failed to convert to GUI Thread.");
  }

  auto& userFlags = GetWin32UserFlagsFromTeb();
  userFlags &= ~kValidateHandlesFlag;
}

}  // namespace mozilla

#endif  // MOZGLUE_MISC_WINDOWSUSERHANDLEVALIDATION_H_
