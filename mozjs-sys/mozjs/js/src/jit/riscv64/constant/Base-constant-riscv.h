// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef jit_riscv64_constant_Base_constant_riscv_h_
#define jit_riscv64_constant_Base_constant_riscv_h_

#include "mozilla/Assertions.h"

#include <stdint.h>

namespace js {
namespace jit {

// On RISC-V Simulator breakpoints can have different codes:
// - Breaks between 0 and kMaxWatchpointCode are treated as simple watchpoints,
//   the simulator will run through them and print the registers.
// - Breaks between kMaxWatchpointCode and kMaxStopCode are treated as stop()
//   instructions (see Assembler::stop()).
// - Breaks larger than kMaxStopCode are simple breaks, dropping you into the
//   debugger.
const uint32_t kMaxTracepointCode = 63;
const uint32_t kMaxWatchpointCode = 31;
const uint32_t kMaxStopCode = 127;
const uint32_t kWasmTrapCode = 6;
static_assert(kMaxWatchpointCode < kMaxStopCode);
static_assert(kMaxTracepointCode < kMaxStopCode);

// Debug parameters.
//
// For example:
//
// __ Debug(TRACE_ENABLE | LOG_TRACE);
// starts tracing: set riscv-sim-trace is true.
// __ Debug(TRACE_ENABLE | LOG_REGS);
// PrintAllregs.
// __ Debug(TRACE_DISABLE | LOG_TRACE);
// stops tracing: set riscv-sim-trace is false.
const uint32_t kDebuggerTracingDirectivesMask = 0b111 << 3;
enum DebugParameters : uint32_t {
  NO_PARAM = 1 << 5,
  BREAK = 1 << 0,
  LOG_TRACE = 1 << 1,
  LOG_REGS = 1 << 2,
  LOG_ALL = LOG_TRACE,
  // Trace control.
  TRACE_ENABLE = 1 << 3 | NO_PARAM,
  TRACE_DISABLE = 1 << 4 | NO_PARAM,
};

// ----- Fields offset and length.
// RISCV constants
const int kBaseOpcodeShift = 0;
const int kBaseOpcodeBits = 7;
const int kFunct6Shift = 26;
const int kFunct6Bits = 6;
const int kFunct7Shift = 25;
const int kFunct7Bits = 7;
const int kFunct5Shift = 27;
const int kFunct5Bits = 5;
const int kFunct3Shift = 12;
const int kFunct3Bits = 3;
const int kFunct2Shift = 25;
const int kFunct2Bits = 2;
const int kRs1Shift = 15;
const int kRs1Bits = 5;
const int kVs1Shift = 15;
const int kVs1Bits = 5;
const int kVs2Shift = 20;
const int kVs2Bits = 5;
const int kVdShift = 7;
const int kVdBits = 5;
const int kRs2Shift = 20;
const int kRs2Bits = 5;
const int kRs3Shift = 27;
const int kRs3Bits = 5;
const int kRdShift = 7;
const int kRdBits = 5;
const int kRlShift = 25;
const int kAqShift = 26;
const int kImm12Shift = 20;
const int kImm12Bits = 12;
const int kImm11Shift = 2;
const int kImm11Bits = 11;
const int kShamtShift = 20;
const int kShamtBits = 5;
const uint32_t kShamtMask = (((1 << kShamtBits) - 1) << kShamtShift);
const int kShamtWShift = 20;
// FIXME: remove this once we have a proper way to handle the wide shift amount
const int kShamtWBits = 6;
const int kArithShiftShift = 30;
const int kImm20Shift = 12;
const int kImm20Bits = 20;
const int kCsrShift = 20;
const int kCsrBits = 12;
const int kMemOrderBits = 4;
const int kPredOrderShift = 24;
const int kSuccOrderShift = 20;

// for C extension
const int kRvcFunct4Shift = 12;
const int kRvcFunct4Bits = 4;
const int kRvcFunct3Shift = 13;
const int kRvcFunct3Bits = 3;
const int kRvcRs1Shift = 7;
const int kRvcRs1Bits = 5;
const int kRvcRs2Shift = 2;
const int kRvcRs2Bits = 5;
const int kRvcRdShift = 7;
const int kRvcRdBits = 5;
const int kRvcRs1sShift = 7;
const int kRvcRs1sBits = 3;
const int kRvcRs2sShift = 2;
const int kRvcRs2sBits = 3;
const int kRvcFunct2Shift = 5;
const int kRvcFunct2BShift = 10;
const int kRvcFunct2Bits = 2;
const int kRvcFunct6Shift = 10;
const int kRvcFunct6Bits = 6;

const uint32_t kRvcOpcodeMask =
    0b11 | (((1 << kRvcFunct3Bits) - 1) << kRvcFunct3Shift);
const uint32_t kRvcFunct3Mask =
    (((1 << kRvcFunct3Bits) - 1) << kRvcFunct3Shift);
const uint32_t kRvcFunct4Mask =
    (((1 << kRvcFunct4Bits) - 1) << kRvcFunct4Shift);
const uint32_t kRvcFunct6Mask =
    (((1 << kRvcFunct6Bits) - 1) << kRvcFunct6Shift);
const uint32_t kRvcFunct2Mask =
    (((1 << kRvcFunct2Bits) - 1) << kRvcFunct2Shift);
const uint32_t kRvcFunct2BMask =
    (((1 << kRvcFunct2Bits) - 1) << kRvcFunct2BShift);
const uint32_t kCRTypeMask = kRvcOpcodeMask | kRvcFunct4Mask;
const uint32_t kCSTypeMask = kRvcOpcodeMask | kRvcFunct6Mask;
const uint32_t kCATypeMask = kRvcOpcodeMask | kRvcFunct6Mask | kRvcFunct2Mask;
const uint32_t kRvcBImm8Mask = (((1 << 5) - 1) << 2) | (((1 << 3) - 1) << 10);

// RISCV Instruction bit masks
const uint32_t kBaseOpcodeMask = ((1 << kBaseOpcodeBits) - 1)
                                 << kBaseOpcodeShift;
const uint32_t kFunct3Mask = ((1 << kFunct3Bits) - 1) << kFunct3Shift;
const uint32_t kFunct5Mask = ((1 << kFunct5Bits) - 1) << kFunct5Shift;
const uint32_t kFunct6Mask = ((1 << kFunct6Bits) - 1) << kFunct6Shift;
const uint32_t kFunct7Mask = ((1 << kFunct7Bits) - 1) << kFunct7Shift;
const uint32_t kFunct2Mask = 0b11 << kFunct7Shift;
const uint32_t kRTypeMask = kBaseOpcodeMask | kFunct3Mask | kFunct7Mask;
const uint32_t kRATypeMask = kBaseOpcodeMask | kFunct3Mask | kFunct5Mask;
const uint32_t kRFPTypeMask = kBaseOpcodeMask | kFunct7Mask;
const uint32_t kR4TypeMask = kBaseOpcodeMask | kFunct3Mask | kFunct2Mask;
const uint32_t kITypeMask = kBaseOpcodeMask | kFunct3Mask;
const uint32_t kSTypeMask = kBaseOpcodeMask | kFunct3Mask;
const uint32_t kBTypeMask = kBaseOpcodeMask | kFunct3Mask;
const uint32_t kUTypeMask = kBaseOpcodeMask;
const uint32_t kJTypeMask = kBaseOpcodeMask;
const uint32_t kRs1FieldMask = ((1 << kRs1Bits) - 1) << kRs1Shift;
const uint32_t kRs2FieldMask = ((1 << kRs2Bits) - 1) << kRs2Shift;
const uint32_t kRs3FieldMask = ((1 << kRs3Bits) - 1) << kRs3Shift;
const uint32_t kRdFieldMask = ((1 << kRdBits) - 1) << kRdShift;
const uint32_t kBImm12Mask = kFunct7Mask | kRdFieldMask;
const uint32_t kImm20Mask = ((1 << kImm20Bits) - 1) << kImm20Shift;
const uint32_t kImm12Mask = ((1 << kImm12Bits) - 1) << kImm12Shift;
const uint32_t kImm11Mask = ((1 << kImm11Bits) - 1) << kImm11Shift;

// for RVV extension
#define RVV_LMUL(V) \
  V(m1)             \
  V(m2)             \
  V(m4)             \
  V(m8)             \
  V(RESERVERD)      \
  V(mf8)            \
  V(mf4)            \
  V(mf2)

enum Vlmul {
#define DEFINE_FLAG(name) name,
  RVV_LMUL(DEFINE_FLAG)
#undef DEFINE_FLAG
};

#define RVV_SEW(V) \
  V(E8)            \
  V(E16)           \
  V(E32)           \
  V(E64)

#define DEFINE_FLAG(name) name,
enum VSew {
  RVV_SEW(DEFINE_FLAG)
#undef DEFINE_FLAG
};

constexpr int kRvvELEN = 64;
constexpr int kRvvVLEN = 128;
constexpr int kRvvSLEN = kRvvVLEN;
const int kRvvFunct6Shift = 26;
const int kRvvFunct6Bits = 6;
const uint32_t kRvvFunct6Mask =
    (((1 << kRvvFunct6Bits) - 1) << kRvvFunct6Shift);

const int kRvvVmBits = 1;
const int kRvvVmShift = 25;
const uint32_t kRvvVmMask = (((1 << kRvvVmBits) - 1) << kRvvVmShift);

const int kRvvVs2Bits = 5;
const int kRvvVs2Shift = 20;
const uint32_t kRvvVs2Mask = (((1 << kRvvVs2Bits) - 1) << kRvvVs2Shift);

const int kRvvVs1Bits = 5;
const int kRvvVs1Shift = 15;
const uint32_t kRvvVs1Mask = (((1 << kRvvVs1Bits) - 1) << kRvvVs1Shift);

const int kRvvRs1Bits = kRvvVs1Bits;
const int kRvvRs1Shift = kRvvVs1Shift;
const uint32_t kRvvRs1Mask = (((1 << kRvvRs1Bits) - 1) << kRvvRs1Shift);

const int kRvvRs2Bits = 5;
const int kRvvRs2Shift = 20;
const uint32_t kRvvRs2Mask = (((1 << kRvvRs2Bits) - 1) << kRvvRs2Shift);

const int kRvvImm5Bits = kRvvVs1Bits;
const int kRvvImm5Shift = kRvvVs1Shift;
const uint32_t kRvvImm5Mask = (((1 << kRvvImm5Bits) - 1) << kRvvImm5Shift);

const int kRvvVdBits = 5;
const int kRvvVdShift = 7;
const uint32_t kRvvVdMask = (((1 << kRvvVdBits) - 1) << kRvvVdShift);

const int kRvvRdBits = kRvvVdBits;
const int kRvvRdShift = kRvvVdShift;
const uint32_t kRvvRdMask = (((1 << kRvvRdBits) - 1) << kRvvRdShift);

const int kRvvZimmBits = 11;
const int kRvvZimmShift = 20;
const uint32_t kRvvZimmMask = (((1 << kRvvZimmBits) - 1) << kRvvZimmShift);

const int kRvvUimmShift = kRvvRs1Shift;
const int kRvvUimmBits = kRvvRs1Bits;
const uint32_t kRvvUimmMask = (((1 << kRvvUimmBits) - 1) << kRvvUimmShift);

const int kRvvWidthBits = 3;
const int kRvvWidthShift = 12;
const uint32_t kRvvWidthMask = (((1 << kRvvWidthBits) - 1) << kRvvWidthShift);

const int kRvvMopBits = 2;
const int kRvvMopShift = 26;
const uint32_t kRvvMopMask = (((1 << kRvvMopBits) - 1) << kRvvMopShift);

const int kRvvMewBits = 1;
const int kRvvMewShift = 28;
const uint32_t kRvvMewMask = (((1 << kRvvMewBits) - 1) << kRvvMewShift);

const int kRvvNfBits = 3;
const int kRvvNfShift = 29;
const uint32_t kRvvNfMask = (((1 << kRvvNfBits) - 1) << kRvvNfShift);

const int kNopByte = 0x00000013;

enum BaseOpcode : uint32_t {
  LOAD = 0b0000011,       // I form: LB LH LW LBU LHU
  LOAD_FP = 0b0000111,    // I form: FLW FLD FLQ
  MISC_MEM = 0b0001111,   // I special form: FENCE FENCE.I
  OP_IMM = 0b0010011,     // I form: ADDI SLTI SLTIU XORI ORI ANDI
  AUIPC = 0b0010111,      // U form: AUIPC
  OP_IMM_32 = 0b0011011,  // I form: ADDIW SLLIW SRLIW SRAIW
  // Note:  SRLIW SRAIW I form first, then func3 101 special shift encoding
  STORE = 0b0100011,     // S form: SB SH SW SD
  STORE_FP = 0b0100111,  // S form: FSW FSD FSQ
  AMO = 0b0101111,       // R form: All A instructions
  OP = 0b0110011,      // R: ADD SUB SLL SLT SLTU XOR SRL SRA OR AND and 32M set
  LUI = 0b0110111,     // U form: LUI
  OP_32 = 0b0111011,   // R: ADDW SUBW SLLW SRLW SRAW MULW DIVW DIVUW REMW REMUW
  MADD = 0b1000011,    // R4 type: FMADD.S FMADD.D FMADD.Q
  MSUB = 0b1000111,    // R4 type: FMSUB.S FMSUB.D FMSUB.Q
  NMSUB = 0b1001011,   // R4 type: FNMSUB.S FNMSUB.D FNMSUB.Q
  NMADD = 0b1001111,   // R4 type: FNMADD.S FNMADD.D FNMADD.Q
  OP_FP = 0b1010011,   // R type: Q ext
  BRANCH = 0b1100011,  // B form: BEQ BNE, BLT, BGE, BLTU BGEU
  JALR = 0b1100111,    // I form: JALR
  JAL = 0b1101111,     // J form: JAL
  SYSTEM = 0b1110011,  // I form: ECALL EBREAK Zicsr ext
  OP_V = 0b1010111,    // V form: RVV

  // C extension
  C0 = 0b00,
  C1 = 0b01,
  C2 = 0b10,
  FUNCT2_0 = 0b00,
  FUNCT2_1 = 0b01,
  FUNCT2_2 = 0b10,
  FUNCT2_3 = 0b11,
};

// ----- Emulated conditions.
// On RISC-V we use this enum to abstract from conditional branch instructions.
// The 'U' prefix is used to specify unsigned comparisons.
// Opposite conditions must be paired as odd/even numbers
// because 'NegateCondition' function flips LSB to negate condition.
enum RiscvCondition {  // Any value < 0 is considered no_condition.
  overflow = 0,
  no_overflow = 1,
  Uless = 2,
  Ugreater_equal = 3,
  Uless_equal = 4,
  Ugreater = 5,
  equal = 6,
  not_equal = 7,  // Unordered or Not Equal.
  less = 8,
  greater_equal = 9,
  less_equal = 10,
  greater = 11,
  cc_always = 12,

  // Aliases.
  eq = equal,
  ne = not_equal,
  ge = greater_equal,
  lt = less,
  gt = greater,
  le = less_equal,
  al = cc_always,
  ult = Uless,
  uge = Ugreater_equal,
  ule = Uless_equal,
  ugt = Ugreater,
};

// ----- Coprocessor conditions.
enum FPUCondition {
  kNoFPUCondition = -1,
  EQ = 0x02,  // Ordered and Equal
  NE = 0x03,  // Unordered or Not Equal
  LT = 0x04,  // Ordered and Less Than
  GE = 0x05,  // Ordered and Greater Than or Equal
  LE = 0x06,  // Ordered and Less Than or Equal
  GT = 0x07,  // Ordered and Greater Than
};

enum CheckForInexactConversion {
  kCheckForInexactConversion,
  kDontCheckForInexactConversion
};

enum class MaxMinKind : int { kMin = 0, kMax = 1 };

// ----------------------------------------------------------------------------
// RISCV flags

enum ControlStatusReg {
  csr_fflags = 0x001,   // Floating-Point Accrued Exceptions (RW)
  csr_frm = 0x002,      // Floating-Point Dynamic Rounding Mode (RW)
  csr_fcsr = 0x003,     // Floating-Point Control and Status Register (RW)
  csr_cycle = 0xc00,    // Cycle counter for RDCYCLE instruction (RO)
  csr_time = 0xc01,     // Timer for RDTIME instruction (RO)
  csr_instret = 0xc02,  // Insns-retired counter for RDINSTRET instruction (RO)
  csr_cycleh = 0xc80,   // Upper 32 bits of cycle, RV32I only (RO)
  csr_timeh = 0xc81,    // Upper 32 bits of time, RV32I only (RO)
  csr_instreth = 0xc82  // Upper 32 bits of instret, RV32I only (RO)
};

enum FFlagsMask {
  kInvalidOperation = 0b10000,  // NV: Invalid
  kDivideByZero = 0b1000,       // DZ:  Divide by Zero
  kOverflow = 0b100,            // OF: Overflow
  kUnderflow = 0b10,            // UF: Underflow
  kInexact = 0b1                // NX:  Inexact
};

enum FPURoundingMode {
  RNE = 0b000,  // Round to Nearest, ties to Even
  RTZ = 0b001,  // Round towards Zero
  RDN = 0b010,  // Round Down (towards -infinity)
  RUP = 0b011,  // Round Up (towards +infinity)
  RMM = 0b100,  // Round to Nearest, tiest to Max Magnitude
  DYN = 0b111   // In instruction's rm field, selects dynamic rounding mode;
                // In Rounding Mode register, Invalid
};

enum MemoryOdering {
  PSI = 0b1000,  // PI or SI
  PSO = 0b0100,  // PO or SO
  PSR = 0b0010,  // PR or SR
  PSW = 0b0001,  // PW or SW
  PSIORW = PSI | PSO | PSR | PSW
};

const int kFloat32ExponentBias = 127;
const int kFloat32MantissaBits = 23;
const int kFloat32ExponentBits = 8;
const int kFloat64ExponentBias = 1023;
const int kFloat64MantissaBits = 52;
const int kFloat64ExponentBits = 11;

enum FClassFlag {
  kNegativeInfinity = 1,
  kNegativeNormalNumber = 1 << 1,
  kNegativeSubnormalNumber = 1 << 2,
  kNegativeZero = 1 << 3,
  kPositiveZero = 1 << 4,
  kPositiveSubnormalNumber = 1 << 5,
  kPositiveNormalNumber = 1 << 6,
  kPositiveInfinity = 1 << 7,
  kSignalingNaN = 1 << 8,
  kQuietNaN = 1 << 9
};

enum OffsetSize : uint32_t {
  kOffset21 = 21,  // RISCV jal
  kOffset12 = 12,  // RISCV imm12
  kOffset20 = 20,  // RISCV imm20
  kOffset13 = 13,  // RISCV branch
  kOffset32 = 32,  // RISCV auipc + instr_I
  kOffset11 = 11,  // RISCV C_J
  kOffset9 = 9,    // RISCV compressed branch
};

// The classes of immediate branch ranges, in order of increasing range.
enum ImmBranchRangeType {
  CondBranchRangeType,    // RISCV branch
  UncondBranchRangeType,  // RISCV jal
  UnknownBranchRangeType,

  // Number of 'short-range' branch range types.
  // RISCV branch and jal are both considered 'short-range'.
  NumShortBranchRangeTypes = UnknownBranchRangeType
};

inline ImmBranchRangeType OffsetSizeToImmBranchRangeType(OffsetSize bits) {
  switch (bits) {
    case kOffset21:
      return UncondBranchRangeType;
    case kOffset13:
      return CondBranchRangeType;
    default:
      MOZ_CRASH("Unimplement");
  }
}

inline OffsetSize ImmBranchRangeTypeToOffsetSize(ImmBranchRangeType type) {
  switch (type) {
    case CondBranchRangeType:
      return kOffset13;
    case UncondBranchRangeType:
      return kOffset21;
    default:
      MOZ_CRASH("Unimplement");
  }
}

inline int32_t ImmBranchMaxForwardOffset(OffsetSize bits) {
  return (1 << (bits - 1)) - 1;
}

inline int32_t ImmBranchMaxForwardOffset(ImmBranchRangeType type) {
  return ImmBranchMaxForwardOffset(ImmBranchRangeTypeToOffsetSize(type));
}

}  // namespace jit
}  // namespace js
#endif  //  jit_riscv64_constant_Base_constant_riscv_h_
