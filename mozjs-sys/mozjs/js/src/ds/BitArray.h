/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ds_BitArray_h
#define ds_BitArray_h

#include "mozilla/Assertions.h"

#include <limits.h>
#include <stddef.h>
#include <utility>

#include "jstypes.h"

namespace js {

/**
 * A bit array is an array of bits represented by an array of words.
 */

/**
 * Fixed size bit array using internal storage.
 */
template <size_t nbits, typename StorageType>
class BitArray {
 public:
  using WordT = StorageType;
  static constexpr size_t bitsPerElement = sizeof(WordT) * CHAR_BIT;
  static constexpr size_t numWords = HowMany(nbits, bitsPerElement);

 private:
  WordT map[numWords];

 public:
  constexpr BitArray() : map() {};

  inline bool get(size_t bitIndex) const {
    auto [index, mask] = getIndexAndMask(bitIndex);
    return map[index] & mask;
  }

  void set(size_t bitIndex) {
    auto [index, mask] = getIndexAndMask(bitIndex);
    map[index] |= mask;
  }

  void unset(size_t bitIndex) {
    auto [index, mask] = getIndexAndMask(bitIndex);
    map[index] &= ~mask;
  }

  bool isAllClear() const {
    for (size_t i = 0; i < numWords; i++) {
      if (map[i]) {
        return false;
      }
    }
    return true;
  }

  // For iterating over the set bits in the bit array, get a word at a time.
  WordT getWord(size_t wordIndex) const {
    MOZ_ASSERT(wordIndex < numWords);
    return map[wordIndex];
  }

  static auto getIndexAndMask(size_t bitIndex) {
    MOZ_ASSERT(bitIndex < nbits);
    size_t wordIndex = bitIndex / bitsPerElement;
    MOZ_ASSERT(wordIndex < numWords);
    WordT wordMask = WordT(1) << (bitIndex % bitsPerElement);
    return std::pair{wordIndex, wordMask};
  }

  static size_t offsetOfMap() { return offsetof(BitArray, map); }
};

/**
 * Variable size bit array using external storage.
 */
template <typename StorageType>
class ExternalBitArray {
 public:
  using WordT = StorageType;
  static constexpr size_t bitsPerElement = sizeof(WordT) * CHAR_BIT;

 private:
  WordT* array_;

#ifdef DEBUG
  size_t length_;
#endif

  auto getIndexAndMask(size_t bitIndex) const {
    MOZ_ASSERT(bitIndex < length_);
    size_t wordIndex = bitIndex / bitsPerElement;
    MOZ_ASSERT(wordIndex < NumWordsForLength(length_));
    WordT wordMask = WordT(1) << (bitIndex % bitsPerElement);
    return std::pair{wordIndex, wordMask};
  }

 public:
  ExternalBitArray(WordT* array, size_t length)
      : array_(array)
#ifdef DEBUG
        ,
        length_(length)
#endif
  {
  }

  bool get(size_t bitIndex) const {
    auto [index, mask] = getIndexAndMask(bitIndex);
    return array_[index] & mask;
  }

  void set(size_t bitIndex) {
    auto [index, mask] = getIndexAndMask(bitIndex);
    array_[index] |= mask;
  }

  void unset(size_t bitIndex) {
    auto [index, mask] = getIndexAndMask(bitIndex);
    array_[index] &= ~mask;
  }

  /**
   * Return the number of words needed to represent `length` bits.
   */
  static constexpr size_t NumWordsForLength(size_t length) {
    return HowMany(length, bitsPerElement);
  }

  /**
   * Return the bit length representable in `numWords` words.
   */
  static constexpr size_t LengthForNumWords(size_t numWords) {
    return numWords * bitsPerElement;
  }
};

} /* namespace js */

#endif /* ds_BitArray_h */
