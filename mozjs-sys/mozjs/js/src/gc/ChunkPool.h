/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef gc_ChunkPool_h
#define gc_ChunkPool_h

#include "js/HeapAPI.h"

namespace js {
namespace gc {

class ChunkPool {
  ArenaChunk* head_;
  size_t count_;

 public:
  ChunkPool() : head_(nullptr), count_(0) {}
  ChunkPool(const ChunkPool& other) = delete;
  ChunkPool(ChunkPool&& other) { *this = std::move(other); }

  ~ChunkPool() {
    MOZ_ASSERT(!head_);
    MOZ_ASSERT(count_ == 0);
  }

  ChunkPool& operator=(const ChunkPool& other) = delete;
  ChunkPool& operator=(ChunkPool&& other) {
    head_ = other.head_;
    other.head_ = nullptr;
    count_ = other.count_;
    other.count_ = 0;
    return *this;
  }

  bool empty() const { return !head_; }
  size_t count() const { return count_; }

  ArenaChunk* head() {
    MOZ_ASSERT(head_);
    return head_;
  }
  ArenaChunk* maybeHead() { return head_; }
  ArenaChunk* pop();
  void push(ArenaChunk* chunk);
  ArenaChunk* remove(ArenaChunk* chunk);

  void sort();

  // Linear time, use with caution.
  bool contains(ArenaChunk* chunk) const;

 private:
  ArenaChunk* mergeSort(ArenaChunk* list, size_t count);
  bool isSorted() const;

#ifdef DEBUG
 public:
  bool verify() const;
  void verifyChunks() const;
#endif

 public:
  // Pool mutation does not invalidate an Iter unless the mutation
  // is of the ArenaChunk currently being visited by the Iter.
  class Iter {
   public:
    explicit Iter(ChunkPool& pool) : current_(pool.head_) {}
    bool done() const { return !current_; }
    void next();
    ArenaChunk* get() const { return current_; }
    operator ArenaChunk*() const { return get(); }
    ArenaChunk* operator->() const { return get(); }

   private:
    ArenaChunk* current_;
  };

  Iter iter() { return Iter(*this); }
};

} /* namespace gc */
} /* namespace js */

#endif /* gc_ChunkPool_h */
