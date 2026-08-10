/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "UniquePtrExtensions.h"

#include "mozilla/Assertions.h"
#include "mozilla/DebugOnly.h"

#ifdef XP_WIN
#  include <windows.h>
#else
#  include <errno.h>
#  include <unistd.h>
#  include <fcntl.h>
#endif

namespace mozilla {
namespace detail {

void FileHandleDeleter::operator()(FileHandleHelper aHelper) {
  if (aHelper != nullptr) {
    DebugOnly<bool> ok;
#ifdef XP_WIN
    ok = CloseHandle(aHelper);
#else
    ok = close(aHelper) == 0 || errno == EINTR;
#endif
    MOZ_ASSERT(ok, "failed to close file handle");
  }
}

}  // namespace detail

#ifdef XP_UNIX
void SetCloseOnExec(detail::FileHandleType aFile) {
  // The fcntl calls shouldn't fail if aFile is valid.
  if (aFile >= 0) {
    int fdFlags = fcntl(aFile, F_GETFD);
    MOZ_ASSERT(fdFlags >= 0);
    if (fdFlags >= 0) {
      DebugOnly<int> rv = fcntl(aFile, F_SETFD, fdFlags | FD_CLOEXEC);
      MOZ_ASSERT(rv != -1);
    }
  }
}
#endif

#ifndef __wasm__
UniqueFileHandle DuplicateFileHandle(detail::FileHandleType aFile) {
#  ifdef XP_WIN
  if (aFile != INVALID_HANDLE_VALUE && aFile != NULL) {
    HANDLE handle;
    HANDLE currentProcess = ::GetCurrentProcess();
    if (::DuplicateHandle(currentProcess, aFile, currentProcess, &handle, 0,
                          false, DUPLICATE_SAME_ACCESS)) {
      return UniqueFileHandle{handle};
    }
  }
#  else
  if (aFile != -1) {
    int fd;
    // Set cloexec atomically if supported; otherwise fall back to non-atomic.
#    ifdef F_DUPFD_CLOEXEC
    fd = fcntl(aFile, F_DUPFD_CLOEXEC, 0);
#    else
    fd = dup(aFile);
    SetCloseOnExec(fd);
#    endif
    return UniqueFileHandle{fd};
  }
#  endif
  return nullptr;
}
#endif

}  // namespace mozilla
