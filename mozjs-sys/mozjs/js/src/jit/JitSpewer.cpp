/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef JS_JITSPEW

#  include "jit/JitSpewer.h"

#  include "mozilla/Atomics.h"
#  include "mozilla/Sprintf.h"
#  include "mozilla/ThreadLocal.h"
#  include "mozilla/Vector.h"

#  include "jit/MIR.h"
#  include "jit/MIRGenerator.h"
#  include "jit/MIRGraph.h"
#  include "threading/LockGuard.h"
#  include "util/GetPidProvider.h"  // getpid()
#  include "vm/Logging.h"
#  include "vm/MutexIDs.h"

#  ifndef JIT_SPEW_DIR
#    if defined(_WIN32)
#      define JIT_SPEW_DIR "."
#    elif defined(__ANDROID__)
#      define JIT_SPEW_DIR "/data/local/tmp"
#    else
#      define JIT_SPEW_DIR "/tmp"
#    endif
#  endif

using namespace js;
using namespace js::jit;
using namespace js::jitspew::detail;

class JitSpewGraphOutput {
 private:
  Mutex outputLock_ MOZ_UNANNOTATED;
  Fprinter jsonOutput_;
  GraphSpewer graphSpewer_;
  bool firstFunction_;
  bool asyncLogging_;
  bool inited_;

  void release();

 public:
  JitSpewGraphOutput()
      : outputLock_(mutexid::JitSpewGraphOutput),
        graphSpewer_(jsonOutput_),
        firstFunction_(false),
        asyncLogging_(false),
        inited_(false) {}

  // File output is terminated safely upon destruction.
  ~JitSpewGraphOutput();

  bool init();
  bool isEnabled() { return inited_; }
  void setAsyncLogging(bool incremental) { asyncLogging_ = incremental; }
  bool getAsyncLogging() { return asyncLogging_; }

  void beginFunction();
  void spewPass(JitSpewGraphSpewer* gs);
  void endFunction(JitSpewGraphSpewer* gs);
};

// JitSpewGraphOutput singleton.
MOZ_RUNINIT static JitSpewGraphOutput jitSpewGraphOutput;

bool jitspew::detail::LoggingChecked = false;
mozilla::Atomic<uint32_t, mozilla::Relaxed>
    jitspew::detail::filteredOutCompilations(0);

// Set the JS_LOG level for the LogModule matching `channel`.
static void SetChannelLogLevel(JitSpewChannel channel,
                               mozilla::LogLevel level) {
  const js::LogModule* mod = jitspew::detail::channelModules[channel];
  if (mod->interface.isComplete() && mod->logger) {
    mod->interface.getLevelRef(mod->logger) = level;
  }
}

static size_t ChannelIndentLevel[] = {
#  define JITSPEW_CHANNEL(name, help) 0,
    JITSPEW_CHANNEL_LIST(JITSPEW_CHANNEL)
#  undef JITSPEW_CHANNEL
};

struct SpewTlsState;

class TlsBufPrinter final : public js::GenericPrinter {
 public:
  explicit TlsBufPrinter(SpewTlsState* state) : state_(state) {}
  void put(const char* s, size_t len) override;

 private:
  SpewTlsState* state_;
};

// Per-thread state used by AutoJitSpewMessage to assemble lines and dispatch
// them to the matching JS_LOG module.
struct SpewTlsState {
  mozilla::Vector<char, 256> buf;
  TlsBufPrinter printer{this};
  JitSpewChannel owner = JitSpew_Terminator;
};

// TLS slot for per-thread spew state.
static MOZ_THREAD_LOCAL(SpewTlsState*) tlsState;

static SpewTlsState* GetOrCreateSpewTlsState() {
  SpewTlsState* state = tlsState.get();
  if (!state) {
    AutoEnterOOMUnsafeRegion oomUnsafe;
    state = js_new<SpewTlsState>();
    if (!state) {
      oomUnsafe.crash("OOM allocating JIT spew TLS state");
    }
    tlsState.set(state);
  }
  return state;
}

// Writes a single line to the JS_LOG LogModule.
static void FlushTlsBufLine(SpewTlsState* state) {
  auto& buf = state->buf;
  if (!buf.append('\0')) {
    buf.clear();
    return;
  }
  const js::LogModule* mod = jitspew::detail::channelModules[state->owner];
  if (mod->interface.isComplete() && mod->logger) {
    mod->interface.logPrint(mod->logger, mozilla::LogLevel::Debug, "%s",
                            buf.begin());
  }
  buf.clear();
}

void TlsBufPrinter::put(const char* s, size_t len) {
  auto& buf = state_->buf;
  while (len > 0) {
    const char* nl = static_cast<const char*>(memchr(s, '\n', len));
    size_t chunk = nl ? size_t(nl - s) : len;
    if (chunk && !buf.append(s, chunk)) {
      setPendingOutOfMemory();
    }
    if (!nl) {
      // Wait until we see a line terminator.
      return;
    }
    FlushTlsBufLine(state_);
    s += chunk + 1;
    len -= chunk + 1;
  }
}

// The IONFILTER environment variable specifies an expression to select only
// certain functions for spewing to reduce amount of log data generated.
static const char* gSpewFilter = nullptr;

static bool FilterContainsLocation(JSScript* function) {
  // If there is no filter we accept all outputs.
  if (!gSpewFilter || !gSpewFilter[0]) {
    return true;
  }

  // Disable wasm output when filter is set.
  if (!function) {
    return false;
  }

  const char* filename = function->filename();
  const size_t line = function->lineno();
  const size_t filelen = strlen(filename);
  const char* index = strstr(gSpewFilter, filename);
  while (index) {
    if (index == gSpewFilter || index[-1] == ',') {
      if (index[filelen] == 0 || index[filelen] == ',') {
        return true;
      }
      if (index[filelen] == ':' && line != size_t(-1)) {
        size_t read_line = strtoul(&index[filelen + 1], nullptr, 10);
        if (read_line == line) {
          return true;
        }
      }
    }
    index = strstr(index + filelen, filename);
  }
  return false;
}

void jit::EnableIonDebugSyncLogging() {
  jitSpewGraphOutput.init();
  jitSpewGraphOutput.setAsyncLogging(false);
  SetChannelLogLevel(JitSpew_IonSyncLogs, mozilla::LogLevel::Debug);
}

void jit::EnableIonDebugAsyncLogging() {
  jitSpewGraphOutput.init();
  jitSpewGraphOutput.setAsyncLogging(true);
}

void JitSpewGraphOutput::release() {
  if (jsonOutput_.isInitialized()) {
    jsonOutput_.finish();
  }
  inited_ = false;
}

bool JitSpewGraphOutput::init() {
  if (inited_) {
    return true;
  }

  // Filter expression for spewing
  gSpewFilter = getenv("IONFILTER");

  const size_t bufferLength = 256;
  char jsonBuffer[bufferLength];
  const char* jsonFilename = JIT_SPEW_DIR "/ion.json";

  const char* usePid = getenv("ION_SPEW_BY_PID");
  if (usePid && *usePid != 0) {
    uint32_t pid = getpid();
    size_t len;
    len = SprintfLiteral(jsonBuffer, JIT_SPEW_DIR "/ion%" PRIu32 ".json", pid);
    if (bufferLength <= len) {
      fprintf(stderr,
              "Warning: JitSpewGraphOutput::init: Cannot serialize file name.");
      return false;
    }
    jsonFilename = jsonBuffer;
  }

  if (!jsonOutput_.init(jsonFilename)) {
    release();
    return false;
  }

  graphSpewer_.begin();
  firstFunction_ = true;

  inited_ = true;
  return true;
}

void JitSpewGraphOutput::beginFunction() {
  // If we are doing a synchronous logging then we spew everything as we go,
  // as this is useful in case of failure during the compilation. On the other
  // hand, it is recommended to disable off thread compilation.
  if (!getAsyncLogging() && !firstFunction_) {
    LockGuard<Mutex> guard(outputLock_);
    jsonOutput_.put(",");  // separate functions
  }
}

void JitSpewGraphOutput::spewPass(JitSpewGraphSpewer* gs) {
  if (!getAsyncLogging()) {
    LockGuard<Mutex> guard(outputLock_);
    gs->dump(jsonOutput_);
  }
}

void JitSpewGraphOutput::endFunction(JitSpewGraphSpewer* gs) {
  LockGuard<Mutex> guard(outputLock_);
  if (getAsyncLogging() && !firstFunction_) {
    jsonOutput_.put(",");  // separate functions
  }

  gs->dump(jsonOutput_);
  firstFunction_ = false;
}

JitSpewGraphOutput::~JitSpewGraphOutput() {
  if (!inited_) {
    return;
  }

  graphSpewer_.end();
  release();
}

JitSpewGraphSpewer::JitSpewGraphSpewer(TempAllocator* alloc,
                                       const wasm::CodeMetadata* wasmCodeMeta)
    : graph_(nullptr),
      jsonPrinter_(alloc->lifoAlloc()),
      graphSpewer_(jsonPrinter_, wasmCodeMeta) {}

void JitSpewGraphSpewer::init(MIRGraph* graph, JSScript* function) {
  MOZ_ASSERT(!isSpewing());
  if (!jitSpewGraphOutput.isEnabled()) {
    return;
  }

  if (!FilterContainsLocation(function)) {
    // filter out logs during the compilation.
    filteredOutCompilations++;
    MOZ_ASSERT(!isSpewing());
    return;
  }

  graph_ = graph;
  MOZ_ASSERT(isSpewing());
}

void JitSpewGraphSpewer::beginFunction(JSScript* function) {
  if (!isSpewing()) {
    return;
  }
  graphSpewer_.beginFunction(function);
  jitSpewGraphOutput.beginFunction();
}

void JitSpewGraphSpewer::beginWasmFunction(unsigned funcIndex) {
  if (!isSpewing()) {
    return;
  }
  graphSpewer_.beginWasmFunction(funcIndex);
  jitSpewGraphOutput.beginFunction();
}

void JitSpewGraphSpewer::spewPass(const char* pass, BacktrackingAllocator* ra) {
  if (!isSpewing()) {
    return;
  }

  graphSpewer_.spewPass(pass, graph_, ra);
  jitSpewGraphOutput.spewPass(this);

  // As this function is used for debugging, we ignore any of the previous
  // failures and ensure there is enough ballast space, such that we do not
  // exhaust the ballast space before running the next phase.
  AutoEnterOOMUnsafeRegion oomUnsafe;
  if (!graph_->alloc().ensureBallast()) {
    oomUnsafe.crash(
        "Could not ensure enough ballast space after spewing graph "
        "information.");
  }
}

void JitSpewGraphSpewer::endFunction() {
  if (!jitSpewGraphOutput.isEnabled()) {
    return;
  }

  if (!isSpewing()) {
    MOZ_ASSERT(filteredOutCompilations != 0);
    filteredOutCompilations--;
    return;
  }

  graphSpewer_.endFunction();

  jitSpewGraphOutput.endFunction(this);
  graph_ = nullptr;
}

void JitSpewGraphSpewer::dump(Fprinter& jsonOut) {
  if (!jsonPrinter_.hadOutOfMemory()) {
    jsonPrinter_.exportInto(jsonOut);
  } else {
    jsonOut.put("{}");
  }
  jsonOut.flush();
  jsonPrinter_.clear();
}

static void PrintHelpAndExit(int status = 0) {
  fflush(nullptr);
  FILE* out = status == 0 ? stdout : stderr;
  fputs(
      "\n"
      "Use MOZ_LOG=help to see the full list of JS_LOG modules.\n"
      "\n"
      "usage: IONFLAGS=option,option,option,...\n"
      "\n",
      out);
#  define EMIT(tok, chan) fprintf(out, "  %-22s %s\n", tok, chan##Module.help);
  IONFLAGS_CHANNEL_LIST(EMIT)
#  undef EMIT
  fputs(
      "\n"
      "  all                    Enable every JIT spew module at Debug level\n"
      "  bl-all                 Enable all baseline modules\n"
      "  stubfolding-details    StubFolding + StubFoldingDetails\n"
      "  unroll-details         Unroll + UnrollDetails\n"
      "  logs                   JSON visualization logging to /tmp/ion.json\n"
      "  logs-sync              Same as logs, but flushes between passes "
      "(sync. compiled functions only)\n"
      "  help                   Print this message and exit\n"
      "\n"
      "See also SPEW=help for information on the Structured Spewer.\n",
      out);
  exit(status);
}

static bool IsFlag(const char* found, const char* flag) {
  return strlen(found) == strlen(flag) && strcmp(found, flag) == 0;
}

void jit::CheckLogging() {
  if (LoggingChecked) {
    return;
  }

  LoggingChecked = true;
  tlsState.infallibleInit();

  char* env = getenv("IONFLAGS");
  if (!env) {
    return;
  }

  auto enable = [](JitSpewChannel channel) {
    SetChannelLogLevel(channel, mozilla::LogLevel::Debug);
  };

  struct TokenToChannel {
    const char* tok;
    JitSpewChannel chan;
  };
  static constexpr TokenToChannel tokenToChannelTable[] = {
#  define ENTRY(tok, chan) {tok, JitSpew_##chan},
      IONFLAGS_CHANNEL_LIST(ENTRY)
#  undef ENTRY
  };

  const char* found = strtok(env, ",");
  while (found) {
    fprintf(stderr, "found tag: %s\n", found);

    // Check if this matches a flag in tokenToChannelTable.
    bool handled = false;
    for (const auto& entry : tokenToChannelTable) {
      if (IsFlag(found, entry.tok)) {
        enable(entry.chan);
        handled = true;
        break;
      }
    }

    if (!handled) {
      if (IsFlag(found, "help")) {
        PrintHelpAndExit();
      } else if (IsFlag(found, "stubfolding-details")) {
        enable(JitSpew_StubFolding);
        enable(JitSpew_StubFoldingDetails);
      } else if (IsFlag(found, "unroll-details")) {
        enable(JitSpew_Unroll);
        enable(JitSpew_UnrollDetails);
      } else if (IsFlag(found, "logs")) {
        EnableIonDebugAsyncLogging();
      } else if (IsFlag(found, "logs-sync")) {
        EnableIonDebugSyncLogging();
      } else if (IsFlag(found, "all")) {
#  define JITSPEW_ENABLE_ALL(name, help) enable(JitSpew_##name);
        JITSPEW_CHANNEL_LIST(JITSPEW_ENABLE_ALL)
#  undef JITSPEW_ENABLE_ALL
      } else if (IsFlag(found, "bl-all")) {
        enable(JitSpew_BaselineAbort);
        enable(JitSpew_BaselineScripts);
        enable(JitSpew_BaselineOp);
        enable(JitSpew_BaselineIC);
        enable(JitSpew_BaselineICFallback);
        enable(JitSpew_BaselineOSR);
        enable(JitSpew_BaselineBailouts);
        enable(JitSpew_BaselineDebugModeOSR);
      } else {
        fprintf(stderr, "Unknown flag.\n");
        PrintHelpAndExit(64);
      }
    }
    found = strtok(nullptr, ",");
  }
}

JitSpewIndent::JitSpewIndent(JitSpewChannel channel) : channel_(channel) {
  ChannelIndentLevel[channel]++;
}

JitSpewIndent::~JitSpewIndent() { ChannelIndentLevel[channel_]--; }

AutoJitSpewMessage::AutoJitSpewMessage(JitSpewChannel channel)
    : enabled_(JitSpewEnabled(channel)) {
  if (!enabled_) {
    return;
  }
  SpewTlsState* state = GetOrCreateSpewTlsState();
  MOZ_ASSERT(state->owner == JitSpew_Terminator,
             "Nested AutoJitSpewMessage on the same thread is not supported");
  state->owner = channel;
  state->buf.clear();
  for (size_t i = ChannelIndentLevel[channel]; i != 0; i--) {
    state->printer.put("  ", 2);
  }
}

AutoJitSpewMessage::AutoJitSpewMessage(JitSpewChannel channel, const char* fmt,
                                       ...)
    : AutoJitSpewMessage(channel) {
  if (!enabled_) {
    return;
  }
  va_list ap;
  va_start(ap, fmt);
  tlsState.get()->printer.vprintf(fmt, ap);
  va_end(ap);
}

void AutoJitSpewMessage::append(const char* fmt, ...) {
  if (!enabled_) {
    return;
  }
  va_list ap;
  va_start(ap, fmt);
  tlsState.get()->printer.vprintf(fmt, ap);
  va_end(ap);
}

js::GenericPrinter& AutoJitSpewMessage::printer() {
  MOZ_ASSERT(enabled_);
  return tlsState.get()->printer;
}

AutoJitSpewMessage::~AutoJitSpewMessage() {
  if (!enabled_) {
    return;
  }
  SpewTlsState* state = tlsState.get();
  MOZ_ASSERT(state);
  MOZ_ASSERT(state->owner != JitSpew_Terminator);
  if (!state->buf.empty()) {
    FlushTlsBufLine(state);
  }
  state->owner = JitSpew_Terminator;
}

void jit::JitSpewVA(JitSpewChannel channel, const char* fmt, va_list ap) {
  if (!JitSpewEnabled(channel)) {
    return;
  }
  AutoJitSpewMessage msg(channel);
  msg.printer().vprintf(fmt, ap);
}

void jit::JitSpew(JitSpewChannel channel, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  JitSpewVA(channel, fmt, ap);
  va_end(ap);
}

void jit::JitSpewDef(JitSpewChannel channel, const char* str,
                     MDefinition* def) {
  if (!JitSpewEnabled(channel)) {
    return;
  }
  AutoJitSpewMessage msg(channel, "%s", str);
  def->dump(msg.printer());
  def->dumpLocation(msg.printer());
}

#endif /* JS_JITSPEW */

#if defined(JS_JITSPEW) || defined(ENABLE_JS_AOT_ICS)

const char* js::jit::ValTypeToString(JSValueType type) {
  switch (type) {
    case JSVAL_TYPE_DOUBLE:
      return "Double";
    case JSVAL_TYPE_INT32:
      return "Int32";
    case JSVAL_TYPE_BOOLEAN:
      return "Boolean";
    case JSVAL_TYPE_UNDEFINED:
      return "Undefined";
    case JSVAL_TYPE_NULL:
      return "Null";
    case JSVAL_TYPE_MAGIC:
      return "Magic";
    case JSVAL_TYPE_STRING:
      return "String";
    case JSVAL_TYPE_SYMBOL:
      return "Symbol";
    case JSVAL_TYPE_PRIVATE_GCTHING:
      return "PrivateGCThing";
    case JSVAL_TYPE_BIGINT:
      return "BigInt";
    case JSVAL_TYPE_OBJECT:
      return "Object";
    case JSVAL_TYPE_UNKNOWN:
      return "None";
    default:
      MOZ_CRASH("Unknown JSValueType");
  }
}

#endif /* defined(JS_JITSPEW) || defined(ENABLE_JS_AOT_ICS) */
