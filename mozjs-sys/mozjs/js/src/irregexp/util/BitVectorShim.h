// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UTIL_BIT_VECTOR_H_
#define V8_UTIL_BIT_VECTOR_H_

#include "irregexp/util/ZoneShim.h"

namespace v8 {
namespace internal {

class BitVector : ZoneObject {
 private:
  class IteratorBase {
   public:
    int32_t operator*() const {
      MOZ_ASSERT(end_ != ptr_);
      MOZ_ASSERT(target_->Contains(current_index_));
      return current_index_;
    }

    bool operator==(const IteratorBase& other) const {
      MOZ_ASSERT(target_ == other.target_);
      MOZ_ASSERT(end_ == other.end_);
      MOZ_ASSERT_IF(current_index_ == other.current_index_, ptr_ == other.ptr_);
      return current_index_ == other.current_index_;
    }

   protected:
    IteratorBase(const BitVector* target, uintptr_t* ptr, uintptr_t* end,
                 int32_t current_index)
        : ptr_(ptr),
          end_(end),
#ifdef DEBUG
          target_(target),
#endif
          current_index_(current_index) {
    }

    static constexpr struct StartTag {
    } kStartTag = {};
    static constexpr struct EndTag {
    } kEndTag = {};

    uintptr_t* ptr_;
    uintptr_t* end_;
#ifdef DEBUG
    const BitVector* target_;
#endif
    int32_t current_index_;
  };  // IteratorBase

 public:
  // Iterator for the elements of this BitVector.
  class Iterator : public IteratorBase {
   public:
    inline void operator++() {
      int32_t bit_in_word = current_index_ & (kDataBits - 1);
      if (bit_in_word < kDataBits - 1) {
        uintptr_t remaining_bits = *ptr_ >> (bit_in_word + 1);
        if (remaining_bits) {
          int32_t next_bit_in_word = std::countr_zero(remaining_bits);
          current_index_ += next_bit_in_word + 1;
          return;
        }
      }

      // Move {current_index_} down to the beginning of the current word, before
      // starting to search for the next non-empty word.
      current_index_ = js::RoundDown(current_index_, kDataBits);
      do {
        ++ptr_;
        current_index_ += kDataBits;
        if (ptr_ == end_) return;
      } while (*ptr_ == 0);

      uintptr_t trailing_zeros = std::countr_zero(*ptr_);
      current_index_ += trailing_zeros;
    }

   private:
    explicit Iterator(const BitVector* target, StartTag)
        : IteratorBase(target, target->data_begin_, target->data_end_, 0) {
      MOZ_ASSERT(ptr_ < end_);
      while (*ptr_ == 0) {
        ++ptr_;
        current_index_ += kDataBits;
        if (ptr_ == end_) return;
      }
      current_index_ += std::countr_zero(*ptr_);
    }

    explicit Iterator(const BitVector* target, EndTag)
        : IteratorBase(target, target->data_end_, target->data_end_,
                       target->data_length() * kDataBits) {}

    friend class BitVector;
  };  // Iterator

 public:
  static constexpr uint32_t kDataBits = sizeof(uintptr_t) * CHAR_BIT;
  static constexpr uint32_t kDataBitShift = mozilla::FloorLog2(kDataBits);

  BitVector() = default;

  BitVector(int32_t length, Zone* zone) : length_(length) {
    MOZ_ASSERT(length >= 0);
    int32_t data_length = (length + kDataBits - 1) >> kDataBitShift;
    if (data_length > 1) {
      data_.ptr_ = zone->AllocateArray<uintptr_t>(data_length);
      std::fill_n(data_.ptr_, data_length, 0);
      data_begin_ = data_.ptr_;
      data_end_ = data_begin_ + data_length;
    }
  }

  BitVector(const BitVector& other, Zone* zone)
      : length_(other.length_), data_(other.data_.inline_) {
    if (!other.is_inline()) {
      int32_t data_length = other.data_length();
      MOZ_ASSERT(data_length > 1);
      data_.ptr_ = zone->AllocateArray<uintptr_t>(data_length);
      data_begin_ = data_.ptr_;
      data_end_ = data_begin_ + data_length;
      std::copy_n(other.data_begin_, data_length, data_begin_);
    }
  }

  // Disallow copy and copy-assignment.
  BitVector(const BitVector&) = delete;
  BitVector& operator=(const BitVector&) = delete;

  BitVector(BitVector&& other) { *this = std::move(other); }

  BitVector& operator=(BitVector&& other) {
    length_ = other.length_;
    data_ = other.data_;
    if (other.is_inline()) {
      data_begin_ = &data_.inline_;
      data_end_ = data_begin_ + other.data_length();
    } else {
      data_begin_ = other.data_begin_;
      data_end_ = other.data_end_;
      // Reset other to inline.
      other.length_ = 0;
      other.data_begin_ = &other.data_.inline_;
      other.data_end_ = other.data_begin_ + 1;
    }
    return *this;
  }

  void CopyFrom(const BitVector& other) {
    MOZ_ASSERT(other.length() == length());
    MOZ_ASSERT(is_inline() == other.is_inline());
    std::copy_n(other.data_begin_, data_length(), data_begin_);
  }

  void Resize(int32_t new_length, Zone* zone) {
    MOZ_ASSERT(new_length > length());
    int32_t old_data_length = data_length();
    MOZ_ASSERT(old_data_length >= 1);
    int32_t new_data_length = (new_length + kDataBits - 1) >> kDataBitShift;
    if (new_data_length > old_data_length) {
      uintptr_t* new_data = zone->AllocateArray<uintptr_t>(new_data_length);

      // Copy over the data.
      std::copy_n(data_begin_, old_data_length, new_data);
      // Zero out the rest of the data.
      std::fill(new_data + old_data_length, new_data + new_data_length, 0);

      data_begin_ = new_data;
      data_end_ = new_data + new_data_length;
    }
    length_ = new_length;
  }

  bool Contains(int32_t i) const {
    MOZ_ASSERT(i >= 0 && i < length());
    return (data_begin_[word(i)] & bit(i)) != 0;
  }

  void Add(int32_t i) {
    MOZ_ASSERT(i >= 0 && i < length());
    data_begin_[word(i)] |= bit(i);
  }

  void AddAll() {
    if (MOZ_UNLIKELY(length() == 0)) return;
    int32_t partial_size = length() % kDataBits;
    int32_t bulk_size = data_length() - (partial_size != 0 ? 1 : 0);
    std::fill_n(data_begin_, bulk_size, ~uintptr_t{0});
    if (partial_size != 0) {
      data_begin_[bulk_size] = ~uintptr_t{0} >> (kDataBits - partial_size);
    }
  }

  void Remove(int32_t i) {
    MOZ_ASSERT(i >= 0 && i < length());
    data_begin_[word(i)] &= ~bit(i);
  }

  void Union(const BitVector& other) {
    MOZ_ASSERT(other.length() <= length());
    for (int32_t i = 0; i < other.data_length(); i++) {
      data_begin_[i] |= other.data_begin_[i];
    }
  }

  bool UnionIsChanged(const BitVector& other) {
    MOZ_ASSERT(other.length() <= length());
    bool changed = false;
    for (int32_t i = 0; i < other.data_length(); i++) {
      uintptr_t old_data = data_begin_[i];
      data_begin_[i] |= other.data_begin_[i];
      if (data_begin_[i] != old_data) changed = true;
    }
    return changed;
  }

  void Intersect(const BitVector& other) {
    MOZ_ASSERT(other.length() == length());
    for (int32_t i = 0; i < data_length(); i++) {
      data_begin_[i] &= other.data_begin_[i];
    }
  }

  bool IntersectIsChanged(const BitVector& other) {
    MOZ_ASSERT(other.length() == length());
    bool changed = false;
    for (int32_t i = 0; i < data_length(); i++) {
      uintptr_t old_data = data_begin_[i];
      data_begin_[i] &= other.data_begin_[i];
      if (data_begin_[i] != old_data) changed = true;
    }
    return changed;
  }

  void Subtract(const BitVector& other) {
    MOZ_ASSERT(other.length() == length());
    for (int32_t i = 0; i < data_length(); i++) {
      data_begin_[i] &= ~other.data_begin_[i];
    }
  }

  bool IsSubsetOf(const BitVector& other) const {
    MOZ_ASSERT(other.length() == length());
    for (int32_t i = 0; i < data_length(); i++) {
      if ((data_begin_[i] & ~other.data_begin_[i]) != 0) return false;
    }
    return true;
  }

  void Clear() { std::fill_n(data_begin_, data_length(), 0); }

  bool IsEmpty() const {
    return std::all_of(data_begin_, data_end_, std::logical_not<uintptr_t>{});
  }

  bool Equals(const BitVector& other) const {
    return std::equal(data_begin_, data_end_, other.data_begin_);
  }

  int32_t length() const { return length_; }

  bool is_inline() const { return data_begin_ == &data_.inline_; }
  int32_t data_length() const {
    return static_cast<uint32_t>(data_end_ - data_begin_);
  }

  MOZ_ALWAYS_INLINE static int32_t word(int32_t index) {
    MOZ_ASSERT(index >= 0);
    return index >> kDataBitShift;
  }
  MOZ_ALWAYS_INLINE static intptr_t bit(int32_t index) {
    MOZ_ASSERT(index >= 0);
    return uintptr_t{1} << (index & (kDataBits - 1));
  }

  Iterator begin() const { return Iterator(this, Iterator::kStartTag); }

  Iterator end() const { return Iterator(this, Iterator::kEndTag); }

 private:
  union DataStorage {
    uintptr_t* ptr_;    // valid if >1 machine word is needed
    uintptr_t inline_;  // valid if <=1 machine word is needed

    explicit DataStorage(uintptr_t value) : inline_(value) {}
  };

  int32_t length_ = 0;
  DataStorage data_{0};
  uintptr_t* data_begin_ = &data_.inline_;
  uintptr_t* data_end_ = &data_.inline_ + 1;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_UTIL_BIT_VECTOR_H_
