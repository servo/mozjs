// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "jit/riscv64/extension/extension-riscv-b.h"
#include "jit/riscv64/Assembler-riscv64.h"
#include "jit/riscv64/constant/Constant-riscv64.h"
#include "jit/riscv64/Architecture-riscv64.h"

namespace js {
namespace jit {

// RV32B Standard Extension
void AssemblerRISCVB::sh1add(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0010000, 0b010, rd, rs1, rs2);
}
void AssemblerRISCVB::sh2add(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0010000, 0b100, rd, rs1, rs2);
}
void AssemblerRISCVB::sh3add(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0010000, 0b110, rd, rs1, rs2);
}
void AssemblerRISCVB::add_uw(Register rd, Register rs1, Register rs2) {
  GenInstrALUW_rr(0b0000100, 0b000, rd, rs1, rs2);
}
void AssemblerRISCVB::sh1add_uw(Register rd, Register rs1, Register rs2) {
  GenInstrALUW_rr(0b0010000, 0b010, rd, rs1, rs2);
}
void AssemblerRISCVB::sh2add_uw(Register rd, Register rs1, Register rs2) {
  GenInstrALUW_rr(0b0010000, 0b100, rd, rs1, rs2);
}
void AssemblerRISCVB::sh3add_uw(Register rd, Register rs1, Register rs2) {
  GenInstrALUW_rr(0b0010000, 0b110, rd, rs1, rs2);
}
void AssemblerRISCVB::slli_uw(Register rd, Register rs1, uint8_t shamt) {
  GenInstrIShift(0b000010, 0b001, OP_IMM_32, rd, rs1, shamt);
}

void AssemblerRISCVB::andn(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0100000, 0b111, rd, rs1, rs2);
}
void AssemblerRISCVB::orn(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0100000, 0b110, rd, rs1, rs2);
}
void AssemblerRISCVB::xnor(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0100000, 0b100, rd, rs1, rs2);
}

void AssemblerRISCVB::clz(Register rd, Register rs) {
  GenInstrIShiftW(0b0110000, 0b001, OP_IMM, rd, rs, 0);
}
void AssemblerRISCVB::ctz(Register rd, Register rs) {
  GenInstrIShiftW(0b0110000, 0b001, OP_IMM, rd, rs, 1);
}
void AssemblerRISCVB::cpop(Register rd, Register rs) {
  GenInstrIShiftW(0b0110000, 0b001, OP_IMM, rd, rs, 2);
}
void AssemblerRISCVB::clzw(Register rd, Register rs) {
  GenInstrIShiftW(0b0110000, 0b001, OP_IMM_32, rd, rs, 0);
}
void AssemblerRISCVB::ctzw(Register rd, Register rs) {
  GenInstrIShiftW(0b0110000, 0b001, OP_IMM_32, rd, rs, 1);
}
void AssemblerRISCVB::cpopw(Register rd, Register rs) {
  GenInstrIShiftW(0b0110000, 0b001, OP_IMM_32, rd, rs, 2);
}

void AssemblerRISCVB::max(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000101, 0b110, rd, rs1, rs2);
}
void AssemblerRISCVB::maxu(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000101, 0b111, rd, rs1, rs2);
}
void AssemblerRISCVB::min(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000101, 0b100, rd, rs1, rs2);
}
void AssemblerRISCVB::minu(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000101, 0b101, rd, rs1, rs2);
}

void AssemblerRISCVB::sext_b(Register rd, Register rs) {
  GenInstrIShiftW(0b0110000, 0b001, OP_IMM, rd, rs, 0b100);
}
void AssemblerRISCVB::sext_h(Register rd, Register rs) {
  GenInstrIShiftW(0b0110000, 0b001, OP_IMM, rd, rs, 0b101);
}
void AssemblerRISCVB::zext_h(Register rd, Register rs) {
  GenInstrALUW_rr(0b0000100, 0b100, rd, rs, zero_reg);
}

void AssemblerRISCVB::rol(Register rd, Register rs1, Register rs2) {
  GenInstrR(0b0110000, 0b001, OP, rd, rs1, rs2);
}

void AssemblerRISCVB::ror(Register rd, Register rs1, Register rs2) {
  GenInstrR(0b0110000, 0b101, OP, rd, rs1, rs2);
}

void AssemblerRISCVB::orc_b(Register rd, Register rs) {
  GenInstrI(0b101, OP_IMM, rd, rs, 0b001010000111);
}

void AssemblerRISCVB::rori(Register rd, Register rs1, uint8_t shamt) {
  MOZ_ASSERT(is_uint6(shamt));
  GenInstrI(0b101, OP_IMM, rd, rs1, 0b011000000000 | shamt);
}

void AssemblerRISCVB::rolw(Register rd, Register rs1, Register rs2) {
  GenInstrR(0b0110000, 0b001, OP_32, rd, rs1, rs2);
}
void AssemblerRISCVB::roriw(Register rd, Register rs1, uint8_t shamt) {
  MOZ_ASSERT(is_uint5(shamt));
  GenInstrI(0b101, OP_IMM_32, rd, rs1, 0b011000000000 | shamt);
}
void AssemblerRISCVB::rorw(Register rd, Register rs1, Register rs2) {
  GenInstrR(0b0110000, 0b101, OP_32, rd, rs1, rs2);
}

void AssemblerRISCVB::rev8(Register rd, Register rs) {
  GenInstrI(0b101, OP_IMM, rd, rs, 0b011010111000);
}

void AssemblerRISCVB::bclr(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0100100, 0b001, rd, rs1, rs2);
}

void AssemblerRISCVB::bclri(Register rd, Register rs, uint8_t shamt) {
  GenInstrIShift(0b010010, 0b001, OP_IMM, rd, rs, shamt);
}
void AssemblerRISCVB::bext(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0100100, 0b101, rd, rs1, rs2);
}
void AssemblerRISCVB::bexti(Register rd, Register rs1, uint8_t shamt) {
  GenInstrIShift(0b010010, 0b101, OP_IMM, rd, rs1, shamt);
}
void AssemblerRISCVB::binv(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0110100, 0b001, rd, rs1, rs2);
}
void AssemblerRISCVB::binvi(Register rd, Register rs1, uint8_t shamt) {
  GenInstrIShift(0b011010, 0b001, OP_IMM, rd, rs1, shamt);
}
void AssemblerRISCVB::bset(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0010100, 0b001, rd, rs1, rs2);
}
void AssemblerRISCVB::bseti(Register rd, Register rs1, uint8_t shamt) {
  GenInstrIShift(0b001010, 0b001, OP_IMM, rd, rs1, shamt);
}
}  // namespace jit
}  // namespace js
