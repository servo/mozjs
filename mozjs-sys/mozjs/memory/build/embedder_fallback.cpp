/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozmemory.h"
#include "mozjemalloc.h"

// Expected to provide SERVO_EMBEDDER_MALLOC_PREFIX
#include "servo_embedder_malloc_prefix.h"

#define MOZ_EMBED_CONCAT(a, b) a##b
#define MOZ_EMBED_CONCAT1(a, b) MOZ_EMBED_CONCAT(a, b)

// embedder responsible for providing memalign

struct EmbedderMalloc {
#define MALLOC_DECL(name, return_type, ...)                                \
  static inline return_type name(ARGS_HELPER(TYPED_ARGS, ##__VA_ARGS__)) { \
    return :: MOZ_EMBED_CONCAT1(SERVO_EMBEDDER_MALLOC_PREFIX, name)(ARGS_HELPER(ARGS, ##__VA_ARGS__));                       \
  }
#define MALLOC_FUNCS MALLOC_FUNCS_MALLOC_BASE
#include "malloc_decls.h"
};

#undef MOZ_EMBED_CONCAT
#undef MOZ_EMBED_CONCAT1

// Todo: why are these functions free-standing, is this correct? (copied from fallback)

#define MALLOC_DECL(name, return_type, ...)                                   \
  MOZ_JEMALLOC_API return_type name(ARGS_HELPER(TYPED_ARGS, ##__VA_ARGS__)) { \
    return DummyArenaAllocator<EmbedderMalloc>::name(                         \
        ARGS_HELPER(ARGS, ##__VA_ARGS__));                                    \
  }
#define MALLOC_FUNCS MALLOC_FUNCS_ARENA
#include "malloc_decls.h"
