/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_mozalloc_abort_h
#define mozilla_mozalloc_abort_h

#include "mozilla/Types.h"

/**
 * Terminate this process in such a way that breakpad is triggered, if
 * at all possible.
 *
 * Note: [[noreturn]] seems to break crash stacks on ARM, so we don't
 * use that annotation there.
 */
extern "C"
#if !defined(__arm__)
    [[noreturn]]
#endif
    MFBT_API void mozalloc_abort(const char* const msg);

#endif /* ifndef mozilla_mozalloc_abort_h */
