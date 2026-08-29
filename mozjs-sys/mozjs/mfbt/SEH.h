/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_SEH_h
#define mozilla_SEH_h

/*
 * SEH exception macros.
 */
#ifdef HAVE_SEH_EXCEPTIONS
#  define MOZ_SEH_TRY __try
#  define MOZ_SEH_EXCEPT(expr) __except (expr)
#else
#  define MOZ_SEH_TRY if (true)
#  define MOZ_SEH_EXCEPT(expr) else
#endif

#endif /* mozilla_SEH_h */
