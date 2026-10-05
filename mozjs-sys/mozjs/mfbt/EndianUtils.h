/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Functions for reading and writing integers in various endiannesses. */

/*
 * The classes LittleEndian and BigEndian expose static methods for
 * reading and writing 16-, 32-, and 64-bit signed and unsigned integers
 * in their respective endianness.  The addresses read from or written
 * to may be misaligned (although misaligned accesses may incur
 * architecture-specific performance costs).  The naming scheme is:
 *
 * {Little,Big}Endian::{read,write}{Uint,Int}<bitsize>
 *
 * For instance, LittleEndian::readInt32 will read a 32-bit signed
 * integer from memory in little endian format.  Similarly,
 * BigEndian::writeUint16 will write a 16-bit unsigned integer to memory
 * in big-endian format.
 *
 * The class NativeEndian exposes methods for conversion of existing
 * data to and from the native endianness.  These methods are intended
 * for cases where data needs to be transferred, serialized, etc.
 * swap{To,From}{Little,Big}Endian byteswap a single value if necessary.
 * Bulk conversion functions are also provided which optimize the
 * no-conversion-needed case:
 *
 * - copyAndSwap{To,From}{Little,Big}Endian;
 * - swap{To,From}{Little,Big}EndianInPlace.
 *
 * The *From* variants are intended to be used for reading data and the
 * *To* variants for writing data.
 *
 * Methods on NativeEndian work with integer data of any type.
 * Floating-point data is not supported.
 *
 * For clarity in networking code, "Network" may be used as a synonym
 * for "Big" in any of the above methods or class names.
 *
 * As an example, reading a file format header whose fields are stored
 * in big-endian format might look like:
 *
 * class ExampleHeader
 * {
 * private:
 *   uint32_t mMagic;
 *   uint32_t mLength;
 *   uint32_t mTotalRecords;
 *   uint64_t mChecksum;
 *
 * public:
 *   ExampleHeader(const void* data)
 *   {
 *     const uint8_t* ptr = static_cast<const uint8_t*>(data);
 *     mMagic = BigEndian::readUint32(ptr); ptr += sizeof(uint32_t);
 *     mLength = BigEndian::readUint32(ptr); ptr += sizeof(uint32_t);
 *     mTotalRecords = BigEndian::readUint32(ptr); ptr += sizeof(uint32_t);
 *     mChecksum = BigEndian::readUint64(ptr);
 *   }
 *   ...
 * };
 */

#ifndef mozilla_EndianUtils_h
#define mozilla_EndianUtils_h

#include "mozilla/Assertions.h"
#include "mozilla/DebugOnly.h"

#include <bit>
#include <stdint.h>
#include <string.h>

namespace mozilla {

/* FIXME: move to std::byteswap with C++23
 */
template <typename T>
constexpr T byteswap(T n) {
  if constexpr (sizeof(T) == 2) {
    return __builtin_bswap16(n);
  } else if constexpr (sizeof(T) == 4) {
    return __builtin_bswap32(n);
  } else if constexpr (sizeof(T) == 8) {
    return __builtin_bswap64(n);
  }
}

namespace detail {

class EndianUtils {
  /**
   * Assert that the memory regions [aDest, aDest+aCount) and
   * [aSrc, aSrc+aCount] do not overlap.  aCount is given in bytes.
   */
  static void assertNoOverlap(const void* aDest, const void* aSrc,
                              size_t aCount) {
    DebugOnly<const uint8_t*> byteDestPtr = static_cast<const uint8_t*>(aDest);
    DebugOnly<const uint8_t*> byteSrcPtr = static_cast<const uint8_t*>(aSrc);
    MOZ_ASSERT(
        (byteDestPtr <= byteSrcPtr && byteDestPtr + aCount <= byteSrcPtr) ||
        (byteSrcPtr <= byteDestPtr && byteSrcPtr + aCount <= byteDestPtr));
  }

  template <typename T>
  static void assertAligned(T* aPtr) {
    MOZ_ASSERT((uintptr_t(aPtr) % sizeof(T)) == 0, "Unaligned pointer!");
  }

 protected:
  /**
   * Return |aValue| converted from SourceEndian encoding to DestEndian
   * encoding.
   */
  template <std::endian SourceEndian, std::endian DestEndian, typename T>
  static constexpr T maybeSwap(T aValue) {
    if constexpr (SourceEndian == DestEndian) {
      return aValue;
    }
    return byteswap(aValue);
  }

  /**
   * Convert |aCount| elements at |aPtr| from SourceEndian encoding to
   * DestEndian encoding.
   */
  template <std::endian SourceEndian, std::endian DestEndian, typename T>
  static inline void maybeSwapInPlace(T* aPtr, size_t aCount) {
    assertAligned(aPtr);

    if constexpr (SourceEndian == DestEndian) {
      return;
    }
    for (size_t i = 0; i < aCount; i++) {
      aPtr[i] = byteswap(aPtr[i]);
    }
  }

  /**
   * Write |aCount| elements to the unaligned address |aDest| in DestEndian
   * format, using elements found at |aSrc| in SourceEndian format.
   */
  template <std::endian SourceEndian, std::endian DestEndian, typename T>
  static void copyAndSwapTo(void* aDest, const T* aSrc, size_t aCount) {
    assertNoOverlap(aDest, aSrc, aCount * sizeof(T));
    assertAligned(aSrc);

    if constexpr (SourceEndian == DestEndian) {
      memcpy(aDest, aSrc, aCount * sizeof(T));
      return;
    }

    uint8_t* byteDestPtr = static_cast<uint8_t*>(aDest);
    for (size_t i = 0; i < aCount; ++i) {
      const T Val = maybeSwap<SourceEndian, DestEndian>(aSrc[i]);
      memcpy(byteDestPtr, static_cast<const void*>(&Val), sizeof(T));
      byteDestPtr += sizeof(T);
    }
  }

  /**
   * Write |aCount| elements to |aDest| in DestEndian format, using elements
   * found at the unaligned address |aSrc| in SourceEndian format.
   */
  template <std::endian SourceEndian, std::endian DestEndian, typename T>
  static void copyAndSwapFrom(T* aDest, const void* aSrc, size_t aCount) {
    assertNoOverlap(aDest, aSrc, aCount * sizeof(T));
    assertAligned(aDest);

    if constexpr (SourceEndian == DestEndian) {
      memcpy(aDest, aSrc, aCount * sizeof(T));
      return;
    }

    const uint8_t* byteSrcPtr = static_cast<const uint8_t*>(aSrc);
    for (size_t i = 0; i < aCount; ++i) {
      T Val;
      memcpy(static_cast<void*>(&Val), byteSrcPtr, sizeof(T));
      aDest[i] = maybeSwap<SourceEndian, DestEndian>(Val);
      byteSrcPtr += sizeof(T);
    }
  }
};

template <std::endian ThisEndian>
class Endian : private EndianUtils {
 protected:
  /** Read a uint16_t in ThisEndian endianness from |aPtr| and return it. */
  [[nodiscard]] static uint16_t readUint16(const void* aPtr) {
    return read<uint16_t>(aPtr);
  }

  /** Read a uint32_t in ThisEndian endianness from |aPtr| and return it. */
  [[nodiscard]] static uint32_t readUint32(const void* aPtr) {
    return read<uint32_t>(aPtr);
  }

  /** Read a uint64_t in ThisEndian endianness from |aPtr| and return it. */
  [[nodiscard]] static uint64_t readUint64(const void* aPtr) {
    return read<uint64_t>(aPtr);
  }

  /** Read a uintptr_t in ThisEndian endianness from |aPtr| and return it. */
  [[nodiscard]] static uintptr_t readUintptr(const void* aPtr) {
    return read<uintptr_t>(aPtr);
  }

  /** Read an int16_t in ThisEndian endianness from |aPtr| and return it. */
  [[nodiscard]] static int16_t readInt16(const void* aPtr) {
    return read<int16_t>(aPtr);
  }

  /** Read an int32_t in ThisEndian endianness from |aPtr| and return it. */
  [[nodiscard]] static int32_t readInt32(const void* aPtr) {
    return read<uint32_t>(aPtr);
  }

  /** Read an int64_t in ThisEndian endianness from |aPtr| and return it. */
  [[nodiscard]] static int64_t readInt64(const void* aPtr) {
    return read<int64_t>(aPtr);
  }

  /** Read an intptr_t in ThisEndian endianness from |aPtr| and return it. */
  [[nodiscard]] static intptr_t readIntptr(const void* aPtr) {
    return read<intptr_t>(aPtr);
  }

  /** Write |aValue| to |aPtr| using ThisEndian endianness. */
  static void writeUint16(void* aPtr, uint16_t aValue) { write(aPtr, aValue); }

  /** Write |aValue| to |aPtr| using ThisEndian endianness. */
  static void writeUint32(void* aPtr, uint32_t aValue) { write(aPtr, aValue); }

  /** Write |aValue| to |aPtr| using ThisEndian endianness. */
  static void writeUint64(void* aPtr, uint64_t aValue) { write(aPtr, aValue); }

  /** Write |aValue| to |aPtr| using ThisEndian endianness. */
  static void writeUintptr(void* aPtr, uintptr_t aValue) {
    write(aPtr, aValue);
  }

  /** Write |aValue| to |aPtr| using ThisEndian endianness. */
  static void writeInt16(void* aPtr, int16_t aValue) { write(aPtr, aValue); }

  /** Write |aValue| to |aPtr| using ThisEndian endianness. */
  static void writeInt32(void* aPtr, int32_t aValue) { write(aPtr, aValue); }

  /** Write |aValue| to |aPtr| using ThisEndian endianness. */
  static void writeInt64(void* aPtr, int64_t aValue) { write(aPtr, aValue); }

  /** Write |aValue| to |aPtr| using ThisEndian endianness. */
  static void writeIntptr(void* aPtr, intptr_t aValue) { write(aPtr, aValue); }

  /*
   * Converts a value of type T to little-endian format.
   *
   * This function is intended for cases where you have data in your
   * native-endian format and you need it to appear in little-endian
   * format for transmission.
   */
  template <typename T>
  [[nodiscard]] static constexpr T swapToLittleEndian(T aValue) {
    return maybeSwap<ThisEndian, std::endian::little>(aValue);
  }

  /*
   * Copies |aCount| values of type T starting at |aSrc| to |aDest|, converting
   * them to little-endian format if ThisEndian is std::endian::big.  |aSrc| as
   * a typed pointer must be aligned; |aDest| need not be.
   *
   * As with memcpy, |aDest| and |aSrc| must not overlap.
   */
  template <typename T>
  static void copyAndSwapToLittleEndian(void* aDest, const T* aSrc,
                                        size_t aCount) {
    copyAndSwapTo<ThisEndian, std::endian::little>(aDest, aSrc, aCount);
  }

  /*
   * Likewise, but converts values in place.
   */
  template <typename T>
  static void swapToLittleEndianInPlace(T* aPtr, size_t aCount) {
    maybeSwapInPlace<ThisEndian, std::endian::little>(aPtr, aCount);
  }

  /*
   * Converts a value of type T to big-endian format.
   */
  template <typename T>
  [[nodiscard]] static constexpr T swapToBigEndian(T aValue) {
    return maybeSwap<ThisEndian, std::endian::big>(aValue);
  }

  /*
   * Copies |aCount| values of type T starting at |aSrc| to |aDest|, converting
   * them to big-endian format if ThisEndian is std::endian::little.  |aSrc| as
   * a typed pointer must be aligned; |aDest| need not be.
   *
   * As with memcpy, |aDest| and |aSrc| must not overlap.
   */
  template <typename T>
  static void copyAndSwapToBigEndian(void* aDest, const T* aSrc,
                                     size_t aCount) {
    copyAndSwapTo<ThisEndian, std::endian::big>(aDest, aSrc, aCount);
  }

  /*
   * Likewise, but converts values in place.
   */
  template <typename T>
  static void swapToBigEndianInPlace(T* aPtr, size_t aCount) {
    maybeSwapInPlace<ThisEndian, std::endian::big>(aPtr, aCount);
  }

  /*
   * Synonyms for the big-endian functions, for better readability
   * in network code.
   */

  template <typename T>
  [[nodiscard]] static constexpr T swapToNetworkOrder(T aValue) {
    return swapToBigEndian(aValue);
  }

  template <typename T>
  static void copyAndSwapToNetworkOrder(void* aDest, const T* aSrc,
                                        size_t aCount) {
    copyAndSwapToBigEndian(aDest, aSrc, aCount);
  }

  template <typename T>
  static void swapToNetworkOrderInPlace(T* aPtr, size_t aCount) {
    swapToBigEndianInPlace(aPtr, aCount);
  }

  /*
   * Converts a value of type T from little-endian format.
   */
  template <typename T>
  [[nodiscard]] static constexpr T swapFromLittleEndian(T aValue) {
    return maybeSwap<std::endian::little, ThisEndian>(aValue);
  }

  /*
   * Copies |aCount| values of type T starting at |aSrc| to |aDest|, converting
   * them to little-endian format if ThisEndian is std::endian::big.  |aDest| as
   * a typed pointer must be aligned; |aSrc| need not be.
   *
   * As with memcpy, |aDest| and |aSrc| must not overlap.
   */
  template <typename T>
  static void copyAndSwapFromLittleEndian(T* aDest, const void* aSrc,
                                          size_t aCount) {
    copyAndSwapFrom<std::endian::little, ThisEndian>(aDest, aSrc, aCount);
  }

  /*
   * Likewise, but converts values in place.
   */
  template <typename T>
  static void swapFromLittleEndianInPlace(T* aPtr, size_t aCount) {
    maybeSwapInPlace<std::endian::little, ThisEndian>(aPtr, aCount);
  }

  /*
   * Converts a value of type T from big-endian format.
   */
  template <typename T>
  [[nodiscard]] static constexpr T swapFromBigEndian(T aValue) {
    return maybeSwap<std::endian::big, ThisEndian>(aValue);
  }

  /*
   * Copies |aCount| values of type T starting at |aSrc| to |aDest|, converting
   * them to big-endian format if ThisEndian is std::endian::little.  |aDest| as
   * a typed pointer must be aligned; |aSrc| need not be.
   *
   * As with memcpy, |aDest| and |aSrc| must not overlap.
   */
  template <typename T>
  static void copyAndSwapFromBigEndian(T* aDest, const void* aSrc,
                                       size_t aCount) {
    copyAndSwapFrom<std::endian::big, ThisEndian>(aDest, aSrc, aCount);
  }

  /*
   * Likewise, but converts values in place.
   */
  template <typename T>
  static void swapFromBigEndianInPlace(T* aPtr, size_t aCount) {
    maybeSwapInPlace<std::endian::big, ThisEndian>(aPtr, aCount);
  }

  /*
   * Synonyms for the big-endian functions, for better readability
   * in network code.
   */
  template <typename T>
  [[nodiscard]] static constexpr T swapFromNetworkOrder(T aValue) {
    return swapFromBigEndian(aValue);
  }

  template <typename T>
  static void copyAndSwapFromNetworkOrder(T* aDest, const void* aSrc,
                                          size_t aCount) {
    copyAndSwapFromBigEndian(aDest, aSrc, aCount);
  }

  template <typename T>
  static void swapFromNetworkOrderInPlace(T* aPtr, size_t aCount) {
    swapFromBigEndianInPlace(aPtr, aCount);
  }

 private:
  /**
   * Read a value of type T, encoded in endianness ThisEndian from |aPtr|.
   * Return that value encoded in native endianness.
   */
  template <typename T>
  static T read(const void* aPtr) {
    T Val;
    memcpy(static_cast<void*>(&Val), aPtr, sizeof(T));
    return maybeSwap<ThisEndian, std::endian::native>(Val);
  }

  /**
   * Write a value of type T, in native endianness, to |aPtr|, in ThisEndian
   * endianness.
   */
  template <typename T>
  static void write(void* aPtr, T aValue) {
    T tmp = maybeSwap<std::endian::native, ThisEndian>(aValue);
    memcpy(aPtr, &tmp, sizeof(T));
  }

  Endian() = delete;
  Endian(const Endian& aTther) = delete;
  void operator=(const Endian& aOther) = delete;
};

template <std::endian ThisEndian>
class EndianReadWrite : public Endian<ThisEndian> {
 private:
  typedef Endian<ThisEndian> super;

 public:
  using super::readInt16;
  using super::readInt32;
  using super::readInt64;
  using super::readIntptr;
  using super::readUint16;
  using super::readUint32;
  using super::readUint64;
  using super::readUintptr;
  using super::writeInt16;
  using super::writeInt32;
  using super::writeInt64;
  using super::writeIntptr;
  using super::writeUint16;
  using super::writeUint32;
  using super::writeUint64;
  using super::writeUintptr;
};

} /* namespace detail */

class LittleEndian final : public detail::EndianReadWrite<std::endian::little> {
};

class BigEndian final : public detail::EndianReadWrite<std::endian::big> {};

typedef BigEndian NetworkEndian;

class NativeEndian final : public detail::Endian<std::endian::native> {
 private:
  typedef detail::Endian<std::endian::native> super;

 public:
  /*
   * These functions are intended for cases where you have data in your
   * native-endian format and you need the data to appear in the appropriate
   * endianness for transmission, serialization, etc.
   */
  using super::copyAndSwapToBigEndian;
  using super::copyAndSwapToLittleEndian;
  using super::copyAndSwapToNetworkOrder;
  using super::swapToBigEndian;
  using super::swapToBigEndianInPlace;
  using super::swapToLittleEndian;
  using super::swapToLittleEndianInPlace;
  using super::swapToNetworkOrder;
  using super::swapToNetworkOrderInPlace;

  /*
   * These functions are intended for cases where you have data in the
   * given endianness (e.g. reading from disk or a file-format) and you
   * need the data to appear in native-endian format for processing.
   */
  using super::copyAndSwapFromBigEndian;
  using super::copyAndSwapFromLittleEndian;
  using super::copyAndSwapFromNetworkOrder;
  using super::swapFromBigEndian;
  using super::swapFromBigEndianInPlace;
  using super::swapFromLittleEndian;
  using super::swapFromLittleEndianInPlace;
  using super::swapFromNetworkOrder;
  using super::swapFromNetworkOrderInPlace;
};

} /* namespace mozilla */

#endif /* mozilla_EndianUtils_h */
