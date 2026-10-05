/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_PerfSpewer_h
#define jit_PerfSpewer_h

#ifdef JS_ION_PERF
#  include <stdio.h>
#endif
#include "js/AllocPolicy.h"
#include "js/ColumnNumber.h"
#include "js/JitCodeAPI.h"
#include "js/Vector.h"

#ifdef JS_JITSPEW
#  include "jit/GraphSpewer.h"
#endif

class JSScript;
enum class JSOp : uint8_t;

namespace js {

namespace wasm {
struct OpBytes;
struct CodeMetadata;
}  // namespace wasm

namespace jit {

class JitCode;
class BacktrackingAllocator;
class CompilerFrameInfo;
class MacroAssembler;
class MBasicBlock;
class MIRGraph;
class LInstruction;
enum class CacheOp : uint16_t;

using ProfilerJitCodeVector = Vector<JS::JitCodeRecord, 0, SystemAllocPolicy>;

void ResetPerfSpewer(bool enabled);

struct AutoLockPerfSpewer {
  AutoLockPerfSpewer();
  ~AutoLockPerfSpewer();
};

bool PerfEnabled();

class PerfSpewer {
 protected:
  // An entry to insert into the DEBUG_INFO jitdump record. It maps from a code
  // offset (relative to startOffset_) to a line number and column number.
  struct DebugEntry {
    constexpr DebugEntry() : offset(0), line(0), column(0) {}
    constexpr DebugEntry(uint32_t offset_, uint32_t line_, uint32_t column_ = 0)
        : offset(offset_), line(line_), column(column_) {}

    uint32_t offset;
    uint32_t line;
    uint32_t column;
  };

  // The debug records for this perf spewer.
  Vector<DebugEntry, 0, SystemAllocPolicy> debugInfo_;

  // The start offset that debugInfo_ is relative to.
  uint32_t startOffset_ = 0;

  // The generated IR file that we write into for IONPERF=ir. The move
  // constructors assert that this file has been closed/finished.
  FILE* irFile_ = nullptr;
  uint32_t irFileLines_ = 0;

  // The filename of irFile_.
  JS::UniqueChars irFileName_;

  virtual const char* CodeName(uint32_t op) = 0;
  virtual const char* IRFileExtension() { return ".txt"; }

  // Append an opcode to opcodes_ and the implicit debugInfo_ entry referencing
  // it.
  void recordOpcode(uint32_t offset, uint32_t opcode);
  void recordOpcode(uint32_t offset, uint32_t opcode, JS::UniqueChars&& str);
  void recordOpcode(uint32_t offset, JS::UniqueChars&& str);

  // Save the debugInfo_ vector to the JIT dump file.
  void saveDebugInfo(const char* filename, uintptr_t base,
                     JS::JitCodeRecord* maybeProfilerRecord,
                     AutoLockPerfSpewer& lock);

  // Save the generated IR file, if any, and the debug info to the JIT dump
  // file.
  void saveJitCodeDebugInfo(JSScript* script, JitCode* code,
                            JS::JitCodeRecord* maybeProfilerRecord,
                            AutoLockPerfSpewer& lock);

  // Save the generated IR file, if any, and the debug info to the JIT dump
  // file.
  void saveWasmCodeDebugInfo(uintptr_t codeBase,
                             JS::JitCodeRecord* maybeProfilerRecord,
                             AutoLockPerfSpewer& lock);

  void saveJSProfile(JitCode* code, JS::UniqueChars& desc, JSScript* script);
  void saveWasmProfile(uintptr_t codeBase, size_t codeSize,
                       JS::UniqueChars& desc);

  virtual void disable(AutoLockPerfSpewer& lock);
  virtual void disable();

 public:
  PerfSpewer() = default;
  ~PerfSpewer();
  PerfSpewer(PerfSpewer&&);
  PerfSpewer& operator=(PerfSpewer&&);

  // Mark the start code offset that this perf spewer is relative to.
  void markStartOffset(uint32_t offset) { startOffset_ = offset; }

  // Start recording. This may create a temp file if we're recording IR.
  virtual void startRecording(const wasm::CodeMetadata* wasmCodeMeta = nullptr);

  // Finish recording and get ready for saving to jitdump, but do not yet
  // write the debug info.
  virtual void endRecording();

  void recordOffset(MacroAssembler& masm, const char*);

  static void Init();

  static void CollectJitCodeInfo(JS::UniqueChars& function_name, JitCode* code,
                                 JS::JitCodeRecord* maybeProfilerRecord,
                                 AutoLockPerfSpewer& lock);
  static void CollectJitCodeInfo(JS::UniqueChars& function_name,
                                 void* code_addr, uint64_t code_size,
                                 JS::JitCodeRecord* maybeProfilerRecord,
                                 AutoLockPerfSpewer& lock);

  // Explicitly free heap memory allocated using the system allocator. This must
  // be called when the PerfSpewer is allocated in a LifoAlloc, since the
  // destructor won't be called when the LifoAlloc is freed.
  void reset() {
    endRecording();
    debugInfo_.clearAndFree();
    irFileName_ = JS::UniqueChars();
  }
};

void CollectPerfSpewerJitCodeProfile(JitCode* code, const char* msg);
void CollectPerfSpewerJitCodeProfile(uintptr_t base, uint64_t size,
                                     const char* msg);

void CollectPerfSpewerWasmMap(uintptr_t base, uintptr_t size,
                              JS::UniqueChars&& desc);

class IonPerfSpewer : public PerfSpewer {
  const char* CodeName(uint32_t op) override;
  const char* IRFileExtension() override;

  void disable() override;

#ifdef JS_JITSPEW
  Fprinter graphPrinter_;
  UniqueGraphSpewer graphSpewer_ = nullptr;
#endif

 public:
  IonPerfSpewer() = default;
  IonPerfSpewer(IonPerfSpewer&&) = default;
  IonPerfSpewer& operator=(IonPerfSpewer&&) = default;

  void startRecording(
      const wasm::CodeMetadata* wasmCodeMeta = nullptr) override;
  void endRecording() override;

  void recordPass(const char* pass, MIRGraph* graph,
                  BacktrackingAllocator* ra = nullptr);
  void recordInstruction(MacroAssembler& masm, LInstruction* ins);

  void saveJSProfile(JSContext* cx, JSScript* script, JitCode* code);
  void saveWasmProfile(uintptr_t codeBase, size_t codeSize,
                       JS::UniqueChars& desc);
};

class WasmBaselinePerfSpewer : public PerfSpewer {
  const char* CodeName(uint32_t op) override;

  bool needsToRecordInstruction_;

 public:
  WasmBaselinePerfSpewer();
  WasmBaselinePerfSpewer(WasmBaselinePerfSpewer&&) = default;
  WasmBaselinePerfSpewer& operator=(WasmBaselinePerfSpewer&&) = default;

  [[nodiscard]] bool needsToRecordInstruction() const;
  void recordInstruction(MacroAssembler& masm, const wasm::OpBytes& op);
  void saveProfile(uintptr_t codeBase, size_t codeSize, JS::UniqueChars& desc);
};

class BaselineInterpreterPerfSpewer : public PerfSpewer {
  // An opcode to insert into the generated IR source file.
  struct Op {
    uint32_t offset = 0;
    uint32_t opcode = 0;
    // This string is used to replace the opcode, to define things like
    // Prologue/Epilogue, or to add operand info.
    JS::UniqueChars str;

    explicit Op(uint32_t offset_, uint32_t opcode_)
        : offset(offset_), opcode(opcode_) {}
    explicit Op(uint32_t offset_, JS::UniqueChars&& str_)
        : offset(offset_), opcode(0), str(std::move(str_)) {}

    Op(Op&& copy) {
      offset = copy.offset;
      opcode = copy.opcode;
      str = std::move(copy.str);
    }

    // Do not copy the UniqueChars member.
    Op(Op& copy) = delete;
  };
  Vector<Op, 0, SystemAllocPolicy> ops_;

  const char* CodeName(uint32_t op) override;

 public:
  void recordOffset(MacroAssembler& masm, const JSOp& op);
  void recordOffset(MacroAssembler& masm, const char* name);
  void saveProfile(JitCode* code);
};

class BaselinePerfSpewer : public PerfSpewer {
  const char* CodeName(uint32_t op) override;

 public:
  void recordInstruction(MacroAssembler& masm, jsbytecode* pc, unsigned line,
                         JS::LimitedColumnNumberOneOrigin column,
                         CompilerFrameInfo& frame);
  void saveProfile(JSContext* cx, JSScript* script, JitCode* code);
};

class InlineCachePerfSpewer : public PerfSpewer {
  const char* CodeName(uint32_t op) override;

 public:
  void recordInstruction(MacroAssembler& masm, const CacheOp& op);
};

class BaselineICPerfSpewer : public InlineCachePerfSpewer {
 public:
  void saveProfile(JitCode* code, const char* stubName);
};

class IonICPerfSpewer : public InlineCachePerfSpewer {
 public:
  explicit IonICPerfSpewer(JSScript* script, jsbytecode* pc);

  void saveProfile(JSContext* cx, JSScript* script, JitCode* code,
                   const char* stubName);
};

class PerfSpewerRangeRecorder {
  using OffsetPair = std::tuple<uint32_t, JS::UniqueChars>;
  Vector<OffsetPair, 0, js::SystemAllocPolicy> ranges;

  MacroAssembler& masm;

  void appendEntry(JS::UniqueChars& desc);

 public:
  explicit PerfSpewerRangeRecorder(MacroAssembler& masm_) : masm(masm_) {};

  void recordOffset(const char* name);
  void recordOffset(const char* name, JSContext* cx, JSScript* script);
  void recordVMWrapperOffset(const char* name);
  void collectRangesForJitCode(JitCode* code);
};

}  // namespace jit
}  // namespace js

#endif /* jit_PerfSpewer_h */
