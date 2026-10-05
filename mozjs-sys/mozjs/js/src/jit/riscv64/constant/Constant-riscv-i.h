// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef jit_riscv64_constant_Constant_riscv64_i_h_
#define jit_riscv64_constant_Constant_riscv64_i_h_
#include "jit/riscv64/constant/Base-constant-riscv.h"
namespace js {
namespace jit {

enum OpcodeRISCV32I : uint32_t {
  // Note use RO (RiscV Opcode) prefix
  // RV32I Base Instruction Set
  RO_LUI = LUI,
  RO_AUIPC = AUIPC,
  RO_JAL = JAL,
  RO_JALR = JALR | (0b000 << kFunct3Shift),
  RO_BEQ = BRANCH | (0b000 << kFunct3Shift),
  RO_BNE = BRANCH | (0b001 << kFunct3Shift),
  RO_BLT = BRANCH | (0b100 << kFunct3Shift),
  RO_BGE = BRANCH | (0b101 << kFunct3Shift),
  RO_BLTU = BRANCH | (0b110 << kFunct3Shift),
  RO_BGEU = BRANCH | (0b111 << kFunct3Shift),
  RO_LB = LOAD | (0b000 << kFunct3Shift),
  RO_LH = LOAD | (0b001 << kFunct3Shift),
  RO_LW = LOAD | (0b010 << kFunct3Shift),
  RO_LBU = LOAD | (0b100 << kFunct3Shift),
  RO_LHU = LOAD | (0b101 << kFunct3Shift),
  RO_SB = STORE | (0b000 << kFunct3Shift),
  RO_SH = STORE | (0b001 << kFunct3Shift),
  RO_SW = STORE | (0b010 << kFunct3Shift),
  RO_ADDI = OP_IMM | (0b000 << kFunct3Shift),
  RO_SLTI = OP_IMM | (0b010 << kFunct3Shift),
  RO_SLTIU = OP_IMM | (0b011 << kFunct3Shift),
  RO_XORI = OP_IMM | (0b100 << kFunct3Shift),
  RO_ORI = OP_IMM | (0b110 << kFunct3Shift),
  RO_ANDI = OP_IMM | (0b111 << kFunct3Shift),

  OP_SHL = 0b0010011 | (0b001 << kFunct3Shift),
  RO_SLLI = OP_SHL | (0b000000 << kFunct6Shift),

  OP_SHR = 0b0010011 | (0b101 << kFunct3Shift),
  RO_SRLI = OP_SHR | (0b000000 << kFunct6Shift),
  RO_SRAI = OP_SHR | (0b010000 << kFunct6Shift),

  RO_ADD = OP | (0b000 << kFunct3Shift) | (0b0000000 << kFunct7Shift),
  RO_SUB = OP | (0b000 << kFunct3Shift) | (0b0100000 << kFunct7Shift),
  RO_SLL = OP | (0b001 << kFunct3Shift) | (0b0000000 << kFunct7Shift),
  RO_SLT = OP | (0b010 << kFunct3Shift) | (0b0000000 << kFunct7Shift),
  RO_SLTU = OP | (0b011 << kFunct3Shift) | (0b0000000 << kFunct7Shift),
  RO_XOR = OP | (0b100 << kFunct3Shift) | (0b0000000 << kFunct7Shift),
  RO_SRL = OP | (0b101 << kFunct3Shift) | (0b0000000 << kFunct7Shift),
  RO_SRA = OP | (0b101 << kFunct3Shift) | (0b0100000 << kFunct7Shift),
  RO_OR = OP | (0b110 << kFunct3Shift) | (0b0000000 << kFunct7Shift),
  RO_AND = OP | (0b111 << kFunct3Shift) | (0b0000000 << kFunct7Shift),
  RO_FENCE = MISC_MEM | (0b000 << kFunct3Shift),
  RO_ECALL = SYSTEM | (0b000 << kFunct3Shift),
  // RO_EBREAK = SYSTEM | (0b000 << kFunct3Shift), // Same as ECALL, use imm12

  // RV64I Base Instruction Set (in addition to RV32I)
  RO_LWU = LOAD | (0b110 << kFunct3Shift),
  RO_LD = LOAD | (0b011 << kFunct3Shift),
  RO_SD = STORE | (0b011 << kFunct3Shift),
  RO_ADDIW = OP_IMM_32 | (0b000 << kFunct3Shift),

  OP_SHLW = OP_IMM_32 | (0b001 << kFunct3Shift),
  RO_SLLIW = OP_SHLW | (0b0000000 << kFunct7Shift),
  OP_SHRW = OP_IMM_32 | (0b101 << kFunct3Shift),
  RO_SRLIW = OP_SHRW | (0b0000000 << kFunct7Shift),
  RO_SRAIW = OP_SHRW | (0b0100000 << kFunct7Shift),

  RO_ADDW = OP_32 | (0b000 << kFunct3Shift) | (0b0000000 << kFunct7Shift),
  RO_SUBW = OP_32 | (0b000 << kFunct3Shift) | (0b0100000 << kFunct7Shift),
  RO_SLLW = OP_32 | (0b001 << kFunct3Shift) | (0b0000000 << kFunct7Shift),
  RO_SRLW = OP_32 | (0b101 << kFunct3Shift) | (0b0000000 << kFunct7Shift),
  RO_SRAW = OP_32 | (0b101 << kFunct3Shift) | (0b0100000 << kFunct7Shift),
};
}  // namespace jit
}  // namespace js

#endif  // jit_riscv64_constant_Constant_riscv64_i_h_
