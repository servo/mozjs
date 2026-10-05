// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef jit_riscv64_constant_Constant_riscv64_b_h_
#define jit_riscv64_constant_Constant_riscv64_b_h_

#include "jit/riscv64/constant/Base-constant-riscv.h"

namespace js {
namespace jit {

enum OpcodeRISCVB : uint32_t {
  RO_ADDUW = OP_32 | (0b000 << kFunct3Shift) | (0b0000100 << kFunct7Shift),
  RO_SH1ADDUW = OP_32 | (0b010 << kFunct3Shift) | (0b0010000 << kFunct7Shift),
  RO_SH2ADDUW = OP_32 | (0b100 << kFunct3Shift) | (0b0010000 << kFunct7Shift),
  RO_SH3ADDUW = OP_32 | (0b110 << kFunct3Shift) | (0b0010000 << kFunct7Shift),
  RO_SLLIUW = OP_IMM_32 | (0b001 << kFunct3Shift) | (0b000010 << kFunct6Shift),
  RO_SH1ADD = OP | (0b010 << kFunct3Shift) | (0b0010000 << kFunct7Shift),
  RO_SH2ADD = OP | (0b100 << kFunct3Shift) | (0b0010000 << kFunct7Shift),
  RO_SH3ADD = OP | (0b110 << kFunct3Shift) | (0b0010000 << kFunct7Shift),

  // Zbb
  RO_ANDN = OP | (0b111 << kFunct3Shift) | (0b0100000 << kFunct7Shift),
  RO_ORN = OP | (0b110 << kFunct3Shift) | (0b0100000 << kFunct7Shift),
  RO_XNOR = OP | (0b100 << kFunct3Shift) | (0b0100000 << kFunct7Shift),
  OP_COUNT = OP_IMM | (0b001 << kFunct3Shift) | (0b0110000 << kFunct7Shift),
  RO_CLZ = OP_COUNT | (0b00000 << kShamtShift),
  RO_CTZ = OP_COUNT | (0b00001 << kShamtShift),
  RO_CPOP = OP_COUNT | (0b00010 << kShamtShift),
  OP_COUNTW = OP_IMM_32 | (0b001 << kFunct3Shift) | (0b0110000 << kFunct7Shift),
  RO_CLZW = OP_COUNTW | (0b00000 << kShamtShift),
  RO_CTZW = OP_COUNTW | (0b00001 << kShamtShift),
  RO_CPOPW = OP_COUNTW | (0b00010 << kShamtShift),
  RO_MAX = OP | (0b110 << kFunct3Shift) | (0b0000101 << kFunct7Shift),
  RO_MAXU = OP | (0b111 << kFunct3Shift) | (0b0000101 << kFunct7Shift),
  RO_MIN = OP | (0b100 << kFunct3Shift) | (0b0000101 << kFunct7Shift),
  RO_MINU = OP | (0b101 << kFunct3Shift) | (0b0000101 << kFunct7Shift),
  RO_SEXTB = OP_IMM | (0b001 << kFunct3Shift) | (0b0110000 << kFunct7Shift) |
             (0b00100 << kShamtShift),
  RO_SEXTH = OP_IMM | (0b001 << kFunct3Shift) | (0b0110000 << kFunct7Shift) |
             (0b00101 << kShamtShift),
  RO_ZEXTH = OP_32 | (0b100 << kFunct3Shift) | (0b0000100 << kFunct7Shift) |
             (0b00000 << kShamtShift),

  // Zbb: bitwise rotation
  RO_ROL = OP | (0b001 << kFunct3Shift) | (0b0110000 << kFunct7Shift),
  RO_ROR = OP | (0b101 << kFunct3Shift) | (0b0110000 << kFunct7Shift),
  RO_ORCB = OP_IMM | (0b101 << kFunct3Shift) | (0b001010000111 << kImm12Shift),
  RO_RORI = OP_IMM | (0b101 << kFunct3Shift) | (0b011000 << kFunct6Shift),

  RO_ROLW = OP_32 | (0b001 << kFunct3Shift) | (0b0110000 << kFunct7Shift),
  RO_RORIW = OP_IMM_32 | (0b101 << kFunct3Shift) | (0b0110000 << kFunct7Shift),
  RO_RORW = OP_32 | (0b101 << kFunct3Shift) | (0b0110000 << kFunct7Shift),

  RO_REV8 = OP_IMM | (0b101 << kFunct3Shift) | (0b011010 << kFunct6Shift),
  RO_REV8_IMM12 = 0b011010111000,

  // Zbs
  RO_BCLR = OP | (0b001 << kFunct3Shift) | (0b0100100 << kFunct7Shift),
  RO_BCLRI = OP_IMM | (0b001 << kFunct3Shift) | (0b010010 << kFunct6Shift),

  RO_BEXT = OP | (0b101 << kFunct3Shift) | (0b0100100 << kFunct7Shift),
  RO_BEXTI = OP_IMM | (0b101 << kFunct3Shift) | (0b010010 << kFunct6Shift),

  RO_BINV = OP | (0b001 << kFunct3Shift) | (0b0110100 << kFunct7Shift),
  RO_BINVI = OP_IMM | (0b001 << kFunct3Shift) | (0b011010 << kFunct6Shift),

  RO_BSET = OP | (0b001 << kFunct3Shift) | (0b0010100 << kFunct7Shift),
  RO_BSETI = OP_IMM | (0b001 << kFunct3Shift) | (0b0010100 << kFunct7Shift),
};

}  // namespace jit
}  // namespace js

#endif
