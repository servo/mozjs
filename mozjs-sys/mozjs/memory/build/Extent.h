/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef EXTENT_H
#define EXTENT_H

#include "mozjemalloc_types.h"

#include "BaseAlloc.h"
#include "RedBlackTree.h"

#include "mozilla/UniquePtr.h"

// ***************************************************************************
// Extent data structures.

struct arena_t;

enum ChunkType;

// Tree of extents.
struct extent_node_t : public BaseAllocClass {
  union {
    // Linkage for the size/address-ordered tree for chunk recycling.
    RedBlackTreeNode<extent_node_t> mLinkBySize;
    // Arena id for huge allocations. It's meant to match mArena->mId,
    // which only holds true when the arena hasn't been disposed of.
    arena_id_t mArenaId;
  };

  // Linkage for the address-ordered tree.
  RedBlackTreeNode<extent_node_t> mLinkByAddr;

  // Pointer to the extent that this tree node is responsible for.
  void* mAddr;

  // Total region size.
  size_t mSize;

  union {
    // What type of chunk is there; used for chunk recycling.
    ChunkType mChunkType;

    // A pointer to the associated arena, for huge allocations.
    arena_t* mArena;
  };
};

struct ExtentTreeSzTrait {
  static RedBlackTreeNode<extent_node_t>& GetTreeNode(extent_node_t* aThis) {
    return aThis->mLinkBySize;
  }

  static inline Order Compare(extent_node_t* aNode, extent_node_t* aOther) {
    Order ret = CompareInt(aNode->mSize, aOther->mSize);
    return (ret != Order::eEqual) ? ret
                                  : CompareAddr(aNode->mAddr, aOther->mAddr);
  }

  using SearchKey = size_t;

  // This comparison is used to lookup an extent of a size.  It will never
  // return eEqual instead it'll choose the leftmost node in the tree with
  // the given size.
  static inline Order Compare(SearchKey aKey, extent_node_t* aOther) {
    Order ret = CompareInt(aKey, aOther->mSize);
    return (ret != Order::eEqual) ? ret : Order::eLess;
  }
};

struct ExtentTreeTrait {
  static RedBlackTreeNode<extent_node_t>& GetTreeNode(extent_node_t* aThis) {
    return aThis->mLinkByAddr;
  }

  static inline Order Compare(extent_node_t* aNode, extent_node_t* aOther) {
    return CompareAddr(aNode->mAddr, aOther->mAddr);
  }

  using SearchKey = void*;

  static inline Order Compare(SearchKey aKey, extent_node_t* aOther) {
    // aKey is a pointer to memory that may be within a node.
    return CompareAddr(aKey, aOther->mAddr);
  }
};

struct ExtentTreeBoundsTrait : public ExtentTreeTrait {
  static inline Order CompareBounds(void* aKey, extent_node_t* aNode) {
    uintptr_t key_addr = reinterpret_cast<uintptr_t>(aKey);
    uintptr_t node_addr = reinterpret_cast<uintptr_t>(aNode->mAddr);
    size_t node_size = aNode->mSize;

    // Is aKey within aNode?
    if (node_addr <= key_addr && key_addr < node_addr + node_size) {
      return Order::eEqual;
    }

    return CompareAddr(aKey, aNode->mAddr);
  }

  static inline Order Compare(extent_node_t* aKey, extent_node_t* aNode) {
    return CompareBounds(aKey->mAddr, aNode);
  }
  static inline Order Compare(SearchKey aKey, extent_node_t* aNode) {
    return CompareBounds(aKey, aNode);
  }
};

using UniqueBaseNode = mozilla::UniquePtr<extent_node_t>;

#endif /* ! EXTENT_H */
