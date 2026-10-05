/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "wasm/WasmStructLayout.h"

#include "mozilla/DebugOnly.h"
#include "mozilla/HashFunctions.h"

#include "jstypes.h"  // RoundUp

// This is a simple implementation of a layouter.  It places the OOL pointer
// at the start of the IL payload area, regardless of whether an OOL area is
// actually necessary.

namespace js::wasm {

//=========================================================================
// BitVector

// See comment in WasmStructLayout.h for meaning of "byte", "offset" and
// "chunk".

#ifdef DEBUG
static bool Is8Aligned(uint32_t n) { return (n & 7) == 0; }

static bool IsWordAligned(uintptr_t x) { return (x % sizeof(void*)) == 0; }
#endif

static uint32_t IndexOfLeastSignificantZeroBit(uint8_t n) {
  for (uint32_t i = 0; i < 8; i++) {
    if (((n >> i) & 1) == 0) {
      return i;
    }
  }
  MOZ_CRASH();
}
static uint32_t IndexOfLeastSignificantZero2Bits(uint8_t n) {
  for (uint32_t i = 0; i < 8; i += 2) {
    if (((n >> i) & 3) == 0) {
      return i;
    }
  }
  MOZ_CRASH();
}
static uint32_t IndexOfLeastSignificantZero4Bits(uint8_t n) {
  for (uint32_t i = 0; i < 8; i += 4) {
    if (((n >> i) & 0xF) == 0) {
      return i;
    }
  }
  MOZ_CRASH();
}

static uint32_t IndexOfMostSignificantOneBit(uint8_t n) {
  for (int32_t i = 7; i >= 0; i--) {
    if (((n >> i) & 1) == 1) {
      return uint32_t(i);
    }
  }
  MOZ_CRASH();
}

#ifdef DEBUG
static uint32_t OffsetToChunkNumber(uint32_t offset) { return offset / 8; }
#endif

uint32_t BitVector::hashNonZero() const {
  mozilla::HashNumber hash(42);
  for (uint8_t b : chunks_) {
    if (b != 0) {
      hash = mozilla::AddToHash(hash, b);
    }
  }
  return uint32_t(hash);
}

uint32_t BitVector::totalOffset() const {
  if (chunks_.empty()) {
    return 0;
  }
  // Find the highest non-zero chunk.
  size_t i;
  for (i = chunks_.length(); i >= 1; i--) {
    if (chunks_[i - 1] != 0) {
      break;
    }
  }
  if (i == 0) {
    // There are chunks, but none got used.
    return 0;
  }
  i--;
  MOZ_ASSERT(i < chunks_.length());
  return 8 * uint32_t(i) + IndexOfMostSignificantOneBit(chunks_[i]) + 1;
}

BitVector::Result BitVector::addMoreChunks() {
  for (uint32_t i = 0; i < LookbackLimit / 2; i++) {
    if (!chunks_.append(0)) {
      return Result::OOM;
    }
  }
  return Result::OK;
}

BitVector::Result BitVector::init(uint32_t chunksReserved,
                                  uint32_t chunksTotal) {
  MOZ_ASSERT_IF(chunksReserved > 0, chunksReserved < chunksTotal);
  if (!chunks_.resize(chunksTotal)) {
    return Result::OOM;
  }
  for (uint32_t i = 0; i < chunksReserved; i++) {
    chunks_[i] = 0xFF;
  }
  for (uint32_t i = chunksReserved; i < chunksTotal; i++) {
    chunks_[i] = 0;
  }
  return Result::OK;
}

BitVector::Result BitVector::allocate(uint32_t size, uint32_t firstChunk,
                                      uint32_t lastChunkPlus1,
                                      uint32_t* offset) {
  MOZ_ASSERT(firstChunk < lastChunkPlus1);
  MOZ_ASSERT(lastChunkPlus1 <= chunks_.length());

  // We don't want to re-scan the entire vector every search; that's
  // expensive (quadratic).  Instead just re-scan the last 24 chunks and
  // accept that we'll miss out on the opportunity to use alignment holes
  // more than 192 bytes back from the current "fill point" for the struct.
  if (lastChunkPlus1 - firstChunk > LookbackLimit) {
    firstChunk = lastChunkPlus1 - LookbackLimit;
  }

  // These are arranged in order of conceptually-simplest first.
  switch (size) {
    case 8: {
      // Any chunk that is zero will do.
      for (uint32_t i = firstChunk; i < lastChunkPlus1; i++) {
        if (chunks_[i] == 0) {
          *offset = i * 8;
          chunks_[i] = 0xFF;
          return Result::OK;
        }
      }
      break;
    }
    case 16: {
      // Any chunk-pair that is zero will do.  Note this 8-aligns 16-byte
      // requests, but we can't avoid that because the underlying JS heap
      // allocator only provides 8-aligned addresses anyway.
      for (uint32_t i = firstChunk + 1; i < lastChunkPlus1; i++) {
        if (chunks_[i - 1] == 0 && chunks_[i] == 0) {
          *offset = (i - 1) * 8;
          chunks_[i - 1] = 0xFF;
          chunks_[i] = 0xFF;
          return Result::OK;
        }
      }
      break;
    }
    // The 4, 2 and 1-byte cases are the most complex.  We have to find a
    // single chunk with that many consecutive, aligned bits, as zero.
    case 1: {
      // Any chunk that has an unset bit is fine.
      for (uint32_t i = firstChunk; i < lastChunkPlus1; i++) {
        if (chunks_[i] != 0xFF) {
          uint32_t bitShift = IndexOfLeastSignificantZeroBit(chunks_[i]);
          *offset = i * 8 + bitShift;
          chunks_[i] |= (1 << bitShift);
          return Result::OK;
        }
      }
      break;
    }
    case 4: {
      // Find a chunk in which either the upper or lower half is zero.
      for (uint32_t i = firstChunk; i < lastChunkPlus1; i++) {
        if ((chunks_[i] & (0xF << 0)) == 0 || (chunks_[i] & (0xF << 4)) == 0) {
          uint32_t bitShift = IndexOfLeastSignificantZero4Bits(chunks_[i]);
          *offset = i * 8 + bitShift;
          chunks_[i] |= (0x0F << bitShift);
          return Result::OK;
        }
      }
      break;
    }
    case 2: {
      // Find a chunk in which an adjacent bit-pair is zero.
      for (uint32_t i = firstChunk; i < lastChunkPlus1; i++) {
        if ((chunks_[i] & (3 << 0)) == 0 || (chunks_[i] & (3 << 2)) == 0 ||
            (chunks_[i] & (3 << 4)) == 0 || (chunks_[i] & (3 << 6)) == 0) {
          uint32_t bitShift = IndexOfLeastSignificantZero2Bits(chunks_[i]);
          *offset = i * 8 + bitShift;
          chunks_[i] |= (3 << bitShift);
          return Result::OK;
        }
      }
      break;
    }
    default: {
      MOZ_CRASH();
    }
  }
  return Result::Fail;
}

// Given that `offset` was allocated by a call to `allocate`
// (requesting size `size`), free up that area.
void BitVector::deallocate(uint32_t offset, uint32_t size) {
  MOZ_ASSERT(OffsetToChunkNumber(offset + size - 1) < chunks_.length());
  switch (size) {
    case 8: {
      MOZ_ASSERT((offset % 8) == 0);
      uint32_t chunk = offset / 8;
      MOZ_ASSERT(chunks_[chunk] == 0xFF);
      chunks_[chunk] = 0;
      break;
    }
    case 16: {
      MOZ_ASSERT((offset % 8) == 0);  // re 8, see comment on ::allocate
      uint32_t chunk = offset / 8;
      MOZ_ASSERT(chunk + 1 < chunks_.length());
      MOZ_ASSERT(chunks_[chunk] == 0xFF);
      MOZ_ASSERT(chunks_[chunk + 1] == 0xFF);
      chunks_[chunk] = 0;
      chunks_[chunk + 1] = 0;
      break;
    }
    case 1: {
      uint32_t chunk = offset / 8;
      uint32_t shift = offset % 8;  // 0, 1, 2, 3, 4, 5, 6 or 7
      uint8_t mask = 1 << shift;
      MOZ_ASSERT((chunks_[chunk] & mask) == mask);
      chunks_[chunk] &= ~mask;
      break;
    }
    case 4: {
      MOZ_ASSERT((offset % 4) == 0);
      uint32_t chunk = offset / 8;
      uint32_t shift = offset % 8;  // 0 or 4
      uint8_t mask = 0xF << shift;
      MOZ_ASSERT((chunks_[chunk] & mask) == mask);
      chunks_[chunk] &= ~mask;
      break;
    }
    case 2: {
      MOZ_ASSERT((offset % 2) == 0);
      uint32_t chunk = offset / 8;
      uint32_t shift = offset % 8;  // 0, 2, 4 or 6
      uint8_t mask = 0x3 << shift;
      MOZ_ASSERT((chunks_[chunk] & mask) == mask);
      chunks_[chunk] &= ~mask;
      break;
    }
    default: {
      MOZ_CRASH();
    }
  }
}

//=========================================================================
// FixedSizeBitVector

BitVector::Result FixedSizeBitVector::init(uint32_t layoutBytesReserved,
                                           uint32_t layoutBytesTotal) {
  MOZ_ASSERT(layoutBytesTotal > 0);
  MOZ_ASSERT(layoutBytesReserved < layoutBytesTotal);
  MOZ_ASSERT(Is8Aligned(layoutBytesReserved));
  MOZ_ASSERT(Is8Aligned(layoutBytesTotal));
  chunksReserved_ = layoutBytesReserved / 8;
  chunksTotal_ = layoutBytesTotal / 8;
  return BitVector::init(chunksReserved_, chunksTotal_);
}

BitVector::Result FixedSizeBitVector::allocate(uint32_t size,
                                               uint32_t* offset) {
  return BitVector::allocate(size, chunksReserved_, chunksTotal_, offset);
}

//=========================================================================
// VariableSizeBitVector

BitVector::Result VariableSizeBitVector::init() {
  // This initial size of 1 is important in that it needs to be less than
  // ::LookbackLimit.
  return BitVector::init(0, 1 /*see ::unused()*/);
}

BitVector::Result VariableSizeBitVector::allocate(uint32_t size,
                                                  uint32_t* offset) {
  // First, try to find it given the chunks we already have.
  Result res = BitVector::allocate(size, 0, chunks_.length(), offset);
  if (res == Result::OOM) {
    return Result::OOM;
  }
  if (res == Result::OK) {
    used_ = true;
    return res;
  }
  // That failed, so add some more (uncommitted) chunks on the end of `chunks_`
  // and try again.  This second attempt *must* succeed since we can make the
  // OOL block arbitrarily large.
  res = addMoreChunks();
  if (res == Result::OOM) {
    return Result::OOM;
  }
  res = BitVector::allocate(size, 0, chunks_.length(), offset);
  if (res == Result::OOM) {
    return Result::OOM;
  }
  MOZ_RELEASE_ASSERT(res == Result::OK);
  used_ = true;
  return Result::OK;
}

bool VariableSizeBitVector::unused() const { return !used_; }

uint32_t VariableSizeBitVector::totalOffset() const {
  uint32_t res = BitVector::totalOffset();
  MOZ_ASSERT(used_ == (res > 0));
  return res;
}

//=========================================================================
// StructLayout

bool StructLayout::init(uint32_t firstUsableILOffset, uint32_t usableILSize) {
  // Not actually necessary, but it would be strange if this wasn't so.
  MOZ_ASSERT(IsWordAligned(firstUsableILOffset));
  MOZ_ASSERT(IsWordAligned(usableILSize));
  // Must have at least enough space to hold the OOL pointer
  MOZ_ASSERT(usableILSize >= sizeof(void*));
  oolptrILO_ = InvalidOffset;
  BitVector::Result res = ilBitVector_.init(firstUsableILOffset,
                                            firstUsableILOffset + usableILSize);
  if (res == BitVector::Result::OOM) {
    return false;
  }
  res = oolBitVector_.init();
  if (res == BitVector::Result::OOM) {
    return false;
  }
  return true;
}

// Add a field of the specified size, and get back its access path.  The two
// release assertions together guarantee that the maximum offset that could be
// generated is roughly `16 * js::wasm::MaxStructFields`, so there is no need
// to use checked integers in the layout computations.

bool StructLayout::addField(uint32_t fieldSize, FieldAccessPath* path) {
  MOZ_ASSERT(fieldSize == 16 || fieldSize == 8 || fieldSize == 4 ||
             fieldSize == 2 || fieldSize == 1);
  // Guard against field-offset overflow.
  numFieldsProcessed_++;
  MOZ_RELEASE_ASSERT(numFieldsProcessed_ <= js::wasm::MaxStructFields);
  MOZ_RELEASE_ASSERT(fieldSize <= 16);

  *path = FieldAccessPath();

  // This is complex.  In between calls to ::addField, we maintain the
  // following invariant:
  //
  // (0) If the OOL area is not in use, then it is possible to allocate the OOL
  //     pointer in the IL area.
  //
  // With that in place, the code below deals with 4 cases:
  //
  // (1) The OOL area is unused, and both the field and a dummy OOL pointer fit
  //     into the IL area.  Allocate the field IL and leave the OOL area
  //     unused.  Because we just established that a dummy OOL pointer fits in
  //     IL after the field, and because of (N) below, (0) is true after the
  //     call.
  //
  // (2) The OOL area is unused, but the field and dummy OOL pointer don't both
  //     fit in the IL area.  We need to bring the OOL area into use.  Allocate
  //     the OOL pointer in the IL area (which due to (0) cannot fail), and
  //     allocate the field in the OOL area.  This is a one-time transitional
  //     case that separates multiple occurrences of (1) from multiple
  //     occurrences of (3) and (4).  This means the OOL area is now in use, so
  //     (0) is trivially true after the call.
  //
  // (3) The OOL area is in use, but the field fits in the IL area anyways,
  //     presumably because it falls into an alignment hole in the IL area.
  //     Just allocate it IL and leave everything else unchanged.  Since the
  //     OOL area is in use, (0) is trivially true after the call.
  //
  // (4) The OOL area is in use, and the field doesn't fit in the IL area.
  //     Allocate it in the OOL area.  Since the OOL area is in use, (0) is
  //     trivially true after the call.
  //
  // (N) Note: for (1) and (2) it is important to try for the allocation of the
  //     field first and the dummy OOL pointer second.

  // For cases (1) and (2) we need to back out tentative allocations.  Hash the
  // current state so we can later assert it is unchanged after backouts.
  mozilla::DebugOnly<uint32_t> initialHash = hash();

  // These need to agree.
  MOZ_ASSERT(oolBitVector_.unused() == (oolptrILO_ == InvalidOffset));

  // Try for Case (1)
  if (oolBitVector_.unused()) {
    uint32_t fieldOffset = InvalidOffset;
    BitVector::Result res = ilBitVector_.allocate(fieldSize, &fieldOffset);
    if (res == BitVector::Result::OOM) {
      return false;
    }
    // The field fits, now try for the dummy OOL pointer
    mozilla::DebugOnly<uint32_t> hash2 = hash();
    if (res == BitVector::Result::OK) {
      uint32_t dummyOffset = InvalidOffset;
      res = ilBitVector_.allocate(sizeof(void*), &dummyOffset);
      if (res == BitVector::Result::OOM) {
        return false;
      }
      if (res == BitVector::Result::OK) {
        // Case (1) established -- they both fit.
        // Back out the dummy OOL pointer allocation, and we're done.
        MOZ_ASSERT(fieldOffset != dummyOffset);
        ilBitVector_.deallocate(dummyOffset, sizeof(void*));
        MOZ_ASSERT(hash() == hash2);
        *path = FieldAccessPath(fieldOffset);
        return true;
      }
      // The field fits, but the OOL pointer doesn't.  Back out the field
      // allocation, so that we have changed nothing.
      ilBitVector_.deallocate(fieldOffset, fieldSize);
    }
  }

  // "state is unchanged from when we started"
  MOZ_ASSERT(hash() == initialHash);
  MOZ_ASSERT(oolBitVector_.unused() == (oolptrILO_ == InvalidOffset));

  // Try for Case (2)
  if (oolBitVector_.unused()) {
    // We need to bring the OOL area into use.  First, try to allocate the OOL
    // pointer field.  This must succeed (apart from OOMing) because of (1).
    uint32_t oolptrOffset = InvalidOffset;
    BitVector::Result res = ilBitVector_.allocate(sizeof(void*), &oolptrOffset);
    if (res == BitVector::Result::OOM) {
      return false;
    }
    MOZ_ASSERT(res == BitVector::Result::OK);
    // Case (2) established
    oolptrILO_ = oolptrOffset;
    // Allocate the field in the OOL area; it is the first item there.
    uint32_t fieldOffset = InvalidOffset;
    res = oolBitVector_.allocate(fieldSize, &fieldOffset);
    if (res == BitVector::Result::OOM) {
      return false;
    }
    // Allocation in the OOL area can't fail.
    MOZ_RELEASE_ASSERT(res == BitVector::Result::OK);
    MOZ_ASSERT(!oolBitVector_.unused());
    // We expect this because this is the first item in the OOL area.
    MOZ_ASSERT(fieldOffset == 0);
    *path = FieldAccessPath(oolptrILO_, fieldOffset);
    return true;
  }

  // "state is unchanged from when we started"
  MOZ_ASSERT(hash() == initialHash);
  MOZ_ASSERT(!oolBitVector_.unused() && oolptrILO_ != InvalidOffset);

  // Cases (3) and (4).  In both cases, the OOL area is in use.
  // Re-try allocating the field IL.  Note this is not redundant w.r.t. the
  // logic above, since that involved trying to allocate both the field and
  // the dummy OOL pointer; this only tries to allocate the field.
  uint32_t fieldOffset = InvalidOffset;
  BitVector::Result res = ilBitVector_.allocate(fieldSize, &fieldOffset);
  if (res == BitVector::Result::OOM) {
    return false;
  }
  if (res == BitVector::Result::OK) {
    // Case (3) established
    *path = FieldAccessPath(fieldOffset);
    return true;
  }
  // Case (4) established
  fieldOffset = InvalidOffset;
  res = oolBitVector_.allocate(fieldSize, &fieldOffset);
  if (res == BitVector::Result::OOM) {
    return false;
  }
  // Allocation in the OOL area can't fail.
  MOZ_RELEASE_ASSERT(res == BitVector::Result::OK);
  *path = FieldAccessPath(oolptrILO_, fieldOffset);
  return true;
}

uint32_t StructLayout::hash() const {
  uint32_t h = ilBitVector_.hashNonZero();
  h = (h << 16) | (h >> 16);
  h ^= oolBitVector_.hashNonZero();
  return h;
}

uint32_t StructLayout::totalSizeIL() const {
  return js::RoundUp(ilBitVector_.totalOffset(), sizeof(void*));
}

bool StructLayout::hasOOL() const { return !oolBitVector_.unused(); }

uint32_t StructLayout::totalSizeOOL() const {
  MOZ_ASSERT(hasOOL());
  MOZ_ASSERT(oolptrILO_ != InvalidOffset);
  return js::RoundUp(oolBitVector_.totalOffset(), sizeof(void*));
}

FieldAccessPath StructLayout::oolPointerPath() const {
  MOZ_ASSERT(hasOOL());
  MOZ_ASSERT(oolptrILO_ != InvalidOffset);
  return FieldAccessPath(oolptrILO_);
}

}  // namespace js::wasm
