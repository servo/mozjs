/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ds/Bitmap.h"

#include <algorithm>

#include "js/UniquePtr.h"

using namespace js;

SparseBitmap::~SparseBitmap() {
  for (auto iter = data.iter(); !iter.done(); iter.next()) {
    js_delete(iter.get().value());
  }
}

size_t SparseBitmap::sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) {
  size_t size = data.shallowSizeOfExcludingThis(mallocSizeOf);
  for (auto iter = data.iter(); !iter.done(); iter.next()) {
    size += mallocSizeOf(iter.get().value());
  }
  return size;
}

SparseBitmap::BitBlock* SparseBitmap::createBlock(Data::AddPtr p,
                                                  size_t blockId) {
  MOZ_ASSERT(!p);
  auto block = js::MakeUnique<BitBlock>();
  if (!block || !data.add(p, blockId, block.get())) {
    return nullptr;
  }
  std::fill(block->begin(), block->end(), 0);
  return block.release();
}

bool SparseBitmap::getBit(size_t bit) const {
  size_t word = bit / JS_BITS_PER_WORD;
  size_t blockWord = blockStartWord(word);

  const BitBlock* block = getBlock(blockWord / WordsInBlock);
  if (block) {
    return (*block)[word - blockWord] & bitMask(bit);
  }
  return false;
}

bool SparseBitmap::readonlyThreadsafeGetBit(size_t bit) const {
  size_t word = bit / JS_BITS_PER_WORD;
  size_t blockWord = blockStartWord(word);

  const BitBlock* block = readonlyThreadsafeGetBlock(blockWord / WordsInBlock);
  if (block) {
    return (*block)[word - blockWord] & bitMask(bit);
  }
  return false;
}

void SparseBitmap::bitwiseAndWith(const DenseBitmap& other) {
  for (auto iter = data.modIter(); !iter.done(); iter.next()) {
    BitBlock& block = *iter.get().value();
    size_t blockWord = iter.get().key() * WordsInBlock;
    bool anySet = false;
    size_t numWords = wordIntersectCount(blockWord, other);
    for (size_t i = 0; i < numWords; i++) {
      block[i] &= other.word(blockWord + i);
      anySet |= !!block[i];
    }
    if (!anySet) {
      js_delete(&block);
      iter.remove();
    }
  }
}

bool SparseBitmap::bitwiseOrWith(const SparseBitmap& other) {
  for (auto iter = other.data.iter(); !iter.done(); iter.next()) {
    const BitBlock& otherBlock = *iter.get().value();
    BitBlock* block = getOrCreateBlock(iter.get().key());
    if (!block) {
      return false;
    }
    for (size_t i = 0; i < WordsInBlock; i++) {
      (*block)[i] |= otherBlock[i];
    }
  }

  return true;
}

void SparseBitmap::bitwiseOrInto(DenseBitmap& other) const {
  for (auto iter = data.iter(); !iter.done(); iter.next()) {
    BitBlock& block = *iter.get().value();
    size_t blockWord = iter.get().key() * WordsInBlock;
    size_t numWords = wordIntersectCount(blockWord, other);
#ifdef DEBUG
    // Any words out of range in other should be zero in this bitmap.
    for (size_t i = numWords; i < WordsInBlock; i++) {
      MOZ_ASSERT(!block[i]);
    }
#endif
    for (size_t i = 0; i < numWords; i++) {
      other.word(blockWord + i) |= block[i];
    }
  }
}
