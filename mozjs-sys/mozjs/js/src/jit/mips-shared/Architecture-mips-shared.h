/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_mips_shared_Architecture_mips_shared_h
#define jit_mips_shared_Architecture_mips_shared_h

#include <algorithm>
#include <bit>
#include <limits.h>
#include <stdint.h>

#include "jit/shared/Architecture-shared.h"

#include "js/Utility.h"

#if defined(_MIPS_SIM)
#  if (_MIPS_SIM != _ABI64)
#    error "Unsupported ABI"
#  endif
#elif !defined(JS_SIMULATOR_MIPS64)
#  error "Unknown ABI"
#endif

#if (defined(__mips_isa_rev) && (__mips_isa_rev >= 6))
#  define MIPSR6
#endif

namespace js {
namespace jit {

// How far forward/back can a jump go? Provide a generous buffer for thunks.
static const uint32_t JumpImmediateRange = UINT32_MAX;

class Registers {
 public:
  enum RegisterID {
    r0 = 0,
    r1,
    r2,
    r3,
    r4,
    r5,
    r6,
    r7,
    r8,
    r9,
    r10,
    r11,
    r12,
    r13,
    r14,
    r15,
    r16,
    r17,
    r18,
    r19,
    r20,
    r21,
    r22,
    r23,
    r24,
    r25,
    r26,
    r27,
    r28,
    r29,
    r30,
    r31,
    zero = r0,
    at = r1,
    v0 = r2,
    v1 = r3,
    a0 = r4,
    a1 = r5,
    a2 = r6,
    a3 = r7,
    a4 = r8,
    a5 = r9,
    a6 = r10,
    a7 = r11,
    t4 = r12,
    t5 = r13,
    t6 = r14,
    t7 = r15,
    ta0 = a4,
    ta1 = a5,
    ta2 = a6,
    ta3 = a7,
    s0 = r16,
    s1 = r17,
    s2 = r18,
    s3 = r19,
    s4 = r20,
    s5 = r21,
    s6 = r22,
    s7 = r23,
    t8 = r24,
    t9 = r25,
    k0 = r26,
    k1 = r27,
    gp = r28,
    sp = r29,
    fp = r30,
    ra = r31,
    invalid_reg
  };
  typedef uint8_t Code;
  typedef RegisterID Encoding;

  // Content spilled during bailouts.
  union RegisterContent {
    uintptr_t r;
  };

  static const char* const RegNames[];
  static const char* GetName(Code code) {
    MOZ_ASSERT(code < Total);
    return RegNames[code];
  }
  static const char* GetName(Encoding i) { return GetName(Code(i)); }

  static Code FromName(const char* name);

  static const Encoding StackPointer = sp;
  static const Encoding Invalid = invalid_reg;

  static const uint32_t Total = 32;
  static const uint32_t Allocatable;

  typedef uint32_t SetType;
  static const SetType AllMask = 0xffffffff;
  static const SetType SharedArgRegMask =
      (1 << a0) | (1 << a1) | (1 << a2) | (1 << a3);
  static const SetType ArgRegMask;

  static const SetType VolatileMask =
      (1 << Registers::v0) | (1 << Registers::v1) | (1 << Registers::a0) |
      (1 << Registers::a1) | (1 << Registers::a2) | (1 << Registers::a3) |
      (1 << Registers::a4) | (1 << Registers::a5) | (1 << Registers::a6) |
      (1 << Registers::a7) | (1 << Registers::t4) | (1 << Registers::t5) |
      (1 << Registers::t6) | (1 << Registers::t7) | (1 << Registers::t8) |
      (1 << Registers::t9);

  // We use this constant to save registers when entering functions. This
  // is why $ra is added here even though it is not "Non Volatile".
  static const SetType NonVolatileMask =
      (1 << Registers::s0) | (1 << Registers::s1) | (1 << Registers::s2) |
      (1 << Registers::s3) | (1 << Registers::s4) | (1 << Registers::s5) |
      (1 << Registers::s6) | (1 << Registers::s7) | (1 << Registers::fp) |
      (1 << Registers::ra);

  static const SetType WrapperMask = VolatileMask |          // = arguments
                                     (1 << Registers::t4) |  // = outReg
                                     (1 << Registers::t5);   // = argBase

  static const SetType NonAllocatableMask =
      (1 << Registers::zero) | (1 << Registers::at) |  // at = scratch
      (1 << Registers::t8) |                           // t8 = scratch
      (1 << Registers::t9) |                           // t9 = scratch or call
      (1 << Registers::k0) | (1 << Registers::k1) | (1 << Registers::gp) |
      (1 << Registers::sp) | (1 << Registers::ra) | (1 << Registers::fp);

  // Registers returned from a JS -> JS call.
  static const SetType JSCallMask;

  // Registers returned from a JS -> C call.
  static const SetType SharedCallMask = (1 << Registers::v0);
  static const SetType CallMask;

  static const SetType AllocatableMask = AllMask & ~NonAllocatableMask;

  static uint32_t SetSize(SetType x) { return std::popcount(x); }
  static uint32_t FirstBit(SetType x) {
    MOZ_ASSERT(x);
    return std::countr_zero(x);
  }
  static uint32_t LastBit(SetType x) {
    MOZ_ASSERT(x);
    return std::bit_width(x) - 1;
  }
};

// Smallest integer type that can hold a register bitmask.
typedef uint32_t PackedRegisterMask;

class FloatRegistersMIPSShared {
 public:
  enum FPRegisterID {
    f0 = 0,
    f1,
    f2,
    f3,
    f4,
    f5,
    f6,
    f7,
    f8,
    f9,
    f10,
    f11,
    f12,
    f13,
    f14,
    f15,
    f16,
    f17,
    f18,
    f19,
    f20,
    f21,
    f22,
    f23,
    f24,
    f25,
    f26,
    f27,
    f28,
    f29,
    f30,
    f31,
    invalid_freg
  };
  typedef uint32_t Code;
  typedef FPRegisterID Encoding;

  // Content spilled during bailouts.
  union RegisterContent {
    double d;
  };

  static const char* GetName(Encoding code) {
    static const char* const Names[] = {
        "f0",  "f1",  "f2",  "f3",  "f4",  "f5",  "f6",  "f7",
        "f8",  "f9",  "f10", "f11", "f12", "f13", "f14", "f15",
        "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
        "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31"};
    return Names[code];
  }

  static const Encoding Invalid = invalid_freg;

  typedef uint64_t SetType;
};

static const uint32_t SpillSlotSize =
    std::max(sizeof(Registers::RegisterContent),
             sizeof(FloatRegistersMIPSShared::RegisterContent));

template <typename T>
class TypedRegisterSet;

class FloatRegisterMIPSShared {
 public:
  bool isSimd128() const { return false; }

  typedef FloatRegistersMIPSShared::SetType SetType;

  static uint32_t SetSize(SetType x) { return std::popcount(x); }
  static uint32_t FirstBit(SetType x) {
    MOZ_ASSERT(x);
    return std::countr_zero(x);
  }
  static uint32_t LastBit(SetType x) {
    MOZ_ASSERT(x);
    return std::bit_width(x) - 1;
  }
};

class MIPSFlags final {
  static inline bool initialized = false;

  static inline uint32_t flags = 0;
  static inline bool hasFPU = false;
  static inline bool hasR2 = false;
  static inline bool isLoongson = false;

 public:
  MIPSFlags() = delete;

  // MIPSFlags::Init is called from the JitContext constructor to read the
  // hardware flags. This method must only be called exactly once.
  static void Init();

  static bool IsInitialized() { return initialized; }

  static uint32_t GetFlags() {
    MOZ_ASSERT(IsInitialized());
    return flags;
  }

  static bool HasFPU() { return hasFPU; }
  static bool HasR2() { return hasR2; }
  static bool IsLoongson() { return isLoongson; }
};

inline uint32_t GetMIPSFlags() { return MIPSFlags::GetFlags(); }
inline bool hasFPU() { return MIPSFlags::HasFPU(); }
inline bool isLoongson() { return MIPSFlags::IsLoongson(); }
inline bool hasR2() { return MIPSFlags::HasR2(); }

// MIPS doesn't have double registers that can NOT be treated as float32.
inline bool hasUnaliasedDouble() { return false; }

// MIPS64 doesn't support it.
inline bool hasMultiAlias() { return false; }

}  // namespace jit
}  // namespace js

#endif /* jit_mips_shared_Architecture_mips_shared_h */
