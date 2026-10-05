/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef util_PolicyAllocator_h
#define util_PolicyAllocator_h

#include "js/Utility.h"

namespace js {

// Adapts an AllocPolicy into a C++ standard allocator, suitable for use in STL
// containers. These containers don't support OOM handling, so this allocator
// will crash on OOM.
//
// Note: this requires the allocator to overload operator==. This should return
// true only if the storage allocated by one allocator can be deallocated
// through another. This is always the case for stateless allocators.
template <typename T, typename AllocPolicy>
struct PolicyAllocator : private AllocPolicy {
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;

  template <typename U, typename AP>
  friend struct PolicyAllocator;

  explicit PolicyAllocator(const AllocPolicy& policy) : AllocPolicy(policy) {}
  template <typename U>
  explicit PolicyAllocator(const PolicyAllocator<U, AllocPolicy>& other)
      : AllocPolicy(other) {}

  T* allocate(size_t n) {
    js::AutoEnterOOMUnsafeRegion oomUnsafe;
    T* result = this->template pod_malloc<T>(n);
    if (MOZ_UNLIKELY(!result)) {
      oomUnsafe.crash("PolicyAllocator::allocate");
    }
    return result;
  }

  void deallocate(T* p, size_t n) { this->free_(p, n); }

  template <class U>
  struct rebind {
    using other = PolicyAllocator<U, AllocPolicy>;
  };

  const AllocPolicy& allocPolicy() const { return *this; }
  template <typename U>
  bool operator==(const PolicyAllocator<U, AllocPolicy>& other) const {
    return allocPolicy() == other.allocPolicy();
  }
};

} /* namespace js */

#endif /* util_PolicyAllocator_h */
