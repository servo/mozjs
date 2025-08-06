/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Attributes.h"
#include "mozilla/Types.h"

extern "C" MOZ_EXPORT const char* __ubsan_default_options() {
  return "print_stacktrace=1:handle_sigill=1:handle_abort=1:handle_sigtrap=1";
}

extern "C" MOZ_EXPORT const char* __ubsan_default_suppressions() { return ""; }
