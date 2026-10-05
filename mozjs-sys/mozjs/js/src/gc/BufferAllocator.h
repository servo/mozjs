/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// GC-internal header file for the buffer allocator.

#ifndef gc_BufferAllocator_h
#define gc_BufferAllocator_h

#include "mozilla/Array.h"
#include "mozilla/Atomics.h"
#include "mozilla/BitSet.h"
#include "mozilla/Maybe.h"
#include "mozilla/TimeStamp.h"

#include <cstdint>
#include <stddef.h>

#include "jstypes.h"  // JS_PUBLIC_API

#include "ds/SlimLinkedList.h"
#include "js/HeapAPI.h"
#include "threading/LockGuard.h"
#include "threading/Mutex.h"
#include "threading/ProtectedData.h"

class JS_PUBLIC_API JSTracer;

namespace JS {
class JS_PUBLIC_API Zone;
}  // namespace JS

namespace js {

class GCMarker;
class Nursery;

namespace gc {

class BufferAllocator;
class BufferAllocatorRuntime;  // Defined in GCRuntime.h.
struct BufferChunk;
class Cell;
class GCRuntime;
struct LargeBuffer;
struct SmallBufferRegion;

// An RAII guard to lock and unlock the buffer allocator lock.
class AutoLockBufferAllocator : public LockGuard<Mutex> {
 public:
  explicit AutoLockBufferAllocator(BufferAllocatorRuntime* runtime);
  friend class UnlockGuard<AutoLockBufferAllocator>;
};

// A lock guard that is locked only when needed. Defined as a class so it can be
// forward declared elsewhere.
class MaybeLockBufferAllocator
    : public mozilla::Maybe<AutoLockBufferAllocator> {
 public:
  using Base = mozilla::Maybe<AutoLockBufferAllocator>;
  using Base::Base;
};

// BufferAllocator allocates dynamically sized blocks of memory which can be
// reclaimed by the garbage collector and are associated with GC things.
//
// Although these blocks can be reclaimed by GC, explicit free and resize are
// also supported. This is important for buffers that can grow or shrink.
//
// The allocator uses a different strategy depending on the size of the
// allocation requested. There are three size ranges, divided as follows:
//
//   Size:            Kind:   Allocator implementation:
//    16 B  -   4 KB  Small   Uses a free list allocator from 16KB regions
//     4 KB - 512 KB  Medium  Uses a free list allocator from 1 MB chunks
//     1 MB -         Large   Uses the OS page allocator (e.g. mmap)
//
// The smallest supported allocation size is 16 bytes. This is used for a
// dynamic slots allocation with zero slots to set a unique ID on a native
// object. This is the size of two JS::Values.
//
// These size ranges are chosen to be the same as those used by jemalloc
// (although that uses a different kind of allocator for its equivalent of small
// allocations).
//
// Supported operations
// --------------------
//
//  - Allocate a buffer. Buffers are always owned by a GC cell, and the
//    allocator tracks whether the owner is in the nursery or the tenured heap.
//
//  - Trace an edge to buffer. When the owning cell is traced it must trace the
//    edge to the buffer. This will mark the buffer in a GC and prevent it from
//    being swept. This does not trace the buffer contents.
//
//  - Free a buffer. This allows uniquely owned buffers to be freed and reused
//    without waiting for the next GC. It is a hint only and is not supported
//    for all buffer kinds.
//
//  - Reallocate a buffer. This allows uniquely owned buffers to be resized,
//    possibly in-place. This is important for performance on some benchmarks.
//
// Integration with the rest of the GC
// -----------------------------------
//
// The GC calls the allocator at several points during minor and major GC (at
// the start, when sweeping starts and at the end). Allocations are swept on a
// background thread and the memory used by unmarked allocations is reclaimed.
//
// The allocator tries hard to avoid locking, and where it is necessary tries to
// minimize the time spent holding locks. A lock is required for the following
// operations:
//
//  - allocating a new chunk
//  - main thread operations on large buffers while sweeping off-thread
//  - merging back swept data on the main thread
//
// No locks are required to allocate on the main thread, even when off-thread
// sweeping is taking place. This is achieved by moving data to be swept to
// separate containers from those used for allocation on the main thread.
//
// Multithreaded use is supported but requires external synchronization using a
// mutex. This is configured by calling set/clearMultiThreadedUse() and checked
// internally by checkAccess().
//
// Small and medium allocations
// ----------------------------
//
// These are allocated in their own zone-specific chunks using a segregated free
// list strategy. This is the main part of the allocator.
//
// The requested allocation size is used to pick a size class, which is used to
// find a free regions of suitable size to satisfy that request. In general size
// classes for allocations are calculated by rounding up to the next power of
// two; size classes for free regions are calculated by rounding down to the
// previous power of two. This means that free regions of a particular size
// class are always large enough to satisfy an allocation of that size class.
//
// The allocation size class is used to index into an array giving a list of
// free regions of that size class. The first region in the list is used and
// its start address updated; it may also be moved to a different list if it is
// now empty or too small to satisfy further allocations for this size class.
//
// Medium allocations are allocated out of chunks directly and small allocations
// out of 16KB sub-regions (which are essentially medium allocations
// themselves).
//
// Data about allocations is stored in a header in the chunk or region, using
// bitmaps for boolean flags.
//
// Sweeping works by processing a list of chunks, scanning each one for
// allocated but unmarked buffers and rebuilding the free region data for that
// chunk. Sweeping happens separately for minor and major GC and only
// nursery-owned or tenured-owned buffers are swept at one time. This means that
// chunks containing nursery-owned allocations are swept twice during a major
// GC, once to sweep nursery-owned allocations and once for tenured-owned
// allocations. This is required because the sweeping happens at different
// times.
//
// Chunks containing nursery-owned buffers are stored in a separate list to
// chunks that only contain tenured-owned buffers to reduce the number of chunks
// that need to be swept for minor GC. They are stored in |mixedChunks| and are
// moved to |mixedChunksToSweep| at the start of minor GC. They are then swept
// on a background thread and are placed in |sweptMixedChunks| if they are not
// freed. From there they can merged back into one of the main thread lists
// (since they may no longer contain nursery-owned buffers).
//
// Chunks containing tenured-owned buffers are stored in |tenuredChunks| and are
// moved to |tenuredChunksToSweep| at the start of major GC. They are
// unavailable for allocation after this point and will be swept on a background
// thread and placed in |sweptTenuredChunks| if they are not freed. From there
// they will be merged back into |tenuredChunks|. This means that allocation
// during an incremental GC will allocate a new chunk.
//
// Merging swept data requires taking a lock and so only happens when
// necessary. This happens when a new chunk is needed or at various points
// during GC. During sweeping no additional synchronization is required for
// allocation.
//
// Since major GC requires doing a minor GC at the start and we don't want to
// have to wait for minor GC sweeping to finish there is an optimization where
// chunks containing nursery-owned buffers swept as part of minor GC at the
// start of a major GC are moved directly from |sweptMixedChunks| to
// |tenuredChunksToSweep| at the end of minor GC sweeping. This is controlled by
// the |majorStartedWhileMinorSweeping| flag.
//
// Similarly, if a major GC finishes while minor GC is sweeping then rather than
// waiting for it to finish we set the |majorFinishedWhileMinorSweeping| flag so
// that we clear the |allocatedDuringCollection| for these chunks the end of the
// minor sweeping.
//
// Free works by extending neighboring free regions to cover the freed
// allocation or adding a new one if necessary. Free regions are found by
// checking the allocated bitmap. Free is not supported if the containing chunk
// is currently being swept off-thread. Free is only supported for medium sized
// allocations.
//
// Reallocation works by resizing in place if possible. It is always possible to
// shrink a medium allocation in place if the target size is still medium.
// In-place growth is possible if there is enough free space following the
// allocation. This is not supported if the containing chunk is currently being
// swept off-thread.  If not possible, a new allocation is made and the contents
// of the buffer copied. Resizing in place is only supported for medium sized
// allocations.
//
// Large allocations
// -----------------
//
// These are implemented using the OS page allocator. Allocations of this size
// are relatively rare and not much attempt is made to optimize them. They are
// chunk aligned which allows them to be distinguished from the other allocation
// kinds by checking the low bits the pointer.
//
// Shrinking large allocations in place is only supported on Posix-like systems.
//
// Naming conventions
// ------------------
//
// The following conventions are used in the code:
//  - alloc:  client pointer to allocated memory
//  - buffer: pointer to per-allocation metadata
//

class BufferAllocator : public SlimLinkedListElement<BufferAllocator> {
 public:
  static constexpr size_t MinSmallAllocShift = 4;    // 16 B
  static constexpr size_t MinMediumAllocShift = 12;  //  4 KB
  static constexpr size_t MinLargeAllocShift = 20;   //  1 MB

  // TODO: Ideally this would equal MinSmallAllocShift but we're constrained by
  // the size of FreeRegion which won't fit into 16 bytes.
  static constexpr size_t MinSizeClassShift = 5;  // 32 B
  static_assert(MinSizeClassShift >= MinSmallAllocShift);

  static constexpr size_t SmallSizeClasses =
      MinMediumAllocShift - MinSizeClassShift + 1;
  static constexpr size_t MediumSizeClasses =
      MinLargeAllocShift - MinMediumAllocShift + 1;
  static constexpr size_t AllocSizeClasses =
      SmallSizeClasses + MediumSizeClasses;

  static constexpr size_t FullChunkSizeClass = AllocSizeClasses;

  struct Stats;

  using AutoLock = AutoLockBufferAllocator;
  using MaybeLock = MaybeLockBufferAllocator;

 private:
  template <typename Derived, size_t SizeBytes, size_t GranularityBytes>
  friend struct AllocSpace;

  using BufferChunkList = SlimLinkedList<BufferChunk>;

  struct FreeRegion;
  using FreeList = SlimLinkedList<FreeRegion>;

  using SizeClassBitSet = mozilla::BitSet<AllocSizeClasses, uint32_t>;

  // Segregated free list: an array of free lists, one per size class.
  class FreeLists {
    using FreeListArray = mozilla::Array<FreeList, AllocSizeClasses>;

    FreeListArray lists;
    SizeClassBitSet available;

   public:
    class FreeListIter;
    class FreeRegionIter;

    FreeLists() = default;

    FreeLists(FreeLists&& other);
    FreeLists& operator=(FreeLists&& other);

    FreeListIter freeListIter();
    FreeRegionIter freeRegionIter();

    bool isEmpty() const { return available.IsEmpty(); }

    bool hasSizeClass(size_t sizeClass) const;
    const auto& availableSizeClasses() const { return available; }

    // Returns SIZE_MAX if none available.
    size_t getFirstAvailableSizeClass(size_t minSizeClass,
                                      size_t maxSizeClass) const;
    size_t getLastAvailableSizeClass(size_t minSizeClass,
                                     size_t maxSizeClass) const;

    FreeRegion* getFirstRegion(size_t sizeClass);

    void pushFront(size_t sizeClass, FreeRegion* region);
    void pushBack(size_t sizeClass, FreeRegion* region);

    void append(FreeLists&& other);

    void remove(size_t sizeClass, FreeRegion* region);

    void clear();

    template <typename Func>
    void forEachRegion(Func&& func);

    void assertEmpty() const;
    void assertContains(size_t sizeClass, FreeRegion* region) const;
    void checkAvailable() const;

    void getStats(Stats& stats);
  };

  class ChunkLists {
    using ChunkListArray =
        mozilla::Array<BufferChunkList, AllocSizeClasses + 1>;
    using AvailableBitSet = mozilla::BitSet<AllocSizeClasses + 1, uint32_t>;

    ChunkListArray lists;
    AvailableBitSet available;

   public:
    class ChunkListIter;
    class ChunkIter;

    ChunkLists() = default;

    ChunkLists(const ChunkLists& other) = delete;
    ChunkLists& operator=(const ChunkLists& other) = delete;

    ChunkListIter chunkListIter();
    ChunkIter chunkIter();
    const auto& availableSizeClasses() const { return available; }

    void pushFront(size_t sizeClass, BufferChunk* chunk);
    void pushBack(BufferChunk* chunk);
    void pushBack(size_t sizeClass, BufferChunk* chunk);

    // Returns SIZE_MAX if none available.
    size_t getFirstAvailableSizeClass(size_t minSizeClass,
                                      size_t maxSizeClass) const;

    BufferChunk* popFirstChunk(size_t sizeClass);

    void remove(size_t sizeClass, BufferChunk* chunk);

    BufferChunkList extractAllChunks();

    bool isEmpty() const;
    void checkAvailable() const;
  };

  using LargeAllocList = SlimLinkedList<LargeBuffer>;

  enum class State : uint8_t { NotCollecting, Marking, Sweeping };

  enum class SizeKind : uint8_t { Small, Medium };

  enum class SweepKind : uint8_t { Tenured = 0, Nursery };

  // The main GC runtime.
  MainThreadOrGCTaskData<GCRuntime*> gc;

  // The zone this allocator is associated with.
  MainThreadOrGCTaskData<JS::Zone*> zone;

  // Chunks containing medium and small buffers. They may contain both
  // nursery-owned and tenured-owned buffers.
  MainThreadOrGCTaskData<BufferChunkList> mixedChunks;

  // Chunks containing only tenured-owned small and medium buffers.
  MainThreadOrGCTaskData<BufferChunkList> tenuredChunks;

  // Free lists for the small and medium buffers in |mixedChunks| and
  // |tenuredChunks|. Used for allocation.
  MainThreadOrGCTaskData<FreeLists> freeLists;

  // Chunks that may contain nursery-owned buffers waiting to be swept during a
  // minor GC. Populated from |mixedChunks|.
  MainThreadOrGCTaskData<BufferChunkList> mixedChunksToSweep;

  // Chunks that contain only tenured-owned buffers waiting to be swept during a
  // major GC. Populated from |tenuredChunks|.
  MainThreadOrGCTaskData<BufferChunkList> tenuredChunksToSweep;

  // Chunks that have been swept. Populated by a background thread.
  MutexData<BufferChunkList> sweptMixedChunks;
  MutexData<BufferChunkList> sweptTenuredChunks;

  // Chunks that have been swept and are available for allocation but have not
  // had their free regions merged into |freeLists|. Owned by the main thread.
  MainThreadOrGCTaskData<ChunkLists> availableMixedChunks;
  MainThreadOrGCTaskData<ChunkLists> availableTenuredChunks;

  // List of large nursery-owned buffers.
  MainThreadOrGCTaskData<LargeAllocList> largeNurseryAllocs;

  // List of large tenured-owned buffers.
  MainThreadOrGCTaskData<LargeAllocList> largeTenuredAllocs;

  // Large buffers waiting to be swept.
  MainThreadOrGCTaskData<LargeAllocList> largeNurseryAllocsToSweep;
  MainThreadOrGCTaskData<LargeAllocList> largeTenuredAllocsToSweep;

  // Large buffers that have been swept.
  MutexData<LargeAllocList> sweptLargeTenuredAllocs;

  // Flag to indicate that data from sweeping is available to be merged. This
  // includes chunks in the |sweptMixedChunks| or |sweptTenuredChunks| lists and
  // the minorSweepingFinished flag.
  mozilla::Atomic<bool, mozilla::Relaxed> hasSweepDataToMerge;

  // GC state for minor and major GC.
  MainThreadOrGCTaskData<State> minorState;
  MainThreadOrGCTaskData<State> majorState;

  // Flags to tell the main thread that sweeping has finished and the state
  // should be updated.
  MutexData<bool> minorSweepingFinished;
  MutexData<bool> majorSweepingFinished;

  // A major GC was started while a minor GC was still sweeping. Chunks swept by
  // the minor GC will be moved directly to the list of chunks to sweep for the
  // major GC. This happens for the minor GC at the start of every major GC.
  MainThreadOrGCTaskData<bool> majorStartedWhileMinorSweeping;
  MainThreadOrGCTaskData<bool> majorSweepingStartedWhileMinorSweeping;

  // A major GC finished while a minor GC was still sweeping. Some post major GC
  // cleanup will be deferred to the end of the minor sweeping.
  MainThreadOrGCTaskData<bool> majorFinishedWhileMinorSweeping;

  // A mutex that must be held to call public APIs if the allocator is being
  // used by multiple threads. This is checked in debug builds.
  Mutex* multiThreadedMutex = nullptr;

 public:
  explicit BufferAllocator(GCRuntime* gc, JS::Zone* zone);
  ~BufferAllocator();

  static inline size_t GetGoodAllocSize(size_t requiredBytes);
  static inline size_t GetGoodElementCount(size_t requiredElements,
                                           size_t elementSize);
  static inline size_t GetGoodPower2AllocSize(size_t requiredBytes);
  static inline size_t GetGoodPower2ElementCount(size_t requiredElements,
                                                 size_t elementSize);
  static bool IsBufferAlloc(void* alloc);

  void* alloc(size_t bytes, bool nurseryOwned);
  void* allocInGC(size_t bytes, bool nurseryOwned);
  void* realloc(void* alloc, size_t bytes, bool nurseryOwned);
  void free(void* alloc);
  size_t getAllocSize(void* alloc);
  bool isNurseryOwned(void* alloc);

  void startMinorCollection(MaybeLock& lock);
  bool startMinorSweeping();
  void sweepForMinorCollection();

  void startMajorCollection(MaybeLock& lock);
  void startMajorSweeping(MaybeLock& lock);
  void sweepForMajorCollection(bool shouldDecommit);
  void finishMajorCollection(const AutoLock& lock);
  void clearMarkStateAfterBarrierVerification();
  void clearChunkMarkBits(BufferChunk* chunk);
  void clearMarkBitsInStolenChunks();

  bool isEmpty() const;

  static void* TraceEdge(JSTracer* trc, void** bufferp, const char* name);

  bool markTenuredAlloc(void* alloc);
  bool isMarkedBlack(void* alloc);

  // Allow use off main thread while holding the given mutex. Must be called
  // from the main thread.
  void setMultiThreadedUse(Mutex* mutex);
  void clearMultiThreadedUse();

  // For debugging, used to implement GetMarkInfo. Returns false for allocations
  // being swept on another thread.
  bool isPointerWithinBuffer(void* ptr);

  size_t getSizeOfNurseryBuffers();

  void addBufferSizesAndCounts(size_t* usedBytesOut, size_t* freeBytesOut,
                               size_t* adminBytesOut, size_t* totalChunksOut,
                               size_t* freeRegionsOut, size_t* largeAllocsOut);

  static void printStatsHeader(FILE* file);
  static void printStats(GCRuntime* gc, mozilla::TimeStamp creationTime,
                         bool isMajorGC, FILE* file);

  struct Stats {
    size_t usedBytes = 0;
    size_t freeBytes = 0;
    size_t adminBytes = 0;
    size_t mixedSmallRegions = 0;
    size_t tenuredSmallRegions = 0;
    size_t mixedChunks = 0;
    size_t tenuredChunks = 0;
    size_t availableMixedChunks = 0;
    size_t availableTenuredChunks = 0;
    size_t freeRegions = 0;
    size_t largeNurseryAllocs = 0;
    size_t largeTenuredAllocs = 0;
  };
  void getStats(Stats& stats);

#ifdef DEBUG
  bool hasAlloc(void* alloc);
  void checkGCStateNotInUse();
  void checkGCStateNotInUse(MaybeLock& lock);
  void checkGCStateNotInUse(const AutoLock& lock);
#endif

 private:
  void checkAccess() const;
  void checkMainThread() const;
  bool isUsedByMainThread() const;

  BufferAllocatorRuntime* runtime() const;
  friend class AutoLockBufferAllocator;

  void markNurseryOwnedAlloc(void* alloc, bool nurseryOwned);
  friend class js::Nursery;

  void maybeMergeSweptData();
  void maybeMergeSweptData(MaybeLock& lock);
  void mergeSweptData();
  void mergeSweptData(const AutoLock& lock);
  void abortMajorSweeping(const AutoLock& lock);
  void clearAllocatedDuringCollectionState(const AutoLock& lock);

  // Small allocation methods:

  static inline bool IsSmallAllocSize(size_t bytes);
  static bool IsSmallAlloc(void* alloc);

  void* allocSmall(size_t bytes, bool nurseryOwned, bool inGC);
  void* retrySmallAlloc(size_t requestedBytes, size_t sizeClass, bool inGC);
  bool allocNewSmallRegion(bool inGC);
  void traceSmallAlloc(JSTracer* trc, void** allocp, const char* name);
  void markSmallNurseryOwnedBuffer(void* alloc, bool nurseryOwned);
  bool markSmallTenuredAlloc(void* alloc);

  // Medium allocation methods:

  static bool IsMediumAlloc(void* alloc);
  static bool CanSweepAlloc(bool nurseryOwned,
                            BufferAllocator::SweepKind sweepKind);

  void* allocMedium(size_t bytes, bool nurseryOwned, bool inGC);
  void* retryMediumAlloc(size_t requestedBytes, size_t sizeClass, bool inGC);
  template <typename Alloc, typename GrowHeap>
  void* refillFreeListsAndRetryAlloc(size_t sizeClass, size_t maxSizeClass,
                                     Alloc&& alloc, GrowHeap&& growHeap);
  enum class RefillResult { Fail = 0, Success, Retry };
  template <typename GrowHeap>
  RefillResult refillFreeLists(size_t sizeClass, size_t maxSizeClass,
                               GrowHeap&& growHeap);
  bool useAvailableChunk(size_t sizeClass, size_t maxSizeClass);
  bool useAvailableChunk(size_t sizeClass, size_t maxSizeClass, ChunkLists& src,
                         BufferChunkList& dst);
  SizeClassBitSet getChunkSizeClassesToMove(size_t maxSizeClass,
                                            ChunkLists& src) const;
  void* bumpAlloc(size_t bytes, size_t sizeClass, size_t maxSizeClass);
  void* allocFromRegion(FreeRegion* region, size_t bytes, size_t sizeClass);
  void* allocMediumAligned(size_t bytes, bool inGC);
  void* retryAlignedAlloc(size_t sizeClass, bool inGC);
  void* alignedAlloc(size_t sizeClass);
  void* alignedAllocFromRegion(FreeRegion* region, size_t sizeClass);
  void updateFreeListsAfterAlloc(FreeLists* freeLists, FreeRegion* region,
                                 size_t sizeClass);
  void setAllocated(void* alloc, size_t bytes, bool nurseryOwned, bool inGC);
  void setChunkHasNurseryAllocs(BufferChunk* chunk);
  void recommitRegion(FreeRegion* region);
  bool stealOrAllocNewChunk(size_t sizeClass, bool inGC);
  bool allocNewChunk(bool inGC);
  bool sweepChunk(BufferChunk* chunk, SweepKind sweepKind, bool shouldDecommit);
  void addSweptRegion(BufferChunk* chunk, uintptr_t freeStart,
                      uintptr_t freeEnd, bool shouldDecommit,
                      bool expectUnchanged, FreeLists& freeLists);
  bool sweepSmallBufferRegion(BufferChunk* chunk, SmallBufferRegion* region,
                              SweepKind sweepKind);
  void addSweptRegion(SmallBufferRegion* region, uintptr_t freeStart,
                      uintptr_t freeEnd, bool shouldDecommit,
                      bool expectUnchanged, FreeLists& freeLists);
  void freeMedium(void* alloc);
  bool growMedium(void* alloc, size_t newBytes);
  bool shrinkMedium(void* alloc, size_t newBytes);
  FreeRegion* makeFreeRegion(uintptr_t start, uintptr_t bytes,
                             bool anyDecommitted, bool expectUnchanged = false);
  void pushFreeRegionBack(FreeLists* freeLists, FreeRegion* region,
                          SizeKind kind);
  void pushFreeRegionFront(FreeLists* freeLists, FreeRegion* region,
                           SizeKind kind);
  void updateFreeRegionStart(FreeLists* freeLists, FreeRegion* region,
                             uintptr_t newStart, SizeKind kind);
  FreeLists* getChunkFreeLists(BufferChunk* chunk);
  ChunkLists* getChunkAvailableLists(BufferChunk* chunk);
  void maybeUpdateAvailableLists(ChunkLists* availableChunks,
                                 BufferChunk* chunk, size_t oldChunkSizeClass);
  bool canModifyAllocations(BufferChunk* chunk);
  bool isConcurrentMarking() const;
  bool isSweepingChunk(BufferChunk* chunk);
  void traceMediumAlloc(JSTracer* trc, void** allocp, const char* name);
  bool isMediumBufferNurseryOwned(void* alloc) const;
  void markMediumNurseryOwnedBuffer(void* alloc, bool nurseryOwned);
  bool markMediumTenuredAlloc(void* alloc);

  // Determine whether a size class is for a small or medium allocation.
  static SizeKind SizeClassKind(size_t sizeClass);

  // Get the size class for an allocation. This rounds up to a class that is
  // large enough to hold the required size.
  static size_t SizeClassForSmallAlloc(size_t bytes);
  static size_t SizeClassForMediumAlloc(size_t bytes);

  // Get the maximum size class of allocations that can use a free region. This
  // rounds down to the largest class that can fit in this region.
  static size_t SizeClassForFreeRegion(size_t bytes, SizeKind kind);

  static void CheckFreeRegionClass(FreeRegion* region, size_t sizeClass);

  static size_t SizeClassBytes(size_t sizeClass);
  friend struct BufferChunk;

  static void ClearAllocatedDuringCollection(ChunkLists& chunks);
  static void ClearAllocatedDuringCollection(BufferChunkList& list);
  static void ClearAllocatedDuringCollection(LargeAllocList& list);

  // Large allocation methods:

  static inline bool IsLargeAllocSize(size_t bytes);
  static bool IsLargeAlloc(void* alloc);
  static void TraceLargeAlloc(JSTracer* trc, void** allocp, const char* name);

  void* allocLarge(size_t bytes, bool nurseryOwned, bool inGC);
  bool isLargeTenuredMarked(LargeBuffer* buffer);
  void freeLarge(void* alloc);
  bool shrinkLarge(LargeBuffer* buffer, size_t newBytes);
  void unmapLarge(LargeBuffer* buffer, bool isSweeping, MaybeLock& lock);
  void unregisterLarge(LargeBuffer* buffer, bool isSweeping, MaybeLock& lock);
  void traceLargeBuffer(JSTracer* trc, LargeBuffer* buffer, const char* name);
  void markLargeNurseryOwnedBuffer(LargeBuffer* buffer, bool nurseryOwned);
  bool markLargeTenuredBuffer(LargeBuffer* buffer);

  // Lookup a large buffer by pointer in the map.
  LargeBuffer* lookupLargeBuffer(void* alloc);
  LargeBuffer* lookupLargeBuffer(void* alloc, MaybeLock& lock);
  bool needLockToAccessBufferMap() const;

  void increaseHeapSize(size_t bytes, bool nurseryOwned, bool checkThresholds,
                        bool updateRetainedSize);
  void decreaseHeapSize(size_t bytes, bool nurseryOwned,
                        bool updateRetainedSize);

  // Testing functions we allow access.
  friend void* TestAllocAligned(JS::Zone* zone, size_t bytes);
  friend size_t TestGetAllocSizeKind(void* alloc);

#ifdef DEBUG
  void checkChunkListsGCStateNotInUse(ChunkLists& chunkLists,
                                      bool hasNurseryOwnedAllocs,
                                      bool allowAllocatedDuringCollection);
  void checkChunkListGCStateNotInUse(BufferChunkList& chunks,
                                     bool hasNurseryOwnedAllocs,
                                     bool allowAllocatedDuringCollection,
                                     bool allowFreeLists);
  void checkChunkGCStateNotInUse(BufferChunk* chunk,
                                 bool allowAllocatedDuringCollection,
                                 bool allowFreeLists);
  void checkAllocListGCStateNotInUse(LargeAllocList& list, bool isNurseryOwned);
  void verifyChunk(BufferChunk* chunk, bool hasNurseryOwnedAllocs);
  void verifyFreeRegion(BufferChunk* chunk, uintptr_t endOffset,
                        size_t expectedSize, size_t& freeRegionCount);
  void verifySmallBufferRegion(SmallBufferRegion* region,
                               size_t& freeRegionCount);
  void verifyFreeRegion(SmallBufferRegion* chunk, uintptr_t endOffset,
                        size_t expectedSize, size_t& freeRegionCount);
#endif
};

static constexpr size_t SmallAllocGranularityShift =
    BufferAllocator::MinSmallAllocShift;
static constexpr size_t MediumAllocGranularityShift =
    BufferAllocator::MinMediumAllocShift;

static constexpr size_t SmallAllocGranularity = 1 << SmallAllocGranularityShift;
static constexpr size_t MediumAllocGranularity = 1
                                                 << MediumAllocGranularityShift;

static constexpr size_t MinSmallAllocSize =
    1 << BufferAllocator::MinSmallAllocShift;
static constexpr size_t MinMediumAllocSize =
    1 << BufferAllocator::MinMediumAllocShift;
static constexpr size_t MinLargeAllocSize =
    1 << BufferAllocator::MinLargeAllocShift;

static constexpr size_t MinAllocSize = MinSmallAllocSize;

static constexpr size_t MaxSmallAllocSize =
    MinMediumAllocSize - SmallAllocGranularity;
static constexpr size_t MaxMediumAllocSize =
    MinLargeAllocSize - MediumAllocGranularity;
static constexpr size_t MaxAlignedAllocSize = MinLargeAllocSize / 4;

}  // namespace gc
}  // namespace js

#endif  // gc_BufferAllocator_h
