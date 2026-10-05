/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Copyright (c) 1994-2006 Sun Microsystems Inc.
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// - Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// - Redistribution in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// - Neither the name of Sun Microsystems or the names of contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2021 the V8 project authors. All rights reserved.

#ifndef jit_riscv64_Assembler_riscv64_h
#define jit_riscv64_Assembler_riscv64_h

#include "mozilla/Assertions.h"
#include "mozilla/Sprintf.h"

#include <stdint.h>

#include "jit/CompactBuffer.h"
#include "jit/JitCode.h"
#include "jit/JitSpewer.h"
#include "jit/Registers.h"
#include "jit/RegisterSets.h"
#include "jit/riscv64/Architecture-riscv64.h"
#include "jit/riscv64/base/base-assembler-riscv.h"
#include "jit/riscv64/base/base-riscv-i.h"
#include "jit/riscv64/constant/Constant-riscv64.h"
#include "jit/riscv64/extension/extension-riscv-a.h"
#include "jit/riscv64/extension/extension-riscv-b.h"
#include "jit/riscv64/extension/extension-riscv-c.h"
#include "jit/riscv64/extension/extension-riscv-d.h"
#include "jit/riscv64/extension/extension-riscv-f.h"
#include "jit/riscv64/extension/extension-riscv-m.h"
#include "jit/riscv64/extension/extension-riscv-v.h"
#include "jit/riscv64/extension/extension-riscv-zfa.h"
#include "jit/riscv64/extension/extension-riscv-zfh.h"
#include "jit/riscv64/extension/extension-riscv-zicond.h"
#include "jit/riscv64/extension/extension-riscv-zicsr.h"
#include "jit/riscv64/extension/extension-riscv-zifencei.h"
#include "jit/riscv64/Register-riscv64.h"
#include "jit/shared/Assembler-shared.h"
#include "jit/shared/Disassembler-shared.h"
#include "jit/shared/IonAssemblerBufferWithConstantPools.h"
#include "js/HashTable.h"
#include "wasm/WasmTypeDecls.h"
namespace js {
namespace jit {

struct ScratchFloat32Scope : public AutoFloatRegisterScope {
  explicit ScratchFloat32Scope(MacroAssembler& masm)
      : AutoFloatRegisterScope(masm, ScratchFloat32Reg) {}
};

struct ScratchDoubleScope : public AutoFloatRegisterScope {
  explicit ScratchDoubleScope(MacroAssembler& masm)
      : AutoFloatRegisterScope(masm, ScratchDoubleReg) {}
};

struct ScratchFloat32Scope2 : public AutoFloatRegisterScope {
  explicit ScratchFloat32Scope2(MacroAssembler& masm)
      : AutoFloatRegisterScope(masm, ScratchFloat32Reg2) {}
};

struct ScratchDoubleScope2 : public AutoFloatRegisterScope {
  explicit ScratchDoubleScope2(MacroAssembler& masm)
      : AutoFloatRegisterScope(masm, ScratchDoubleReg2) {}
};

class MacroAssembler;

static constexpr uint32_t ABIStackAlignment = 16;
static constexpr uint32_t CodeAlignment = 16;
static constexpr uint32_t JitStackAlignment = 16;
static constexpr uint32_t JitStackValueAlignment =
    JitStackAlignment / sizeof(Value);
static const uint32_t WasmStackAlignment = 16;
static const uint32_t WasmTrapInstructionLength = 2 * kInstrSize;
// See comments in wasm::GenerateFunctionPrologue.  The difference between these
// is the size of the largest callable prologue on the platform.
static constexpr uint32_t WasmCheckedCallEntryOffset = 0u;
static constexpr uint32_t WasmCheckedTailEntryOffset = 20u;

static const Scale ScalePointer = TimesEight;

class Assembler;

using Buffer =
    js::jit::AssemblerBufferWithConstantPools<Instruction, Assembler,
                                              js::jit::AssemblerBufferSettings{
                                                  .instSize = kInstrSize,
                                                  .guardSize = 2,
                                                  .headerSize = 2,
                                                  .pcBias = 8,
                                                  .alignFillInst = kNopByte,
                                                  .nopFillInst = kNopByte,
                                                  .numShortBranchRanges =
                                                      NumShortBranchRangeTypes,
                                              }>;

class Assembler : public AssemblerShared,
                  public AssemblerRISCVI,
                  public AssemblerRISCVA,
                  public AssemblerRISCVB,
                  public AssemblerRISCVF,
                  public AssemblerRISCVD,
                  public AssemblerRISCVM,
                  public AssemblerRISCVC,
                  public AssemblerRISCVZfa,
                  public AssemblerRISCVZfh,
                  public AssemblerRISCVZicond,
                  public AssemblerRISCVZicsr,
                  public AssemblerRISCVZifencei {
  GeneralRegisterSet scratch_register_list_;

  static constexpr int kInvalidSlotPos = -1;

#ifdef JS_JITSPEW
  Sprinter* printer;
#endif
  bool enoughLabelCache_ = true;

 protected:
  using LabelOffset = int32_t;
  using LabelCache =
      HashMap<LabelOffset, BufferOffset, js::DefaultHasher<LabelOffset>,
              js::SystemAllocPolicy>;
  LabelCache label_cache_;
  void NoEnoughLabelCache() { enoughLabelCache_ = false; }
  CompactBufferWriter jumpRelocations_;
  CompactBufferWriter dataRelocations_;
  Buffer m_buffer;
  bool isFinished = false;

  // Return the Instruction at a given byte offset.
  Instruction* getInstructionAt(BufferOffset offset) {
    return m_buffer.getInst(offset);
  }

  struct RelativePatch {
    // the offset within the code buffer where the value is loaded that
    // we want to fix-up
    BufferOffset offset;
    void* target;
    RelocationKind kind;

    RelativePatch(BufferOffset offset, void* target, RelocationKind kind)
        : offset(offset), target(target), kind(kind) {}
  };

  js::Vector<RelativePatch, 8, SystemAllocPolicy> jumps_;

  void addPendingJump(BufferOffset src, ImmPtr target, RelocationKind kind) {
    enoughMemory_ &= jumps_.append(RelativePatch(src, target.value, kind));
    if (kind == RelocationKind::JITCODE) {
      jumpRelocations_.writeUnsigned(src.getOffset());
    }
  }

  void addLongJump(BufferOffset src, BufferOffset dst) {
    CodeLabel cl;
    cl.patchAt()->bind(src.getOffset());
    cl.target()->bind(dst.getOffset());
    cl.setLinkMode(CodeLabel::JumpImmediate);
    addCodeLabel(std::move(cl));
  }

 public:
  static bool FLAG_riscv_debug;

  Assembler()
      : scratch_register_list_((1 << t5.code()) | (1 << t4.code()) |
                               (1 << t6.code())),
#ifdef JS_JITSPEW
        printer(nullptr),
#endif
        m_buffer(/*poolMaxOffset*/ GetPoolMaxOffset(), /*nopFill*/ 0),
        isFinished(false) {
  }
  static uint32_t NopFill;
  static uint32_t AsmPoolMaxOffset;
  static uint32_t GetPoolMaxOffset();
  bool reserve(size_t size);
  bool oom() const;
  void setPrinter(Sprinter* sp) {
#ifdef JS_JITSPEW
    printer = sp;
#endif
  }
  void finish() {
    MOZ_ASSERT(!isFinished);
    isFinished = true;
  }

  void enterNoPool(size_t maxInst, size_t maxNewDeadlines = 0) {
    m_buffer.enterNoPool(maxInst, maxNewDeadlines);
  }
  void leaveNoPool() { m_buffer.leaveNoPool(); }

  void enterNoNops() { m_buffer.enterNoNops(); }
  void leaveNoNops() { m_buffer.leaveNoNops(); }

  bool swapBuffer(wasm::Bytes& bytes);
  // Size of the instruction stream, in bytes.
  size_t size() const;
  // Size of the data table, in bytes.
  size_t bytesNeeded() const;
  // Size of the jump relocation table, in bytes.
  size_t jumpRelocationTableBytes() const;
  size_t dataRelocationTableBytes() const;
  void copyJumpRelocationTable(uint8_t* dest);
  void copyDataRelocationTable(uint8_t* dest);
  // Copy the assembly code to the given buffer, and perform any pending
  // relocations relying on the target address.
  void executableCopy(uint8_t* buffer);
  // API for speaking with the IonAssemblerBufferWithConstantPools generate an
  // initial placeholder instruction that we want to later fix up.
  static void InsertIndexIntoTag(uint8_t* load, uint32_t index);
  static void PatchConstantPoolLoad(void* loadAddr, void* constPoolAddr);
  // We're not tracking short-range branches for ARM for now.
  static void PatchShortRangeBranchToVeneer(Buffer*, unsigned rangeIdx,
                                            BufferOffset deadline,
                                            BufferOffset veneer);
  struct PoolHeader {
    uint32_t data;

    struct Header {
      // The size should take into account the pool header.
      // The size is in units of Instruction (4bytes), not byte.
      union {
        struct {
          uint32_t size : 15;

          // "Natural" guards are part of the normal instruction stream,
          // while "non-natural" guards are inserted for the sole purpose
          // of skipping around a pool.
          uint32_t isNatural : 1;
          uint32_t ONES : 16;
        };
        uint32_t data;
      };

      Header(int size_, bool isNatural_)
          : size(size_), isNatural(isNatural_), ONES(0xffff) {}

      explicit Header(uint32_t data) : data(data) {
        static_assert(sizeof(Header) == sizeof(uint32_t));
        MOZ_ASSERT(ONES == 0xffff);
      }

      uint32_t raw() const {
        static_assert(sizeof(Header) == sizeof(uint32_t));
        return data;
      }
    };

    PoolHeader(int size_, bool isNatural_)
        : data(Header(size_, isNatural_).raw()) {}

    uint32_t size() const {
      Header tmp(data);
      return tmp.size;
    }

    uint32_t isNatural() const {
      Header tmp(data);
      return tmp.isNatural;
    }
  };

  static void WritePoolHeader(uint8_t* start, Pool* p, bool isNatural);
  static void WritePoolGuard(BufferOffset branch, Instruction* inst,
                             BufferOffset dest);
  void processCodeLabels(uint8_t* rawCode);

  // Get the next usable buffer offset. Note that a constant pool may be placed
  // here before the next instruction is emitted.
  BufferOffset nextOffset() const { return m_buffer.nextOffset(); }

  // Get the buffer offset of the next inserted instruction. This may flush
  // constant pools and emit veneers.
  BufferOffset nextInstrOffset(unsigned numInsts, unsigned numNewDeadlines) {
    return m_buffer.nextInstrOffset(numInsts, numNewDeadlines);
  }

  void comment(const char* msg) { spew("; %s", msg); }

#ifdef JS_JITSPEW
  inline void spew(const char* fmt, ...) MOZ_FORMAT_PRINTF(2, 3) {
    if (MOZ_UNLIKELY(printer || JitSpewEnabled(JitSpew_Codegen))) {
      va_list va;
      va_start(va, fmt);
      spewVA(fmt, va);
      va_end(va);
    }
  }
#else
  MOZ_ALWAYS_INLINE void spew(const char* fmt, ...) MOZ_FORMAT_PRINTF(2, 3) {}
#endif

#ifdef JS_JITSPEW
  MOZ_COLD void spewVA(const char* fmt, va_list va) MOZ_FORMAT_PRINTF(2, 0) {
    // Buffer to hold the formatted string. Note that this may contain
    // '%' characters, so do not pass it directly to printf functions.
    char buf[200];

    int i = VsprintfLiteral(buf, fmt, va);
    if (i > -1) {
      if (printer) {
        printer->printf("%s\n", buf);
      }
      js::jit::JitSpew(js::jit::JitSpew_Codegen, "%s", buf);
    }
  }
#endif

  enum Condition {
    Overflow = overflow,
    Below = Uless,
    BelowOrEqual = Uless_equal,
    Above = Ugreater,
    AboveOrEqual = Ugreater_equal,
    Equal = equal,
    NotEqual = not_equal,
    GreaterThan = greater,
    GreaterThanOrEqual = greater_equal,
    LessThan = less,
    LessThanOrEqual = less_equal,
    Always = cc_always,
    CarrySet,
    CarryClear,
    Signed,
    NotSigned,
    Zero,
    NonZero,
  };

  enum DoubleCondition {
    // These conditions will only evaluate to true if the comparison is ordered
    // - i.e. neither operand is NaN.
    DoubleOrdered,
    DoubleEqual,
    DoubleNotEqual,
    DoubleGreaterThan,
    DoubleGreaterThanOrEqual,
    DoubleLessThan,
    DoubleLessThanOrEqual,
    // If either operand is NaN, these conditions always evaluate to true.
    DoubleUnordered,
    DoubleEqualOrUnordered,
    DoubleNotEqualOrUnordered,
    DoubleGreaterThanOrUnordered,
    DoubleGreaterThanOrEqualOrUnordered,
    DoubleLessThanOrUnordered,
    DoubleLessThanOrEqualOrUnordered,
  };

  Register getStackPointer() const { return StackPointer; }
  void flushBuffer() {}
#ifdef JS_DISASM_RISCV64
  static int disassembleInstr(Instruction* instr, bool enable_spew = false);
#endif /* JS_DISASM_RISCV64 */
  int jumpChainTargetAt(BufferOffset pos);
  static int jumpChainTargetAt(Instruction* instruction, BufferOffset pos,
                               Instruction* instruction2 = nullptr);
  BufferOffset jumpChainGetNextLink(BufferOffset pos);
  uint32_t jumpChainUseNextLink(Label* label);
  // Returns true if the target was successfully assembled and spewed.
  bool jumpChainPutTargetAt(BufferOffset pos, BufferOffset target_pos);
  int32_t branchOffsetHelper(Label* L, OffsetSize bits);
  int32_t branchLongOffsetHelper(Label* L);

  void nopAlign(int m) { m_buffer.align(m); }

  virtual BufferOffset emit(Instr x) {
    MOZ_ASSERT(hasCreator());
    BufferOffset offset = m_buffer.putInt(x);
#if (defined(DEBUG) || defined(JS_JITSPEW)) && defined(JS_DISASM_RISCV64)
    if (offset.assigned()) {
      DEBUG_PRINTF("0x%" PRIx64 "(%x):", uint64_t(getInstructionAt(offset)),
                   unsigned(offset.getOffset()));
      disassembleInstr(getInstructionAt(offset),
                       JitSpewEnabled(JitSpew_Codegen));
    }
#endif
    return offset;
  }
  virtual BufferOffset emit(ShortInstr x) { MOZ_CRASH(); }
  virtual BufferOffset emit(uint64_t x) { MOZ_CRASH(); }
  virtual BufferOffset emit(uint32_t x) {
    BufferOffset offset = m_buffer.putInt(x);
    if (offset.assigned()) {
      DEBUG_PRINTF("0x%" PRIx64 "(%x): uint32_t: %" PRId32 "\n",
                   uint64_t(getInstructionAt(offset)),
                   unsigned(offset.getOffset()), x);
    }
    return offset;
  }

  static Condition InvertCondition(Condition);

  static DoubleCondition InvertCondition(DoubleCondition);

  static uint64_t ExtractLoad64Value(Instruction* inst0);
  static void UpdateLoad64Value(Instruction* inst0, uint64_t value);
  static void PatchDataWithValueCheck(CodeLocationLabel label, ImmPtr newValue,
                                      ImmPtr expectedValue);
  static void PatchDataWithValueCheck(CodeLocationLabel label,
                                      PatchedImmPtr newValue,
                                      PatchedImmPtr expectedValue);
  static void PatchWrite_Imm32(CodeLocationLabel label, Imm32 imm);

  static void PatchWrite_NearCall(CodeLocationLabel start,
                                  CodeLocationLabel toCall) {
    Instruction* inst = Instruction::At(start.raw());
    uint8_t* dest = toCall.raw();

    // Overwrite whatever instruction used to be here with a call.
    // Always use long jump for two reasons:
    // - Jump has to be the same size because of PatchWrite_NearCallSize.
    // - Return address has to be at the end of replaced block.
    // Short jump wouldn't be more efficient.

    // WriteLiPtrInstructions writes 6 instructions to load an address.
    Assembler::WriteLiPtrInstructions(inst, SavedScratchRegister,
                                      uintptr_t(dest));

    Instruction* jalr = (inst + 6 * kInstrSize);
    jalr->SetIFormat(RO_JALR, ra.code(), SavedScratchRegister.code(), 0);
  }

  static uint32_t PatchWrite_NearCallSize() { return 7 * kInstrSize; }

  static void TraceJumpRelocations(JSTracer* trc, JitCode* code,
                                   CompactBufferReader& reader);
  static void TraceDataRelocations(JSTracer* trc, JitCode* code,
                                   CompactBufferReader& reader);

  static void ToggleToJmp(CodeLocationLabel inst_);
  static void ToggleToCmp(CodeLocationLabel inst_);
  static void ToggleCall(CodeLocationLabel inst_, bool enable);

  static void Bind(uint8_t* rawCode, const CodeLabel& label);
  // label operations
  void bind(Label* label, BufferOffset boff = BufferOffset());
  void bind(CodeLabel* label) { label->target()->bind(currentOffset()); }
  uint32_t currentOffset() { return nextOffset().getOffset(); }
  void retarget(Label* label, Label* target);
  static uint32_t NopSize() { return kInstrSize; }

  static uintptr_t GetPointer(uint8_t* instPtr) {
    Instruction* inst = Instruction::At(instPtr);
    return Assembler::ExtractLoad64Value(inst);
  }

  static bool HasRoundInstruction(RoundingMode mode) {
    switch (mode) {
      case RoundingMode::Up:
      case RoundingMode::Down:
      case RoundingMode::NearestTiesToEven:
      case RoundingMode::TowardsZero:
        return true;
    }
    MOZ_CRASH("unexpected mode");
  }

  static bool HasZbaExtension() { return RVFlags::HasZbaExtension(); }

  static bool HasZbbExtension() { return RVFlags::HasZbbExtension(); }

  static bool HasZbsExtension() { return RVFlags::HasZbsExtension(); }

  static bool HasZfhminExtension() { return RVFlags::HasZfhminExtension(); }

  static bool HasZfaExtension() { return RVFlags::HasZfaExtension(); }

  static bool HasZicondExtension() { return RVFlags::HasZicondExtension(); }

  void verifyHeapAccessDisassembly(uint32_t begin, uint32_t end,
                                   const Disassembler::HeapAccess& heapAccess) {
    MOZ_CRASH();
  }

  void setUnlimitedBuffer() { m_buffer.setUnlimited(); }

  GeneralRegisterSet* GetScratchRegisterList() {
    return &scratch_register_list_;
  }

  void writeDataRelocation(ImmGCPtr ptr, BufferOffset offset) {
    // Raw GC pointer relocations and Value relocations both end up in
    // TraceOneDataRelocation.
    if (ptr.value) {
      if (gc::IsInsideNursery(ptr.value)) {
        embedsNurseryPointers_ = true;
      }
      dataRelocations_.writeUnsigned(offset.getOffset());
    }
  }

  bool appendRawCode(const uint8_t* code, size_t numBytes);

  void assertNoGCThings() const {
#ifdef DEBUG
    MOZ_ASSERT(dataRelocations_.length() == 0);
    for (const auto& j : jumps_) {
      MOZ_ASSERT(j.kind == RelocationKind::HARDCODED);
    }
#endif
  }

  // Assembler Pseudo Instructions (Tables 25.2, 25.3, RISC-V Unprivileged ISA)
  void break_(uint32_t code, bool break_as_stop = false);
  void RV_li(Register rd, int64_t imm);
  static int RV_li_count(int64_t imm, bool is_get_temp_reg = false);
  void GeneralLi(Register rd, int64_t imm);
  static int GeneralLiCount(int64_t imm, bool is_get_temp_reg = false);
  void RecursiveLiImpl(Register rd, int64_t imm);
  void RecursiveLi(Register rd, int64_t imm);
  static int RecursiveLiCount(int64_t imm);
  static int RecursiveLiImplCount(int64_t imm);
  // Returns the number of instructions required to load the immediate
  static int li_estimate(int64_t imm, bool is_get_temp_reg = false);

  // Loads an immediate, always using 8 instructions, regardless of the value,
  // so that it can be modified later.
  BufferOffset li_constant(Register rd, int64_t imm);

  // Loads an immediate, always using 6 instructions, regardless of the value,
  // so that it can be modified later.
  BufferOffset li_ptr(Register rd, int64_t imm);

  void SignExtendByte(Register rd, Register rs) {
    if (HasZbbExtension()) {
      sext_b(rd, rs);
      return;
    }
    slli(rd, rs, xlen - 8);
    srai(rd, rd, xlen - 8);
  }

  void SignExtendShort(Register rd, Register rs) {
    if (HasZbbExtension()) {
      sext_h(rd, rs);
      return;
    }
    slli(rd, rs, xlen - 16);
    srai(rd, rd, xlen - 16);
  }

  void SignExtendWord(Register rd, Register rs) { sext_w(rd, rs); }
  void ZeroExtendWord(Register rd, Register rs) {
    if (HasZbaExtension()) {
      zext_w(rd, rs);
      return;
    }
    slli(rd, rs, 32);
    srli(rd, rd, 32);
  }

 protected:
  // Load the value from the six instruction sequence starting at |instr|.
  //
  // Also see Assembler::li_ptr.
  static uintptr_t LoadLiPtrInstructions(Instruction* instr);

  // Updates the six instruction sequence to load |value| into a register.
  //
  // Also see Assembler::li_ptr.
  static void UpdateLiPtrInstructions(Instruction* instr, uintptr_t value);

  // Write the six instruction sequence to load |value| into |reg|.
  //
  // The instruction sequence at |instr| must either be an existing li_ptr
  // immediate or a sequence of six nop instructions.
  //
  // Also see Assembler::li_ptr.
  static void WriteLiPtrInstructions(Instruction* instr, Register reg,
                                     uintptr_t value);

  // Load the value from the eight instruction sequence starting at |instr|.
  //
  // Also see Assembler::li_constant.
  static int64_t LoadLiConstantInstructions(Instruction* instr);

  // Updates the eight instruction sequence to load |value| into a register.
  //
  // Also see Assembler::li_constant.
  static void UpdateLiConstantInstructions(Instruction* instr, int64_t value);
};

class ABIArgGenerator : public ABIArgGeneratorShared {
 public:
  explicit ABIArgGenerator(ABIKind kind)
      : ABIArgGeneratorShared(kind),
        intRegIndex_(0),
        floatRegIndex_(0),
        current_() {}

  ABIArg next(MIRType);
  ABIArg& current() { return current_; }

 protected:
  unsigned intRegIndex_;
  unsigned floatRegIndex_;
  ABIArg current_;
};

class UseScratchRegisterScope {
 public:
  explicit UseScratchRegisterScope(Assembler& assembler);
  explicit UseScratchRegisterScope(Assembler* assembler);
  ~UseScratchRegisterScope();

  Register Acquire();
  void Release(const Register& reg);
  bool hasAvailable() const;
  void Include(const GeneralRegisterSet& list) {
    *available_ = GeneralRegisterSet::Union(*available_, list);
  }
  void Exclude(const GeneralRegisterSet& list) {
    *available_ = GeneralRegisterSet::Subtract(*available_, list);
  }

 private:
  GeneralRegisterSet* available_;
  GeneralRegisterSet old_available_;
};

// Register or immediate operand.
class Operand {
  enum Tag { REG, IMM };

 public:
  explicit Operand(Register rm) : tag(REG), rm_(rm.code()) {}
  explicit Operand(int64_t immediate) : tag(IMM), value_(immediate) {}

  bool is_reg() const { return tag == REG; }
  bool is_imm() const { return tag == IMM; }

  int64_t immediate() const {
    MOZ_ASSERT(is_imm());
    return value_;
  }

  Register rm() const {
    MOZ_ASSERT(is_reg());
    return Register::FromCode(rm_);
  }

 private:
  Tag tag;
  union {
    uint32_t rm_;    // valid if tag == REG
    int64_t value_;  // valid if tag == IMM
  };
};

static const uint32_t NumIntArgRegs = 8;
static const uint32_t NumFloatArgRegs = 8;
static inline bool GetIntArgReg(uint32_t usedIntArgs, Register* out) {
  if (usedIntArgs < NumIntArgRegs) {
    *out = Register::FromCode(a0.code() + usedIntArgs);
    return true;
  }
  return false;
}

static inline bool GetFloatArgReg(uint32_t usedFloatArgs, FloatRegister* out) {
  if (usedFloatArgs < NumFloatArgRegs) {
    *out = FloatRegister::FromCode(fa0.encoding() + usedFloatArgs);
    return true;
  }
  return false;
}

// Get a register in which we plan to put a quantity that will be used as an
// integer argument. This differs from GetIntArgReg in that if we have no more
// actual argument registers to use we will fall back on using whatever
// CallTempReg* don't overlap the argument registers, and only fail once those
// run out too.
static inline bool GetTempRegForIntArg(uint32_t usedIntArgs,
                                       uint32_t usedFloatArgs, Register* out) {
  // NOTE: We can't properly determine which regs are used if there are
  // float arguments. If this is needed, we will have to guess.
  MOZ_ASSERT(usedFloatArgs == 0);

  if (GetIntArgReg(usedIntArgs, out)) {
    return true;
  }
  // Unfortunately, we have to assume things about the point at which
  // GetIntArgReg returns false, because we need to know how many registers it
  // can allocate.
  usedIntArgs -= NumIntArgRegs;
  if (usedIntArgs >= NumCallTempNonArgRegs) {
    return false;
  }
  *out = CallTempNonArgRegs[usedIntArgs];
  return true;
}

// Forbids nop filling for testing purposes. Nestable, but nested calls have
// no effect on the no-nops status; it is only the top level one that counts.
class AutoForbidNops {
  Assembler* asm_;

 public:
  explicit AutoForbidNops(Assembler* asm_) : asm_(asm_) { asm_->enterNoNops(); }
  ~AutoForbidNops() { asm_->leaveNoNops(); }

  AutoForbidNops(const AutoForbidNops&) = delete;
  AutoForbidNops& operator=(const AutoForbidNops&) = delete;
};

// Forbids pool generation during a specified interval. Nestable, but nested
// calls must imply a no-pool area of the assembler buffer that is completely
// contained within the area implied by the outermost level call.
class AutoForbidPoolsAndNops {
  Assembler* asm_;

 public:
  explicit AutoForbidPoolsAndNops(Assembler* assem, size_t margin,
                                  size_t maxBranches = 0)
      : asm_(assem) {
    asm_->enterNoPool(margin, maxBranches);
    asm_->enterNoNops();
  }
  ~AutoForbidPoolsAndNops() {
    asm_->leaveNoNops();
    asm_->leaveNoPool();
  }

  AutoForbidPoolsAndNops(const AutoForbidPoolsAndNops&) = delete;
  AutoForbidPoolsAndNops& operator=(const AutoForbidPoolsAndNops&) = delete;
};

}  // namespace jit
}  // namespace js
#endif /* jit_riscv64_Assembler_riscv64_h */
