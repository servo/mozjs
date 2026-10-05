/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef ARENA_AVAIL_RUNS_H
#define ARENA_AVAIL_RUNS_H

#include "BaseArray.h"
#include "Constants.h"
#include "Chunk.h"
#include "Globals.h"

struct ArenaAvailTreeTrait {
  static mozilla::DoublyLinkedListElement<arena_chunk_map_t>& Get(
      arena_chunk_map_t* aThis) {
    return aThis->link;
  }
  static const mozilla::DoublyLinkedListElement<arena_chunk_map_t>& Get(
      const arena_chunk_map_t* aThis) {
    return aThis->link;
  }
};

class ArenaAvailRunsSize {
 private:
  using RunList =
      mozilla::DoublyLinkedList<arena_chunk_map_t, ArenaAvailTreeTrait>;
  // The list is ordered by memory availability, runs with with dirty
  // pages are at the front, then runs with fresh pages and finally runs
  // with decommitted/madvised pages.  Pages without flags are considered
  // "fresh".
  //
  // This order is maintained with constant time insertions using
  // "bookmarks" into the list.  We could use 3 separate lists but that
  // would require removals to know which list to remove an item from.
  RunList mRuns;
  arena_chunk_map_t* mFirstFreshRun = nullptr;

  // Try to categorise a run.  This is fast but inaccurate.
  //
  // Free runs are always merged and so a run may contain pages with
  // different availability, a run with dirty pages and decommitted pages
  // can't be clearly defined.
  //
  // Return the first set of page bits for any page with "interesting"
  // bits, within the first 4 pages.  This means that in a run with both
  // dirty and decommitted pages, whichever occurs first in the run
  // categorises the run.
  //
  // This is limited to 4 pages to keep it fast and makes sense because
  // SplitRun() always uses the first pages of a run and roughly half of all
  // requests are for a single page.  4 pages will also ensure that if the
  // run isn't aligned with the system's real page (where decommitted memory
  // must be aligned) that the decommitted status will still be observed
  // (when gRealPageSize / gPageSize <= 4).
  static unsigned CategoriseRun(arena_chunk_map_t* aMapElm) {
    arena_chunk_t* chunk = mozilla::GetChunkForPtr(aMapElm);
    size_t pageind = (uintptr_t(aMapElm) - uintptr_t(chunk->mPageMap)) /
                     sizeof(arena_chunk_map_t);
    size_t num_pages =
        (aMapElm->bits & ~mozilla::gPageSizeMask) >> mozilla::gPageSize2Pow;
    // TODO I will check if the loop gets unrolled.
    for (unsigned i = pageind; i < pageind + std::min(num_pages, size_t(4));
         i++) {
      unsigned bits = chunk->mPageMap[i].bits & mozilla::gPageSizeMask;
      if (bits & (CHUNK_MAP_DIRTY | CHUNK_MAP_MADVISED | CHUNK_MAP_DECOMMITTED |
                  CHUNK_MAP_FRESH)) {
        return bits;
      }
    }

    return 0;
  }

 public:
  arena_chunk_map_t* Search() { return &(*mRuns.begin()); }

  bool IsEmpty() const { return mRuns.isEmpty(); }

  void Insert(arena_chunk_map_t* aElem) {
    unsigned bits = CategoriseRun(aElem);
    if (bits & CHUNK_MAP_DIRTY) {
      mRuns.pushFront(aElem);
#ifndef XP_LINUX
    } else if (bits & CHUNK_MAP_MADVISED_OR_DECOMMITTED) {
      mRuns.pushBack(aElem);
      if (!mFirstFreshRun) {
        // The run isn't fresh but this is the correct insertion point.
        mFirstFreshRun = aElem;
      }
    } else {
      // When the list is empty this will insert at the end,  This tested
      // well on MacOS and Windows, but not on Linux, hence the ifdef above.
      mRuns.insertBefore(RunList::Iterator(mFirstFreshRun), aElem);
      mFirstFreshRun = aElem;
    }
#else
    } else {
      mRuns.pushBack(aElem);
    }
#endif
  }

  void Remove(arena_chunk_map_t* aElem) {
    MOZ_ASSERT(aElem);
    if (aElem == mFirstFreshRun) {
      // Move mFirstFreshRun to the next run, or clear it if this is the
      // last run.  We can't get mNext directly because it's private,
      // instead construct then advance an iterator.
      mFirstFreshRun = &(*(++RunList::Iterator(aElem)));
    }
    mRuns.remove(aElem);
  }
};

class ArenaAvailRuns {
 private:
  BaseArray<ArenaAvailRunsSize> mSizeClasses;
  // If a given size class is empty then its slot in mHints points to the
  // next size class index worth checking.
  // Hints may be:
  //   0                  -> no information.
  //   MaxSizeClass() + 1 -> all the larger size classes are empty.
  //   n                  -> mSizeClasses[n] may be non-empty, n will never
  //                         point to a smaller size class.
  BaseArray<unsigned> mHints;

  static unsigned GetSizeClass(size_t aSize) {
    // aSize must be a multiple of gPageSize;
    MOZ_ASSERT((aSize % mozilla::gPageSize) == 0);
    return aSize >> mozilla::gPageSize2Pow;
  }

  static unsigned MaxSizeClass() {
    return GetSizeClass(PAGE_CEILING(mozilla::gMaxLargeClass));
  }

  // This is not in arena_chunk_map_t because that's defined before
  // gPageSizeMask.
  static size_t RunSize(const arena_chunk_map_t* aElem) {
    return aElem->bits & ~mozilla::gPageSizeMask;
  }

 public:
  ArenaAvailRuns() {
    mSizeClasses.Init(MaxSizeClass() + 1);
    mHints.Init(MaxSizeClass() + 1);
  }

  arena_chunk_map_t* SearchOrNext(size_t aSize) {
    unsigned size_class = GetSizeClass(aSize);
    MOZ_ASSERT(size_class <= MaxSizeClass());

    arena_chunk_map_t* elem = mSizeClasses[size_class].Search();
    if (MOZ_LIKELY(elem)) {
      MOZ_ASSERT(RunSize(elem) >= aSize);
      return elem;
    }

    if (size_class == MaxSizeClass()) {
      // There are no other size classes to check.
      return nullptr;
    }

    // Search for a non-empty size-class.
    unsigned start_size_class = size_class;
    do {
      unsigned prev_size_class = size_class;
      size_class = mHints[prev_size_class];
      if (size_class == 0) {
        // No hint available
        size_class = prev_size_class + 1;
      }

      if (size_class > MaxSizeClass()) {
        // Set the hint beyond the maximum so the next search will
        // terminate quickly.
        mHints[prev_size_class] = MaxSizeClass() + 1;
        mHints[start_size_class] = MaxSizeClass() + 1;
        return nullptr;
      }
    } while (mSizeClasses[size_class].IsEmpty());

    // This must be a populated size class.
    mHints[start_size_class] = size_class;
    elem = mSizeClasses[size_class].Search();
    MOZ_ASSERT(elem);
    MOZ_ASSERT(RunSize(elem) >= aSize);
    return elem;
  }

  void Insert(arena_chunk_map_t* aElem) {
    unsigned size_class = GetSizeClass(RunSize(aElem));

    if (mSizeClasses[size_class].IsEmpty() && size_class != 0) {
      // Update any hints in preceding empty classes.  This can stop when it
      // finds a non-empty class.  It does update the hint in the first
      // non-empty class so that when that class does become empty the hint
      // will be ready.
      for (int i = size_class - 1; i >= 0; i--) {
        mHints[i] = size_class;
        if (!mSizeClasses[i].IsEmpty()) {
          break;
        }
      }
    }

    mSizeClasses[size_class].Insert(aElem);
  }

  void Remove(arena_chunk_map_t* aElem) {
    mSizeClasses[GetSizeClass(RunSize(aElem))].Remove(aElem);

    // A removal doesn't update the hint.
  }
};

#endif /* ! ARENA_AVAIL_RUNS_H */
