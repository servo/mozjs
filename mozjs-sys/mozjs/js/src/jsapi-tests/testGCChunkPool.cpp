/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <utility>

#include "gc/GCLock.h"
#include "gc/GCRuntime.h"
#include "jsapi-tests/tests.h"

BEGIN_TEST(testGCChunkPool) {
  using namespace js::gc;

  const int N = 10;
  ChunkPool pool;

  // Create.
  for (int i = 0; i < N; ++i) {
    void* ptr = ArenaChunk::allocate(&cx->runtime()->gc, StallAndRetry::No);
    CHECK(ptr);
    ArenaChunk* chunk = ArenaChunk::emplace(ptr, &cx->runtime()->gc, true);
    CHECK(chunk);
    pool.push(chunk);
  }
  MOZ_ASSERT(pool.verify());

  // Iterate.
  uint32_t i = 0;
  for (ChunkPool::Iter iter(pool); !iter.done(); iter.next(), ++i) {
    CHECK(iter.get());
  }
  CHECK(i == pool.count());
  MOZ_ASSERT(pool.verify());

  // Push/Pop.
  for (int i = 0; i < N; ++i) {
    ArenaChunk* chunkA = pool.pop();
    ArenaChunk* chunkB = pool.pop();
    ArenaChunk* chunkC = pool.pop();
    pool.push(chunkA);
    pool.push(chunkB);
    pool.push(chunkC);
  }
  MOZ_ASSERT(pool.verify());

  // Remove.
  ArenaChunk* chunk = nullptr;
  int offset = N / 2;
  for (ChunkPool::Iter iter(pool); !iter.done(); iter.next(), --offset) {
    if (offset == 0) {
      chunk = pool.remove(iter.get());
      break;
    }
  }
  CHECK(chunk);
  MOZ_ASSERT(!pool.contains(chunk));
  MOZ_ASSERT(pool.verify());
  pool.push(chunk);

  // Destruct.
  js::AutoLockGC lock(cx->runtime());
  for (ChunkPool::Iter iter(pool); !iter.done();) {
    ArenaChunk* chunk = iter.get();
    iter.next();
    pool.remove(chunk);
    UnmapPages(chunk, ChunkSize);
  }

  return true;
}
END_TEST(testGCChunkPool)
