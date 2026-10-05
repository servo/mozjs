// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "jit/riscv64/base/Instruction.h"

#include "mozilla/Assertions.h"

namespace js::jit {

OffsetSize InstructionBase::GetOffsetSize() const {
  if (IsIllegalInstruction()) {
    MOZ_CRASH("IllegalInstruction");
  }
  if (IsShortInstruction()) {
    switch (InstructionBits() & kRvcOpcodeMask) {
      case RO_C_J:
        return kOffset11;
      case RO_C_BEQZ:
      case RO_C_BNEZ:
        return kOffset9;
      default:
        MOZ_CRASH("IllegalInstruction");
    }
  } else {
    switch (InstructionBits() & kBaseOpcodeMask) {
      case BRANCH:
        return kOffset13;
      case JAL:
        return kOffset21;
      default:
        MOZ_CRASH("IllegalInstruction");
    }
  }
}

InstructionBase::Type InstructionBase::InstructionType() const {
  if (IsIllegalInstruction()) {
    return kUnsupported;
  }
  // RV64C Instruction
  if (IsShortInstruction()) {
    switch (InstructionBits() & kRvcOpcodeMask) {
      case RO_C_ADDI4SPN:
        return kCIWType;
      case RO_C_FLD:
      case RO_C_LW:
      case RO_C_LD:
        return kCLType;
      case RO_C_FSD:
      case RO_C_SW:
      case RO_C_SD:
        return kCSType;
      case RO_C_NOP_ADDI:
      case RO_C_LI:
      case RO_C_ADDIW:
      case RO_C_LUI_ADD:
        return kCIType;
      case RO_C_MISC_ALU:
        if (Bits(11, 10) != 0b11)
          return kCBType;
        else
          return kCAType;
      case RO_C_J:
        return kCJType;
      case RO_C_BEQZ:
      case RO_C_BNEZ:
        return kCBType;
      case RO_C_SLLI:
      case RO_C_FLDSP:
      case RO_C_LWSP:
      case RO_C_LDSP:
        return kCIType;
      case RO_C_JR_MV_ADD:
        return kCRType;
      case RO_C_FSDSP:
      case RO_C_SWSP:
      case RO_C_SDSP:
        return kCSSType;
      default:
        break;
    }
  } else {
    // RISCV routine
    switch (InstructionBits() & kBaseOpcodeMask) {
      case LOAD:
        return kIType;
      case LOAD_FP:
        return kIType;
      case MISC_MEM:
        return kIType;
      case OP_IMM:
        return kIType;
      case AUIPC:
        return kUType;
      case OP_IMM_32:
        return kIType;
      case STORE:
        return kSType;
      case STORE_FP:
        return kSType;
      case AMO:
        return kRType;
      case OP:
        return kRType;
      case LUI:
        return kUType;
      case OP_32:
        return kRType;
      case MADD:
      case MSUB:
      case NMSUB:
      case NMADD:
        return kR4Type;
      case OP_FP:
        return kRType;
      case BRANCH:
        return kBType;
      case JALR:
        return kIType;
      case JAL:
        return kJType;
      case SYSTEM:
        return kIType;
      case OP_V:
        return kVType;
    }
  }
  return kUnsupported;
}

}  // namespace js::jit
