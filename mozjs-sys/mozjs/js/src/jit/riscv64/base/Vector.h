// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef jit_riscv64_base_Vector_h
#define jit_riscv64_base_Vector_h

#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"

#include <stdarg.h>
#include <stdio.h>

namespace js::jit {

// Vector as used by the original code to allow for minimal modification.
// Functions exactly like a character array with helper methods.

template <typename T>
class V8Vector {
 public:
  V8Vector() : start_(nullptr), length_(0) {}
  V8Vector(T* data, int length) : start_(data), length_(length) {
    MOZ_ASSERT(length == 0 || (length > 0 && data != nullptr));
  }

  // Returns the length of the vector.
  int length() const { return length_; }

  // Returns the pointer to the start of the data in the vector.
  T* start() const { return start_; }

  // Access individual vector elements - checks bounds in debug mode.
  T& operator[](int index) const {
    MOZ_ASSERT(0 <= index && index < length_);
    return start_[index];
  }

  inline V8Vector<T> operator+(int offset) {
    MOZ_ASSERT(offset < length_);
    return V8Vector<T>(start_ + offset, length_ - offset);
  }

 private:
  T* start_;
  int length_;
};

template <typename T, int kSize>
class EmbeddedVector : public V8Vector<T> {
 public:
  EmbeddedVector() : V8Vector<T>(buffer_, kSize) {}

  EmbeddedVector(const EmbeddedVector&) = delete;
  EmbeddedVector& operator=(const EmbeddedVector&) = delete;

 private:
  T buffer_[kSize];
};

// Helper function for printing to a Vector.
static inline int MOZ_FORMAT_PRINTF(2, 3)
    SNPrintF(V8Vector<char> str, const char* format, ...) {
  va_list args;
  va_start(args, format);
  int result = vsnprintf(str.start(), str.length(), format, args);
  va_end(args);
  return result;
}

}  // namespace js::jit

#endif  // jit_riscv64_base_Vector_h
