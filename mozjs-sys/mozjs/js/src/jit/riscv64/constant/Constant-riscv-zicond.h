// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef jit_riscv64_constant_Constant_riscv64_zicond_h_
#define jit_riscv64_constant_Constant_riscv64_zicond_h_

#include "jit/riscv64/constant/Base-constant-riscv.h"

namespace js::jit {

enum OpcodeRISCVZICOND : uint32_t {
  // RV32/RV64 Zicond Standard Extension
  RO_CZERO_EQZ = OP | (0b101 << kFunct3Shift) | (0b0000111 << kFunct7Shift),
  RO_CZERO_NEZ = OP | (0b111 << kFunct3Shift) | (0b0000111 << kFunct7Shift),
};

}  // namespace js::jit

#endif  // jit_riscv64_constant_Constant_riscv64_zicond_h_
