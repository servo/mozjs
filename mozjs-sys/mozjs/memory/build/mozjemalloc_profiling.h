/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZJEMALLOC_PROFILING_H
#define MOZJEMALLOC_PROFILING_H

#include "mozilla/RefCounted.h"
#include "mozilla/RefPtr.h"
#include "mozilla/TimeStamp.h"
#include "mozjemalloc_types.h"
#include "mozmemory_wrap.h"

namespace mozilla {

struct PurgeStats {
  arena_id_t arena_id;
  const char* arena_label;
  const char* caller;

  // The number of previously-dirty pages that are now clean.
  size_t pages_dirty = 0;

  // The total number of pages that were cleaned (includes already clean
  // pages).
  size_t pages_total = 0;

  // The number of pages that can't be purged because of alignment because
  // of logical/hardware page alignment.
  size_t pages_unpurgable = 0;

  size_t system_calls = 0;
  size_t chunks = 0;

  PurgeStats(arena_id_t aId, const char* aLabel, const char* aCaller)
      : arena_id(aId), arena_label(aLabel), caller(aCaller) {}
};

#ifdef MOZJEMALLOC_PROFILING_CALLBACKS
class MallocProfilerCallbacks
    : public external::AtomicRefCounted<MallocProfilerCallbacks> {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(MallocProfilerCallbacks)

  virtual ~MallocProfilerCallbacks() {}

  using TS = mozilla::TimeStamp;

  virtual void OnPurge(TS aStart, TS aEnd, const PurgeStats& aStats,
                       ArenaPurgeResult aResult) = 0;
};

MOZ_JEMALLOC_API void jemalloc_set_profiler_callbacks(
    RefPtr<MallocProfilerCallbacks>&& aCallbacks);
#endif

}  // namespace mozilla

#endif  // ! MOZJEMALLOC_PROFILING_H
