/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_shared_IonAssemblerBuffer_h
#define jit_shared_IonAssemblerBuffer_h

#include "mozilla/Assertions.h"
#include "mozilla/Vector.h"

#include <bit>
#include <compare>  // std::strong_ordering

#include "jit/ProcessExecutableMemory.h"
#include "jit/shared/Assembler-shared.h"

namespace js {
namespace jit {

// The offset into a buffer, in bytes.
class BufferOffset {
  int offset;

 public:
  friend BufferOffset nextOffset();

  constexpr BufferOffset() : offset(INT_MIN) {}

  explicit BufferOffset(int offset_) : offset(offset_) {
    MOZ_ASSERT(offset >= 0);
  }

  explicit BufferOffset(Label* l) : offset(l->offset()) {
    MOZ_ASSERT(offset >= 0);
  }

  int getOffset() const { return offset; }
  bool assigned() const { return offset != INT_MIN; }

  // A BOffImm is a Branch Offset Immediate. It is an architecture-specific
  // structure that holds the immediate for a pc relative branch. diffB takes
  // the label for the destination of the branch, and encodes the immediate
  // for the branch. This will need to be fixed up later, since A pool may be
  // inserted between the branch and its destination.
  template <class BOffImm>
  BOffImm diffB(BufferOffset other) const {
    if (!BOffImm::IsInRange(offset - other.offset)) {
      return BOffImm();
    }
    return BOffImm(offset - other.offset);
  }

  template <class BOffImm>
  BOffImm diffB(Label* other) const {
    MOZ_ASSERT(other->bound());
    if (!BOffImm::IsInRange(offset - other->offset())) {
      return BOffImm();
    }
    return BOffImm(offset - other->offset());
  }

  constexpr auto operator<=>(const BufferOffset& other) const = default;
};

template <class Inst>
class AssemblerBuffer {
 protected:
  mozilla::Vector<uint8_t, 256, SystemAllocPolicy> buffer_;

  bool m_oom;

  // How many bytes can be in the buffer.  Normally this is
  // MaxCodeBytesPerBuffer, but for pasteup buffers where we handle far jumps
  // explicitly it can be larger.
  uint32_t maxSize;

  LifoAlloc lifoAlloc_;

 public:
  explicit AssemblerBuffer()
      : m_oom(false),
        maxSize(MaxCodeBytesPerBuffer),
        lifoAlloc_(8192, js::BackgroundMallocArena) {}

 public:
  bool isAligned(size_t alignment) const {
    MOZ_ASSERT(std::has_single_bit(alignment));
    return !(size() & (alignment - 1));
  }

  void setUnlimited() { maxSize = MaxCodeBytesPerProcess; }

 public:
  bool ensureSpace(size_t numBytes) {
    if (MOZ_UNLIKELY(uint64_t(buffer_.length()) + numBytes > maxSize)) {
      return fail_oom();
    }

    if (MOZ_UNLIKELY(!buffer_.reserve(buffer_.length() + numBytes))) {
      return fail_oom();
    }

    return true;
  }

  BufferOffset putByte(uint8_t value) {
    return putBytes(sizeof(value), &value);
  }

  BufferOffset putShort(uint16_t value) {
    return putBytes(sizeof(value), &value);
  }

  BufferOffset putInt(uint32_t value) {
    return putBytes(sizeof(value), &value);
  }

  MOZ_ALWAYS_INLINE
  BufferOffset putU32Aligned(uint32_t value) {
    // On some platforms, we can generate faster stores if we
    // guarantee that the write is aligned.
    if (!ensureSpace(sizeof(value))) {
      return BufferOffset();
    }
    BufferOffset ret = nextOffset();
    size_t pos = buffer_.length();
    MOZ_ASSERT((pos & 3) == 0);
    buffer_.infallibleGrowByUninitialized(sizeof(value));
    *reinterpret_cast<uint32_t*>(&data()[pos]) = value;
    return ret;
  }

  BufferOffset putBytes(size_t numBytes, const void* inst) {
    if (!ensureSpace(numBytes)) {
      return BufferOffset();
    }

    BufferOffset ret = nextOffset();
    if (inst) {
      buffer_.infallibleAppend(static_cast<const uint8_t*>(inst), numBytes);
    } else {
      buffer_.infallibleGrowByUninitialized(numBytes);
    }
    return ret;
  }

  unsigned int size() const { return buffer_.length(); }
  BufferOffset nextOffset() const { return BufferOffset(size()); }

  bool oom() const { return m_oom; }

  bool fail_oom() {
    m_oom = true;
#ifdef DEBUG
    JitContext* context = MaybeGetJitContext();
    if (context) {
      context->setOOM();
    }
#endif
    return false;
  }

 public:
  Inst* getInstOrNull(BufferOffset off) {
    if (!off.assigned()) {
      return nullptr;
    }
    return getInst(off);
  }

  Inst* getInst(BufferOffset off) {
    const int offset = off.getOffset();
    MOZ_ASSERT(off.assigned() && offset >= 0 && unsigned(offset) < size());
    return (Inst*)&buffer_[offset];
  }

  uint8_t* data() { return buffer_.begin(); }
  const uint8_t* data() const { return buffer_.begin(); }

  using ThisClass = AssemblerBuffer<Inst>;

  class AssemblerBufferInstIterator {
    BufferOffset bo_;
    ThisClass* buffer_;

   public:
    explicit AssemblerBufferInstIterator(BufferOffset bo, ThisClass* buffer)
        : bo_(bo), buffer_(buffer) {}
    void advance(int offset) { bo_ = BufferOffset(bo_.getOffset() + offset); }
    Inst* next() {
      advance(cur()->size());
      return cur();
    }
    Inst* peek() {
      return buffer_->getInst(BufferOffset(bo_.getOffset() + cur()->size()));
    }
    Inst* cur() const { return buffer_->getInst(bo_); }
  };
};

}  // namespace jit
}  // namespace js

#endif  // jit_shared_IonAssemblerBuffer_h
