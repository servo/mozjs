/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
//===- RISCVMatInt.cpp - Immediate materialisation -------------*- C++
//
//  Part of the LLVM Project, under the Apache License v2.0 with LLVM
//  Exceptions. See https://llvm.org/LICENSE.txt for license information.
//  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <bit>

#include "jit/riscv64/Assembler-riscv64.h"
#include "jit/riscv64/base/Integer.h"

namespace js {
namespace jit {
void Assembler::RecursiveLi(Register rd, int64_t imm) {
  if (imm > 0 && RecursiveLiImplCount(imm) > 2) {
    unsigned LeadingZeros = std::countl_zero((uint64_t)imm);
    uint64_t ShiftedVal = (uint64_t)imm << LeadingZeros;
    int countFillZero = RecursiveLiImplCount(ShiftedVal) + 1;
    if (countFillZero < RecursiveLiImplCount(imm)) {
      RecursiveLiImpl(rd, ShiftedVal);
      srli(rd, rd, LeadingZeros);
      return;
    }
  }
  RecursiveLiImpl(rd, imm);
}

int Assembler::RecursiveLiCount(int64_t imm) {
  if (imm > 0 && RecursiveLiImplCount(imm) > 2) {
    unsigned LeadingZeros = std::countl_zero((uint64_t)imm);
    uint64_t ShiftedVal = (uint64_t)imm << LeadingZeros;
    // Fill in the bits that will be shifted out with 1s. An example where
    // this helps is trailing one masks with 32 or more ones. This will
    // generate ADDI -1 and an SRLI.
    int countFillZero = RecursiveLiImplCount(ShiftedVal) + 1;
    if (countFillZero < RecursiveLiImplCount(imm)) {
      return countFillZero;
    }
  }
  return RecursiveLiImplCount(imm);
}

inline int64_t signExtend(uint64_t V, int N) {
  return int64_t(V << (64 - N)) >> (64 - N);
}

void Assembler::RecursiveLiImpl(Register rd, int64_t imm) {
  if (is_int32(imm)) {
    // Depending on the active bits in the immediate Value v, the following
    // instruction sequences are emitted:
    //
    // v == 0                        : ADDI
    // v[0,12) != 0 && v[12,32) == 0 : ADDI
    // v[0,12) == 0 && v[12,32) != 0 : LUI
    // v[0,32) != 0                  : LUI+ADDI(W)
    auto [Hi20, Lo12] = ToHigh20Low12(int32_t(imm));

    if (Hi20) {
      lui(rd, (int32_t)Hi20);
    }

    if (Lo12 || Hi20 == 0) {
      if (Hi20) {
        addiw(rd, rd, Lo12);
      } else {
        addi(rd, zero_reg, Lo12);
      }
    }
    return;
  }

  // In the worst case, for a full 64-bit constant, a sequence of 8
  // instructions (i.e., LUI+ADDIW+SLLI+ADDI+SLLI+ADDI+SLLI+ADDI) has to be
  // emitted. Note that the first two instructions (LUI+ADDIW) can contribute
  // up to 32 bits while the following ADDI instructions contribute up to 12
  // bits each.
  //
  // On the first glance, implementing this seems to be possible by simply
  // emitting the most significant 32 bits (LUI+ADDIW) followed by as many
  // left shift (SLLI) and immediate additions (ADDI) as needed. However, due
  // to the fact that ADDI performs a sign extended addition, doing it like
  // that would only be possible when at most 11 bits of the ADDI instructions
  // are used. Using all 12 bits of the ADDI instructions, like done by GAS,
  // actually requires that the constant is processed starting with the least
  // significant bit.
  //
  // In the following, constants are processed from LSB to MSB but instruction
  // emission is performed from MSB to LSB by recursively calling
  // generateInstSeq. In each recursion, first the lowest 12 bits are removed
  // from the constant and the optimal shift amount, which can be greater than
  // 12 bits if the constant is sparse, is determined. Then, the shifted
  // remaining constant is processed recursively and gets emitted as soon as
  // it fits into 32 bits. The emission of the shifts and additions is
  // subsequently performed when the recursion returns.

  int64_t Lo12 = imm << 52 >> 52;
  int64_t Hi52 = ((uint64_t)imm + 0x800ull) >> 12;
  int ShiftAmount = 12 + std::countr_zero((uint64_t)Hi52);
  Hi52 = signExtend(Hi52 >> (ShiftAmount - 12), 64 - ShiftAmount);

  // If the remaining bits don't fit in 12 bits, we might be able to reduce
  // the shift amount in order to use LUI which will zero the lower 12 bits.
  bool Unsigned = false;
  if (ShiftAmount > 12 && !is_int12(Hi52)) {
    if (is_int32((uint64_t)Hi52 << 12)) {
      // Reduce the shift amount and add zeros to the LSBs so it will match
      // LUI.
      ShiftAmount -= 12;
      Hi52 = (uint64_t)Hi52 << 12;
    }
  }
  RecursiveLi(rd, Hi52);

  if (Unsigned) {
  } else {
    slli(rd, rd, ShiftAmount);
  }
  if (Lo12) {
    addi(rd, rd, Lo12);
  }
}

int Assembler::RecursiveLiImplCount(int64_t imm) {
  int count = 0;
  if (is_int32(imm)) {
    // Depending on the active bits in the immediate Value v, the following
    // instruction sequences are emitted:
    //
    // v == 0                        : ADDI
    // v[0,12) != 0 && v[12,32) == 0 : ADDI
    // v[0,12) == 0 && v[12,32) != 0 : LUI
    // v[0,32) != 0                  : LUI+ADDI(W)
    auto [Hi20, Lo12] = ToHigh20Low12(int32_t(imm));

    if (Hi20) {
      // lui(rd, (int32_t)Hi20);
      count++;
    }

    if (Lo12 || Hi20 == 0) {
      //   unsigned AddiOpc = (IsRV64 && Hi20) ? RISCV::ADDIW : RISCV::ADDI;
      //   Res.push_back(RISCVMatInt::Inst(AddiOpc, Lo12));
      count++;
    }
    return count;
  }

  // In the worst case, for a full 64-bit constant, a sequence of 8
  // instructions (i.e., LUI+ADDIW+SLLI+ADDI+SLLI+ADDI+SLLI+ADDI) has to be
  // emitted. Note that the first two instructions (LUI+ADDIW) can contribute
  // up to 32 bits while the following ADDI instructions contribute up to 12
  // bits each.
  //
  // On the first glance, implementing this seems to be possible by simply
  // emitting the most significant 32 bits (LUI+ADDIW) followed by as many
  // left shift (SLLI) and immediate additions (ADDI) as needed. However, due
  // to the fact that ADDI performs a sign extended addition, doing it like
  // that would only be possible when at most 11 bits of the ADDI instructions
  // are used. Using all 12 bits of the ADDI instructions, like done by GAS,
  // actually requires that the constant is processed starting with the least
  // significant bit.
  //
  // In the following, constants are processed from LSB to MSB but instruction
  // emission is performed from MSB to LSB by recursively calling
  // generateInstSeq. In each recursion, first the lowest 12 bits are removed
  // from the constant and the optimal shift amount, which can be greater than
  // 12 bits if the constant is sparse, is determined. Then, the shifted
  // remaining constant is processed recursively and gets emitted as soon as
  // it fits into 32 bits. The emission of the shifts and additions is
  // subsequently performed when the recursion returns.

  int64_t Lo12 = imm << 52 >> 52;
  int64_t Hi52 = ((uint64_t)imm + 0x800ull) >> 12;
  int ShiftAmount = 12 + std::countr_zero((uint64_t)Hi52);
  Hi52 = signExtend(Hi52 >> (ShiftAmount - 12), 64 - ShiftAmount);

  // If the remaining bits don't fit in 12 bits, we might be able to reduce
  // the shift amount in order to use LUI which will zero the lower 12 bits.
  bool Unsigned = false;
  if (ShiftAmount > 12 && !is_int12(Hi52)) {
    if (is_int32((uint64_t)Hi52 << 12)) {
      // Reduce the shift amount and add zeros to the LSBs so it will match
      // LUI.
      ShiftAmount -= 12;
      Hi52 = (uint64_t)Hi52 << 12;
    }
  }

  count += RecursiveLiImplCount(Hi52);

  if (Unsigned) {
  } else {
    // slli(rd, rd, ShiftAmount);
    count++;
  }
  if (Lo12) {
    // addi(rd, rd, Lo12);
    count++;
  }
  return count;
}

}  // namespace jit
}  // namespace js
