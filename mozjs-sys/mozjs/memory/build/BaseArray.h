/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BASEARRAY_H
#define BASEARRAY_H

#include "BaseAlloc.h"

#include "mozilla/Assertions.h"
#include "mozilla/CheckedInt.h"

//---------------------------------------------------------------------------
// Array implementation
//---------------------------------------------------------------------------

// Unlike mfbt/Array.h this array has a dynamic size, but unlike a vector its
// size is set explicitly rather than grown as needed.
// It uses the base allocator to allocate / free, shrink and grow its
// storage.
template <typename T>
class BaseArray {
 private:
  size_t mCapacity = 0;
  T* mArray = nullptr;

 public:
  BaseArray() {}

  ~BaseArray() {
    for (size_t i = 0; i < mCapacity; i++) {
      mArray[i].~T();
    }
    sBaseAlloc.free(mArray);
  }

  const T& operator[](size_t aIndex) const {
    MOZ_ASSERT(aIndex < mCapacity);
    return mArray[aIndex];
  }
  T& operator[](size_t aIndex) {
    MOZ_ASSERT(aIndex < mCapacity);
    return mArray[aIndex];
  }

  T* begin() { return mArray; }
  const T* begin() const { return mArray; }
  const T* end() const { return &mArray[mCapacity]; }

  bool Init(size_t aCapacity) {
    MOZ_ASSERT(mCapacity == 0);
    MOZ_ASSERT(mArray == nullptr);

    auto size_bytes = mozilla::CheckedInt<size_t>(sizeof(T)) * aCapacity;
    MOZ_ASSERT(size_bytes.isValid());
    if (!size_bytes.isValid()) {
      return false;
    }

    mArray = reinterpret_cast<T*>(sBaseAlloc.alloc(size_bytes.value()));
    if (!mArray) {
      return false;
    }

    for (size_t i = 0; i < aCapacity; i++) {
      new (&mArray[i]) T();
    }
    mCapacity = aCapacity;

    return true;
  }

  size_t Capacity() const { return mCapacity; }

  bool GrowTo(size_t aNewCapacity) {
    MOZ_ASSERT(aNewCapacity > mCapacity);
    if (mCapacity == 0) {
      return Init(aNewCapacity);
    }
    auto size_bytes = mozilla::CheckedInt<size_t>(sizeof(T)) * aNewCapacity;
    MOZ_ASSERT(size_bytes.isValid());
    if (!size_bytes.isValid()) {
      return false;
    }

    T* new_array =
        reinterpret_cast<T*>(sBaseAlloc.realloc(mArray, size_bytes.value()));
    if (!new_array) {
      return false;
    }
    mArray = new_array;

    for (size_t i = mCapacity; i < aNewCapacity; i++) {
      new (&mArray[i]) T();
    }
    mCapacity = aNewCapacity;
    return true;
  }

  size_t SizeOfExcludingThis() { return sBaseAlloc.usable_size(mArray); }
};

#endif /* ! BASEARRAY_H */
