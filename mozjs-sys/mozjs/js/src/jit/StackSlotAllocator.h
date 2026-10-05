/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_StackSlotAllocator_h
#define jit_StackSlotAllocator_h

#include "jit/LIR.h"
#include "jit/Registers.h"

namespace js {
namespace jit {

class StackSlotAllocator {
  js::Vector<uint32_t, 4, SystemAllocPolicy> normalSlots;
  js::Vector<uint32_t, 4, SystemAllocPolicy> doubleSlots;
  js::Vector<uint32_t, 4, SystemAllocPolicy> quadSlots;
  uint32_t height_;

  [[nodiscard]] bool incrementHeight(uint32_t amount) {
    // See MaxBytes for why we don't need to check for overflow here.
    if (amount > MaxBytes || height_ + amount > MaxBytes) {
      return false;
    }
    height_ += amount;
    return true;
  }

  void freeSlot(uint32_t offset) { (void)normalSlots.append(offset); }
  void freeDoubleSlot(uint32_t offset) { (void)doubleSlots.append(offset); }
  void freeQuadSlot(uint32_t offset) { (void)quadSlots.append(offset); }

  [[nodiscard]] bool allocateSlot(uint32_t* slotOffset) {
    // Re-use a normal slot if we have one.
    if (!normalSlots.empty()) {
      *slotOffset = normalSlots.popCopy();
      return true;
    }

    // Otherwise split a double slot in half if we can.
    if (!doubleSlots.empty()) {
      uint32_t doubleSlotOffset = doubleSlots.popCopy();
      freeSlot(doubleSlotOffset - 4);
      *slotOffset = doubleSlotOffset;
      return true;
    }

    // Or else grow the stack by a word.
    if (!incrementHeight(4)) {
      return false;
    }
    *slotOffset = height_;
    return true;
  }

  [[nodiscard]] bool allocateDoubleSlot(uint32_t* slotOffset) {
    if (!doubleSlots.empty()) {
      *slotOffset = doubleSlots.popCopy();
      return true;
    }

    // Grow the stack, freeing the padding word for reuse if we need to align.
    if (height_ % 8 != 0) {
      if (!incrementHeight(4)) {
        return false;
      }
      freeSlot(height_);
    }
    if (!incrementHeight(8)) {
      return false;
    }
    *slotOffset = height_;
    return true;
  }

  [[nodiscard]] bool allocateQuadSlot(uint32_t* slotOffset) {
    if (!quadSlots.empty()) {
      *slotOffset = quadSlots.popCopy();
      return true;
    }

    // Grow the stack, freeing any padding for reuse if we need to align. This
    // relies on the fact that any architecture specific alignment of the stack
    // pointer is done a priori.
    if (height_ % 8 != 0) {
      if (!incrementHeight(4)) {
        return false;
      }
      freeSlot(height_);
    }
    if (height_ % 16 != 0) {
      if (!incrementHeight(8)) {
        return false;
      }
      freeDoubleSlot(height_);
    }
    if (!incrementHeight(16)) {
      return false;
    }
    *slotOffset = height_;
    return true;
  }

 public:
  StackSlotAllocator() : height_(0) {}

  // The maximum size we allow. This is a conservative value, even values this
  // low are very likely to hit stack overflows.
  static constexpr size_t MaxBytes = 2 * 1024 * 1024;
  // Check that as long as we keep height_ within MaxBytes and only add by up
  // to MaxBytes, that we cannot overflow. This is used in several places.
  static_assert(uint64_t(MaxBytes) + uint64_t(MaxBytes) <= UINT32_MAX);
  // Any offset we provide can be stored in LStackSlot.
  static_assert(MaxBytes <= LStackSlot::MAX_SLOT);

  [[nodiscard]] bool allocateStackArea(LStackArea* alloc) {
    uint32_t size = alloc->size();

    // Check that size is within MaxBytes so that we don't need to check for
    // overflow below when adding to height_.
    if (size > MaxBytes) {
      return false;
    }

    MOZ_ASSERT(size % 4 == 0);
    switch (alloc->alignment()) {
      case 8: {
        // Grow the stack, freeing the padding word for reuse if we need to
        // align.
        if ((height_ + size) % 8 != 0) {
          if (!incrementHeight(4)) {
            return false;
          }
          freeSlot(height_);
        }
        break;
      }
      default:
        MOZ_CRASH("unexpected stack results area alignment");
    }

    // See MaxBytes for why we don't need to check for overflow here.
    uint32_t areaSlotOffset = height_ + size;
    if (areaSlotOffset > MaxBytes) {
      return false;
    }
    MOZ_ASSERT(areaSlotOffset % alloc->alignment() == 0);

    alloc->setBase(areaSlotOffset);
    height_ = areaSlotOffset;
    return true;
  }

  [[nodiscard]] bool allocateSlot(LStackSlot::Width width,
                                  uint32_t* slotOffset) {
    switch (width) {
      case LStackSlot::Word:
        return allocateSlot(slotOffset);
      case LStackSlot::DoubleWord:
        return allocateDoubleSlot(slotOffset);
      case LStackSlot::QuadWord:
        return allocateQuadSlot(slotOffset);
    }
    MOZ_CRASH("Unknown slot width");
  }

  // This method is used by the Simple allocator to free stack slots so that
  // they can be reused. The Backtracking allocator doesn't call this.
  void freeSlot(LStackSlot::Width width, uint32_t slotOffset) {
    switch (width) {
      case LStackSlot::Word:
        freeSlot(slotOffset);
        return;
      case LStackSlot::DoubleWord:
        freeDoubleSlot(slotOffset);
        return;
      case LStackSlot::QuadWord:
        freeQuadSlot(slotOffset);
        return;
    }
    MOZ_CRASH("Unknown slot width");
  }

  uint32_t stackHeight() const { return height_; }
};

}  // namespace jit
}  // namespace js

#endif /* jit_StackSlotAllocator_h */
