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

#ifndef jit_riscv64_base_Base_assembler_riscv_h
#define jit_riscv64_base_Base_assembler_riscv_h

#include <stdio.h>

#include "jit/Label.h"
#include "jit/riscv64/Architecture-riscv64.h"
#include "jit/riscv64/base/Instruction.h"
#include "jit/riscv64/constant/Constant-riscv64.h"
#include "jit/riscv64/Register-riscv64.h"
#include "jit/shared/IonAssemblerBuffer.h"

#define xlen (uint8_t(sizeof(void*) * 8))

namespace js {
namespace jit {

typedef FloatRegister FPURegister;
#define zero_reg zero

#if defined(DEBUG)
// Only useful when defined(DEBUG). See op.getBoolOption("riscv-debug") in
// js/src/shell/js.cpp.
#  define DEBUG_PRINTF(...)     \
    if (FLAG_riscv_debug) {     \
      std::printf(__VA_ARGS__); \
    }
#else
#  define DEBUG_PRINTF(...) \
    do {                    \
    } while (0)
#endif /* defined(DEBUG) */

int ToNumber(Register reg);
Register ToRegister(uint32_t num);

struct High20Low12 {
  int32_t high20;
  int32_t low12;
};

constexpr auto ToHigh20Low12(int32_t imm) {
  // Cast |imm| to int64 to avoid arithmetic overflow when adding 0x800.
  int64_t value = imm;
  return High20Low12{
      // NB: Bit-and with 0xFFFFF keeps the generated assembly code smaller.
      .high20 = int32_t(((value + 0x800) >> 12) & 0xFFFFF),
      .low12 = int32_t(value << 52 >> 52),
  };
}

class AssemblerRiscvBase {
 protected:
  virtual int32_t branchOffsetHelper(Label* L, OffsetSize bits) = 0;

  virtual BufferOffset emit(Instr x) = 0;
  virtual BufferOffset emit(ShortInstr x) = 0;
  virtual BufferOffset emit(uint64_t x) = 0;
  virtual uint32_t currentOffset() = 0;
  // Instruction generation.

  // ----- Top-level instruction formats match those in the ISA manual
  // (R, I, S, B, U, J). These match the formats defined in LLVM's
  // RISCVInstrFormats.td.
  void GenInstrR(uint8_t funct7, uint8_t funct3, BaseOpcode opcode, Register rd,
                 Register rs1, Register rs2);
  void GenInstrR(uint8_t funct7, uint8_t funct3, BaseOpcode opcode,
                 FPURegister rd, FPURegister rs1, FPURegister rs2);
  void GenInstrR(uint8_t funct7, uint8_t funct3, BaseOpcode opcode, Register rd,
                 FPURegister rs1, Register rs2);
  void GenInstrR(uint8_t funct7, uint8_t funct3, BaseOpcode opcode,
                 FPURegister rd, Register rs1, Register rs2);
  void GenInstrR(uint8_t funct7, uint8_t funct3, BaseOpcode opcode,
                 FPURegister rd, FPURegister rs1, Register rs2);
  void GenInstrR(uint8_t funct7, uint8_t funct3, BaseOpcode opcode, Register rd,
                 FPURegister rs1, FPURegister rs2);
  void GenInstrR4(uint8_t funct2, BaseOpcode opcode, Register rd, Register rs1,
                  Register rs2, Register rs3, FPURoundingMode frm);
  void GenInstrR4(uint8_t funct2, BaseOpcode opcode, FPURegister rd,
                  FPURegister rs1, FPURegister rs2, FPURegister rs3,
                  FPURoundingMode frm);
  void GenInstrRAtomic(uint8_t funct5, bool aq, bool rl, uint8_t funct3,
                       Register rd, Register rs1, Register rs2);
  void GenInstrRFrm(uint8_t funct7, BaseOpcode opcode, Register rd,
                    Register rs1, Register rs2, FPURoundingMode frm);
  BufferOffset GenInstrI(uint8_t funct3, BaseOpcode opcode, Register rd,
                         Register rs1, int16_t imm12);
  BufferOffset GenInstrI(uint8_t funct3, BaseOpcode opcode, FPURegister rd,
                         Register rs1, int16_t imm12);
  void GenInstrIShift(uint8_t funct7, uint8_t funct3, BaseOpcode opcode,
                      Register rd, Register rs1, uint8_t shamt);
  void GenInstrIShiftW(uint8_t funct7, uint8_t funct3, BaseOpcode opcode,
                       Register rd, Register rs1, uint8_t shamt);
  void GenInstrS(uint8_t funct3, BaseOpcode opcode, Register rs1, Register rs2,
                 int16_t imm12);
  void GenInstrS(uint8_t funct3, BaseOpcode opcode, Register rs1,
                 FPURegister rs2, int16_t imm12);
  void GenInstrB(uint8_t funct3, BaseOpcode opcode, Register rs1, Register rs2,
                 int16_t imm12);
  void GenInstrU(BaseOpcode opcode, Register rd, int32_t imm20);
  void GenInstrJ(BaseOpcode opcode, Register rd, int32_t imm20);
  void GenInstrCR(uint8_t funct4, BaseOpcode opcode, Register rd, Register rs2);
  void GenInstrCA(uint8_t funct6, BaseOpcode opcode, Register rd, uint8_t funct,
                  Register rs2);
  void GenInstrCI(uint8_t funct3, BaseOpcode opcode, Register rd, int8_t imm6);
  void GenInstrCIU(uint8_t funct3, BaseOpcode opcode, Register rd,
                   uint8_t uimm6);
  void GenInstrCIU(uint8_t funct3, BaseOpcode opcode, FPURegister rd,
                   uint8_t uimm6);
  void GenInstrCIW(uint8_t funct3, BaseOpcode opcode, Register rd,
                   uint8_t uimm8);
  void GenInstrCSS(uint8_t funct3, BaseOpcode opcode, FPURegister rs2,
                   uint8_t uimm6);
  void GenInstrCSS(uint8_t funct3, BaseOpcode opcode, Register rs2,
                   uint8_t uimm6);
  void GenInstrCL(uint8_t funct3, BaseOpcode opcode, Register rd, Register rs1,
                  uint8_t uimm5);
  void GenInstrCL(uint8_t funct3, BaseOpcode opcode, FPURegister rd,
                  Register rs1, uint8_t uimm5);
  void GenInstrCS(uint8_t funct3, BaseOpcode opcode, Register rs2, Register rs1,
                  uint8_t uimm5);
  void GenInstrCS(uint8_t funct3, BaseOpcode opcode, FPURegister rs2,
                  Register rs1, uint8_t uimm5);
  void GenInstrCJ(uint8_t funct3, BaseOpcode opcode, uint16_t uint11);
  void GenInstrCB(uint8_t funct3, BaseOpcode opcode, Register rs1,
                  uint8_t uimm8);
  void GenInstrCBA(uint8_t funct3, uint8_t funct2, BaseOpcode opcode,
                   Register rs1, int8_t imm6);

  // ----- Instruction class templates match those in LLVM's RISCVInstrInfo.td
  void GenInstrBranchCC_rri(uint8_t funct3, Register rs1, Register rs2,
                            int16_t imm12);
  void GenInstrLoad_ri(uint8_t funct3, Register rd, Register rs1,
                       int16_t imm12);
  void GenInstrStore_rri(uint8_t funct3, Register rs1, Register rs2,
                         int16_t imm12);
  void GenInstrALU_ri(uint8_t funct3, Register rd, Register rs1, int16_t imm12);
  void GenInstrShift_ri(bool arithshift, uint8_t funct3, Register rd,
                        Register rs1, uint8_t shamt);
  void GenInstrALU_rr(uint8_t funct7, uint8_t funct3, Register rd, Register rs1,
                      Register rs2);
  void GenInstrCSR_ir(uint8_t funct3, Register rd, ControlStatusReg csr,
                      Register rs1);
  void GenInstrCSR_ii(uint8_t funct3, Register rd, ControlStatusReg csr,
                      uint8_t rs1);
  void GenInstrShiftW_ri(bool arithshift, uint8_t funct3, Register rd,
                         Register rs1, uint8_t shamt);
  void GenInstrALUW_rr(uint8_t funct7, uint8_t funct3, Register rd,
                       Register rs1, Register rs2);
  void GenInstrPriv(uint8_t funct7, Register rs1, Register rs2);
  void GenInstrLoadFP_ri(uint8_t funct3, FPURegister rd, Register rs1,
                         int16_t imm12);
  void GenInstrStoreFP_rri(uint8_t funct3, Register rs1, FPURegister rs2,
                           int16_t imm12);
  void GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3, FPURegister rd,
                        FPURegister rs1, FPURegister rs2);
  void GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3, FPURegister rd,
                        Register rs1, Register rs2);
  void GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3, FPURegister rd,
                        FPURegister rs1, Register rs2);
  void GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3, Register rd,
                        FPURegister rs1, Register rs2);
  void GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3, Register rd,
                        FPURegister rs1, FPURegister rs2);
};

}  // namespace jit
}  // namespace js

#endif  // jit_riscv64_base_Base_assembler_riscv_h
