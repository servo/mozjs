// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UTIL_BIT_FIELD_H_
#define V8_UTIL_BIT_FIELD_H_

#include "mozilla/Assertions.h"

namespace v8 {
namespace base {

// ----------------------------------------------------------------------------
// BitField is a help template for encoding and decode bitfield with
// unsigned content.
// Instantiate them via 'using', which is cheaper than deriving a new class:
// using MyBitField = base::BitField<MyEnum, 4, 2>;
// The BitField class is final to enforce this style over derivation.

template <class T, int shift, int size, class U = uint32_t>
class BitField final {
 public:
  static_assert(std::is_unsigned_v<U>);
  static_assert(shift < 8 * sizeof(U));  // Otherwise shifts by {shift} are UB.
  static_assert(size < 8 * sizeof(U));   // Otherwise shifts by {size} are UB.
  static_assert(shift + size <= 8 * sizeof(U));
  static_assert(size > 0);

  // Make sure we don't create bitfields that are too large for their value.
  // Carve out an exception for 32-bit size_t, for uniformity between 32-bit
  // and 64-bit code.
  static_assert(size <= 8 * sizeof(T) ||
                    (std::is_same_v<T, size_t> && sizeof(size_t) == 4),
                "Bitfield is unnecessarily big!");
  static_assert(!std::is_same_v<T, bool> || size == 1,
                "Bitfield is unnecessarily big!");

  using FieldType = T;
  using BaseType = U;

  // A type U mask of bit field.  To use all bits of a type U of x bits
  // in a bitfield without compiler warnings we have to compute 2^x
  // without using a shift count of x in the computation.
  static constexpr int kShift = shift;
  static constexpr int kSize = size;
  static constexpr U kMask = ((U{1} << kShift) << kSize) - (U{1} << kShift);
  static constexpr int kLastUsedBit = kShift + kSize - 1;
  static constexpr U kNumValues = U{1} << kSize;
  static constexpr U kMax = kNumValues - 1;

  template <class T2, int size2>
  using Next = BitField<T2, kShift + kSize, size2, U>;

  // Tells whether the provided value fits into the bit field.
  static constexpr bool is_valid(T value) {
    return (static_cast<U>(value) & ~kMax) == 0;
  }

  // Returns a type U with the bit field value encoded.
  static constexpr U encode(T value) {
    if constexpr (std::is_enum_v<T> || sizeof(T) * 8 <= kSize ||
                  std::is_same_v<T, bool>) {
      // For enums, we trust that they are within the valid range, since they
      // are typed and we assume that the enum itself has a valid value. Assert
      // just in case (e.g. in case valid enum values are outside the bitfield
      // size).
      //
      // Similarly, if T fits exactly in the bitfield (either in bytes, or
      // because bools can be stored as 1 bit), we trust that they are valid.
      MOZ_ASSERT(is_valid(value));
    } else {
      // For non-enums (in practice, integers), we don't trust that they are
      // valid, since we pass them around without static value interval
      // information.
      MOZ_RELEASE_ASSERT(is_valid(value));
    }
    return static_cast<U>(value) << kShift;
  }

  // Returns a type U with the bit field value updated.
  [[nodiscard]] static constexpr U update(U previous, T value) {
    return (previous & ~kMask) | encode(value);
  }

  // Extracts the bit field from the value.
  static constexpr T decode(U value) {
    return static_cast<T>((value & kMask) >> kShift);
  }
};

}  // namespace base
}  // namespace v8

#endif  // V8_UTIL_BIT_FIELD_H_
