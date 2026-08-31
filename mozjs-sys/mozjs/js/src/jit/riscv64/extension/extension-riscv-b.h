// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef jit_riscv64_extension_Extension_riscv_b_h_
#define jit_riscv64_extension_Extension_riscv_b_h_

#include <stdint.h>

#include "jit/riscv64/base/base-assembler-riscv.h"
#include "jit/riscv64/Register-riscv64.h"

namespace js {
namespace jit {
class AssemblerRISCVB : public AssemblerRiscvBase {
  // RV32B Extension
 public:
  // Zba Extension
  void sh1add(Register rd, Register rs1, Register rs2);
  void sh2add(Register rd, Register rs1, Register rs2);
  void sh3add(Register rd, Register rs1, Register rs2);
  void add_uw(Register rd, Register rs1, Register rs2);
  void zext_w(Register rd, Register rs1) { add_uw(rd, rs1, zero_reg); }
  void sh1add_uw(Register rd, Register rs1, Register rs2);
  void sh2add_uw(Register rd, Register rs1, Register rs2);
  void sh3add_uw(Register rd, Register rs1, Register rs2);
  void slli_uw(Register rd, Register rs1, uint8_t shamt);

  // Zbb Extension
  void andn(Register rd, Register rs1, Register rs2);
  void orn(Register rd, Register rs1, Register rs2);
  void xnor(Register rd, Register rs1, Register rs2);

  void clz(Register rd, Register rs);
  void ctz(Register rd, Register rs);
  void cpop(Register rd, Register rs);
  void clzw(Register rd, Register rs);
  void ctzw(Register rd, Register rs);
  void cpopw(Register rd, Register rs);

  void max(Register rd, Register rs1, Register rs2);
  void maxu(Register rd, Register rs1, Register rs2);
  void min(Register rd, Register rs1, Register rs2);
  void minu(Register rd, Register rs1, Register rs2);

  void sext_b(Register rd, Register rs);
  void sext_h(Register rd, Register rs);
  void zext_h(Register rd, Register rs);

  // Zbb: bitwise rotation
  void rol(Register rd, Register rs1, Register rs2);
  void ror(Register rd, Register rs1, Register rs2);
  void rori(Register rd, Register rs1, uint8_t shamt);
  void orc_b(Register rd, Register rs);
  void rev8(Register rd, Register rs);
  void rolw(Register rd, Register rs1, Register rs2);
  void roriw(Register rd, Register rs1, uint8_t shamt);
  void rorw(Register rd, Register rs1, Register rs2);

  // Zbs
  void bclr(Register rd, Register rs1, Register rs2);
  void bclri(Register rd, Register rs1, uint8_t shamt);
  void bext(Register rd, Register rs1, Register rs2);
  void bexti(Register rd, Register rs1, uint8_t shamt);
  void binv(Register rd, Register rs1, Register rs2);
  void binvi(Register rd, Register rs1, uint8_t shamt);
  void bset(Register rd, Register rs1, Register rs2);
  void bseti(Register rd, Register rs1, uint8_t shamt);
};
}  // namespace jit
}  // namespace js
#endif
