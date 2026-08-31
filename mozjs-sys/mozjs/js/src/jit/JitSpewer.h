/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_JitSpewer_h
#define jit_JitSpewer_h

#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/IntegerPrintfMacros.h"

#include <stdarg.h>

#include "jit/GraphSpewer.h"
#include "jit/JitSpewChannelList.h"
#include "js/Printer.h"
#include "js/TypeDecls.h"
#include "vm/Logging.h"
#include "wasm/WasmTypeDecls.h"

enum JSValueType : uint8_t;

namespace js {
namespace jit {

enum JitSpewChannel {
#define JITSPEW_CHANNEL(name, help) JitSpew_##name,
  JITSPEW_CHANNEL_LIST(JITSPEW_CHANNEL)
#undef JITSPEW_CHANNEL
      JitSpew_Terminator
};

class BacktrackingAllocator;
class MDefinition;
class MIRGenerator;
class MIRGraph;
class TempAllocator;

const char* ValTypeToString(JSValueType type);

// The JitSpewer is only available on debug builds.
// None of the global functions have effect on non-debug builds.
#ifdef JS_JITSPEW

// Class made to hold the MIR and LIR graphs of an Wasm / Ion compilation and
// automatically spew them to JITSPEW.
class JitSpewGraphSpewer {
 private:
  MIRGraph* graph_;
  LSprinter jsonPrinter_;
  GraphSpewer graphSpewer_;

 public:
  explicit JitSpewGraphSpewer(TempAllocator* alloc,
                              const wasm::CodeMetadata* wasmCodeMeta = nullptr);

  bool isSpewing() const { return graph_; }
  void init(MIRGraph* graph, JSScript* function);
  void beginFunction(JSScript* function);
  void beginWasmFunction(unsigned funcIndex);
  void spewPass(const char* pass, BacktrackingAllocator* ra = nullptr);
  void endFunction();

  void dump(Fprinter& json);
};

void CheckLogging();

class JitSpewIndent {
  JitSpewChannel channel_;

 public:
  explicit JitSpewIndent(JitSpewChannel channel);
  ~JitSpewIndent();
};

// RAII helper that buffers one logical message (one or more lines) of spew
// on `channel` and emits it on destruction. The buffer is a single per-thread
// Vector reused across messages.
class MOZ_RAII AutoJitSpewMessage {
  bool enabled_;

 public:
  explicit AutoJitSpewMessage(JitSpewChannel channel);
  AutoJitSpewMessage(JitSpewChannel channel, const char* fmt, ...)
      MOZ_FORMAT_PRINTF(3, 4);
  ~AutoJitSpewMessage();

  void append(const char* fmt, ...) MOZ_FORMAT_PRINTF(2, 3);

  // Returns a GenericPrinter that appends to this message's buffer. Only
  // valid while this AutoJitSpewMessage is alive and enabled.
  js::GenericPrinter& printer();

  AutoJitSpewMessage(const AutoJitSpewMessage&) = delete;
  void operator=(const AutoJitSpewMessage&) = delete;
};

void JitSpew(JitSpewChannel channel, const char* fmt, ...)
    MOZ_FORMAT_PRINTF(2, 3);

}  // namespace jit

namespace jitspew::detail {
extern bool LoggingChecked;
extern mozilla::Atomic<uint32_t, mozilla::Relaxed> filteredOutCompilations;

// Array of LogModules indexed by JitSpewChannel.
inline constexpr const js::LogModule* const channelModules[] = {
#  define JITSPEW_MODULE_PTR(name, help) &name##Module,
    JITSPEW_CHANNEL_LIST(JITSPEW_MODULE_PTR)
#  undef JITSPEW_MODULE_PTR
};
}  // namespace jitspew::detail

namespace jit {

inline bool JitSpewEnabled(JitSpewChannel channel) {
  MOZ_ASSERT(jitspew::detail::LoggingChecked);
  if (jitspew::detail::filteredOutCompilations) {
    return false;
  }
  return jitspew::detail::channelModules[channel]->shouldLog(
      mozilla::LogLevel::Debug);
}

void JitSpewVA(JitSpewChannel channel, const char* fmt, va_list ap)
    MOZ_FORMAT_PRINTF(2, 0);
void JitSpewDef(JitSpewChannel channel, const char* str, MDefinition* def);

void EnableIonDebugSyncLogging();
void EnableIonDebugAsyncLogging();

#  define JitSpewIfEnabled(channel, fmt, ...) \
    do {                                      \
      if (JitSpewEnabled(channel)) {          \
        JitSpew(channel, fmt, __VA_ARGS__);   \
      }                                       \
    } while (false);

#else

class JitSpewGraphSpewer {
 public:
  explicit JitSpewGraphSpewer(
      TempAllocator* alloc, const wasm::CodeMetadata* wasmCodeMeta = nullptr) {}

  bool isSpewing() { return false; }
  void init(MIRGraph* graph, JSScript* function) {}
  void beginFunction(JSScript* function) {}
  void beginWasmFunction(unsigned funcIndex) {}
  void spewPass(const char* pass, BacktrackingAllocator* ra = nullptr) {}
  void endFunction() {}

  void dump(Fprinter& c1, Fprinter& json) {}
};

static inline void CheckLogging() {}

class JitSpewIndent {
 public:
  explicit JitSpewIndent(JitSpewChannel channel) {}
  ~JitSpewIndent() = default;
};

class MOZ_RAII AutoJitSpewMessage {
 public:
  explicit AutoJitSpewMessage(JitSpewChannel channel) {}
  template <typename... Args>
  AutoJitSpewMessage(JitSpewChannel channel, const char* fmt, Args&&... args) {}
  ~AutoJitSpewMessage() = default;
  template <typename... Args>
  void append(const char* fmt, Args&&... args) {}
  js::GenericPrinter& printer() {
    MOZ_CRASH("Shouldn't call this in non-JS_JITSPEW builds");
  }
};

// The computation of some of the argument of the spewing functions might be
// costly, thus we use variaidic macros to ignore any argument of these
// functions.
static inline void JitSpewCheckArguments(JitSpewChannel channel,
                                         const char* fmt) {}

#  define JitSpewCheckExpandedArgs(channel, fmt, ...) \
    JitSpewCheckArguments(channel, fmt)
#  define JitSpewCheckExpandedArgs_(ArgList) \
    JitSpewCheckExpandedArgs ArgList /* Fix MSVC issue */
#  define JitSpew(...) JitSpewCheckExpandedArgs_((__VA_ARGS__))

#  define JitSpewIfEnabled(channel, fmt, ...) \
    JitSpewCheckArguments(channel, fmt)

static inline bool JitSpewEnabled(JitSpewChannel channel) { return false; }
static inline MOZ_FORMAT_PRINTF(2, 0) void JitSpewVA(JitSpewChannel channel,
                                                     const char* fmt,
                                                     va_list ap) {}
static inline void JitSpewDef(JitSpewChannel channel, const char* str,
                              MDefinition* def) {}

static inline void EnableChannel(JitSpewChannel) {}
static inline void DisableChannel(JitSpewChannel) {}
static inline void EnableIonDebugSyncLogging() {}
static inline void EnableIonDebugAsyncLogging() {}

#endif /* JS_JITSPEW */

}  // namespace jit
}  // namespace js

#endif /* jit_JitSpewer_h */
