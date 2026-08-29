/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef wasm_WasmStructLayout_h
#define wasm_WasmStructLayout_h

#include "mozilla/Assertions.h"
#include "mozilla/Vector.h"

#include <stdint.h>

#include "vm/MallocProvider.h"
#include "wasm/WasmConstants.h"  // MaxStructFields

// [SMDOC] Wasm Struct Layout Overview
//
// (1) Struct layout is almost entirely decoupled from the details of
//     WasmStructObject.  Layout needs only to know the fixed-header-size and
//     inline-payload-size for WasmStructObject.  To avoid header-file cycle
//     complexity, the values WasmStructObject_Size_ASSUMED and
//     WasmStructObject_MaxInlineBytes_ASSUMED are defined in this file.  These
//     assumptions are made safe by static assertions in WasmGcObject.h; see
//     comments there.
//
// (2) A structure layout consists of a FieldAccessPath value (see below) for
//     each field, together with the total object size and inline payload size
//     for the WasmStructObject, and possibly the total OOL size required.
//
// (3) Struct layout info is stored in `class wasm::StructType`.  It is
//     computed early in the compilation process, by `StructType::init`, when
//     decoding the module environment.  The struct's AllocKind is also
//     computed.
//
// (4) Structs that do not need an OOL pointer are not forced to have one.
//     Whether one is required can be determined by calling
//     `StructType::hasOOL`.  If one is present, its offset relative to the
//     start of the WasmStructObject is stored in
//     `StructType::oolPointerOffset_`.
//
// (5) When generating code for field accesses, info (4) is not needed.
//     Instead a field access is described by its FieldAccessPath; this is all
//     that is needed (apart from the field's type) to generate the relevant
//     loads/stores.
//
// (6) StructType is the single point of truth for struct layouts.  However,
//     for performance reasons, at instantiation time, some fields of a
//     StructType are copied into the associated TypeDefInstanceData's
//     `cached.strukt` union, mainly so as to make struct allocation faster.
//     This copying is done by `Instance::init`.
//
// (7) WasmStructObject::createStructIL and related machinery look at fields in
//     the TypeDefInstanceData to get relevant run-time info (the AllocKind,
//     etc).
//
// (8) At run-time, it may be necessary to find the OOL pointer for arbitrary
//     WasmStructObjects, mostly in the GC-support routines.  This can be
//     obtained (at some expense) from
//     `WasmStructObject::hasOOLPointer/getOOLPointer` and related methods.
//
// (9) Note: the allocation machinery will ensure that fields of size 1, 2, 4
//     and 8 bytes are naturally aligned.  However, 16 byte fields are only
//     guaranteed 8 byte alignment.  This is because the underlying heap
//     allocator only provides 8 byte alignment, so even if 16 byte fields were
//     16-aligned relative to the start of a WasmStructObject, there's no
//     guarantee they would be 16-aligned when actually written to the heap.
//
// (10) The struct layout mechanism itself, as used in (3), and implemented by
//      StructLayout::addField, works as follows:
//
//      - it contains a fixed-sized bit vector that tracks commitments to the
//        WasmStructObject
//
//      - it contains a variable-sized bit vector that tracks commitments to
//        the OOL storage block
//
//      - the bit vectors make it possible to find and opportunistically fill
//        alignment holes that would otherwise go unused.  They also
//        (critically) make it possible to back out tentative allocations,
//        which is needed to ensure we can always fit an OOL pointer field in
//        the IL storage area if we later need to.
//
//      See StructLayout::addField comments for the exact algorithm.

namespace js::wasm {

// These values are defined by WasmStructObject's layout but are needed early
// in the compilation pipeline in order to compute struct layouts.  Rather than
// create a header file cycle involving WasmGcObject.h, WasmTypeDef.h and this
// file, it seems simpler to assume what they are and static_assert this is
// correct on WasmGcObject.h.  These values are expected to change rarely, if
// ever.
//
// See comment on static_assert involving these in WasmGcObject.h.
const size_t WasmStructObject_Size_ASSUMED = 16;

#ifdef JS_64BIT
const size_t WasmStructObject_MaxInlineBytes_ASSUMED = 136;
#else
const size_t WasmStructObject_MaxInlineBytes_ASSUMED = 128;
#endif

//=========================================================================
// FieldAccessPath

// FieldAccessPath describes the offsets needed to access a field in a
// StructType.  It contains either one or two values.
//
// Let `obj` be a `WasmStructObject*`.  Then, for a field with path `p`:
//
// * if !p.hasOOL(), the data is at obj + p.ilOffset().
//
// * if p.hasOOL(), let oolptr = *(obj + p.ilOffset()).
//   The data is at oolptr + p.oolOffset().
//
// It is implied from this that the `ilOffset()` values incorporate the fixed
// header size of WasmStructObject; that does not need to be added on here.

class FieldAccessPath {
  uint32_t path_;
  static constexpr uint32_t ILBits = 9;
  static constexpr uint32_t OOLBits = 32 - ILBits;
  static constexpr uint32_t MaxValidILOffset = (1 << ILBits) - 1;
  static constexpr uint32_t MaxValidOOLOffset = (1 << OOLBits) - 1 - 1;
  static constexpr uint32_t InvalidOOLOffset = MaxValidOOLOffset + 1;
  uint32_t getIL() const { return path_ & MaxValidILOffset; }
  uint32_t getOOL() const { return path_ >> ILBits; }
  // Ensure ILBits is sufficient for any valid IL offset.
  static_assert((WasmStructObject_Size_ASSUMED +
                 WasmStructObject_MaxInlineBytes_ASSUMED) < MaxValidILOffset);
  // A crude check to ensure that OOLBits is sufficient for any situation,
  // assuming a worst-case future scenario where fields can be up to 64 bytes
  // long (eg for Intel AVX512 fields).
  static_assert(js::wasm::MaxStructFields * 64 < MaxValidOOLOffset);

 public:
  FieldAccessPath() : path_(0) {}
  explicit FieldAccessPath(uint32_t offsetIL)
      : path_((InvalidOOLOffset << ILBits) | offsetIL) {
    MOZ_ASSERT(offsetIL <= MaxValidILOffset);
  }
  FieldAccessPath(uint32_t offsetIL, uint32_t offsetOOL)
      : path_((offsetOOL << ILBits) | offsetIL) {
    MOZ_ASSERT(offsetIL <= MaxValidILOffset);
    MOZ_ASSERT(offsetOOL <= MaxValidOOLOffset);
  }
  bool hasOOL() const { return getOOL() != InvalidOOLOffset; }
  uint32_t ilOffset() const { return getIL(); }
  uint32_t oolOffset() const {
    MOZ_ASSERT(hasOOL());
    return getOOL();
  }
};

static_assert(sizeof(FieldAccessPath) == sizeof(uint32_t));

//=========================================================================
// BitVector -- helper class for StructLayout

// BitVector is a base class that does the heavy lifting for WasmStructObject
// and OOL-storage layout.  It provides a bit-vector which keeps track of
// committed space inside the object being laid out.

// In what follows, "byte" and "offset" refer to those entities in the
// WasmStructObject and/or its OOL area, that we are laying out.  Whereas
// "chunk" refers to refers to a single byte in `BitVector::chunks_`; this in
// turn covers 8 bytes of object allocation.  Confusingly, chunks are actually
// stored as uint8_t's; hence it is important to be clear about the naming.
//
// Within a chunk (a uint8_t), the least significant bit (bit 0) tracks the
// lowest address in the resulting layout, and bit 7 tracks the address 7 bytes
// higher.

class BitVector {
 public:
  enum class Result { OOM, OK, Fail };

 private:
  // This controls how many chunks we are prepared to look back in order to
  // find an allocation.  See comments at its use points below.
  static const uint32_t LookbackLimit = 24;
  // See comment on ::addMoreChunks.
  static_assert(LookbackLimit > (2 * 3));
  // For the same reason, the LookbackLimit needs to cover an area at least as
  // large as the worst-case inline capacity.
  static_assert(LookbackLimit > WasmStructObject_MaxInlineBytes_ASSUMED / 8);

 public:
  // The chunks.  Using 64 as the in-line vector size implies that the
  // BitVector covers 512 bytes of object allocation, so it will never allocate
  // (C++) heap when laying out WasmStructObject itself (as that's smaller than
  // 512 bytes) and only rarely when laying out an OOL block.
  mozilla::Vector<uint8_t, 64, SystemAllocPolicy> chunks_;

  // Hash the non-zero chunks in the vector.  The non-zero part means the
  // result is invariant to the number of trailing zero chunks it has.
  uint32_t hashNonZero() const;

  // Returns the the highest index (plus one) tracked in the object being laid
  // out.  For this to be efficient requires that `chunks_` does not have a
  // very long tail of uncommitted chunks, which is (another reason) why
  // `addMoreChunks` is relatively conservative.
  uint32_t totalOffset() const;

  // Add some, but not many, extra chunks to `chunks_`; but at a minimum 3, so
  // as to be able to cover a worst-case maximum-size (16 byte) allocation plus
  // 8 byte alignment hole, that would otherwise fail.  Note it is crucial that
  // we don't add more than the LookbackLimit, since that could cause the logic
  // that uses it in ::findAlignedWRK to not allocate at the start of the OOL
  // area; hence addMoreChunks (somewhat arbitrarily) divides it by two.
  Result addMoreChunks();

  // Initialize `chunks_` so as to hold `chunksTotal` chunks, of which the
  // first `chunksReserved` are unavailable for allocation.
  Result init(uint32_t chunksReserved, uint32_t chunksTotal);

  // Search through the area `chunks_[firstChunk, lastChunkPlus1)` to find a
  // naturally-aligned hole of size `size` (for sizes 1, 2, 4 and 8; and
  // 8-aligned for size 16).  If successful, mark the hole as occupied and
  // return the offset of the lowest byte of the hole.
  Result allocate(uint32_t size, uint32_t firstChunk, uint32_t lastChunkPlus1,
                  uint32_t* offset);

  // Given that `offset` was allocated by a call to `allocate`
  // (requesting size `size`), free up that area.
  void deallocate(uint32_t offset, uint32_t size);
};

//=========================================================================
// FixedSizeBitVector -- helper class for StructLayout

// FixedSizeBitVector is a specialization of BitVector that is used to track
// layout commitments in WasmStructObject itself.

class FixedSizeBitVector : public BitVector {
  uint32_t chunksTotal_ = 0;
  uint32_t chunksReserved_ = 0;

 public:
  // Initialize.  `layoutBytesReserved` is the number of bytes unavailable at
  // the start of the layout.  `layoutBytesTotal` is the maximum total number
  // of bytes in the layout, including the reserved area.
  Result init(uint32_t layoutBytesReserved, uint32_t layoutBytesTotal);

  // Ask for an allocation.  This can fail if a suitable hole can't be found.
  Result allocate(uint32_t size, uint32_t* offset);
};

//=========================================================================
// VariableSizedBitVector -- helper class for StructLayout

// VariableSizeBitVector is a specialization of BitVector that is used to
// track layout commitments in the OOL block.

class VariableSizeBitVector : public BitVector {
  bool used_ = false;

 public:
  // Initialize.
  Result init();

  // Ask for an allocation.  This can never fail because the OOL block can be
  // made arbitrarily large.
  Result allocate(uint32_t size, uint32_t* offset);

  // Ask whether any allocations at all have been made so far.
  bool unused() const;

  // This has the same behaviour as the BitVector version.
  uint32_t totalOffset() const;
};

//=========================================================================
// StructLayout, the top level interface for structure layout machinery.

class StructLayout {
  //---------------- Interface ----------------
 public:
  // Initialises the layouter.  `firstUsableILOffset` is the first allowable
  // payload byte offset in the IL object; offsets prior to that are considered
  // reserved.  `usableILSize` is the maximum allowed size of the IL payload
  // area.
  bool init(uint32_t firstUsableILOffset, uint32_t usableILSize);

  // Add a field of the specified size, and get back its access path, or
  // `false` to indicate OOM.
  bool addField(uint32_t fieldSize, FieldAccessPath* path);

  // Return the total IL object size (including reserved area) so far, rounded
  // up to an integral number of words.
  uint32_t totalSizeIL() const;

  // Returns true iff an OOL area is needed.
  bool hasOOL() const;

  // Returns the total OOL block size so far, rounded up to an integral number
  // of words.  Invalid to call if `!hasOOL()`.
  uint32_t totalSizeOOL() const;

  // Returns the access path in the IL area, to get the OOL pointer.
  // Invalid to call if `!hasOOL()`.
  FieldAccessPath oolPointerPath() const;

  //---------------- Implementation ----------------
 private:
  static const uint32_t InvalidOffset = 0xFFFFFFFF;

  // These change as fields are added:
  // BitVectors for the IL and OOL areas.
  FixedSizeBitVector ilBitVector_;
  VariableSizeBitVector oolBitVector_;
  uint32_t oolptrILO_ = InvalidOffset;
  // The total number of fields processed so far
  uint32_t numFieldsProcessed_ = 0;

  uint32_t hash() const;
};

}  // namespace js::wasm

#endif /* wasm_WasmStructLayout_h */
