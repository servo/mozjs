/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// Largest sub-page size class, or zero if there are none
GLOBAL(size_t, gMaxSubPageClass,
       gPageSize / 2 >= kMinSubPageClass ? gPageSize / 2 : 0)

// Number of sub-page bins.
GLOBAL(uint8_t, gNumSubPageClasses, []() GLOBAL_CONSTEXPR -> uint8_t {
  if GLOBAL_CONSTEXPR (gMaxSubPageClass != 0) {
    return mozilla::FloorLog2(gMaxSubPageClass) - LOG2(kMinSubPageClass) + 1;
  }
  return 0;
}())

GLOBAL(uint8_t, gPageSize2Pow, GLOBAL_LOG2(gPageSize))
GLOBAL(uint8_t, gRealPageSize2Pow, GLOBAL_LOG2(gRealPageSize))
GLOBAL(size_t, gPageSizeMask, gPageSize - 1)
GLOBAL(size_t, gRealPageSizeMask, gRealPageSize - 1)

// For system calls that allocate pages we use this to round-up to a real
// page boundary.
GLOBAL(size_t, gPagesPerRealPage,
       gPageSize < gRealPageSize ? gRealPageSize / gPageSize : 1);

// Number of pages in a chunk.
GLOBAL(size_t, gChunkNumPages, kChunkSize >> gPageSize2Pow)

// Number of pages necessary for a chunk header plus a guard page.
GLOBAL(size_t, gChunkHeaderNumPages,
       gPagesPerRealPage +
           (std::max(PAGE_CEILING(sizeof(arena_chunk_t) +
                                  sizeof(arena_chunk_map_t) * gChunkNumPages),
                     REAL_PAGE_CEILING(sizeof(arena_chunk_t) +
                                       sizeof(arena_chunk_map_t) *
                                           gChunkNumPages)) >>
            gPageSize2Pow));

// One chunk, minus the header, minus a guard page
GLOBAL(size_t, gMaxLargeClass,
       kChunkSize - gRealPageSize - (gChunkHeaderNumPages << gPageSize2Pow))

// The minimum run size and minimum regions per run control how often run
// allocation is required and reduce the amount of slow path executions.
// 
GLOBAL(size_t, gMinimumRunSize, std::max(gPageSize, 16_KiB))

// Various checks that regard configuration.
GLOBAL_ASSERT(1ULL << gPageSize2Pow == gPageSize,
              "Page size is not a power of two");
GLOBAL_ASSERT(1ULL << gRealPageSize2Pow == gRealPageSize,
              "Real page size is not a power of two");
GLOBAL_ASSERT(kQuantum >= sizeof(void*));
GLOBAL_ASSERT(kQuantum <= kQuantumWide);
GLOBAL_ASSERT(!kNumQuantumWideClasses ||
              kQuantumWide <= (kMinSubPageClass - kMaxQuantumClass));

GLOBAL_ASSERT(kQuantumWide <= kMaxQuantumClass);

GLOBAL_ASSERT(gMaxSubPageClass >= kMinSubPageClass || gMaxSubPageClass == 0);
GLOBAL_ASSERT(gMaxLargeClass >= gMaxSubPageClass);
GLOBAL_ASSERT(kChunkSize >= gPageSize);
GLOBAL_ASSERT(kChunkSize >= gRealPageSize);
GLOBAL_ASSERT(gPagesPerRealPage < gChunkHeaderNumPages);
GLOBAL_ASSERT(kQuantum * 4 <= kChunkSize);
