/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Functionality related to memory alignment. */

#ifndef mozilla_Alignment_h
#define mozilla_Alignment_h

#include "mozilla/Attributes.h"
#include <cstddef>
#include <cstdint>

namespace mozilla {

/*
 * AlignedElem<N> is a structure whose alignment is guaranteed to be at least N
 * bytes.
 */
template <size_t Align>
struct alignas(Align) AlignedElem {};

template <typename T>
struct MOZ_INHERIT_TYPE_ANNOTATIONS_FROM_TEMPLATE_ARGS AlignedStorage2 {
  union {
    unsigned char mBytes[sizeof(T)];
    uint64_t mDummy;
  };

  const T* addr() const { return reinterpret_cast<const T*>(mBytes); }
  T* addr() { return static_cast<T*>(static_cast<void*>(mBytes)); }

  const void* bytes() const { return static_cast<const void*>(mBytes); }
  void* bytes() { return static_cast<void*>(mBytes); }

  AlignedStorage2() = default;

  // AlignedStorage2 is non-copyable: the default copy constructor violates
  // strict aliasing rules, per bug 1269319.
  AlignedStorage2(const AlignedStorage2&) = delete;
  void operator=(const AlignedStorage2&) = delete;
};

} /* namespace mozilla */

#endif /* mozilla_Alignment_h */
