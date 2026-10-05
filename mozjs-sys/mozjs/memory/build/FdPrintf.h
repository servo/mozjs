/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef FdPrintf_h_
#define FdPrintf_h_

#include <cstdarg>

#ifdef _WIN32
typedef void* platform_handle_t;
#else
typedef int platform_handle_t;
#endif

/*
 * We can't use libc's (f)printf because it would reenter in replace_malloc,
 * So use a custom and simplified version.  Only %p, %zu, %s and %% are
 * supported, %zu, %s, support width specifiers.
 */
int VSNPrintf(char* aBuf, std::size_t aSize, const char* aFormat, va_list aArgs)
#ifdef __GNUC__
    __attribute__((format(printf, 3, 0)))
#endif
    ;

int SNPrintf(char* aBuf, std::size_t aSize, const char* aFormat, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 3, 4)))
#endif
    ;

/*
 * /!\ These functions use a fixed-size internal buffer. The caller is
 * expected to not use a format string that may overflow.
 * The aFd argument is a file descriptor on UNIX and a native win32 file
 * handle on Windows (from CreateFile). We can't use the windows POSIX
 * APIs is that they don't support O_APPEND in a multi-process-safe way,
 * while CreateFile does.
 */
void VFdPrintf(platform_handle_t aFd, const char* aFormat, va_list aArgs)
#ifdef __GNUC__
    __attribute__((format(printf, 2, 0)))
#endif
    ;

void FdPrintf(platform_handle_t aFd, const char* aFormat, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 2, 3)))
#endif
    ;

// Write buffer contents without formatting (eg for use with SNPrintf).
void FdPuts(platform_handle_t aFd, const char* aBuf, std::size_t aLen);

#endif /* FdPrintf_h_ */
