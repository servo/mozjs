/*
 * Copyright 2021 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef wasm_memory_h
#define wasm_memory_h

#include "mozilla/CheckedInt.h"
#include "mozilla/Maybe.h"

#include <compare>  // std::strong_ordering
#include <stdint.h>

#include "js/Value.h"
#include "vm/NativeObject.h"
#include "wasm/WasmConstants.h"
#include "wasm/WasmValType.h"

namespace js {
namespace wasm {

// Limits are parameterized by an AddressType which is used to index the
// underlying resource (either a Memory or a Table). Tables are restricted to
// I32, while memories may use I64 when memory64 is enabled.

enum class AddressType : uint8_t { I32, I64 };

inline ValType ToValType(AddressType at) {
  return at == AddressType::I64 ? ValType::I64 : ValType::I32;
}

inline AddressType MinAddressType(AddressType a, AddressType b) {
  return (a == AddressType::I32 || b == AddressType::I32) ? AddressType::I32
                                                          : AddressType::I64;
}

extern bool ToAddressType(JSContext* cx, HandleValue value,
                          AddressType* addressType);

extern bool ToPageSize(JSContext* cx, HandleValue value, PageSize* pageSize);

extern const char* ToString(AddressType addressType);

static constexpr unsigned PageSizeInBytes(PageSize sz) {
  return 1U << static_cast<uint8_t>(sz);
}

static constexpr unsigned StandardPageSizeBytes =
    PageSizeInBytes(PageSize::Standard);
static_assert(StandardPageSizeBytes == 64 * 1024);

// By spec, see
// https://github.com/WebAssembly/spec/issues/1895#issuecomment-2895078022
static_assert((StandardPageSizeBytes * MaxMemory64StandardPagesValidation) <=
              (uint64_t(1) << 53) - 1);

// Pages is a typed unit representing a multiple of the page size, which
// defaults to wasm::StandardPageSizeBytes. The page size can be customized
// only if the custom page sizes proposal is enabled.
//
// We generally use pages as the unit of length when representing linear memory
// lengths so as to avoid overflow when the specified initial or maximum pages
// would overflow the native word size.
//
// Modules may specify pages up to 2^48 (or 2^64 - 1 with tiny pages) inclusive
// and so Pages is 64-bit on all platforms.
//
// We represent byte lengths using the native word size, as it is assumed that
// consumers of this API will only need byte lengths once it is time to
// allocate memory, at which point the pages will be checked against the
// implementation limits `MaxMemoryPages()` and will then be guaranteed to
// fit in a native word.
struct Pages {
 private:
  // Pages are specified by limit fields, which in general may be up to 2^48,
  // so we must use uint64_t here.
  uint64_t pageCount_;
  PageSize pageSize_;

  constexpr Pages(uint64_t pageCount, PageSize pageSize)
      : pageCount_(pageCount), pageSize_(pageSize) {}

 public:
  static constexpr Pages fromPageCount(uint64_t pageCount, PageSize pageSize) {
    return Pages(pageCount, pageSize);
  }

  static constexpr Pages forPageSize(PageSize pageSize) {
    return Pages(0, pageSize);
  }

  static constexpr bool byteLengthIsMultipleOfPageSize(size_t byteLength,
                                                       PageSize pageSize) {
    return byteLength % PageSizeInBytes(pageSize) == 0;
  }

  // Converts from a byte length to pages, assuming that the length is an
  // exact multiple of the page size.
  static constexpr Pages fromByteLengthExact(size_t byteLength,
                                             PageSize pageSize) {
    MOZ_RELEASE_ASSERT(byteLengthIsMultipleOfPageSize(byteLength, pageSize));
    return Pages(byteLength / PageSizeInBytes(pageSize), pageSize);
  }

  Pages& operator=(const Pages& other) {
    MOZ_RELEASE_ASSERT(other.pageSize_ == pageSize_);
    pageCount_ = other.pageCount_;
    return *this;
  }

  // Get the wrapped page count and size. Only use this if you must, prefer to
  // use or add new APIs to Page.
  uint64_t pageCount() const { return pageCount_; }
  PageSize pageSize() const { return pageSize_; }

  // Return whether the page length may overflow when converted to a byte
  // length in the native word size.
  bool hasByteLength() const {
    mozilla::CheckedInt<size_t> length(pageCount_);
    length *= PageSizeInBytes(pageSize_);
    return length.isValid();
  }

  // Converts from pages to byte length in the native word size. Users must
  // check for overflow, or be assured else-how that overflow cannot happen.
  size_t byteLength() const {
    mozilla::CheckedInt<size_t> length(pageCount_);
    length *= PageSizeInBytes(pageSize_);
    return length.value();
  }

  // Return the byteLength for a 64-bits memory.
  uint64_t byteLength64() const {
    mozilla::CheckedInt<uint64_t> length(pageCount_);
    length *= PageSizeInBytes(pageSize_);
    return length.value();
  }

  // Increment this pages by delta and return whether the resulting value
  // did not overflow. If there is no overflow, then this is set to the
  // resulting value.
  bool checkedIncrement(uint64_t delta) {
    mozilla::CheckedInt<uint64_t> newValue = pageCount_;
    newValue += delta;
    if (!newValue.isValid()) {
      return false;
    }
    pageCount_ = newValue.value();
    return true;
  }

  // Implement pass-through comparison operators so that Pages can be compared.

  constexpr auto operator<=>(const Pages& other) const {
    MOZ_RELEASE_ASSERT(other.pageSize_ == pageSize_);
    return pageCount_ <=> other.pageCount_;
  }
  constexpr auto operator==(const Pages& other) const {
    MOZ_RELEASE_ASSERT(other.pageSize_ == pageSize_);
    return pageCount_ == other.pageCount_;
  }
};

// The largest number of pages the application can request.
extern Pages MaxMemoryPages(AddressType t, PageSize pageSize);

// The byte value of MaxMemoryPages(t).
static inline size_t MaxMemoryBytes(AddressType t, PageSize pageSize) {
  return MaxMemoryPages(t, pageSize).byteLength();
}

// A value representing the largest valid value for boundsCheckLimit.
extern size_t MaxMemoryBoundsCheckLimit(AddressType t, PageSize pageSize);

static inline uint64_t MaxMemoryPagesValidation(AddressType addressType,
                                                PageSize pageSize) {
#ifdef ENABLE_WASM_CUSTOM_PAGE_SIZES
  if (pageSize == PageSize::Tiny) {
    return addressType == AddressType::I32 ? MaxMemory32TinyPagesValidation
                                           : MaxMemory64TinyPagesValidation;
  }
#endif

  MOZ_ASSERT(pageSize == PageSize::Standard);
  return addressType == AddressType::I32 ? MaxMemory32StandardPagesValidation
                                         : MaxMemory64StandardPagesValidation;
}

static inline uint64_t MaxTableElemsValidation(AddressType addressType) {
  return addressType == AddressType::I32 ? MaxTable32ElemsValidation
                                         : MaxTable64ElemsValidation;
}

// Compute the 'clamped' maximum size of a memory. See
// 'WASM Linear Memory structure' in ArrayBufferObject.cpp for background.
extern Pages ClampedMaxPages(AddressType t, Pages initialPages,
                             const mozilla::Maybe<Pages>& sourceMaxPages,
                             bool useHugeMemory);

// For a given WebAssembly/asm.js 'clamped' max pages, return the number of
// bytes to map which will necessarily be a multiple of the system page size and
// greater than clampedMaxPages in bytes.  See "Wasm Linear Memory Structure" in
// vm/ArrayBufferObject.cpp.
extern size_t ComputeMappedSize(Pages clampedMaxPages);

extern uint64_t GetMaxOffsetGuardLimit(bool hugeMemory, PageSize sz);

// Return the next higher valid immediate that satisfies the constraints of the
// platform.
extern uint64_t RoundUpToNextValidBoundsCheckImmediate(uint64_t i);

#ifdef WASM_SUPPORTS_HUGE_MEMORY
// On WASM_SUPPORTS_HUGE_MEMORY platforms, every asm.js or WebAssembly 32-bit
// memory unconditionally allocates a huge region of virtual memory of size
// wasm::HugeMappedSize. This allows all memory resizing to work without
// reallocation and provides enough guard space for most offsets to be folded
// into memory accesses.  See "Linear memory addresses and bounds checking" in
// wasm/WasmMemory.cpp for more information.

// Reserve 4GiB to support any i32 index.
static const uint64_t HugeIndexRange = uint64_t(UINT32_MAX) + 1;
// Reserve 32MiB to support most offset immediates. Any immediate that is over
// this will require a bounds check to be emitted. 32MiB was chosen to
// generously cover the max offset immediate, 20MiB, found in a corpus of wasm
// modules.
static const uint64_t HugeOffsetGuardLimit = 1 << 25;
// Reserve a wasm page (64KiB) to support slop on unaligned accesses.
static const uint64_t HugeUnalignedGuardPage = StandardPageSizeBytes;

// Compute the total memory reservation.
static const uint64_t HugeMappedSize =
    HugeIndexRange + HugeOffsetGuardLimit + HugeUnalignedGuardPage;

// Try to keep the memory reservation aligned to the wasm page size. This
// ensures that it's aligned to the system page size.
static_assert(HugeMappedSize % StandardPageSizeBytes == 0);

#endif

// The size of the guard page for non huge-memories.
static const size_t GuardSize = StandardPageSizeBytes;

// The size of the guard page that included NULL pointer. Reserve a smallest
// range for typical hardware, to catch near NULL pointer accesses, e.g.
// for a structure fields operations.
static const size_t NullPtrGuardSize = 4096;

// Check if a range of wasm memory is within bounds, specified as byte offset
// and length (using 32-bit indices). Omits one check by converting from
// uint32_t to uint64_t, at which point overflow cannot occur.
static inline bool MemoryBoundsCheck(uint32_t offset, uint32_t len,
                                     size_t memLen) {
  uint64_t offsetLimit = uint64_t(offset) + uint64_t(len);
  return offsetLimit <= memLen;
}

// Check if a range of wasm memory is within bounds, specified as byte offset
// and length (using 64-bit indices).
static inline bool MemoryBoundsCheck(uint64_t offset, uint64_t len,
                                     size_t memLen) {
  uint64_t offsetLimit = offset + len;
  bool didOverflow = offsetLimit < offset;
  bool tooLong = memLen < offsetLimit;
  return !didOverflow && !tooLong;
}

}  // namespace wasm
}  // namespace js

#endif  // wasm_memory_h
