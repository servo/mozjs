/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_riscv64_constant_Constant_riscv64_h
#define jit_riscv64_constant_Constant_riscv64_h

#include "jit/riscv64/constant/Base-constant-riscv.h"
#include "jit/riscv64/constant/Constant-riscv-a.h"
#include "jit/riscv64/constant/Constant-riscv-b.h"
#include "jit/riscv64/constant/Constant-riscv-c.h"
#include "jit/riscv64/constant/Constant-riscv-d.h"
#include "jit/riscv64/constant/Constant-riscv-f.h"
#include "jit/riscv64/constant/Constant-riscv-i.h"
#include "jit/riscv64/constant/Constant-riscv-m.h"
#include "jit/riscv64/constant/Constant-riscv-v.h"
#include "jit/riscv64/constant/Constant-riscv-zfa.h"
#include "jit/riscv64/constant/Constant-riscv-zfh.h"
#include "jit/riscv64/constant/Constant-riscv-zicond.h"
#include "jit/riscv64/constant/Constant-riscv-zicsr.h"
#include "jit/riscv64/constant/Constant-riscv-zifencei.h"

namespace js {
namespace jit {

// Difference between address of current opcode and value read from pc
// register.
static constexpr int kPcLoadDelta = 4;

// Bits available for offset field in branches
static constexpr int kBranchOffsetBits = 13;

// Bits available for offset field in jump
static constexpr int kJumpOffsetBits = 21;

// Bits available for offset field in compresed jump
static constexpr int kCJalOffsetBits = 12;

// Bits available for offset field in 4 branch
static constexpr int kCBranchOffsetBits = 9;

// Max offset for b instructions with 12-bit offset field (multiple of 2)
static constexpr int kMaxBranchOffset = (1 << (kBranchOffsetBits - 1)) - 1;

static constexpr int kCBranchOffset = (1 << (kCBranchOffsetBits - 1)) - 1;
// Max offset for jal instruction with 20-bit offset field (multiple of 2)
static constexpr int kMaxJumpOffset = (1 << (kJumpOffsetBits - 1)) - 1;

static constexpr int kCJumpOffset = (1 << (kCJalOffsetBits - 1)) - 1;

static_assert(kCJalOffsetBits == kOffset12);
static_assert(kCBranchOffsetBits == kOffset9);
static_assert(kJumpOffsetBits == kOffset21);
static_assert(kBranchOffsetBits == kOffset13);

}  // namespace jit
}  // namespace js

#endif  // jit_riscv64_constant_Constant_riscv64_h
