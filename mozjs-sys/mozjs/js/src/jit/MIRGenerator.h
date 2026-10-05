/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_MIRGenerator_h
#define jit_MIRGenerator_h

// This file declares the data structures used to build a control-flow graph
// containing MIR.

#include "mozilla/Assertions.h"
#include "mozilla/Atomics.h"
#include "mozilla/Attributes.h"
#include "mozilla/Result.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "jit/CompilationDependencyTracker.h"
#include "jit/CompileInfo.h"
#include "jit/CompileWrappers.h"
#include "jit/JitAllocPolicy.h"
#include "jit/JitContext.h"
#include "jit/JitSpewer.h"
#include "jit/PerfSpewer.h"
#include "js/Utility.h"
#include "vm/GeckoProfiler.h"

namespace js {
namespace jit {

class BacktrackingAllocator;
class JitRuntime;
class MIRGraph;
class OptimizationInfo;

class MIRGenerator final {
 public:
  MIRGenerator(CompileRealm* realm, const JitCompileOptions& options,
               TempAllocator* alloc, MIRGraph* graph,
               const CompileInfo* outerInfo,
               const OptimizationInfo* optimizationInfo,
               const wasm::CodeMetadata* wasmCodeMeta = nullptr);

  TempAllocator& alloc() { return *alloc_; }
  MIRGraph& graph() { return *graph_; }
  [[nodiscard]] bool ensureBallast() { return alloc().ensureBallast(); }
  const JitRuntime* jitRuntime() const { return runtime->jitRuntime(); }
  const CompileInfo& outerInfo() const { return *outerInfo_; }
  const OptimizationInfo& optimizationInfo() const {
    return *optimizationInfo_;
  }
  bool hasProfilingScripts() const {
    return runtime && runtime->profilingScripts();
  }

  template <typename T>
  js::lifo_alloc_pointer<T*> allocate(size_t count = 1) {
    size_t bytes;
    if (MOZ_UNLIKELY(!CalculateAllocSize<T>(count, &bytes))) {
      return nullptr;
    }
    return static_cast<T*>(alloc().allocate(bytes));
  }

  // Set an error state and prints a message. Returns false so errors can be
  // propagated up.
  mozilla::GenericErrorResult<AbortReason> abort(AbortReason r);
  mozilla::GenericErrorResult<AbortReason> abort(AbortReason r,
                                                 const char* message, ...)
      MOZ_FORMAT_PRINTF(3, 4);

  mozilla::GenericErrorResult<AbortReason> abortFmt(AbortReason r,
                                                    const char* message,
                                                    va_list ap)
      MOZ_FORMAT_PRINTF(3, 0);

  // Collect the evaluation result of phases after WarpOracle, such that
  // off-thread compilation can report what error got encountered.
  void setOffThreadStatus(AbortReasonOr<Ok>&& result) {
    MOZ_ASSERT(offThreadStatus_.isOk());
    offThreadStatus_ = std::move(result);
  }
  const AbortReasonOr<Ok>& getOffThreadStatus() const {
    return offThreadStatus_;
  }

  [[nodiscard]] bool instrumentedProfiling() {
    if (!instrumentedProfilingIsCached_) {
      instrumentedProfiling_ = runtime->geckoProfiler().enabled();
      instrumentedProfilingIsCached_ = true;
    }
    return instrumentedProfiling_;
  }

  bool isProfilerInstrumentationEnabled() {
    return !compilingWasm() && instrumentedProfiling();
  }

  gc::Heap initialStringHeap() const {
    return stringsCanBeInNursery_ ? gc::Heap::Default : gc::Heap::Tenured;
  }

  gc::Heap initialBigIntHeap() const {
    return bigIntsCanBeInNursery_ ? gc::Heap::Default : gc::Heap::Tenured;
  }

  // Whether the main thread is trying to cancel this build.
  bool shouldCancel(const char* why) const { return cancelBuild_; }
  void cancel() { cancelBuild_ = true; }

  bool compilingWasm() const { return outerInfo_->compilingWasm(); }

  uint32_t wasmMaxStackArgBytes() const {
    MOZ_ASSERT(compilingWasm());
    return wasmMaxStackArgBytes_;
  }
  void accumulateWasmMaxStackArgBytes(uint32_t n) {
    MOZ_ASSERT(compilingWasm());
    wasmMaxStackArgBytes_ = std::max(n, wasmMaxStackArgBytes_);
  }

  void setNeedsOverrecursedCheck() { needsOverrecursedCheck_ = true; }
  bool needsOverrecursedCheck() const { return needsOverrecursedCheck_; }

  void setNeedsStaticStackAlignment() { needsStaticStackAlignment_ = true; }
  bool needsStaticStackAlignment() const { return needsStaticStackAlignment_; }

 public:
  CompileRealm* realm;
  CompileRuntime* runtime;

 private:
  // The CompileInfo for the outermost script.
  const CompileInfo* outerInfo_;
  const OptimizationInfo* optimizationInfo_;
  const wasm::CodeMetadata* wasmCodeMeta_;

  TempAllocator* alloc_;
  MIRGraph* graph_;
  AbortReasonOr<Ok> offThreadStatus_;
  mozilla::Atomic<bool, mozilla::Relaxed> cancelBuild_;

  uint32_t wasmMaxStackArgBytes_;
  bool needsOverrecursedCheck_;
  bool needsStaticStackAlignment_;

  bool instrumentedProfiling_;
  bool instrumentedProfilingIsCached_;
  bool stringsCanBeInNursery_;
  bool bigIntsCanBeInNursery_;

  bool disableLICM_ = false;

 public:
  void disableLICM() { disableLICM_ = true; }
  bool licmEnabled() const;
  bool branchHintingEnabled() const;

  const wasm::CodeMetadata* wasmCodeMeta() const {
    MOZ_ASSERT(wasmCodeMeta_);
    return wasmCodeMeta_;
  }

 public:
  const JitCompileOptions options;

 private:
#ifdef JS_JITSPEW
  GraphSpewer* graphSpewer_ = nullptr;
#endif
  JitSpewGraphSpewer jitSpewer_;
  IonPerfSpewer perfSpewer_;

 public:
#ifdef JS_JITSPEW
  void setGraphSpewer(GraphSpewer* graphSpewer) {
    MOZ_ASSERT(!graphSpewer_);
    graphSpewer_ = graphSpewer;
  }
#endif
  IonPerfSpewer& perfSpewer() { return perfSpewer_; }

  void spewBeginFunction(JSScript* function);
  void spewBeginWasmFunction(unsigned funcIndex);
  void spewPass(const char* name, BacktrackingAllocator* ra = nullptr);
  void spewEndFunction();

  // Explicitly reset compilation dependencies and perf spewer debug info.
  // This must be called to correctly free compilation dependencies, which may
  // have virtual destructors.
  void cleanup() {
    tracker.reset();
    perfSpewer().reset();
  }

  CompilationDependencyTracker tracker;
};

class AutoSpewEndFunction {
 private:
  MIRGenerator* mir_;

 public:
  explicit AutoSpewEndFunction(MIRGenerator* mir) : mir_(mir) {}
  ~AutoSpewEndFunction();
};

}  // namespace jit
}  // namespace js

#endif /* jit_MIRGenerator_h */
