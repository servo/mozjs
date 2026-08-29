// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef jit_riscv64_constant_Constant_riscv64_zfh_h_
#define jit_riscv64_constant_Constant_riscv64_zfh_h_

#include "jit/riscv64/constant/Base-constant-riscv.h"

namespace js::jit {

enum OpcodeRISCVZFH : uint32_t {
  // RV32F Standard Extension
  RO_FLH = LOAD_FP | (0b001 << kFunct3Shift),
  RO_FSH = STORE_FP | (0b001 << kFunct3Shift),
  RO_FMADD_H = MADD | (0b10 << kFunct2Shift),
  RO_FMSUB_H = MSUB | (0b10 << kFunct2Shift),
  RO_FNMSUB_H = NMSUB | (0b10 << kFunct2Shift),
  RO_FNMADD_H = NMADD | (0b10 << kFunct2Shift),
  RO_FADD_H = OP_FP | (0b0000010 << kFunct7Shift),
  RO_FSUB_H = OP_FP | (0b0000110 << kFunct7Shift),
  RO_FMUL_H = OP_FP | (0b0001010 << kFunct7Shift),
  RO_FDIV_H = OP_FP | (0b0001110 << kFunct7Shift),
  RO_FSQRT_H = OP_FP | (0b0101110 << kFunct7Shift) | (0b00000 << kRs2Shift),
  RO_FSGNJ_H = OP_FP | (0b000 << kFunct3Shift) | (0b0010010 << kFunct7Shift),
  RO_FSGNJN_H = OP_FP | (0b001 << kFunct3Shift) | (0b0010010 << kFunct7Shift),
  RO_FSQNJX_H = OP_FP | (0b010 << kFunct3Shift) | (0b0010010 << kFunct7Shift),
  RO_FMIN_H = OP_FP | (0b000 << kFunct3Shift) | (0b0010110 << kFunct7Shift),
  RO_FMAX_H = OP_FP | (0b001 << kFunct3Shift) | (0b0010110 << kFunct7Shift),
  RO_FCVT_W_H = OP_FP | (0b1100010 << kFunct7Shift) | (0b00000 << kRs2Shift),
  RO_FCVT_WU_H = OP_FP | (0b1100010 << kFunct7Shift) | (0b00001 << kRs2Shift),
  RO_FMV_X_H = OP_FP | (0b1110010 << kFunct7Shift) | (0b000 << kFunct3Shift) |
               (0b00000 << kRs2Shift),
  RO_FEQ_H = OP_FP | (0b010 << kFunct3Shift) | (0b1010010 << kFunct7Shift),
  RO_FLT_H = OP_FP | (0b001 << kFunct3Shift) | (0b1010010 << kFunct7Shift),
  RO_FLE_H = OP_FP | (0b000 << kFunct3Shift) | (0b1010010 << kFunct7Shift),
  RO_FCLASS_H = OP_FP | (0b001 << kFunct3Shift) | (0b1110010 << kFunct7Shift),
  RO_FMV_H_X = OP_FP | (0b000 << kFunct3Shift) | (0b1111010 << kFunct7Shift),

  RO_FCVT_H_W = OP_FP | (0b1101010 << kFunct7Shift) | (0b00000 << kRs2Shift),
  RO_FCVT_H_WU = OP_FP | (0b1101010 << kFunct7Shift) | (0b00001 << kRs2Shift),
  RO_FCVT_D_H = OP_FP | (0b0100001 << kFunct7Shift) | (0b00010 << kRs2Shift),
  RO_FCVT_S_H = OP_FP | (0b0100000 << kFunct7Shift) | (0b00010 << kRs2Shift),

  RO_FCVT_H_D = OP_FP | (0b0100010 << kFunct7Shift) | (0b00001 << kRs2Shift),
  RO_FCVT_H_S = OP_FP | (0b0100010 << kFunct7Shift) | (0b00000 << kRs2Shift),

  // RV64F Standard Extension (in addition to RV32F)
  RO_FCVT_L_H = OP_FP | (0b1100010 << kFunct7Shift) | (0b00010 << kRs2Shift),
  RO_FCVT_LU_H = OP_FP | (0b1100010 << kFunct7Shift) | (0b00011 << kRs2Shift),
  RO_FCVT_H_L = OP_FP | (0b1101010 << kFunct7Shift) | (0b00010 << kRs2Shift),
  RO_FCVT_H_LU = OP_FP | (0b1101010 << kFunct7Shift) | (0b00011 << kRs2Shift),
};

}  // namespace js::jit

#endif  // jit_riscv64_constant_Constant_riscv64_zfh_h_
