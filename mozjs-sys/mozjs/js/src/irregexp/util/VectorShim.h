// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UTIL_VECTOR_H_
#define V8_UTIL_VECTOR_H_

#include <cstring>

#include "js/AllocPolicy.h"
#include "js/Utility.h"
#include "js/Vector.h"

namespace v8 {
namespace internal {

//////////////////////////////////////////////////

// Adapted from:
// https://github.com/v8/v8/blob/5f69bbc233c2d1baf149faf869a7901603929914/src/utils/allocation.h#L36-L58

template <typename T>
T* NewArray(size_t size) {
  static_assert(std::is_trivially_copyable_v<T>);
  js::AutoEnterOOMUnsafeRegion oomUnsafe;
  T* result = js_pod_malloc<T>(size);
  if (!result) {
    oomUnsafe.crash("Irregexp NewArray");
  }
  return result;
}

template <typename T>
void DeleteArray(T* array) {
  static_assert(std::is_trivially_destructible_v<T>);
  js_free(array);
}

}  // namespace internal

namespace base {

//////////////////////////////////////////////////

// A non-resizable vector containing a pointer and a length.
// The Vector may or may not own the pointer, depending on context.
// Origin:
// https://github.com/v8/v8/blob/5f69bbc233c2d1baf149faf869a7901603929914/src/utils/vector.h#L20-L134

template <typename T>
class Vector {
 public:
  constexpr Vector() : start_(nullptr), length_(0) {}

  constexpr Vector(T* data, size_t length) : start_(data), length_(length) {
    MOZ_ASSERT_IF(length != 0, data != nullptr);
  }

  static Vector<T> New(size_t length) {
    return Vector<T>(v8::internal::NewArray<T>(length), length);
  }

  // Returns a vector using the same backing storage as this one,
  // spanning from and including 'from', to but not including 'to'.
  Vector<T> SubVector(size_t from, size_t to) const {
    MOZ_ASSERT(from <= to);
    MOZ_ASSERT(to <= length_);
    return Vector<T>(begin() + from, to - from);
  }

  // Returns the length of the vector. Only use this if you really need an
  // integer return value. Use {size()} otherwise.
  int length() const {
    MOZ_ASSERT(length_ <= static_cast<size_t>(std::numeric_limits<int>::max()));
    return static_cast<int>(length_);
  }

  // Returns the length of the vector as a size_t.
  constexpr size_t size() const { return length_; }

  // Returns whether or not the vector is empty.
  constexpr bool empty() const { return length_ == 0; }

  // Access individual vector elements - checks bounds in debug mode.
  T& operator[](size_t index) const {
    MOZ_ASSERT(index < length_);
    return start_[index];
  }

  const T& at(size_t index) const { return operator[](index); }

  T& first() { return start_[0]; }

  T& last() {
    MOZ_ASSERT(length_ > 0);
    return start_[length_ - 1];
  }

  // Returns a pointer to the start of the data in the vector.
  constexpr T* begin() const { return start_; }

  // Returns a pointer past the end of the data in the vector.
  constexpr T* end() const { return start_ + length_; }

  // Returns a clone of this vector with a new backing store.
  Vector<T> Clone() const {
    T* result = v8::internal::NewArray<T>(length_);
    for (size_t i = 0; i < length_; i++) result[i] = start_[i];
    return Vector<T>(result, length_);
  }

  void Truncate(size_t length) {
    MOZ_ASSERT(length <= length_);
    length_ = length;
  }

  // Releases the array underlying this vector. Once disposed the
  // vector is empty.
  void Dispose() {
    DeleteArray(start_);
    start_ = nullptr;
    length_ = 0;
  }

  Vector<T> operator+(size_t offset) const {
    MOZ_ASSERT(offset <= length_);
    return Vector<T>(start_ + offset, length_ - offset);
  }

  Vector<T> operator+=(size_t offset) {
    MOZ_ASSERT(offset <= length_);
    start_ += offset;
    length_ -= offset;
    return *this;
  }

  // Implicit conversion from Vector<T> to Vector<const T>.
  inline operator Vector<const T>() const {
    return Vector<const T>::cast(*this);
  }

  template <typename S>
  static constexpr Vector<T> cast(Vector<S> input) {
    return Vector<T>(reinterpret_cast<T*>(input.begin()),
                     input.length() * sizeof(S) / sizeof(T));
  }

  bool operator==(const Vector<const T> other) const {
    if (length_ != other.length_) return false;
    if (start_ == other.start_) return true;
    for (size_t i = 0; i < length_; ++i) {
      if (start_[i] != other.start_[i]) {
        return false;
      }
    }
    return true;
  }

 private:
  T* start_;
  size_t length_;
};

// The resulting vector does not contain a null-termination byte. If you want
// the null byte, use ArrayVector("foo").
inline Vector<const char> CStrVector(const char* data) {
  return Vector<const char>(data, strlen(data));
}

template <typename T, size_t N>
inline constexpr Vector<T> ArrayVector(T (&arr)[N]) {
  return {arr, N};
}

// Construct a Vector from a start pointer and a size.
template <typename T>
inline constexpr Vector<T> VectorOf(T* start, size_t size) {
  return {start, size};
}

class DefaultAllocator {
 public:
  using Policy = js::SystemAllocPolicy;
  Policy policy() const { return js::SystemAllocPolicy(); }
};

// SmallVector uses inline storage first, and reallocates when full.
// It is basically equivalent to js::Vector, and is implemented
// as a thin wrapper.
// V8's implementation:
// https://github.com/v8/v8/blob/main/src/base/small-vector.h
template <typename T, size_t Size, typename Allocator = DefaultAllocator>
class SmallVector {
 public:
  explicit SmallVector(const Allocator& allocator = DefaultAllocator())
      : inner_(allocator.policy()) {}
  SmallVector(size_t size, const Allocator& allocator = DefaultAllocator())
      : inner_(allocator.policy()) {
    resize(size);
  }
  SmallVector(size_t size, const T& initialValue,
              const Allocator& allocator = DefaultAllocator())
      : inner_(allocator.policy()) {
    js::AutoEnterOOMUnsafeRegion oomUnsafe;
    if (!inner_.appendN(initialValue, size)) {
      oomUnsafe.crash("Irregexp SmallVector constructor");
    }
  }

  inline bool empty() const { return inner_.empty(); }
  inline T& back() { return inner_.back(); }
  inline const T& back() const { return inner_.back(); }
  inline void pop_back() { inner_.popBack(); };
  template <typename... Args>
  inline void emplace_back(Args&&... args) {
    js::AutoEnterOOMUnsafeRegion oomUnsafe;
    if (!inner_.emplaceBack(args...)) {
      oomUnsafe.crash("Irregexp SmallVector emplace_back");
    }
  };
  inline size_t size() const { return inner_.length(); }
  inline const T& at(size_t index) const { return inner_[index]; }
  T* data() { return inner_.begin(); }
  T* begin() { return inner_.begin(); }
  const T* begin() const { return inner_.begin(); }
  T* end() { return inner_.end(); }
  const T* end() const { return inner_.end(); }

  T& operator[](size_t index) { return inner_[index]; }
  const T& operator[](size_t index) const { return inner_[index]; }

  inline void clear() { inner_.clear(); }

  void push_back(T&& val) {
    js::AutoEnterOOMUnsafeRegion oomUnsafe;
    if (!inner_.append(val)) {
      oomUnsafe.crash("Irregexp SmallVector push_back");
    }
  }

  void resize(size_t new_size) {
    js::AutoEnterOOMUnsafeRegion oomUnsafe;
    if (!inner_.resize(new_size)) {
      oomUnsafe.crash("Irregexp SmallVector resize");
    }
  }

  template <typename OT, size_t OSize, class OAllocator>
  void insert(T* position, SmallVector<OT, OSize, OAllocator>& other) {
    js::AutoEnterOOMUnsafeRegion oomUnsafe;
    if (position == end()) {
      if (!inner_.appendAll(other.inner_)) {
        oomUnsafe.crash("Irregexp SmallVector insert");
      }
    } else {
      // V8 supports splicing an arbitrary iterator into a SmallVector at an
      // arbitrary position. mozilla::Vector doesn't support that. The shim
      // doesn't currently need it. If it ever becomes necessary, we can decide
      // whether to add support for doing it efficiently.
      MOZ_CRASH("unimplemented");
    }
  }

 private:
  js::Vector<T, Size, typename Allocator::Policy> inner_;
};

}  // namespace base

}  // namespace v8

#endif  // V8_UTIL_VECTOR_H_
