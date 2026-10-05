// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef jit_riscv64_extension_Extension_riscv_zicond_h_
#define jit_riscv64_extension_Extension_riscv_zicond_h_

#include "jit/riscv64/base/base-assembler-riscv.h"
#include "jit/riscv64/Register-riscv64.h"

namespace js::jit {

class AssemblerRISCVZicond : public AssemblerRiscvBase {
 public:
  // CSR
  void czero_eqz(Register rd, Register rs1, Register rs2);
  void czero_nez(Register rd, Register rs1, Register rs2);
};

}  // namespace js::jit

#endif  // jit_riscv64_extension_Extension_riscv_zicond_h_
