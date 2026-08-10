/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Copyright (c) 1994-2006 Sun Microsystems Inc.
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// - Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// - Redistribution in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// - Neither the name of Sun Microsystems or the names of contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2021 the V8 project authors. All rights reserved.
#include "jit/riscv64/Assembler-riscv64.h"

#include "mozilla/DebugOnly.h"
#include "mozilla/Maybe.h"

#include "gc/Marking.h"
#include "jit/AutoWritableJitCode.h"
#include "jit/riscv64/base/Integer.h"
#include "jit/riscv64/disasm/Disasm-riscv64.h"

using mozilla::DebugOnly;
namespace js {
namespace jit {

#define UNIMPLEMENTED_RISCV() MOZ_CRASH("RISC_V not implemented");

bool Assembler::FLAG_riscv_debug = false;

// Size of the instruction stream, in bytes.
size_t Assembler::size() const { return m_buffer.size(); }

bool Assembler::swapBuffer(wasm::Bytes& bytes) {
  // For now, specialize to the one use case. As long as wasm::Bytes is a
  // Vector, not a linked-list of chunks, there's not much we can do other
  // than copy.
  MOZ_ASSERT(bytes.empty());
  if (!bytes.resize(bytesNeeded())) {
    return false;
  }
  m_buffer.executableCopy(bytes.begin());
  return true;
}

// Size of the relocation table, in bytes.
size_t Assembler::jumpRelocationTableBytes() const {
  return jumpRelocations_.length();
}

size_t Assembler::dataRelocationTableBytes() const {
  return dataRelocations_.length();
}
// Size of the data table, in bytes.
size_t Assembler::bytesNeeded() const {
  return size() + jumpRelocationTableBytes() + dataRelocationTableBytes();
}

void Assembler::executableCopy(uint8_t* buffer) {
  MOZ_ASSERT(isFinished);
  m_buffer.executableCopy(buffer);
}

uint32_t Assembler::AsmPoolMaxOffset = 1024;

uint32_t Assembler::GetPoolMaxOffset() {
  static bool isSet = false;
  if (!isSet) {
    char* poolMaxOffsetStr = getenv("ASM_POOL_MAX_OFFSET");
    uint32_t poolMaxOffset;
    if (poolMaxOffsetStr &&
        sscanf(poolMaxOffsetStr, "%u", &poolMaxOffset) == 1) {
      AsmPoolMaxOffset = poolMaxOffset;
    }
    isSet = true;
  }
  return AsmPoolMaxOffset;
}

// Pool callbacks stuff:
void Assembler::InsertIndexIntoTag(uint8_t* load_, uint32_t index) {
  MOZ_CRASH("Unimplement");
}

void Assembler::PatchConstantPoolLoad(void* loadAddr, void* constPoolAddr) {
  MOZ_CRASH("Unimplement");
}

void Assembler::processCodeLabels(uint8_t* rawCode) {
  for (const CodeLabel& label : codeLabels_) {
    Bind(rawCode, label);
  }
}

void Assembler::WritePoolGuard(BufferOffset branch, Instruction* inst,
                               BufferOffset dest) {
  DEBUG_PRINTF("\tWritePoolGuard\n");

  int32_t offset = dest.getOffset() - branch.getOffset();

  inst->SetJFormat(RO_JAL, zero_reg.code(), offset);

  DEBUG_PRINTF("%p(%x): ", inst, branch.getOffset());
#ifdef JS_DISASM_RISCV64
  disassembleInstr(inst, JitSpew_Codegen);
#endif /* JS_DISASM_RISCV64 */
}

void Assembler::WritePoolHeader(uint8_t* start, Pool* p, bool isNatural) {
  static_assert(sizeof(PoolHeader) == 4);

  // Get the total size of the pool.
  const uintptr_t totalPoolSize = sizeof(PoolHeader) + p->getPoolSize();
  const uintptr_t totalPoolInstructions = totalPoolSize / kInstrSize;

  MOZ_ASSERT((totalPoolSize & 0x3) == 0);
  MOZ_ASSERT(totalPoolInstructions < (1 << 15));

  PoolHeader header(totalPoolInstructions, isNatural);
  *(PoolHeader*)start = header;
}

void Assembler::copyJumpRelocationTable(uint8_t* dest) {
  if (jumpRelocations_.length()) {
    memcpy(dest, jumpRelocations_.buffer(), jumpRelocations_.length());
  }
}

void Assembler::copyDataRelocationTable(uint8_t* dest) {
  if (dataRelocations_.length()) {
    memcpy(dest, dataRelocations_.buffer(), dataRelocations_.length());
  }
}

void Assembler::RV_li(Register rd, int64_t imm) {
  UseScratchRegisterScope temps(this);
  if (RecursiveLiCount(imm) > GeneralLiCount(imm, temps.hasAvailable())) {
    GeneralLi(rd, imm);
  } else {
    RecursiveLi(rd, imm);
  }
}

int Assembler::RV_li_count(int64_t imm, bool is_get_temp_reg) {
  if (RecursiveLiCount(imm) > GeneralLiCount(imm, is_get_temp_reg)) {
    return GeneralLiCount(imm, is_get_temp_reg);
  }
  return RecursiveLiCount(imm);
}

void Assembler::GeneralLi(Register rd, int64_t imm) {
  // 64-bit imm is put in the register rd.
  // In most cases the imm is 32 bit and 2 instructions are generated. If a
  // temporary register is available, in the worst case, 6 instructions are
  // generated for a full 64-bit immediate. If temporay register is not
  // available the maximum will be 8 instructions. If imm is more than 32 bits
  // and a temp register is available, imm is divided into two 32-bit parts,
  // low_32 and up_32. Each part is built in a separate register. low_32 is
  // built before up_32. If low_32 is negative (upper 32 bits are 1), 0xffffffff
  // is subtracted from up_32 before up_32 is built. This compensates for 32
  // bits of 1's in the lower when the two registers are added. If no temp is
  // available, the upper 32 bit is built in rd, and the lower 32 bits are
  // devided to 3 parts (11, 11, and 10 bits). The parts are shifted and added
  // to the upper part built in rd.
  if (is_int32(imm + 0x800)) {
    // 32-bit case. Maximum of 2 instructions generated
    auto [high_20, low_12] = ToHigh20Low12(int32_t(imm));
    if (high_20) {
      lui(rd, (int32_t)high_20);
      if (low_12) {
        addi(rd, rd, low_12);
      }
    } else {
      addi(rd, zero_reg, low_12);
    }
    return;
  }
  UseScratchRegisterScope temps(this);
  AutoForbidPoolsAndNops afp(this, 8);
  // 64-bit case: divide imm into two 32-bit parts, upper and lower
  int64_t up_32 = imm >> 32;
  int64_t low_32 = imm & 0xffffffffull;
  Register temp_reg = rd;
  // Check if a temporary register is available
  if (up_32 == 0 || low_32 == 0) {
    // No temp register is needed
  } else {
    temp_reg = temps.hasAvailable() ? temps.Acquire() : InvalidReg;
  }
  if (temp_reg != InvalidReg) {
    // keep track of hardware behavior for lower part in sim_low
    int64_t sim_low = 0;
    // Build lower part
    if (low_32 != 0) {
      int64_t high_20 = ((low_32 + 0x800) >> 12);
      int64_t low_12 = low_32 & 0xfff;
      if (high_20) {
        // Adjust to 20 bits for the case of overflow
        high_20 &= 0xfffff;
        sim_low = ((high_20 << 12) << 32) >> 32;
        lui(rd, (int32_t)high_20);
        if (low_12) {
          sim_low += (low_12 << 52 >> 52) | low_12;
          addi(rd, rd, low_12);
        }
      } else {
        sim_low = low_12;
        ori(rd, zero_reg, low_12);
      }
    }
    if (sim_low & 0x100000000) {
      // Bit 31 is 1. Either an overflow or a negative 64 bit
      if (up_32 == 0) {
        // Positive number, but overflow because of the add 0x800
        ZeroExtendWord(rd, rd);
        return;
      }
      // low_32 is a negative 64 bit after the build
      up_32 = (up_32 - 0xffffffff) & 0xffffffff;
    }
    if (up_32 == 0) {
      return;
    }
    // Build upper part in a temporary register
    if (low_32 == 0) {
      // Build upper part in rd
      temp_reg = rd;
    }
    int64_t high_20 = (up_32 + 0x800) >> 12;
    int64_t low_12 = up_32 & 0xfff;
    if (high_20) {
      // Adjust to 20 bits for the case of overflow
      high_20 &= 0xfffff;
      lui(temp_reg, (int32_t)high_20);
      if (low_12) {
        addi(temp_reg, temp_reg, low_12);
      }
    } else {
      ori(temp_reg, zero_reg, low_12);
    }
    // Put it at the bgining of register
    slli(temp_reg, temp_reg, 32);
    if (low_32 != 0) {
      add(rd, rd, temp_reg);
    }
    return;
  }
  // No temp register. Build imm in rd.
  // Build upper 32 bits first in rd. Divide lower 32 bits parts and add
  // parts to the upper part by doing shift and add.
  // First build upper part in rd.
  int64_t high_20 = (up_32 + 0x800) >> 12;
  int64_t low_12 = up_32 & 0xfff;
  if (high_20) {
    // Adjust to 20 bits for the case of overflow
    high_20 &= 0xfffff;
    lui(rd, (int32_t)high_20);
    if (low_12) {
      addi(rd, rd, low_12);
    }
  } else {
    ori(rd, zero_reg, low_12);
  }
  // upper part already in rd. Each part to be added to rd, has maximum of 11
  // bits, and always starts with a 1. rd is shifted by the size of the part
  // plus the number of zeros between the parts. Each part is added after the
  // left shift.
  uint32_t mask = 0x80000000;
  int32_t shift_val = 0;
  int32_t i;
  for (i = 0; i < 32; i++) {
    if ((low_32 & mask) == 0) {
      mask >>= 1;
      shift_val++;
      if (i == 31) {
        // rest is zero
        slli(rd, rd, shift_val);
      }
      continue;
    }
    // The first 1 seen
    int32_t part;
    if ((i + 11) < 32) {
      // Pick 11 bits
      part = ((uint32_t)(low_32 << i) >> i) >> (32 - (i + 11));
      slli(rd, rd, shift_val + 11);
      ori(rd, rd, part);
      i += 10;
      mask >>= 11;
    } else {
      part = (uint32_t)(low_32 << i) >> i;
      slli(rd, rd, shift_val + (32 - i));
      ori(rd, rd, part);
      break;
    }
    shift_val = 0;
  }
}

int Assembler::GeneralLiCount(int64_t imm, bool is_get_temp_reg) {
  int count = 0;
  // imitate Assembler::RV_li
  if (is_int32(imm + 0x800)) {
    // 32-bit case. Maximum of 2 instructions generated
    auto [high_20, low_12] = ToHigh20Low12(int32_t(imm));
    if (high_20) {
      count++;
      if (low_12) {
        count++;
      }
    } else {
      count++;
    }
    return count;
  }
  // 64-bit case: divide imm into two 32-bit parts, upper and lower
  int64_t up_32 = imm >> 32;
  int64_t low_32 = imm & 0xffffffffull;
  // Check if a temporary register is available
  if (is_get_temp_reg) {
    // keep track of hardware behavior for lower part in sim_low
    int64_t sim_low = 0;
    // Build lower part
    if (low_32 != 0) {
      int64_t high_20 = ((low_32 + 0x800) >> 12);
      int64_t low_12 = low_32 & 0xfff;
      if (high_20) {
        // Adjust to 20 bits for the case of overflow
        high_20 &= 0xfffff;
        sim_low = ((high_20 << 12) << 32) >> 32;
        count++;
        if (low_12) {
          sim_low += (low_12 << 52 >> 52) | low_12;
          count++;
        }
      } else {
        sim_low = low_12;
        count++;
      }
    }
    if (sim_low & 0x100000000) {
      // Bit 31 is 1. Either an overflow or a negative 64 bit
      if (up_32 == 0) {
        // Positive number, but overflow because of the add 0x800
        count += HasZbaExtension() ? /* zext.w */ 1 : /* slli; srli */ 2;
        return count;
      }
      // low_32 is a negative 64 bit after the build
      up_32 = (up_32 - 0xffffffff) & 0xffffffff;
    }
    if (up_32 == 0) {
      return count;
    }
    int64_t high_20 = (up_32 + 0x800) >> 12;
    int64_t low_12 = up_32 & 0xfff;
    if (high_20) {
      // Adjust to 20 bits for the case of overflow
      high_20 &= 0xfffff;
      count++;
      if (low_12) {
        count++;
      }
    } else {
      count++;
    }
    // Put it at the bgining of register
    count++;
    if (low_32 != 0) {
      count++;
    }
    return count;
  }
  // No temp register. Build imm in rd.
  // Build upper 32 bits first in rd. Divide lower 32 bits parts and add
  // parts to the upper part by doing shift and add.
  // First build upper part in rd.
  int64_t high_20 = (up_32 + 0x800) >> 12;
  int64_t low_12 = up_32 & 0xfff;
  if (high_20) {
    // Adjust to 20 bits for the case of overflow
    high_20 &= 0xfffff;
    count++;
    if (low_12) {
      count++;
    }
  } else {
    count++;
  }
  // upper part already in rd. Each part to be added to rd, has maximum of 11
  // bits, and always starts with a 1. rd is shifted by the size of the part
  // plus the number of zeros between the parts. Each part is added after the
  // left shift.
  uint32_t mask = 0x80000000;
  int32_t i;
  for (i = 0; i < 32; i++) {
    if ((low_32 & mask) == 0) {
      mask >>= 1;
      if (i == 31) {
        // rest is zero
        count++;
      }
      continue;
    }
    // The first 1 seen
    if ((i + 11) < 32) {
      // Pick 11 bits
      count++;
      count++;
      i += 10;
      mask >>= 11;
    } else {
      count++;
      count++;
      break;
    }
  }
  return count;
}

struct ImmPtrParts {
  int32_t high_20;  // Bits 47:29, 19 bits.
  int16_t low_12;   // Bits 28:17, 12 bits.
  int16_t b11;      // Bits 16:6, 11 bits.
  int16_t a6;       // Bits 5:0, 6 bits.
};

static constexpr auto ToImmPtrParts(int64_t imm) {
  MOZ_ASSERT((imm & 0xffff'0000'0000'0000ll) == 0, "pointers are 48 bits");

  int64_t high_31 = (imm >> 17) & 0x7fffffff;  // 31 bits

  return ImmPtrParts{
      .high_20 = int32_t((high_31 + 0x800) >> 12),
      .low_12 = int16_t(high_31 & 0xfff),
      .b11 = int16_t((imm >> 6) & 0x7ff),
      .a6 = int16_t(imm & 0x3f),
  };
}

// Read or write to an instruction sequence written by |li_ptr|.
class LiPtr {
 public:
  // li_ptr emits a six instruction sequence.
  static constexpr size_t Length = 6;

 private:
  Instruction* start_;

  Instruction* at(size_t index) {
    MOZ_ASSERT(index < Length);
    return start_ + index * kInstrSize;
  }

  const Instruction* at(size_t index) const {
    MOZ_ASSERT(index < Length);
    return start_ + index * kInstrSize;
  }

 public:
  explicit LiPtr(Instruction* start) : start_(start) {}

  /**
   * Return true iff this is a li_ptr instruction sequence.
   */
  bool isValid() const {
    return at(0)->IsLui() && at(1)->IsAddi() && at(2)->IsSlli() &&
           at(3)->IsOri() && at(4)->IsSlli() && at(5)->IsOri();
  }

  /**
   * Disassemble the li_ptr instruction sequence.
   */
  void disassemble() {
#ifdef JS_DISASM_RISCV64
    Assembler::disassembleInstr(at(0));
    Assembler::disassembleInstr(at(1));
    Assembler::disassembleInstr(at(2));
    Assembler::disassembleInstr(at(3));
    Assembler::disassembleInstr(at(4));
    Assembler::disassembleInstr(at(5));
#endif
  }

  /**
   * Return the register to which this li_ptr instruction sequence writes to.
   */
  int target() const {
    MOZ_ASSERT(isValid());

    // All instructions must write to the same register.
    MOZ_ASSERT(at(0)->RdValue() == at(1)->RdValue());
    MOZ_ASSERT(at(0)->RdValue() == at(2)->RdValue());
    MOZ_ASSERT(at(0)->RdValue() == at(3)->RdValue());
    MOZ_ASSERT(at(0)->RdValue() == at(4)->RdValue());
    MOZ_ASSERT(at(0)->RdValue() == at(5)->RdValue());

    return at(0)->RdValue();
  }

  /**
   * Load the constant encoded in the li_ptr instruction sequence.
   */
  uintptr_t load() const {
    MOZ_ASSERT(isValid());

    // lui(rd, high_20);
    int64_t imm = int64_t(at(0)->Imm20UValue() << kImm20Shift);

    // addi(rd, rd, low_12);  // 31 bits in rd.
    imm += int64_t(at(1)->Imm12Value());

    // slli(rd, rd, 11);  // Space for next 11 bits
    MOZ_ASSERT(at(2)->Imm12Value() == 11);
    imm <<= 11;

    // ori(rd, rd, b11);  // 11 bits are added, 42 bit in rd.
    imm |= int64_t(at(3)->Imm12Value());

    // slli(rd, rd, 6);  // Space for next 6 bits
    MOZ_ASSERT(at(4)->Imm12Value() == 6);
    imm <<= 6;

    // ori(rd, rd, a6);  // 6 bits are added, 48 bit in rd.
    imm |= int64_t(at(5)->Imm12Value());

    MOZ_ASSERT((imm & 0xffff'0000'0000'0000ll) == 0, "pointers are 48 bits");
    return static_cast<uintptr_t>(imm);
  }

  /**
   * Update the constant embedded in the li_ptr instruction sequence.
   */
  void update(uintptr_t value) {
    MOZ_ASSERT(isValid());

    auto [high_20, low_12, b11, a6] = ToImmPtrParts(value);

    // lui(rd, high_20);
    at(0)->SetImm20UValue(high_20);

    // addi(rd, rd, low_12);  // 31 bits in rd.
    at(1)->SetImm12Value(low_12);

    // slli(rd, rd, 11);  // Space for next 11 bits
    MOZ_ASSERT(at(2)->Imm12Value() == 11);

    // ori(rd, rd, b11);  // 11 bits are added, 42 bit in rd.
    at(3)->SetImm12Value(b11);

    // slli(rd, rd, 6);  // Space for next 6 bits
    MOZ_ASSERT(at(4)->Imm12Value() == 6);

    // ori(rd, rd, a6);  // 6 bits are added, 48 bit in rd.
    at(5)->SetImm12Value(a6);

    MOZ_ASSERT(load() == value);
  }

  /**
   * Write the li_ptr instruction sequence, overwriting any previous
   * instructions.
   */
  void write(Register rd, uintptr_t value) {
    auto [high_20, low_12, b11, a6] = ToImmPtrParts(value);

    // lui(rd, high_20);
    at(0)->SetUFormat(RO_LUI, rd.code(), high_20);

    // addi(rd, rd, low_12);  // 31 bits in rd.
    at(1)->SetIFormat(RO_ADDI, rd.code(), rd.code(), low_12);

    // slli(rd, rd, 11);  // Space for next 11 bits
    at(2)->SetIFormat(RO_SLLI, rd.code(), rd.code(), 11);

    // ori(rd, rd, b11);  // 11 bits are added, 42 bit in rd.
    at(3)->SetIFormat(RO_ORI, rd.code(), rd.code(), b11);

    // slli(rd, rd, 6);  // Space for next 6 bits
    at(4)->SetIFormat(RO_SLLI, rd.code(), rd.code(), 6);

    // ori(rd, rd, a6);  // 6 bits are added, 48 bit in rd.
    at(5)->SetIFormat(RO_ORI, rd.code(), rd.code(), a6);

    MOZ_ASSERT(load() == value);
  }
};

uintptr_t Assembler::LoadLiPtrInstructions(Instruction* instr) {
  LiPtr ptr(instr);
  if (!ptr.isValid()) {
    // Dump the faulty instruction sequence before crashing.
    ptr.disassemble();
  }
  MOZ_RELEASE_ASSERT(ptr.isValid());

  return ptr.load();
}

void Assembler::UpdateLiPtrInstructions(Instruction* instr, uintptr_t value) {
  LiPtr ptr(instr);
  if (!ptr.isValid()) {
    // Dump the faulty instruction sequence before crashing.
    ptr.disassemble();
  }
  MOZ_RELEASE_ASSERT(ptr.isValid());

  ptr.update(value);
}

void Assembler::WriteLiPtrInstructions(Instruction* instr, Register reg,
                                       uintptr_t value) {
  // Forcibly overwrites whatever was written at |instr| with |value|.
  LiPtr ptr(instr);
  ptr.write(reg, value);
}

BufferOffset Assembler::li_ptr(Register rd, int64_t imm) {
  AutoForbidPoolsAndNops afp(this, 6);
  BufferOffset offset = nextOffset();

  // Initialize rd with an address
  // Pointers are 48 bits
  // 6 fixed instructions are generated
  DEBUG_PRINTF("li_ptr(%d, %" PRIx64 " <%" PRId64 ">)\n", ToNumber(rd), imm,
               imm);

  auto [high_20, low_12, b11, a6] = ToImmPtrParts(imm);

  lui(rd, high_20);
  addi(rd, rd, low_12);  // 31 bits in rd.
  slli(rd, rd, 11);      // Space for next 11 bits
  ori(rd, rd, b11);      // 11 bits are put in. 42 bit in rd
  slli(rd, rd, 6);       // Space for next 6 bits
  ori(rd, rd, a6);       // 6 bits are put in. 48 bits in rd

  MOZ_ASSERT_IF(!oom(), LiPtr(getInstructionAt(offset)).isValid());

  return offset;
}

struct Imm64Parts {
  int32_t high_20;  // Bits 63:48, 16 bits.
  int16_t d12;      // Bits 47:36, 12 bits.
  int16_t c12;      // Bits 35:24, 12 bits.
  int16_t b12;      // Bits 23:12, 12 bits.
  int16_t a12;      // Bits 11:0, 12 bits.
};

static constexpr auto ToImm64Parts(int64_t imm) {
  return Imm64Parts{
      .high_20 = int32_t(
          (imm + (1LL << 47) + (1LL << 35) + (1LL << 23) + (1LL << 11)) >> 48),
      .d12 =
          int16_t((imm + (1LL << 35) + (1LL << 23) + (1LL << 11)) << 16 >> 52),
      .c12 = int16_t((imm + (1LL << 23) + (1LL << 11)) << 28 >> 52),
      .b12 = int16_t((imm + (1LL << 11)) << 40 >> 52),
      .a12 = int16_t(imm << 52 >> 52),
  };
}

// Read or write to an instruction sequence written by |li_constant|.
class LiConstant {
 public:
  // li_constant emits an eight instruction sequence.
  static constexpr size_t Length = 8;

 private:
  Instruction* start_;

  Instruction* at(size_t index) {
    MOZ_ASSERT(index < Length);
    return start_ + index * kInstrSize;
  }

  const Instruction* at(size_t index) const {
    MOZ_ASSERT(index < Length);
    return start_ + index * kInstrSize;
  }

 public:
  explicit LiConstant(Instruction* start) : start_(start) {}

  /**
   * Return true iff this is a li_constant instruction sequence.
   */
  bool isValid() const {
    return at(0)->IsLui() && at(1)->IsAddiw() && at(2)->IsSlli() &&
           at(3)->IsAddi() && at(4)->IsSlli() && at(5)->IsAddi() &&
           at(6)->IsSlli() && at(7)->IsAddi();
  }

  /**
   * Disassemble the li_constant instruction sequence.
   */
  void disassemble() {
#ifdef JS_DISASM_RISCV64
    Assembler::disassembleInstr(at(0));
    Assembler::disassembleInstr(at(1));
    Assembler::disassembleInstr(at(2));
    Assembler::disassembleInstr(at(3));
    Assembler::disassembleInstr(at(4));
    Assembler::disassembleInstr(at(5));
    Assembler::disassembleInstr(at(6));
    Assembler::disassembleInstr(at(7));
#endif
  }

  /**
   * Load the constant encoded in the li_constant instruction sequence.
   */
  int64_t load() const {
    MOZ_ASSERT(isValid());

    // lui(rd, high_20);  // Bits 63:48
    int64_t imm = int64_t(at(0)->Imm20UValue() << kImm20Shift);

    // addiw(rd, rd, d12);  // Bits 47:36
    imm += int64_t(at(1)->Imm12Value());

    // slli(rd, rd, 12);
    MOZ_ASSERT(at(2)->Imm12Value() == 12);
    imm <<= 12;

    // addi(rd, rd, c12);  // Bits 35:24
    imm += int64_t(at(3)->Imm12Value());

    // slli(rd, rd, 12);
    MOZ_ASSERT(at(4)->Imm12Value() == 12);
    imm <<= 12;

    // addi(rd, rd, b12);  // Bits 23:12
    imm += int64_t(at(5)->Imm12Value());

    // slli(rd, rd, 12);
    MOZ_ASSERT(at(6)->Imm12Value() == 12);
    imm <<= 12;

    // addi(rd, rd, a12);  // Bits 11:0
    imm += int64_t(at(7)->Imm12Value());

    return imm;
  }

  /**
   * Update the constant embedded in the li_constant instruction sequence.
   */
  void update(int64_t value) {
    MOZ_ASSERT(isValid());

    auto [high_20, d12, c12, b12, a12] = ToImm64Parts(value);

    // lui(rd, high_20);  // Bits 63:48
    at(0)->SetImm20UValue(high_20);

    // addiw(rd, rd, d12);  // Bits 47:36
    at(1)->SetImm12Value(d12);

    // slli(rd, rd, 12);
    MOZ_ASSERT(at(2)->Shamt() == 12);

    // addi(rd, rd, c12);  // Bits 35:24
    at(3)->SetImm12Value(c12);

    // slli(rd, rd, 12);
    MOZ_ASSERT(at(4)->Shamt() == 12);

    // addi(rd, rd, b12);  // Bits 23:12
    at(5)->SetImm12Value(b12);

    // slli(rd, rd, 12);
    MOZ_ASSERT(at(6)->Shamt() == 12);

    // addi(rd, rd, a12);  // Bits 11:0
    at(7)->SetImm12Value(a12);

    MOZ_ASSERT(load() == value);
  }
};

int64_t Assembler::LoadLiConstantInstructions(Instruction* instr) {
  LiConstant cst(instr);
  if (!cst.isValid()) {
    // Dump the faulty instruction sequence before crashing.
    cst.disassemble();
  }
  MOZ_RELEASE_ASSERT(cst.isValid());

  return cst.load();
}

void Assembler::UpdateLiConstantInstructions(Instruction* instr,
                                             int64_t value) {
  LiConstant cst(instr);
  if (!cst.isValid()) {
    // Dump the faulty instruction sequence before crashing.
    cst.disassemble();
  }
  MOZ_RELEASE_ASSERT(cst.isValid());

  cst.update(value);
}

BufferOffset Assembler::li_constant(Register rd, int64_t imm) {
  AutoForbidPoolsAndNops afp(this, 8);
  BufferOffset offset = nextOffset();

  DEBUG_PRINTF("li_constant(%d, %" PRIx64 " <%" PRId64 ">)\n", ToNumber(rd),
               imm, imm);

  auto [high_20, d12, c12, b12, a12] = ToImm64Parts(imm);

  lui(rd, high_20);    // Bits 63:48
  addiw(rd, rd, d12);  // Bits 47:36
  slli(rd, rd, 12);
  addi(rd, rd, c12);  // Bits 35:24
  slli(rd, rd, 12);
  addi(rd, rd, b12);  // Bits 23:12
  slli(rd, rd, 12);
  addi(rd, rd, a12);  // Bits 11:0

  MOZ_ASSERT_IF(!oom(), LiConstant(getInstructionAt(offset)).isValid());

  return offset;
}

ABIArg ABIArgGenerator::next(MIRType type) {
  switch (type) {
    case MIRType::Int32:
    case MIRType::Int64:
    case MIRType::Pointer:
    case MIRType::WasmAnyRef:
    case MIRType::WasmArrayData:
    case MIRType::StackResults: {
      if (intRegIndex_ == NumIntArgRegs) {
        current_ = ABIArg(stackOffset_);
        stackOffset_ += sizeof(uintptr_t);
        break;
      }
      current_ = ABIArg(Register::FromCode(intRegIndex_ + a0.encoding()));
      intRegIndex_++;
      break;
    }
    case MIRType::Float32:
    case MIRType::Double: {
      if (floatRegIndex_ == NumFloatArgRegs) {
        // A real floating-point argument is passed in a floating-point
        // argument register if [...] at least one floating-point argument
        // register is available. Otherwise, it is passed according to the
        // integer calling convention.
        //
        // <https://riscv-non-isa.github.io/riscv-elf-psabi-doc/#_hardware_floating_point_calling_convention>
        if (kind_ == ABIKind::System && intRegIndex_ != NumIntArgRegs) {
          current_ = ABIArg(Register::FromCode(intRegIndex_ + a0.encoding()));
          intRegIndex_++;
          break;
        }
        current_ = ABIArg(stackOffset_);
        stackOffset_ += sizeof(double);
        break;
      }
      current_ = ABIArg(FloatRegister(
          FloatRegisters::Encoding(floatRegIndex_ + fa0.encoding()),
          type == MIRType::Double ? FloatRegisters::Double
                                  : FloatRegisters::Single));
      floatRegIndex_++;
      break;
    }
    case MIRType::Simd128: {
      MOZ_CRASH("RISCV64 does not support simd yet.");
      break;
    }
    default:
      MOZ_CRASH("Unexpected argument type");
  }
  return current_;
}

bool Assembler::oom() const {
  return AssemblerShared::oom() || m_buffer.oom() || jumpRelocations_.oom() ||
         dataRelocations_.oom() || !enoughLabelCache_;
}

#ifdef JS_DISASM_RISCV64
int Assembler::disassembleInstr(Instruction* instr, bool enable_spew) {
  if (!FLAG_riscv_debug && !enable_spew) {
    return -1;
  }
  disasm::NameConverter converter;
  disasm::Disassembler disasm(converter);
  EmbeddedVector<char, 128> disasm_buffer;

  int size = disasm.InstructionDecode(disasm_buffer, instr);
  DEBUG_PRINTF("%s\n", disasm_buffer.start());
  if (enable_spew) {
    JitSpew(JitSpew_Codegen, "%s", disasm_buffer.start());
  }
  return size;
}
#endif /* JS_DISASM_RISCV64 */

void Assembler::PatchDataWithValueCheck(CodeLocationLabel label,
                                        ImmPtr newValue, ImmPtr expectedValue) {
  PatchDataWithValueCheck(label, PatchedImmPtr(newValue.value),
                          PatchedImmPtr(expectedValue.value));
}

void Assembler::PatchDataWithValueCheck(CodeLocationLabel label,
                                        PatchedImmPtr newValue,
                                        PatchedImmPtr expectedValue) {
  Instruction* inst = Instruction::At(label.raw());

  // Check the previous value matches |expectedValue|.
  DebugOnly<uint64_t> value = Assembler::ExtractLoad64Value(inst);
  MOZ_ASSERT(value == uint64_t(expectedValue.value));

  // Update the instruction with |newValue|.
  Assembler::UpdateLoad64Value(inst, uint64_t(newValue.value));
}

uint64_t Assembler::ExtractLoad64Value(Instruction* inst0) {
  DEBUG_PRINTF("\tExtractLoad64Value: \tpc:%p ", inst0);
  MOZ_ASSERT(!inst0->IsJal(), "unexpected pool guard");

  // This method is called for 48-bit (li_ptr) and 64-bit (li_constant)
  // patchable immediates.
  //
  // The li_ptr and li_constant instruction sequences both start with the "lui"
  // instruction, so we have to inspect the second instruction to determine
  // which instruction sequence to read:
  // - li_constant starts with the instruction sequence "lui; addiw ...".
  // - li_ptr starts with the instruction sequence "lui; addi ...".

  Instruction* instr1 = inst0 + kInstrSize;
  if (instr1->IsAddiw()) {
    return LoadLiConstantInstructions(inst0);
  } else {
    return LoadLiPtrInstructions(inst0);
  }
}

void Assembler::UpdateLoad64Value(Instruction* inst0, uint64_t value) {
  DEBUG_PRINTF("\tUpdateLoad64Value: pc: %p\tvalue: %" PRIx64 "\n", inst0,
               value);
  MOZ_ASSERT(!inst0->IsJal(), "unexpected pool guard");

  // This method is called for 48-bit (li_ptr) and 64-bit (li_constant)
  // patchable immediates.
  //
  // The li_ptr and li_constant instruction sequences both start with the "lui"
  // instruction, so we have to inspect the second instruction to determine
  // which instruction sequence to patch:
  // - li_constant starts with the instruction sequence "lui; addiw ...".
  // - li_ptr starts with the instruction sequence "lui; addi ...".

  Instruction* instr1 = inst0 + kInstrSize;
  if (instr1->IsAddiw()) {
    UpdateLiConstantInstructions(inst0, value);
  } else {
    UpdateLiPtrInstructions(inst0, value);
  }
}

// This just stomps over memory with 32 bits of raw data. Its purpose is to
// overwrite the call of JITed code with 32 bits worth of an offset. This will
// is only meant to function on code that has been invalidated, so it should
// be totally safe. Since that instruction will never be executed again, a
// ICache flush should not be necessary
void Assembler::PatchWrite_Imm32(CodeLocationLabel label, Imm32 imm) {
  // Raw is going to be the return address.
  uint32_t* raw = (uint32_t*)label.raw();
  // Overwrite the 4 bytes before the return address, which will
  // end up being the call instruction.
  *(raw - 1) = imm.value;
}

bool Assembler::jumpChainPutTargetAt(BufferOffset pos,
                                     BufferOffset target_pos) {
  if (m_buffer.oom()) {
    return true;
  }

  Instruction* instruction = getInstructionAt(pos);
  DEBUG_PRINTF("\tjumpChainPutTargetAt: %p (%d) to %p (%d)\n", instruction,
               pos.getOffset(),
               instruction + target_pos.getOffset() - pos.getOffset(),
               target_pos.getOffset());
  switch (instruction->InstructionOpcodeType()) {
    case BRANCH: {
      int32_t offset = target_pos.getOffset() - pos.getOffset();
      if (!is_intn(offset, kBranchOffsetBits)) {
        return false;
      }
      instruction->SetBranchOffset(offset);
    } break;
    case JAL: {
      MOZ_ASSERT(instruction->IsJal());
      int32_t offset = target_pos.getOffset() - pos.getOffset();
      if (!is_intn(offset, kJumpOffsetBits)) {
        return false;
      }
      instruction->SetImm20JValue(offset);
    } break;
    case LUI: {
      uintptr_t target =
          reinterpret_cast<uintptr_t>(getInstructionAt(target_pos));
      UpdateLiPtrInstructions(instruction, target);
    } break;
    case AUIPC: {
      Instruction* instruction2 =
          getInstructionAt(BufferOffset(pos.getOffset() + kInstrSize));
      MOZ_ASSERT(instruction2->IsJalr() || instruction2->IsAddi());
      MOZ_ASSERT(instruction->RdValue() == instruction2->Rs1Value());

      int32_t offset = target_pos.getOffset() - pos.getOffset();
      auto [Hi20, Lo12] = ToHigh20Low12(offset);

      instruction->SetImm20UValue(Hi20);
      instruction2->SetImm12Value(Lo12);
    } break;
    default:
      UNIMPLEMENTED_RISCV();
      break;
  }
  return true;
}

const int kEndOfChain = -1;
const int32_t kEndOfJumpChain = 0;

int Assembler::jumpChainTargetAt(BufferOffset pos) {
  if (oom()) {
    return kEndOfChain;
  }
  Instruction* instruction = getInstructionAt(pos);
  Instruction* instruction2 = nullptr;
  if (instruction->IsAuipc()) {
    instruction2 = getInstructionAt(BufferOffset(pos.getOffset() + kInstrSize));
  }
  return jumpChainTargetAt(instruction, pos, instruction2);
}

int Assembler::jumpChainTargetAt(Instruction* instruction, BufferOffset pos,
                                 Instruction* instruction2) {
  DEBUG_PRINTF("\t jumpChainTargetAt: %p(%x)\n\t",
               reinterpret_cast<Instr*>(instruction), pos.getOffset());
#ifdef JS_DISASM_RISCV64
  disassembleInstr(instruction);
#endif /* JS_DISASM_RISCV64 */

  switch (instruction->InstructionOpcodeType()) {
    case BRANCH: {
      int32_t imm13 = instruction->BranchOffset();
      if (imm13 == kEndOfJumpChain) {
        // EndOfChain sentinel is returned directly, not relative to pc or pos.
        return kEndOfChain;
      }
      DEBUG_PRINTF("\t jumpChainTargetAt: %d %d\n", imm13,
                   pos.getOffset() + imm13);
      return pos.getOffset() + imm13;
    }
    case JAL: {
      int32_t imm21 = instruction->Imm20JValue();
      if (imm21 == kEndOfJumpChain) {
        // EndOfChain sentinel is returned directly, not relative to pc or pos.
        return kEndOfChain;
      }
      DEBUG_PRINTF("\t jumpChainTargetAt: %d %d\n", imm21,
                   pos.getOffset() + imm21);
      return pos.getOffset() + imm21;
    }
    case JALR: {
      int32_t imm12 = instruction->Imm12Value();
      if (imm12 == kEndOfJumpChain) {
        // EndOfChain sentinel is returned directly, not relative to pc or pos.
        return kEndOfChain;
      }
      DEBUG_PRINTF("\t jumpChainTargetAt: %d %d\n", imm12,
                   pos.getOffset() + imm12);
      return pos.getOffset() + imm12;
    }
    case LUI: {
      uintptr_t imm = LoadLiPtrInstructions(instruction);
      uintptr_t instr_address = reinterpret_cast<uintptr_t>(instruction);
      if (imm == kEndOfJumpChain) {
        return kEndOfChain;
      }
      MOZ_ASSERT(instr_address - imm < INT32_MAX);
      int32_t delta = static_cast<int32_t>(instr_address - imm);
      MOZ_ASSERT(pos.getOffset() > delta);
      return pos.getOffset() - delta;
    }
    case AUIPC: {
      MOZ_ASSERT(instruction2 != nullptr);
      MOZ_ASSERT(instruction2->IsJalr() || instruction2->IsAddi());

      int32_t imm_auipc = instruction->Imm20UValue() << kImm20Shift;
      int32_t imm12 = instruction2->Imm12Value();
      int32_t offset = imm_auipc + imm12;
      if (offset == kEndOfJumpChain) {
        return kEndOfChain;
      }
      DEBUG_PRINTF("\t jumpChainTargetAt: %d %d\n", offset,
                   pos.getOffset() + offset);
      return offset + pos.getOffset();
    }
    default: {
      UNIMPLEMENTED_RISCV();
    }
  }
}

BufferOffset Assembler::jumpChainGetNextLink(BufferOffset pos) {
  int link = jumpChainTargetAt(pos);
  return link == kEndOfChain ? BufferOffset() : BufferOffset(link);
}

uint32_t Assembler::jumpChainUseNextLink(Label* L) {
  MOZ_ASSERT(L->used());
  BufferOffset link = jumpChainGetNextLink(BufferOffset(L));
  if (!link.assigned()) {
    L->reset();
    return LabelBase::INVALID_OFFSET;
  }
  int offset = link.getOffset();
  DEBUG_PRINTF("next: %p to offset %d\n", L, offset);
  L->use(offset);
  return offset;
}

void Assembler::bind(Label* label, BufferOffset boff) {
  JitSpew(JitSpew_Codegen, ".set Llabel %p %u", label, currentOffset());
  DEBUG_PRINTF(".set Llabel %p %u\n", label, currentOffset());
  // If our caller didn't give us an explicit target to bind to
  // then we want to bind to the location of the next instruction
  BufferOffset dest = boff.assigned() ? boff : nextOffset();
  if (label->used()) {
    uint32_t next;

    do {
      // A used label holds a link to branch that uses it.
      // It's okay we use it here since jumpChainUseNextLink() mutates `label`.
      BufferOffset b(label);
      DEBUG_PRINTF("\tbind next:%d\n", b.getOffset());
      // Even a 0 offset may be invalid if we're out of memory.
      if (oom()) {
        return;
      }
      int fixup_pos = b.getOffset();
      int dist = dest.getOffset() - fixup_pos;
      next = jumpChainUseNextLink(label);
      DEBUG_PRINTF(
          "\t%p fixup: %d next: %u dest: %d dist: %d nextOffset: %d "
          "currOffset: %d\n",
          label, fixup_pos, next, dest.getOffset(), dist,
          nextOffset().getOffset(), currentOffset());
      Instruction* instr = getInstructionAt(b);
      if (instr->IsBranch()) {
        if (!is_intn(dist, kBranchOffsetBits)) {
          MOZ_ASSERT(next != LabelBase::INVALID_OFFSET);
          MOZ_RELEASE_ASSERT(
              is_intn(static_cast<int>(next) - fixup_pos, kJumpOffsetBits));
          MOZ_ASSERT(getInstructionAt(BufferOffset(next))->IsAuipc());
          MOZ_ASSERT(
              getInstructionAt(BufferOffset(next + kInstrSize))->IsJalr());
          DEBUG_PRINTF("\t\ttrampolining: %d\n", next);
        } else {
          jumpChainPutTargetAt(b, dest);
          BufferOffset deadline(b.getOffset() +
                                ImmBranchMaxForwardOffset(CondBranchRangeType));
          m_buffer.unregisterBranchDeadline(CondBranchRangeType, deadline);
        }
      } else if (instr->IsJal()) {
        if (!is_intn(dist, kJumpOffsetBits)) {
          MOZ_ASSERT(next != LabelBase::INVALID_OFFSET);
          MOZ_RELEASE_ASSERT(
              is_intn(static_cast<int>(next) - fixup_pos, kJumpOffsetBits));
          MOZ_ASSERT(getInstructionAt(BufferOffset(next))->IsAuipc());
          MOZ_ASSERT(
              getInstructionAt(BufferOffset(next + kInstrSize))->IsJalr());
          DEBUG_PRINTF("\t\ttrampolining: %d\n", next);
        } else {
          jumpChainPutTargetAt(b, dest);
          BufferOffset deadline(
              b.getOffset() + ImmBranchMaxForwardOffset(UncondBranchRangeType));
          m_buffer.unregisterBranchDeadline(UncondBranchRangeType, deadline);
        }
      } else {
        MOZ_ASSERT(instr->IsAuipc());
        jumpChainPutTargetAt(b, dest);
      }
    } while (next != LabelBase::INVALID_OFFSET);
  }
  label->bind(dest.getOffset());
}

void Assembler::Bind(uint8_t* rawCode, const CodeLabel& label) {
  if (label.patchAt().bound()) {
    auto mode = label.linkMode();
    intptr_t offset = label.patchAt().offset();
    intptr_t target = label.target().offset();

    if (mode == CodeLabel::RawPointer) {
      *reinterpret_cast<const void**>(rawCode + offset) = rawCode + target;
    } else {
      MOZ_ASSERT(mode == CodeLabel::MoveImmediate ||
                 mode == CodeLabel::JumpImmediate);
      Instruction* inst = Instruction::At(rawCode + offset);
      Assembler::UpdateLoad64Value(inst, uint64_t(rawCode + target));
    }
  }
}

int32_t Assembler::branchLongOffsetHelper(Label* L) {
  if (oom()) {
    return kEndOfJumpChain;
  }

  // Prevent nop sequences in branch instructions.
  AutoForbidNops afn(this);

  BufferOffset next_instr_offset = nextInstrOffset(2, 0);
  DEBUG_PRINTF("\tbranchLongOffsetHelper: %p to (%d)\n", L,
               next_instr_offset.getOffset());

  if (L->bound()) {
    // The label is bound: all uses are already linked.
    JitSpew(JitSpew_Codegen, ".use Llabel %p on %d", L,
            next_instr_offset.getOffset());
    int32_t offset = L->offset() - next_instr_offset.getOffset();
    MOZ_ASSERT((offset & 3) == 0);
    return offset;
  }

  // The label is unbound and previously unused: Store the offset in the label
  // itself for patching by bind().
  if (!L->used()) {
    JitSpew(JitSpew_Codegen, ".use Llabel %p on %d", L,
            next_instr_offset.getOffset());
    L->use(next_instr_offset.getOffset());
    DEBUG_PRINTF("\tLabel %p added to link: %d\n", L,
                 next_instr_offset.getOffset());
    if (!label_cache_.putNew(L->offset(), next_instr_offset)) {
      NoEnoughLabelCache();
    }
    return kEndOfJumpChain;
  }

  LabelCache::Ptr p = label_cache_.lookup(L->offset());
  MOZ_ASSERT(p);
  MOZ_ASSERT(p->key() == L->offset());
  const int32_t target_pos = p->value().getOffset();

  // If the existing instruction at the head of the list is within reach of the
  // new branch, we can simply insert the new branch at the front of the list.
  if (jumpChainPutTargetAt(BufferOffset(target_pos), next_instr_offset)) {
    DEBUG_PRINTF("\tLabel %p added to link: %d\n", L,
                 next_instr_offset.getOffset());
    if (!label_cache_.put(L->offset(), next_instr_offset)) {
      NoEnoughLabelCache();
    }
  } else {
    DEBUG_PRINTF("\tLabel  %p can't be added to link: %d -> %d\n", L,
                 BufferOffset(target_pos).getOffset(),
                 next_instr_offset.getOffset());

    // The label already has a linked list of uses, but we can't reach the head
    // of the list with the allowed branch range. Insert this branch at a
    // different position in the list. We need to find an existing branch
    // `exbr`.
    //
    // In particular, the end of the list is always a viable candidate, so we'll
    // just get that.
    //
    // See also vixl::MozBaseAssembler::LinkAndGetOffsetTo.

    BufferOffset next(L);
    BufferOffset exbr;
    do {
      exbr = next;
      next = jumpChainGetNextLink(next);
    } while (next.assigned());
    mozilla::DebugOnly<bool> ok = jumpChainPutTargetAt(exbr, next_instr_offset);
    MOZ_ASSERT(ok, "Still can't reach list head");
  }

  return kEndOfJumpChain;
}

int32_t Assembler::branchOffsetHelper(Label* L, OffsetSize bits) {
  if (oom()) {
    return kEndOfJumpChain;
  }

  // Prevent nop sequences in branch instructions.
  AutoForbidNops afn(this);

  BufferOffset next_instr_offset = nextInstrOffset(1, 1);
  DEBUG_PRINTF("\tbranchOffsetHelper: %p to %d\n", L,
               next_instr_offset.getOffset());

  if (L->bound()) {
    // The label is bound: all uses are already linked.
    JitSpew(JitSpew_Codegen, ".use Llabel %p on %d", L,
            next_instr_offset.getOffset());
    int32_t offset = L->offset() - next_instr_offset.getOffset();
    DEBUG_PRINTF("\toffset = %d\n", offset);
    MOZ_ASSERT(is_intn(offset, bits));
    MOZ_ASSERT((offset & 1) == 0);
    return offset;
  }

  BufferOffset deadline(next_instr_offset.getOffset() +
                        ImmBranchMaxForwardOffset(bits));
  DEBUG_PRINTF("\tregisterBranchDeadline %d type %d\n", deadline.getOffset(),
               OffsetSizeToImmBranchRangeType(bits));
  m_buffer.registerBranchDeadline(OffsetSizeToImmBranchRangeType(bits),
                                  deadline);

  // The label is unbound and previously unused: Store the offset in the label
  // itself for patching by bind().
  if (!L->used()) {
    JitSpew(JitSpew_Codegen, ".use Llabel %p on %d", L,
            next_instr_offset.getOffset());
    L->use(next_instr_offset.getOffset());
    if (!label_cache_.putNew(L->offset(), next_instr_offset)) {
      NoEnoughLabelCache();
    }
    DEBUG_PRINTF("\tLabel  %p added to link: %d\n", L,
                 next_instr_offset.getOffset());
    return kEndOfJumpChain;
  }

  // The label is unbound and has multiple users. Create a linked list between
  // the branches, and update the linked list head in the label struct. This is
  // not always trivial since the branches in the linked list have limited
  // ranges.

  LabelCache::Ptr p = label_cache_.lookup(L->offset());
  MOZ_ASSERT(p);
  MOZ_ASSERT(p->key() == L->offset());
  const int32_t target_pos = p->value().getOffset();

  // If the existing instruction at the head of the list is within reach of the
  // new branch, we can simply insert the new branch at the front of the list.
  if (jumpChainPutTargetAt(BufferOffset(target_pos), next_instr_offset)) {
    DEBUG_PRINTF("\tLabel  %p added to link: %d\n", L,
                 next_instr_offset.getOffset());
    if (!label_cache_.put(L->offset(), next_instr_offset)) {
      NoEnoughLabelCache();
    }
  } else {
    DEBUG_PRINTF("\tLabel  %p can't be added to link: %d -> %d\n", L,
                 BufferOffset(target_pos).getOffset(),
                 next_instr_offset.getOffset());

    // The label already has a linked list of uses, but we can't reach the head
    // of the list with the allowed branch range. Insert this branch at a
    // different position in the list. We need to find an existing branch
    // `exbr`.
    //
    // In particular, the end of the list is always a viable candidate, so we'll
    // just get that.
    //
    // See also vixl::MozBaseAssembler::LinkAndGetOffsetTo.

    BufferOffset next(L);
    BufferOffset exbr;
    do {
      exbr = next;
      next = jumpChainGetNextLink(next);
    } while (next.assigned());
    mozilla::DebugOnly<bool> ok = jumpChainPutTargetAt(exbr, next_instr_offset);
    MOZ_ASSERT(ok, "Still can't reach list head");
  }

  return kEndOfJumpChain;
}

Assembler::Condition Assembler::InvertCondition(Condition cond) {
  switch (cond) {
    case Equal:
      return NotEqual;
    case NotEqual:
      return Equal;
    case Zero:
      return NonZero;
    case NonZero:
      return Zero;
    case LessThan:
      return GreaterThanOrEqual;
    case LessThanOrEqual:
      return GreaterThan;
    case GreaterThan:
      return LessThanOrEqual;
    case GreaterThanOrEqual:
      return LessThan;
    case Above:
      return BelowOrEqual;
    case AboveOrEqual:
      return Below;
    case Below:
      return AboveOrEqual;
    case BelowOrEqual:
      return Above;
    case Signed:
      return NotSigned;
    case NotSigned:
      return Signed;
    default:
      MOZ_CRASH("unexpected condition");
  }
}

Assembler::DoubleCondition Assembler::InvertCondition(DoubleCondition cond) {
  switch (cond) {
    case DoubleOrdered:
      return DoubleUnordered;
    case DoubleEqual:
      return DoubleNotEqualOrUnordered;
    case DoubleNotEqual:
      return DoubleEqualOrUnordered;
    case DoubleGreaterThan:
      return DoubleLessThanOrEqualOrUnordered;
    case DoubleGreaterThanOrEqual:
      return DoubleLessThanOrUnordered;
    case DoubleLessThan:
      return DoubleGreaterThanOrEqualOrUnordered;
    case DoubleLessThanOrEqual:
      return DoubleGreaterThanOrUnordered;
    case DoubleUnordered:
      return DoubleOrdered;
    case DoubleEqualOrUnordered:
      return DoubleNotEqual;
    case DoubleNotEqualOrUnordered:
      return DoubleEqual;
    case DoubleGreaterThanOrUnordered:
      return DoubleLessThanOrEqual;
    case DoubleGreaterThanOrEqualOrUnordered:
      return DoubleLessThan;
    case DoubleLessThanOrUnordered:
      return DoubleGreaterThanOrEqual;
    case DoubleLessThanOrEqualOrUnordered:
      return DoubleGreaterThan;
    default:
      MOZ_CRASH("unexpected condition");
  }
}

// Break / Trap instructions.
void Assembler::break_(uint32_t code, bool break_as_stop) {
  // We need to invalidate breaks that could be stops as well because the
  // simulator expects a char pointer after the stop instruction.
  // See constants-mips.h for explanation.
  MOZ_ASSERT(
      (break_as_stop && code <= kMaxStopCode && code > kMaxTracepointCode) ||
      (!break_as_stop && (code > kMaxStopCode || code <= kMaxTracepointCode)));

  // since ebreak does not allow additional immediate field, we use the
  // immediate field of lui instruction immediately following the ebreak to
  // encode the "code" info
  ebreak();
  MOZ_ASSERT(is_uint20(code));
  lui(zero_reg, code);
}

void Assembler::ToggleToJmp(CodeLocationLabel inst_) {
  Instruction* inst = Instruction::At(inst_.raw());
  MOZ_ASSERT(inst->IsAddi());

  int32_t offset = inst->Imm12Value();
  MOZ_ASSERT(is_int12(offset));

  // jal(zero, offset);
  inst->SetJFormat(RO_JAL, zero_reg.code(), offset);
}

void Assembler::ToggleToCmp(CodeLocationLabel inst_) {
  Instruction* inst = Instruction::At(inst_.raw());

  // toggledJump is allways used for short jumps.
  MOZ_ASSERT(inst->IsJal());

  // Replace "jal zero_reg, offset" with "addi $zero, $zero, offset"
  int32_t offset = inst->Imm20JValue();
  MOZ_ASSERT(is_int12(offset));

  inst->SetIFormat(RO_ADDI, zero_reg.code(), zero_reg.code(), offset);
}

bool Assembler::reserve(size_t size) {
  // This buffer uses fixed-size chunks so there's no point in reserving
  // now vs. on-demand.
  return !oom();
}

static JitCode* CodeFromJump(Instruction* jump) {
  uint8_t* target = (uint8_t*)Assembler::ExtractLoad64Value(jump);
  return JitCode::FromExecutable(target);
}

void Assembler::TraceJumpRelocations(JSTracer* trc, JitCode* code,
                                     CompactBufferReader& reader) {
  while (reader.more()) {
    JitCode* child =
        CodeFromJump(Instruction::At(code->raw() + reader.readUnsigned()));
    TraceManuallyBarrieredEdge(trc, &child, "rel32");
  }
}

static void TraceOneDataRelocation(JSTracer* trc,
                                   mozilla::Maybe<AutoWritableJitCode>& awjc,
                                   JitCode* code, Instruction* inst) {
  void* ptr = (void*)Assembler::ExtractLoad64Value(inst);
  void* prior = ptr;

  // Data relocations can be for Values or for raw pointers. If a Value is
  // zero-tagged, we can trace it as if it were a raw pointer. If a Value
  // is not zero-tagged, we have to interpret it as a Value to ensure that the
  // tag bits are masked off to recover the actual pointer.
  uintptr_t word = reinterpret_cast<uintptr_t>(ptr);
  if (word >> JSVAL_TAG_SHIFT) {
    // This relocation is a Value with a non-zero tag.
    Value v = Value::fromRawBits(word);
    TraceManuallyBarrieredEdge(trc, &v, "jit-masm-value");
    ptr = (void*)v.bitsAsPunboxPointer();
  } else {
    // This relocation is a raw pointer or a Value with a zero tag.
    // No barrier needed since these are constants.
    TraceManuallyBarrieredGenericPointerEdge(
        trc, reinterpret_cast<gc::Cell**>(&ptr), "jit-masm-ptr");
  }

  if (ptr != prior) {
    if (awjc.isNothing()) {
      awjc.emplace(code);
    }
    Assembler::UpdateLoad64Value(inst, uint64_t(ptr));
  }
}

/* static */
void Assembler::TraceDataRelocations(JSTracer* trc, JitCode* code,
                                     CompactBufferReader& reader) {
  mozilla::Maybe<AutoWritableJitCode> awjc;
  while (reader.more()) {
    size_t offset = reader.readUnsigned();
    Instruction* inst = Instruction::At(code->raw() + offset);
    TraceOneDataRelocation(trc, awjc, code, inst);
  }
}

UseScratchRegisterScope::UseScratchRegisterScope(Assembler& assembler)
    : available_(assembler.GetScratchRegisterList()),
      old_available_(*available_) {}

UseScratchRegisterScope::UseScratchRegisterScope(Assembler* assembler)
    : available_(assembler->GetScratchRegisterList()),
      old_available_(*available_) {}

UseScratchRegisterScope::~UseScratchRegisterScope() {
  *available_ = old_available_;
}

Register UseScratchRegisterScope::Acquire() {
  MOZ_ASSERT(available_ != nullptr);
  MOZ_ASSERT(!available_->empty());
  Register index = GeneralRegisterSet::FirstRegister(available_->bits());
  available_->takeRegisterIndex(index);
  return index;
}

void UseScratchRegisterScope::Release(const Register& reg) {
  MOZ_ASSERT(available_ != nullptr);
  MOZ_ASSERT(old_available_.hasRegisterIndex(reg));
  MOZ_ASSERT(!available_->hasRegisterIndex(reg));
  Include(GeneralRegisterSet(1 << reg.code()));
}

bool UseScratchRegisterScope::hasAvailable() const {
  return (available_->size()) != 0;
}

void Assembler::retarget(Label* label, Label* target) {
  spew("retarget %p -> %p", label, target);
  if (label->used() && !oom()) {
    if (target->bound()) {
      bind(label, BufferOffset(target));
    } else if (target->used()) {
      // The target is not bound but used. Prepend label's branch list
      // onto target's.
      int32_t next;
      BufferOffset labelBranchOffset(label);

      // Find the head of the use chain for label.
      do {
        next = jumpChainUseNextLink(label);
        labelBranchOffset = BufferOffset(next);
      } while (next != LabelBase::INVALID_OFFSET);

      // Then patch the head of label's use chain to the tail of
      // target's use chain, prepending the entire use chain of target.
      target->use(label->offset());
      jumpChainPutTargetAt(labelBranchOffset, BufferOffset(target));
      MOZ_CRASH("check");
    } else {
      // The target is unbound and unused.  We can just take the head of
      // the list hanging off of label, and dump that into target.
      target->use(label->offset());
    }
  }
  label->reset();
}

bool Assembler::appendRawCode(const uint8_t* code, size_t numBytes) {
  if (m_buffer.oom()) {
    return false;
  }
  m_buffer.putBytes(numBytes, code);
  return !m_buffer.oom();
}

void Assembler::ToggleCall(CodeLocationLabel inst_, bool enabled) {
  LiPtr ptr(Instruction::At(inst_.raw()));
  if (!ptr.isValid()) {
    ptr.disassemble();
  }
  MOZ_RELEASE_ASSERT(ptr.isValid());

  Instruction* next = Instruction::At(inst_.raw() + LiPtr::Length * kInstrSize);
  MOZ_ASSERT(next->IsJalr() || next->IsNop());

  if (enabled) {
    next->SetIFormat(RO_JALR, ra.code(), ptr.target(), 0);
  } else {
    next->SetNop();
  }
}

void Assembler::PatchShortRangeBranchToVeneer(Buffer* buffer, unsigned rangeIdx,
                                              BufferOffset deadline,
                                              BufferOffset veneer) {
  if (buffer->oom()) {
    return;
  }
  DEBUG_PRINTF("\tPatchShortRangeBranchToVeneer\n");

  // Reconstruct the position of the branch from (rangeIdx, deadline).
  ImmBranchRangeType branchRange = static_cast<ImmBranchRangeType>(rangeIdx);
  BufferOffset branch(deadline.getOffset() -
                      ImmBranchMaxForwardOffset(branchRange));
  Instruction* branchInst = buffer->getInst(branch);
  Instruction* veneerInst_1 = buffer->getInst(veneer);
  Instruction* veneerInst_2 =
      buffer->getInst(BufferOffset(veneer.getOffset() + kInstrSize));

  // Verify that the branch range matches what's encoded.
  DEBUG_PRINTF("\t%p(%x): ", branchInst, branch.getOffset());
#ifdef JS_DISASM_RISCV64
  disassembleInstr(branchInst, JitSpew_Codegen);
#endif /* JS_DISASM_RISCV64 */
  DEBUG_PRINTF("\t insert veneer %x, branch: %x deadline: %x\n",
               veneer.getOffset(), branch.getOffset(), deadline.getOffset());
  MOZ_ASSERT(branchRange <= UncondBranchRangeType);
  MOZ_ASSERT(branchInst->GetImmBranchRangeType() == branchRange);

  // We want to insert veneer after branch in the linked list of instructions
  // that use the same unbound label.
  // The veneer should be an unconditional branch.
  int32_t nextElemOffset = jumpChainTargetAt(buffer->getInst(branch), branch);
  int32_t dist;
  // If offset is kEndOfChain, this is the end of the linked list.
  if (nextElemOffset != kEndOfChain) {
    // Make the offset relative to veneer so it targets the same instruction
    // as branchInst.
    dist = nextElemOffset - veneer.getOffset();
  } else {
    dist = kEndOfJumpChain;
  }

  auto [Hi20, Lo12] = ToHigh20Low12(dist);

  // Insert veneer as a long jump.
  veneerInst_1->SetUFormat(RO_AUIPC, t6.code(), Hi20);
  veneerInst_2->SetIFormat(RO_JALR, zero_reg.code(), t6.code(), Lo12);

  // Now link branchInst to veneer.
  int32_t offset = veneer.getOffset() - branch.getOffset();
  if (branchInst->IsBranch()) {
    branchInst->SetBranchOffset(offset);
  } else {
    MOZ_ASSERT(branchInst->IsJal());
    branchInst->SetImm20JValue(offset);
  }
#ifdef JS_DISASM_RISCV64
  DEBUG_PRINTF("\tfix to veneer:");
  disassembleInstr(branchInst);
#endif /* JS_DISASM_RISCV64 */
}
}  // namespace jit
}  // namespace js
