/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _mozilla_pthread_atfork_h_
#define _mozilla_pthread_atfork_h_

#if defined(MOZ_ENABLE_FORKSERVER)

#  if defined(__GLIBC__) && !defined(__UCLIBC__)
// We really are using glibc, not uClibc pretending to be glibc.
#    define LIBC_GLIBC 1
#  endif

extern MFBT_API void run_moz_pthread_atfork_handlers_prefork();
extern MFBT_API void run_moz_pthread_atfork_handlers_postfork_parent();
extern MFBT_API void run_moz_pthread_atfork_handlers_postfork_child();

#endif  // defined(MOZ_ENABLE_FORKSERVER)
#endif  // _mozilla_pthread_atfork_h_
