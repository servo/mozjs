/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Internal class definitions for the buffer allocator.

#ifndef gc_BufferAllocatorInternals_h
#define gc_BufferAllocatorInternals_h

#include <bit>

#include "NamespaceImports.h"

#include "ds/SlimLinkedList.h"
#include "gc/BufferAllocator.h"
#include "gc/IteratorUtils.h"

namespace js::gc {

static constexpr size_t MinFreeRegionSize =
    1 << BufferAllocator::MinSizeClassShift;

static constexpr size_t SmallRegionShift = 14;  // 16 KB
static constexpr size_t SmallRegionSize = 1 << SmallRegionShift;
static constexpr uintptr_t SmallRegionMask = SmallRegionSize - 1;
static_assert(SmallRegionSize >= MinMediumAllocSize);
static_assert(SmallRegionSize <= MaxMediumAllocSize);

// Size classes map to power of two sizes. The full range contains two
// consecutive sub-ranges [MinSmallAllocClass, MaxSmallAllocClass] and
// [MinMediumAllocClass, MaxMediumAllocClass]. MaxSmallAllocClass and
// MinMediumAllocClass are consecutive but both map to the same size, which is
// MinMediumAllocSize.
static constexpr size_t MinSmallAllocClass = 0;
static constexpr size_t MaxSmallAllocClass =
    BufferAllocator::SmallSizeClasses - 1;
static constexpr size_t MinMediumAllocClass = MaxSmallAllocClass + 1;
static constexpr size_t MaxMediumAllocClass =
    MinMediumAllocClass + BufferAllocator::MediumSizeClasses - 1;
static_assert(MaxMediumAllocClass == BufferAllocator::AllocSizeClasses - 1);

#ifdef DEBUG
// Magic check values used debug builds.
static constexpr uint32_t LargeBufferCheckValue = 0xBFA110C2;
static constexpr uint32_t FreeRegionCheckValue = 0xBFA110C3;
#endif

// Iterator that yields the indexes of set bits in a mozilla::BitSet.
template <size_t N, typename Word = size_t>
class BitSetIter {
  using BitSet = mozilla::BitSet<N, Word>;
  const BitSet& bitset;
  size_t bit = 0;

 public:
  explicit BitSetIter(const BitSet& bitset) : bitset(bitset) {
    MOZ_ASSERT(!done());
    if (!bitset[bit]) {
      next();
    }
  }
  bool done() const {
    MOZ_ASSERT(bit <= N || bit == SIZE_MAX);
    return bit >= N;
  }
  void next() {
    MOZ_ASSERT(!done());
    bit++;
    if (bit != N) {
      bit = bitset.FindNext(bit);
    }
  }
  size_t get() const {
    MOZ_ASSERT(!done());
    return bit;
  }
  operator size_t() const { return get(); }
};

// Iterator that yields the indexes of set bits in an AtomicBitmap.
template <size_t N>
class js::gc::AtomicBitmap<N>::Iter {
  const AtomicBitmap& bitmap;
  size_t bit = 0;

 public:
  explicit Iter(AtomicBitmap& bitmap) : bitmap(bitmap) {
    if (!bitmap.getBit(bit)) {
      next();
    }
  }

  bool done() const {
    MOZ_ASSERT(bit <= N);
    return bit == N;
  }

  void next() {
    MOZ_ASSERT(!done());

    bit++;
    if (bit == N) {
      return;
    }

    static constexpr size_t bitsPerWord = sizeof(Word) * CHAR_BIT;
    size_t wordIndex = bit / bitsPerWord;
    size_t bitIndex = bit % bitsPerWord;

    uintptr_t word = bitmap.getWord(wordIndex);
    // Mask word containing |bit|.
    word &= (uintptr_t(-1) << bitIndex);
    while (word == 0) {
      wordIndex++;
      if (wordIndex == WordCount) {
        bit = N;
        return;
      }
      word = bitmap.getWord(wordIndex);
    }

    bitIndex = std::countr_zero(word);
    bit = wordIndex * bitsPerWord + bitIndex;
  }

  size_t get() const {
    MOZ_ASSERT(!done());
    return bit;
  }
};

// Iterator that yields offsets and pointers into a block of memory
// corresponding to the bits set in a BitSet.
template <typename BitmapIter, size_t Granularity, typename T = void>
class BitmapToBlockIter : public BitmapIter {
  uintptr_t baseAddr;

 public:
  template <typename S>
  BitmapToBlockIter(void* base, S&& arg)
      : BitmapIter(std::forward<S>(arg)), baseAddr(uintptr_t(base)) {}
  size_t getOffset() const { return BitmapIter::get() * Granularity; }
  T* get() const { return reinterpret_cast<T*>(baseAddr + getOffset()); }
  operator T*() const { return get(); }
  T* operator->() const { return get(); }
};

template <typename T>
class LinkedListIter {
  T* element;

 public:
  explicit LinkedListIter(SlimLinkedList<T>& list) : element(list.getFirst()) {}
  bool done() const { return !element; }
  void next() {
    MOZ_ASSERT(!done());
    element = element->getNext();
  }
  T* get() const { return element; }
  operator T*() const { return get(); }
  T* operator->() const { return get(); }
};

class BufferAllocator::FreeLists::FreeListIter
    : public BitSetIter<AllocSizeClasses, uint32_t> {
  FreeLists& freeLists;

 public:
  explicit FreeListIter(FreeLists& freeLists)
      : BitSetIter(freeLists.available), freeLists(freeLists) {}
  FreeList& get() {
    size_t sizeClass = BitSetIter::get();
    return freeLists.lists[sizeClass];
  }
  operator FreeList&() { return get(); }
};

class BufferAllocator::FreeLists::FreeRegionIter
    : public NestedIterator<FreeListIter, LinkedListIter<FreeRegion>> {
 public:
  explicit FreeRegionIter(FreeLists& freeLists) : NestedIterator(freeLists) {}
};

class BufferAllocator::ChunkLists::ChunkListIter
    : public BitSetIter<AllocSizeClasses + 1, uint32_t> {
  ChunkLists& chunkLists;

 public:
  explicit ChunkListIter(ChunkLists& chunkLists)
      : BitSetIter(chunkLists.available), chunkLists(chunkLists) {}
  BufferChunkList& get() { return chunkLists.lists[getSizeClass()]; }
  size_t getSizeClass() const { return BitSetIter::get(); }
  operator BufferChunkList&() { return get(); }
};

class BufferAllocator::ChunkLists::ChunkIter
    : public NestedIterator<ChunkListIter, LinkedListIter<BufferChunk>> {
 public:
  explicit ChunkIter(ChunkLists& chunkLists) : NestedIterator(chunkLists) {}
  size_t getSizeClass() const { return iterA().getSizeClass(); }
};

template <typename Derived, size_t Size, size_t Granularity>
struct AllocSpace {
  static_assert(Size > Granularity);
  static_assert(std::has_single_bit(Size));
  static_assert(std::has_single_bit(Granularity));
  static constexpr size_t SizeBytes = Size;
  static constexpr size_t GranularityBytes = Granularity;

  static constexpr uintptr_t AddressMask = SizeBytes - 1;
  static constexpr size_t MaxAllocCount = SizeBytes / GranularityBytes;

  using PerAllocBitmap = mozilla::BitSet<MaxAllocCount>;
  using AtomicPerAllocBitmap =
      mozilla::BitSet<MaxAllocCount, mozilla::Atomic<size_t, mozilla::Relaxed>>;

  // Mark bitmap: one bit minimum per allocation, no gray bits. This is atomic
  // because parallel marking may try and mark the same allocation on different
  // threads at the same time.
  MainThreadOrGCTaskData<AtomicBitmap<MaxAllocCount>> markBits;

  // Allocation start and end bitmaps: for every allocation these have a bit set
  // corresponding to the start of the allocation and to the last byte of the
  // allocation. |allocEndBitmap| is atomic so we can get allocation sizes for
  // resize while sweeping is happening.
  MainThreadOrGCTaskData<PerAllocBitmap> allocStartBitmap;
  MainThreadOrGCTaskData<AtomicPerAllocBitmap> allocEndBitmap;

  // A bitmap indicating whether an allocation is owned by a nursery or a
  // tenured GC thing. This is atomic because we read it for major GC tracing,
  // which can happen at the same time as the chunk is being swept for minor GC.
  MainThreadOrGCTaskData<AtomicPerAllocBitmap> nurseryOwnedBitmap;

  static constexpr uintptr_t firstAllocOffset() {
    return RoundUp(sizeof(Derived), GranularityBytes);
  }

  using AllocIter =
      BitmapToBlockIter<BitSetIter<MaxAllocCount>, GranularityBytes>;
  AllocIter allocIter() { return {asDerived(), allocStartBitmap.ref()}; }

  void setAllocated(void* alloc, size_t bytes, bool allocated);
  void updateEndOffset(void* alloc, size_t oldBytes, size_t newBytes);
  void setDeallocated(void* alloc, size_t bytes);

  bool isAllocated(const void* alloc) const {
    size_t bit = ptrToIndex(alloc);
    return allocStartBitmap.ref()[bit];
  }

  bool isAllocated(uintptr_t offset) const {
    size_t bit = offsetToIndex(offset);
    return allocStartBitmap.ref()[bit];
  }

  size_t allocBytes(const void* alloc) const;

  void setNurseryOwned(void* alloc, bool nurseryOwned) {
    MOZ_ASSERT(isAllocated(alloc));
    size_t bit = ptrToIndex(alloc);
    nurseryOwnedBitmap.ref()[bit] = nurseryOwned;
  }

  bool isNurseryOwned(const void* alloc) const {
    MOZ_ASSERT(isAllocated(alloc));
    size_t bit = ptrToIndex(alloc);
    return nurseryOwnedBitmap.ref()[bit];
  }

  bool setMarked(void* alloc);

  void setUnmarked(void* alloc) {
    MOZ_ASSERT(isAllocated(alloc));
    size_t bit = ptrToIndex(alloc);
    markBits.ref().setBit(bit, false);
  }

  bool isMarked(const void* alloc) const {
    MOZ_ASSERT(isAllocated(alloc));
    size_t bit = ptrToIndex(alloc);
    return markBits.ref().getBit(bit);
  }

  // Find next/previous allocations from |offset|. Return SizeBytes on failure.
  size_t findNextAllocated(uintptr_t offset) const;
  size_t findPrevAllocated(uintptr_t offset) const;

  // Find next/previous free region from a start/end address.
  using FreeRegion = BufferAllocator::FreeRegion;
  FreeRegion* findFollowingFreeRegion(uintptr_t startAddr);
  FreeRegion* findPrecedingFreeRegion(uintptr_t endAddr);

  using FreeLists = BufferAllocator::FreeLists;
  using SweepKind = BufferAllocator::SweepKind;
  struct SweepResult {
    bool isEmpty = false;
    bool hasNurseryOwnedAllocs = false;
    size_t bytesFreed = 0;
  };
  SweepResult sweep(BufferAllocator* allocator, FreeLists& freeLists,
                    SweepKind sweepKind, bool sweptAnyPreviously,
                    bool shouldDecommit);

 protected:
  AllocSpace() {
    MOZ_ASSERT(allocStartBitmap.ref().IsEmpty());
    MOZ_ASSERT(allocEndBitmap.ref().IsEmpty());
    MOZ_ASSERT(nurseryOwnedBitmap.ref().IsEmpty());
  }

  uintptr_t startAddress() const {
    return uintptr_t(static_cast<const Derived*>(this));
  }

  template <size_t Divisor = GranularityBytes, size_t Align = Divisor>
  size_t ptrToIndex(const void* alloc) const {
    MOZ_ASSERT((uintptr_t(alloc) & ~AddressMask) == startAddress());
    uintptr_t offset = uintptr_t(alloc) & AddressMask;
    return offsetToIndex<Divisor, Align>(offset);
  }

  template <size_t Divisor = GranularityBytes, size_t Align = Divisor>
  static size_t offsetToIndex(uintptr_t offset) {
    MOZ_ASSERT(isValidOffset(offset));
    MOZ_ASSERT(offset % Align == 0);
    return offset / Divisor;
  }

  const void* ptrFromOffset(uintptr_t offset) const {
    MOZ_ASSERT(isValidOffset(offset));
    MOZ_ASSERT(offset % GranularityBytes == 0);
    return reinterpret_cast<void*>(startAddress() + offset);
  }

  size_t endBitIndex(size_t startIndex, size_t bytes) {
    MOZ_ASSERT(startIndex < MaxAllocCount);
    MOZ_ASSERT(bytes != 0);
    MOZ_ASSERT(bytes % GranularityBytes == 0);
    size_t endIndex = startIndex + bytes / GranularityBytes - 1;
    MOZ_ASSERT(endIndex < MaxAllocCount);
    return endIndex;
  }

  size_t findEndBit(size_t startIndex) const {
    MOZ_ASSERT(startIndex < MaxAllocCount);
    size_t endIndex = allocEndBitmap.ref().FindNext(startIndex);
    if (endIndex == SIZE_MAX) {
      return MaxAllocCount;
    }
    return endIndex;
  }

#ifdef DEBUG
  static bool isValidOffset(uintptr_t offset) {
    return offset >= firstAllocOffset() && offset < SizeBytes;
  }
#endif

 private:
  Derived* asDerived() { return static_cast<Derived*>(this); }
};

// A chunk containing medium buffer allocations for a single zone. Unlike
// ArenaChunk, allocations from different zones do not share chunks.
struct BufferChunk
    : public ChunkBase,
      public SlimLinkedListElement<BufferChunk>,
      public AllocSpace<BufferChunk, ChunkSize, MediumAllocGranularity> {
  MainThreadOrGCTaskData<Zone*> zone;

  MainThreadOrGCTaskData<bool> allocatedDuringCollection;
  MainThreadOrGCTaskData<bool> stolenFromSweepList;
  MainThreadOrGCTaskData<bool> hasNurseryOwnedAllocs;
  MainThreadOrGCTaskData<bool> hasNurseryOwnedAllocsAfterSweep;

  static constexpr size_t MaxAllocsPerChunk = MaxAllocCount;  // todo remove

  static constexpr size_t PagesPerChunk = ChunkSize / PageSize;
  using PerPageBitmap = mozilla::BitSet<PagesPerChunk, uint32_t>;
  MainThreadOrGCTaskData<PerPageBitmap> decommittedPages;

  // A bitmap indicating which areas of the chunk are used to hold
  // SmallBufferRegions. This is atomic because it can be read to determine the
  // kind of an allocation while the chunk is being swept.
  static constexpr size_t SmallRegionsPerChunk = ChunkSize / SmallRegionSize;
  using SmallRegionBitmap = AtomicBitmap<SmallRegionsPerChunk>;
  MainThreadOrGCTaskData<SmallRegionBitmap> smallRegionBitmap;

  // Free regions in this chunk. When a chunk is swept its free regions are
  // stored here. When the chunk is being used for allocation these are moved to
  // BufferAllocator::freeLists. |ownsFreeLists| indicates whether this is in
  // use.
  MainThreadOrGCTaskData<BufferAllocator::FreeLists> freeLists;
  MainThreadOrGCTaskData<bool> ownsFreeLists;

  using SmallRegionIter = BitmapToBlockIter<SmallRegionBitmap::Iter,
                                            SmallRegionSize, SmallBufferRegion>;
  SmallRegionIter smallRegionIter() { return {this, smallRegionBitmap.ref()}; }

  static const BufferChunk* from(const void* alloc) {
    return from(const_cast<void*>(alloc));
  }
  static BufferChunk* from(void* alloc) {
    ChunkBase* chunk = js::gc::detail::GetGCAddressChunkBase(alloc);
    MOZ_ASSERT(chunk->kind == ChunkKind::Buffers);
    return static_cast<BufferChunk*>(chunk);
  }

  explicit BufferChunk(Zone* zone);
  ~BufferChunk();

  void setSmallBufferRegion(void* alloc, bool smallAlloc);
  bool isSmallBufferRegion(const void* alloc) const;

  size_t sizeClassForAvailableLists() const;

  void clearMarkBits();
  void clearMarkBitsIfStolenChunk();

  bool isPointerWithinAllocation(void* ptr) const;

  void getStats(BufferAllocator::Stats& stats);
};

constexpr size_t FirstMediumAllocOffset = BufferChunk::firstAllocOffset();

// A sub-region backed by a medium allocation which contains small buffer
// allocations.
struct SmallBufferRegion : public AllocSpace<SmallBufferRegion, SmallRegionSize,
                                             SmallAllocGranularity> {
  MainThreadOrGCTaskData<bool> hasNurseryOwnedAllocs_;

  static SmallBufferRegion* from(void* alloc) {
    uintptr_t addr = uintptr_t(alloc) & ~SmallRegionMask;
    auto* region = reinterpret_cast<SmallBufferRegion*>(addr);
#ifdef DEBUG
    BufferChunk* chunk = BufferChunk::from(region);
    MOZ_ASSERT(chunk->isAllocated(region));
    MOZ_ASSERT(chunk->isSmallBufferRegion(region));
#endif
    return region;
  }

  void setHasNurseryOwnedAllocs(bool value);
  bool hasNurseryOwnedAllocs() const;

  bool isPointerWithinAllocation(void* ptr) const;
};

constexpr size_t FirstSmallAllocOffset = SmallBufferRegion::firstAllocOffset();
static_assert(FirstSmallAllocOffset < SmallRegionSize);

// Describes a free region in a buffer chunk. This structure is stored at the
// end of the region.
//
// Medium allocations are made in FreeRegions in increasing address order. The
// final allocation will contain the now empty and unused FreeRegion structure.
// FreeRegions are stored in buckets based on their size in FreeLists. Each
// bucket is a linked list of FreeRegions.
struct BufferAllocator::FreeRegion
    : public SlimLinkedListElement<BufferAllocator::FreeRegion> {
  uintptr_t startAddr;
  bool hasDecommittedPages;

#ifdef DEBUG
  uint32_t checkValue = FreeRegionCheckValue;
#endif

  explicit FreeRegion(uintptr_t startAddr, bool decommitted = false)
      : startAddr(startAddr), hasDecommittedPages(decommitted) {}

  static FreeRegion* fromEndOffset(BufferChunk* chunk, uintptr_t endOffset) {
    MOZ_ASSERT(endOffset <= ChunkSize);
    return fromEndAddr(uintptr_t(chunk) + endOffset);
  }
  static FreeRegion* fromEndOffset(SmallBufferRegion* region,
                                   uintptr_t endOffset) {
    MOZ_ASSERT(endOffset <= SmallRegionSize);
    return fromEndAddr(uintptr_t(region) + endOffset);
  }
  static FreeRegion* fromEndAddr(uintptr_t endAddr) {
    MOZ_ASSERT(endAddr % SmallAllocGranularity == 0);
    auto* region = reinterpret_cast<FreeRegion*>(endAddr - sizeof(FreeRegion));
    region->check();
    return region;
  }

  void check() const { MOZ_ASSERT(checkValue == FreeRegionCheckValue); }

  uintptr_t getEnd() const { return uintptr_t(this + 1); }
  size_t size() const { return getEnd() - startAddr; }
};

// Metadata about a large buffer, stored externally.
struct LargeBuffer : public SlimLinkedListElement<LargeBuffer> {
  void* alloc;
  size_t bytes;
  mozilla::Atomic<bool, mozilla::Relaxed> isMarked;
  bool isNurseryOwned;
  bool allocatedDuringCollection = false;

#ifdef DEBUG
  uint32_t checkValue = LargeBufferCheckValue;
#endif

  LargeBuffer(void* alloc, size_t bytes, bool nurseryOwned)
      : alloc(alloc), bytes(bytes), isNurseryOwned(nurseryOwned) {
    MOZ_ASSERT((bytes % ChunkSize) == 0);
  }

  void check() const { MOZ_ASSERT(checkValue == LargeBufferCheckValue); }

  inline Zone* zone();
  inline Zone* zoneFromAnyThread();

  void* data() { return alloc; }
  size_t allocBytes() const { return bytes; }
  bool isPointerWithinAllocation(void* ptr) const;
};

}  // namespace js::gc

#endif  // gc_BufferAllocatorInternals_h
