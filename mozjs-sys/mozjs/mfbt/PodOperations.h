/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Operations for zeroing POD types, arrays, and so on.
 *
 * These operations are preferable to memset, memcmp, and the like because they
 * don't require remembering to multiply by sizeof(T), array lengths, and so on
 * everywhere.
 */

#ifndef mozilla_PodOperations_h
#define mozilla_PodOperations_h

#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"

#include <cstring>
#include <limits>
#include <type_traits>

namespace mozilla {

template <typename T, size_t Length>
class Array;

template <typename T>
class NotNull;

/** Set the contents of |aT| to 0. */
template <typename T>
static MOZ_ALWAYS_INLINE void PodZero(T* aT) {
  static_assert(std::is_trivially_copyable_v<T>,
                "PodZero requires trivially copyable types");
  memset(aT, 0, sizeof(T));
}

/** Set the contents of |aNElem| elements starting at |aT| to 0. */
template <typename T>
static MOZ_ALWAYS_INLINE void PodZero(T* aT, size_t aNElem) {
  static_assert(std::is_trivially_copyable_v<T>,
                "PodZero requires trivially copyable types");
  /*
   * NB: If the caller uses a constant size, both GCC and Clang inline the
   * memset call if they find it profitable.
   *
   * If the value is dynamic, some might think that it's more profitable to
   * perform an explicit loop over the aNElem. It turns out Clang rolls back the
   * loop anyway, so even if GCC doesn't, keep the codebase simple and clearly
   * convey the intent instead of trying to outsmart the compiler.
   */
  MOZ_ASSERT(aNElem <= std::numeric_limits<size_t>::max() / sizeof(T),
             "trying to zero an impossible number of elements");
  memset(aT, 0, sizeof(T) * aNElem);
}

/** Set the contents of |aNElem| elements starting at |aT| to 0. */
template <typename T>
static MOZ_ALWAYS_INLINE void PodZero(NotNull<T*> aT, size_t aNElem) {
  PodZero(aT.get(), aNElem);
}

/*
 * Arrays implicitly convert to pointers to their first element, which is
 * dangerous when combined with the above PodZero definitions.  Adding an
 * overload for arrays is ambiguous, so we need another identifier.  The
 * ambiguous overload is left to catch mistaken uses of PodZero; if you get a
 * compile error involving PodZero and array types, use PodArrayZero instead.
 */
template <typename T, size_t N>
static void PodZero(T (&aT)[N]) = delete;
template <typename T, size_t N>
static void PodZero(T (&aT)[N], size_t aNElem) = delete;

/** Set the contents of the array |aT| to zero. */
template <class T, size_t N>
static MOZ_ALWAYS_INLINE void PodArrayZero(T (&aT)[N]) {
  static_assert(std::is_trivially_copyable_v<T>,
                "PodArrayZero requires trivially copyable types");
  static_assert(N < std::numeric_limits<size_t>::max() / sizeof(T));
  memset(aT, 0, N * sizeof(T));
}

template <typename T, size_t N>
static MOZ_ALWAYS_INLINE void PodArrayZero(Array<T, N>& aArr) {
  static_assert(std::is_trivially_copyable_v<T>,
                "PodArrayZero requires trivially copyable types");
  static_assert(N < std::numeric_limits<size_t>::max() / sizeof(T));
  memset(&aArr[0], 0, N * sizeof(T));
}

/**
 * Copy |aNElem| T elements from |aSrc| to |aDst|.  The two memory ranges must
 * not overlap!
 */
template <typename T>
static MOZ_ALWAYS_INLINE void PodCopy(T* aDst, const T* aSrc, size_t aNElem) {
  static_assert(std::is_trivially_copyable_v<T>,
                "PodCopy requires trivially copyable types");
  MOZ_ASSERT(aDst + aNElem <= aSrc || aSrc + aNElem <= aDst,
             "destination and source must not overlap");
  MOZ_ASSERT(aNElem <= std::numeric_limits<size_t>::max() / sizeof(T),
             "trying to copy an impossible number of elements");

// Linux memcpy for small sizes seems slower than on other
// platforms. So we use a loop for small sizes there only.
//
// See Bug 1967062 for details.
#if defined(XP_LINUX)
  if (aNElem < 128) {
    for (const T* srcend = aSrc + aNElem; aSrc < srcend; aSrc++, aDst++) {
      *aDst = *aSrc;
    }
    return;
  }
#endif

  memcpy(aDst, aSrc, aNElem * sizeof(T));
}

template <typename T>
static MOZ_ALWAYS_INLINE void PodCopy(volatile T* aDst, const volatile T* aSrc,
                                      size_t aNElem) {
  static_assert(std::is_trivially_copyable_v<T>,
                "PodCopy requires trivially copyable types");
  MOZ_ASSERT(aDst + aNElem <= aSrc || aSrc + aNElem <= aDst,
             "destination and source must not overlap");

  /*
   * Volatile |aDst| requires extra work, because it's undefined behavior to
   * modify volatile objects using the mem* functions.  Just write out the
   * loops manually, using operator= rather than memcpy for the same reason,
   * and let the compiler optimize to the extent it can.
   */
  for (const volatile T* srcend = aSrc + aNElem; aSrc < srcend;
       aSrc++, aDst++) {
    *aDst = *aSrc;
  }
}

/*
 * Copy the contents of the array |aSrc| into the array |aDst|, both of size N.
 * The arrays must not overlap!
 */
template <class T, size_t N>
static MOZ_ALWAYS_INLINE void PodArrayCopy(T (&aDst)[N], const T (&aSrc)[N]) {
  PodCopy(aDst, aSrc, N);
}

/**
 * Copy the memory for |aNElem| T elements from |aSrc| to |aDst|.  If the two
 * memory ranges overlap, then the effect is as if the |aNElem| elements are
 * first copied from |aSrc| to a temporary array, and then from the temporary
 * array to |aDst|.
 */
template <typename T>
static MOZ_ALWAYS_INLINE void PodMove(T* aDst, const T* aSrc, size_t aNElem) {
  static_assert(std::is_trivially_copyable_v<T>,
                "PodMove requires trivially copyable types");
  MOZ_ASSERT(aNElem <= std::numeric_limits<size_t>::max() / sizeof(T),
             "trying to move an impossible number of elements");
  memmove(aDst, aSrc, aNElem * sizeof(T));
}

/**
 * Looking for a PodEqual? Use ArrayEqual from ArrayUtils.h.
 * Note that we *cannot* use memcmp for this, due to padding bytes, etc..
 */

}  // namespace mozilla

#endif /* mozilla_PodOperations_h */
