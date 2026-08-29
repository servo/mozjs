// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "jit/riscv64/base/base-riscv-i.h"
#include "jit/riscv64/constant/Constant-riscv64.h"
#include "jit/riscv64/Assembler-riscv64.h"
#include "jit/riscv64/Architecture-riscv64.h"

namespace js {
namespace jit {

void AssemblerRISCVI::lui(Register rd, int32_t imm20) {
  GenInstrU(LUI, rd, imm20);
}

void AssemblerRISCVI::auipc(Register rd, int32_t imm20) {
  GenInstrU(AUIPC, rd, imm20);
}

// Jumps

CodeOffset AssemblerRISCVI::jal(Register rd, int32_t imm21) {
  GenInstrJ(JAL, rd, imm21);
  CodeOffset retAddr = CodeOffset(currentOffset());
  // FIXME: Pad short branches so that we don't need to care about decompression
  // when inserting veneers
  nop();
  return retAddr;
}

BufferOffset AssemblerRISCVI::jalr(Register rd, Register rs1, int16_t imm12) {
  return GenInstrI(0b000, JALR, rd, rs1, imm12);
}

// Branches

void AssemblerRISCVI::beq(Register rs1, Register rs2, int16_t imm13) {
  GenInstrBranchCC_rri(0b000, rs1, rs2, imm13);
  // FIXME: Pad short branches so that we don't need to care about decompression
  // when inserting veneers
  nop();
}

void AssemblerRISCVI::bne(Register rs1, Register rs2, int16_t imm13) {
  GenInstrBranchCC_rri(0b001, rs1, rs2, imm13);
  // FIXME: Pad short branches so that we don't need to care about decompression
  // when inserting veneers
  nop();
}

void AssemblerRISCVI::blt(Register rs1, Register rs2, int16_t imm13) {
  GenInstrBranchCC_rri(0b100, rs1, rs2, imm13);
  // FIXME: Pad short branches so that we don't need to care about decompression
  // when inserting veneers
  nop();
}

void AssemblerRISCVI::bge(Register rs1, Register rs2, int16_t imm13) {
  GenInstrBranchCC_rri(0b101, rs1, rs2, imm13);
  // FIXME: Pad short branches so that we don't need to care about decompression
  // when inserting veneers
  nop();
}

void AssemblerRISCVI::bltu(Register rs1, Register rs2, int16_t imm13) {
  GenInstrBranchCC_rri(0b110, rs1, rs2, imm13);
  // FIXME: Pad short branches so that we don't need to care about decompression
  // when inserting veneers
  nop();
}

void AssemblerRISCVI::bgeu(Register rs1, Register rs2, int16_t imm13) {
  GenInstrBranchCC_rri(0b111, rs1, rs2, imm13);
  // FIXME: Pad short branches so that we don't need to care about decompression
  // when inserting veneers
  nop();
}

// Loads

void AssemblerRISCVI::lb(Register rd, Register rs1, int16_t imm12) {
  GenInstrLoad_ri(0b000, rd, rs1, imm12);
}

void AssemblerRISCVI::lh(Register rd, Register rs1, int16_t imm12) {
  GenInstrLoad_ri(0b001, rd, rs1, imm12);
}

void AssemblerRISCVI::lw(Register rd, Register rs1, int16_t imm12) {
  GenInstrLoad_ri(0b010, rd, rs1, imm12);
}

void AssemblerRISCVI::lbu(Register rd, Register rs1, int16_t imm12) {
  GenInstrLoad_ri(0b100, rd, rs1, imm12);
}

void AssemblerRISCVI::lhu(Register rd, Register rs1, int16_t imm12) {
  GenInstrLoad_ri(0b101, rd, rs1, imm12);
}

// Stores

void AssemblerRISCVI::sb(Register source, Register base, int16_t imm12) {
  GenInstrStore_rri(0b000, base, source, imm12);
}

void AssemblerRISCVI::sh(Register source, Register base, int16_t imm12) {
  GenInstrStore_rri(0b001, base, source, imm12);
}

void AssemblerRISCVI::sw(Register source, Register base, int16_t imm12) {
  GenInstrStore_rri(0b010, base, source, imm12);
}

// Arithmetic with immediate

void AssemblerRISCVI::addi(Register rd, Register rs1, int16_t imm12) {
  GenInstrALU_ri(0b000, rd, rs1, imm12);
}

void AssemblerRISCVI::slti(Register rd, Register rs1, int16_t imm12) {
  GenInstrALU_ri(0b010, rd, rs1, imm12);
}

void AssemblerRISCVI::sltiu(Register rd, Register rs1, int16_t imm12) {
  GenInstrALU_ri(0b011, rd, rs1, imm12);
}

void AssemblerRISCVI::xori(Register rd, Register rs1, int16_t imm12) {
  GenInstrALU_ri(0b100, rd, rs1, imm12);
}

void AssemblerRISCVI::ori(Register rd, Register rs1, int16_t imm12) {
  GenInstrALU_ri(0b110, rd, rs1, imm12);
}

void AssemblerRISCVI::andi(Register rd, Register rs1, int16_t imm12) {
  GenInstrALU_ri(0b111, rd, rs1, imm12);
}

void AssemblerRISCVI::slli(Register rd, Register rs1, uint8_t shamt) {
  GenInstrShift_ri(0, 0b001, rd, rs1, shamt & 0x3f);
}

void AssemblerRISCVI::srli(Register rd, Register rs1, uint8_t shamt) {
  GenInstrShift_ri(0, 0b101, rd, rs1, shamt & 0x3f);
}

void AssemblerRISCVI::srai(Register rd, Register rs1, uint8_t shamt) {
  GenInstrShift_ri(1, 0b101, rd, rs1, shamt & 0x3f);
}

// Arithmetic

void AssemblerRISCVI::add(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000000, 0b000, rd, rs1, rs2);
}

void AssemblerRISCVI::sub(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0100000, 0b000, rd, rs1, rs2);
}

void AssemblerRISCVI::sll(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000000, 0b001, rd, rs1, rs2);
}

void AssemblerRISCVI::slt(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000000, 0b010, rd, rs1, rs2);
}

void AssemblerRISCVI::sltu(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000000, 0b011, rd, rs1, rs2);
}

void AssemblerRISCVI::xor_(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000000, 0b100, rd, rs1, rs2);
}

void AssemblerRISCVI::srl(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000000, 0b101, rd, rs1, rs2);
}

void AssemblerRISCVI::sra(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0100000, 0b101, rd, rs1, rs2);
}

void AssemblerRISCVI::or_(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000000, 0b110, rd, rs1, rs2);
}

void AssemblerRISCVI::and_(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000000, 0b111, rd, rs1, rs2);
}

// Memory fences

void AssemblerRISCVI::fence(uint8_t pred, uint8_t succ) {
  MOZ_ASSERT(is_uint4(pred) && is_uint4(succ));
  uint16_t imm12 = succ | (pred << 4) | (0b0000 << 8);
  GenInstrI(0b000, MISC_MEM, ToRegister(0UL), ToRegister(0UL), imm12);
}

void AssemblerRISCVI::fence_tso() {
  uint16_t imm12 = (0b0011) | (0b0011 << 4) | (0b1000 << 8);
  GenInstrI(0b000, MISC_MEM, ToRegister(0UL), ToRegister(0UL), imm12);
}

// Environment call / break

void AssemblerRISCVI::ecall() {
  GenInstrI(0b000, SYSTEM, ToRegister(0UL), ToRegister(0UL), 0);
}

void AssemblerRISCVI::ebreak() {
  GenInstrI(0b000, SYSTEM, ToRegister(0UL), ToRegister(0UL), 1);
}

// This is a de facto standard (as set by GNU binutils) 32-bit unimplemented
// instruction (i.e., it should always trap, if your implementation has invalid
// instruction traps).
void AssemblerRISCVI::unimp() {
  GenInstrI(0b001, SYSTEM, ToRegister(0), ToRegister(0), 0b110000000000);
}

void AssemblerRISCVI::lwu(Register rd, Register rs1, int16_t imm12) {
  GenInstrLoad_ri(0b110, rd, rs1, imm12);
}

void AssemblerRISCVI::ld(Register rd, Register rs1, int16_t imm12) {
  GenInstrLoad_ri(0b011, rd, rs1, imm12);
}

void AssemblerRISCVI::sd(Register source, Register base, int16_t imm12) {
  GenInstrStore_rri(0b011, base, source, imm12);
}

void AssemblerRISCVI::addiw(Register rd, Register rs1, int16_t imm12) {
  GenInstrI(0b000, OP_IMM_32, rd, rs1, imm12);
}

void AssemblerRISCVI::slliw(Register rd, Register rs1, uint8_t shamt) {
  GenInstrShiftW_ri(0, 0b001, rd, rs1, shamt & 0x1f);
}

void AssemblerRISCVI::srliw(Register rd, Register rs1, uint8_t shamt) {
  GenInstrShiftW_ri(0, 0b101, rd, rs1, shamt & 0x1f);
}

void AssemblerRISCVI::sraiw(Register rd, Register rs1, uint8_t shamt) {
  GenInstrShiftW_ri(1, 0b101, rd, rs1, shamt & 0x1f);
}

void AssemblerRISCVI::addw(Register rd, Register rs1, Register rs2) {
  GenInstrALUW_rr(0b0000000, 0b000, rd, rs1, rs2);
}

void AssemblerRISCVI::subw(Register rd, Register rs1, Register rs2) {
  GenInstrALUW_rr(0b0100000, 0b000, rd, rs1, rs2);
}

void AssemblerRISCVI::sllw(Register rd, Register rs1, Register rs2) {
  GenInstrALUW_rr(0b0000000, 0b001, rd, rs1, rs2);
}

void AssemblerRISCVI::srlw(Register rd, Register rs1, Register rs2) {
  GenInstrALUW_rr(0b0000000, 0b101, rd, rs1, rs2);
}

void AssemblerRISCVI::sraw(Register rd, Register rs1, Register rs2) {
  GenInstrALUW_rr(0b0100000, 0b101, rd, rs1, rs2);
}

}  // namespace jit
}  // namespace js
