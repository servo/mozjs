/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BASEALLOCINTERNALS_H
#define BASEALLOCINTERNALS_H

#include "mozilla/DoublyLinkedList.h"
#include "mozilla/Maybe.h"

#include "BaseAlloc.h"

// Allocation sizes must fit in a 31 bit unsigned integer.
typedef uint32_t base_alloc_size_t;
constexpr static base_alloc_size_t BASE_ALLOC_SIZE_MAX = UINT32_MAX >> 1;

// Cells at least this large are candidates for decommitting, but the first
// and last pages can never be decommited since they contain metadata.
constexpr base_alloc_size_t kDecommitThreshold = 4096 * 4;

// Implemtnation details for the base allocator.  These must be in a header
// file so that the C++ compiler can find them, but they're not part of the
// interface.

// The BaseAllocMetadata and BaseAllocCell classes provide an abstraction for
// cell metadata in the base allocator.
//
// The base allocator uses a layout inspired by dlmalloc, giving it a
// parseable heap that allows merging of neighbouring cells while being
// simple is the reason for choosing this design.  Each cell has metadata on
// either side.
//
// ----------+--------------+---------+---------+------+------------------+
//     BaseAllocMetadata    |   BaseAllocCell   |    BaseAllocMetaData    |
//   Left    |     Right    |                   |   Left   |    Right     |
//   Size    | Size / alloc | Payload | Padding |   Size   | Size / alloc |
// ----------+--------------+---------+---------+----------+--------------+
//                          ^                                             ^
//                          Pointer, 16-byte aligned.       16-byte aligned
//
// All cells track their size in the `sizeof(base_alloc_size_t)` bytes
// immediately before their payload, and in the sizeof(unsigned) bytes
// after.  Duplicating this information is what enables each cell to find
// its neighbours.  The first and last cell have no neighbours and these
// fields contain 0.
//
// Each cell's payload shall be 16-byte aligned as some platforms make it
// the minimum.
//
// Each cell's payload should be on its own cache line(s) (from other
// payloads) to avoid false sharing during use.  Note that this allows the
// size field of one cell to be on another cell's cache line.  We assume
// that allocations and frees in the base allocator are rare and this false
// sharing of the metadata acceptable.
//
// Padding is necessary when sizeof(BaseAllocMetadata) < kBaseQuantum to keep
// the next cell aligned and payloads in different cache lines.
//
// Unallocated cell layout replaces the payload with pointers to manage a
// free list.  This is not a security risk since these allocations are never
// used outside of mozjemalloc.
//
// ----------+--------------+----------+---------+------+------------------+
//     BaseAllocMetadata    |   BaseAllocCell    |    BaseAllocMetaData
//   Left    |     Right    |   Free   |         |   Left   |    Right
//   Size    | Size / alloc | list ptr | Padding |   Size   | Size / alloc
// ----------+--------------+----------+---------+----------+--------------+
//

struct BaseAllocMetadata {
  // The size of the cell to this metadata's left (lower memory address)
  base_alloc_size_t mLeftSize;

  // The size of the cell to this metadata's right (higher memory address)
  base_alloc_size_t mRightSize : 31;

  bool mRightAllocated : 1;

  // There's no constructor because we must preserve either the previous or
  // next size depending on which cell's metadata needs setting.

  void InitForRightCell(base_alloc_size_t aSize) {
    mRightSize = aSize;
    mRightAllocated = false;
  }
  void InitForLeftCell(base_alloc_size_t aSize) { mLeftSize = aSize; }

  void Clear() {
    mLeftSize = 0;
    mRightSize = 0;
    mRightAllocated = false;
  }
};

class BaseAllocCell {
 private:
  // When the cell is free these are used to track it on a "free list".  The
  // Regular cells use mListElem but oversize cells are stored in a search
  // tree using mTreeElem.  They can be part of a union since both are never
  // used at the same time.
  union {
    mozilla::DoublyLinkedListElement<BaseAllocCell> mListElem;
    RedBlackTreeNode<BaseAllocCell> mTreeElem;
  };
  bool mCommitted = true;

  friend struct mozilla::GetDoublyLinkedListElement<BaseAllocCell>;
  friend struct BaseAllocCellRBTrait;

  BaseAllocMetadata* LeftMetadata() {
    // Assert that the address computation here produces a properly aligned
    // result.
    static_assert(((alignof(BaseAllocCell) - sizeof(BaseAllocMetadata)) %
                   alignof(BaseAllocMetadata)) == 0);

    return reinterpret_cast<BaseAllocMetadata*>(
        reinterpret_cast<uintptr_t>(this) - sizeof(BaseAllocMetadata));
  }

  BaseAllocMetadata* RightMetadata();

 public:
  static uintptr_t Align(uintptr_t aPtr);

  explicit BaseAllocCell(base_alloc_size_t aSize) {
    LeftMetadata()->InitForRightCell(aSize);
    RightMetadata()->InitForLeftCell(aSize);
    ClearPayload();
  }

  static BaseAllocCell* GetCell(void* aPtr) {
    return reinterpret_cast<BaseAllocCell*>(aPtr);
  }

  base_alloc_size_t Size() { return LeftMetadata()->mRightSize; }

  void SetSize(base_alloc_size_t aNewSize);

  bool Allocated() { return LeftMetadata()->mRightAllocated; }
  bool Committed() { return mCommitted; }

  void* Ptr() { return this; }

  void SetAllocated() {
    MOZ_ASSERT(!Allocated());
    MOZ_ASSERT(Committed());
    LeftMetadata()->mRightAllocated = true;
  }
  void SetFreed() {
    MOZ_ASSERT(Allocated());
    LeftMetadata()->mRightAllocated = false;
  }

  bool ProbablyNotInList() {
    // This method won't work on allocated cells.
    MOZ_ASSERT(!Allocated());

    return !(mListElem.mNext || mListElem.mPrev);
  }

  // After freeing a cell but before we can use the list pointers we must
  // clear them to avoid assertions in DoublyLinkedList.
  void ClearPayload();

  BaseAllocCell* LeftCell();
  BaseAllocCell* RightCell();

  // RightCellRaw() is the same address calculation of RightCell() but without
  // checking if the cell exists.
  uintptr_t RightCellRaw();

  void Merge(BaseAllocCell* cell);

  // Test if this cell can be split, aSizeRequest is the desired size for
  // the "lower" cell, if a split is valid the address of the split is
  // returned or 0 if splitting would not make a 2nd viable cell.
  uintptr_t CanSplit(base_alloc_size_t aSizeRequest);

  // Test if the cell can be split at this address.
  bool CanSplitHere(uintptr_t aNextAddr);

  // Perform the split with the new address calculated by CanSplit(), the
  // next cell with the remaining size is returned.  This always succeeds.
  BaseAllocCell* Split(uintptr_t aNewSize);

  // The result of committing or decommitting a cell.  The change in
  // committed bytes and any new cells that may have been created are
  // returned.
  struct DeCommitResult {
    size_t mChange = 0;
    BaseAllocCell* mNewCell1 = nullptr;
    BaseAllocCell* mNewCell2 = nullptr;

    explicit DeCommitResult(size_t aChange, BaseAllocCell* aNewCell1 = nullptr,
                            BaseAllocCell* aNewCell2 = nullptr)
        : mChange(aChange), mNewCell1(aNewCell1), mNewCell2(aNewCell2) {}
  };

  // Commit part of the cell enough to satisfy aSizeRequest.  If successful
  // the cell may be split and this will return between 0 and 2 cells that
  // need to be tracked.
  mozilla::Maybe<DeCommitResult> Commit(base_alloc_size_t aSizeRequest);

  // Commit the entire cell.  If successful returns a non-zero number of bytes
  // that were committed.
  size_t CommitAll();

  // Decommit as much of the cell as possible.  The boundaries and free list
  // information cannot be decommited.  Depending on where the page
  // boundaries fall the cell may be split first to make more memory at the
  // beginning and end of the cell available as committed cells.
  //
  // The `this` cell is not necessarily the one that was decommitted.
  DeCommitResult Decommit();

 private:
  // Decommit pages within this cell cell.
  void DoDecommit(uintptr_t aFirstDecommit, uintptr_t aNBytes);

  // disable copy, move and new since this class must only be used in-place.
  BaseAllocCell(const BaseAllocCell&) = delete;
  void operator=(const BaseAllocCell&) = delete;
  BaseAllocCell(BaseAllocCell&&) = delete;
  void operator=(BaseAllocCell&&) = delete;
  void* operator new(size_t) = delete;
  void* operator new[](size_t) = delete;

 public:
  void* operator new(size_t aSize, void* aPtr) {
    MOZ_ASSERT(aSize == sizeof(BaseAllocCell));
    return aPtr;
  }
};

template <>
struct mozilla::GetDoublyLinkedListElement<BaseAllocCell> {
  static DoublyLinkedListElement<BaseAllocCell>& Get(BaseAllocCell* aCell) {
    return aCell->mListElem;
  }
  static const DoublyLinkedListElement<BaseAllocCell>& Get(
      const BaseAllocCell* aCell) {
    return aCell->mListElem;
  }
};

struct BaseAllocCellRBTrait {
  static RedBlackTreeNode<BaseAllocCell>& GetTreeNode(BaseAllocCell* aCell) {
    return aCell->mTreeElem;
  }

  static Order Compare(BaseAllocCell* aCellA, BaseAllocCell* aCellB) {
    Order ret = CompareInt(aCellA->Size(), aCellB->Size());
    return (ret != Order::eEqual) ? ret : CompareAddr(aCellA, aCellB);
  }

  using SearchKey = base_alloc_size_t;

  static Order Compare(SearchKey aSizeA, BaseAllocCell* aCellB) {
    // When sizes are equal this still has to compare by address so that the
    // search key sorts lower than any node.  And therefore SearchOrNext()
    // will return the first entry with the requested size.
    Order ret = CompareInt(aSizeA, aCellB->Size());
    return (ret != Order::eEqual)
               ? ret
               : CompareAddr((BaseAllocCell*)nullptr, aCellB);
  }
};

#endif /* ~ BASEALLOCINTERNALS_H */
