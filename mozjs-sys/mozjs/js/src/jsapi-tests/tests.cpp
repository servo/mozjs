/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jsapi-tests/tests.h"

#include "mozilla/ScopeExit.h"
#include "mozilla/Sprintf.h"
#include "mozilla/Utf8.h"  // mozilla::Utf8Unit

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "js/CharacterEncoding.h"
#include "js/CompilationAndEvaluation.h"  // JS::Evaluate
#include "js/Conversions.h"
#include "js/Equality.h"      // JS::SameValue
#include "js/GlobalObject.h"  // JS::DefaultGlobalClassOps, JS_NewGlobalObject
#include "js/Initialization.h"
#include "js/Prefs.h"
#include "js/PropertyAndElement.h"  // JS_DefineFunction
#include "js/RootingAPI.h"
#include "js/SourceText.h"  // JS::Source{Ownership,Text}
#include "js/Warnings.h"    // JS::SetWarningReporter

using namespace jsapitest;

// A singly linked list of tests which doesn't require runtime intialization.
//
// We rely on the runtime initialization of global variables containing test
// instances to add themselves to these lists, which depends on them already
// being in a valid state.
template <typename T>
class TestList {
  T* first = nullptr;
  T* last = nullptr;

 public:
  T* getFirst() const { return first; }

  void pushBack(T* element) {
    MOZ_ASSERT(!element->next);
    MOZ_ASSERT(bool(first) == bool(last));

    if (!first) {
      first = element;
      last = element;
      return;
    }

    last->next = element;
    last = element;
  }
};

static TestList<TestBase> testList;

TestBase::TestBase(TestKind kind) : kind(kind) { testList.pushBack(this); }

[[noreturn]] static void Die(const char* format, ...) {
  fprintf(stderr, "TEST-UNEXPECTED-FAIL | jsapi-tests | ");
  va_list ap;
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
  fprintf(stderr, "\n");
  exit(1);
}

bool TestBase::fail(const std::string& msg, const char* filename, int lineno) {
  char location[256];
  SprintfLiteral(location, "%s:%d:", filename, lineno);

  std::string message(location);
  message += msg;

  maybeAppendException(message);

  fprintf(stderr, "%s\n", message.c_str());

  if (msgs.length() != 0) {
    msgs += " | ";
  }
  msgs += message;

  return false;
}

/* static */
RuntimeTest* RuntimeTest::From(TestBase* test) {
  MOZ_ASSERT(test->isRuntimeTest());
  return static_cast<RuntimeTest*>(test);
}

RuntimeTest::RuntimeTest() : TestBase(TestKind::Runtime) {}

RuntimeTest::~RuntimeTest() {
  MOZ_RELEASE_ASSERT(!cx);
  MOZ_RELEASE_ASSERT(!global);
}

bool RuntimeTest::initContext(JSContext* maybeReusableContext) {
  if (maybeReusableContext && reuseGlobal) {
    cx = maybeReusableContext;
    global.init(cx, JS::CurrentGlobalOrNull(cx));
    return true;
  }

  MaybeFreeContext(maybeReusableContext);

  cx = createContext();
  if (!cx) {
    return false;
  }

  js::UseInternalJobQueues(cx);

  if (!JS::InitSelfHostedCode(cx)) {
    return false;
  }

  global.init(cx);
  createGlobal();
  if (!global) {
    return false;
  }

  JS::EnterRealm(cx, global);
  return true;
}

JSContext* RuntimeTest::maybeForgetContext() {
  if (!reuseGlobal) {
    return nullptr;
  }

  JSContext* reusableCx = cx;
  global.reset();
  cx = nullptr;
  return reusableCx;
}

/* static */
void RuntimeTest::MaybeFreeContext(JSContext* maybeCx) {
  if (maybeCx) {
    JS::LeaveRealm(maybeCx, nullptr);
    JS_DestroyContext(maybeCx);
  }
}

void RuntimeTest::uninit() {
  global.reset();
  MaybeFreeContext(cx);
  cx = nullptr;
  msgs.clear();
}

bool RuntimeTest::exec(const char* utf8, const char* filename, int lineno) {
  JS::CompileOptions opts(cx);
  opts.setFileAndLine(filename, lineno);

  JS::SourceText<mozilla::Utf8Unit> srcBuf;
  JS::RootedValue v(cx);
  return (srcBuf.init(cx, utf8, strlen(utf8), JS::SourceOwnership::Borrowed) &&
          JS::Evaluate(cx, opts, srcBuf, &v)) ||
         fail(utf8, filename, lineno);
}

bool RuntimeTest::execDontReport(const char* utf8, const char* filename,
                                 int lineno) {
  JS::CompileOptions opts(cx);
  opts.setFileAndLine(filename, lineno);

  JS::SourceText<mozilla::Utf8Unit> srcBuf;
  JS::RootedValue v(cx);
  return srcBuf.init(cx, utf8, strlen(utf8), JS::SourceOwnership::Borrowed) &&
         JS::Evaluate(cx, opts, srcBuf, &v);
}

bool RuntimeTest::evaluate(const char* utf8, const char* filename, int lineno,
                           JS::MutableHandleValue vp) {
  JS::CompileOptions opts(cx);
  opts.setFileAndLine(filename, lineno);

  JS::SourceText<mozilla::Utf8Unit> srcBuf;
  return (srcBuf.init(cx, utf8, strlen(utf8), JS::SourceOwnership::Borrowed) &&
          JS::Evaluate(cx, opts, srcBuf, vp)) ||
         fail(utf8, filename, lineno);
}

std::string RuntimeTest::jsvalToSource(JS::HandleValue v) {
  JS::Rooted<JSString*> str(cx, JS_ValueToSource(cx, v));
  if (str) {
    if (JS::UniqueChars bytes = JS_EncodeStringToUTF8(cx, str)) {
      return bytes.get();
    }
  }
  JS_ClearPendingException(cx);
  return "<<error converting value to string>>";
}

std::string RuntimeTest::toSource(char c) {
  char buf[2] = {c, '\0'};
  return buf;
}

std::string RuntimeTest::toSource(long v) {
  char buf[40];
  SprintfLiteral(buf, "%ld", v);
  return buf;
}

std::string RuntimeTest::toSource(unsigned long v) {
  char buf[40];
  SprintfLiteral(buf, "%lu", v);
  return buf;
}

std::string RuntimeTest::toSource(long long v) {
  char buf[40];
  SprintfLiteral(buf, "%lld", v);
  return buf;
}

std::string RuntimeTest::toSource(unsigned long long v) {
  char buf[40];
  SprintfLiteral(buf, "%llu", v);
  return buf;
}

std::string RuntimeTest::toSource(double d) {
  char buf[40];
  SprintfLiteral(buf, "%17lg", d);
  return buf;
}

std::string RuntimeTest::toSource(unsigned int v) {
  return toSource((unsigned long)v);
}

std::string RuntimeTest::toSource(int v) { return toSource((long)v); }

std::string RuntimeTest::toSource(bool v) { return v ? "true" : "false"; }

std::string RuntimeTest::toSource(JS::RegExpFlags flags) {
  std::string str;
  if (flags.hasIndices()) {
    str += "d";
  }
  if (flags.global()) {
    str += "g";
  }
  if (flags.ignoreCase()) {
    str += "i";
  }
  if (flags.multiline()) {
    str += "m";
  }
  if (flags.dotAll()) {
    str += "s";
  }
  if (flags.unicode()) {
    str += "u";
  }
  if (flags.unicodeSets()) {
    str += "v";
  }
  if (flags.sticky()) {
    str += "y";
  }
  return str;
}

std::string RuntimeTest::toSource(JSAtom* v) {
  JS::RootedValue val(cx, JS::StringValue((JSString*)v));
  return jsvalToSource(val);
}

bool RuntimeTest::checkSame(const JS::Value& actualArg,
                            const JS::Value& expectedArg,
                            const char* actualExpr, const char* expectedExpr,
                            const char* filename, int lineno) {
  bool same = false;
  JS::RootedValue actual(cx, actualArg);
  JS::RootedValue expected(cx, expectedArg);
  if (JS::SameValue(cx, actual, expected, &same) && same) {
    return true;
  }

  return false;
}

void RuntimeTest::maybeAppendException(std::string& message) {
  if (JS_IsExceptionPending(cx)) {
    message += " -- ";

    js::gc::AutoSuppressGC gcoff(cx);
    JS::RootedValue v(cx);
    JS_GetPendingException(cx, &v);
    JS_ClearPendingException(cx);
    JS::Rooted<JSString*> s(cx, JS::ToString(cx, v));
    if (s) {
      if (JS::UniqueChars bytes = JS_EncodeStringToLatin1(cx, s)) {
        message += bytes.get();
      }
    }
  }
}

/* static */
const JSClass* RuntimeTest::basicGlobalClass() {
  static const JSClass c = {
      "global",
      JSCLASS_GLOBAL_FLAGS,
      &JS::DefaultGlobalClassOps,
  };
  return &c;
}

/* static */
void RuntimeTest::reportWarning(JSContext* cx, JSErrorReport* report) {
  MOZ_RELEASE_ASSERT(report->isWarning());

  fprintf(stderr, "%s:%u:%s\n",
          report->filename ? report->filename.c_str() : "<no filename>",
          (unsigned int)report->lineno, report->message().c_str());
}

/* static */
bool RuntimeTest::print(JSContext* cx, unsigned argc, JS::Value* vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

  JS::Rooted<JSString*> str(cx);
  for (unsigned i = 0; i < args.length(); i++) {
    str = JS::ToString(cx, args[i]);
    if (!str) {
      return false;
    }
    JS::UniqueChars bytes = JS_EncodeStringToUTF8(cx, str);
    if (!bytes) {
      return false;
    }
    printf("%s%s", i ? " " : "", bytes.get());
  }

  putchar('\n');
  fflush(stdout);
  args.rval().setUndefined();
  return true;
}

bool RuntimeTest::definePrint() {
  return JS_DefineFunction(cx, global, "print", (JSNative)print, 0, 0);
}

JSContext* RuntimeTest::createContext() {
  JSContext* cx = JS_NewContext(8L * 1024 * 1024);
  if (!cx) {
    return nullptr;
  }
  JS::SetWarningReporter(cx, &reportWarning);
  return cx;
}

JSObject* RuntimeTest::createGlobal(JSPrincipals* principals) {
  /* Create the global object. */
  JS::RootedObject newGlobal(cx);
  JS::RealmOptions options;
  options.creationOptions().setSharedMemoryAndAtomicsEnabled(true);
  newGlobal = JS_NewGlobalObject(cx, getGlobalClass(), principals,
                                 JS::FireOnNewGlobalHook, options);
  if (!newGlobal) {
    return nullptr;
  }

  global = newGlobal;
  return newGlobal;
}

FrontendTest::FrontendTest() : TestBase(TestKind::Frontend) {}

TempFile::TempFile() : name(), stream() {}

TempFile::~TempFile() {
  if (stream) {
    close();
  }
  if (name) {
    remove();
  }
}

FILE* TempFile::open(const char* fileName) {
  stream = fopen(fileName, "wb+");
  if (!stream) {
    Die("error opening temporary file '%s': %s", fileName, strerror(errno));
  }
  name = fileName;
  return stream;
}

void TempFile::close() {
  if (fclose(stream) == EOF) {
    Die("error closing temporary file '%s': %s", name, strerror(errno));
  }
  stream = nullptr;
}

void TempFile::remove() {
  if (::remove(name) != 0) {
    Die("error deleting temporary file '%s': %s", name, strerror(errno));
  }
  name = nullptr;
}

TestJSPrincipals::TestJSPrincipals(int rc) { refcount = rc; }

bool TestJSPrincipals::write(JSContext* cx, JSStructuredCloneWriter* writer) {
  MOZ_CRASH("TestJSPrincipals::write not implemented");
}

ExternalData::ExternalData(const char* str)
    : contents_(strdup(str)), len_(strlen(str) + 1) {}
void ExternalData::free() {
  MOZ_ASSERT(!wasFreed());
  ::free(contents_);
  contents_ = nullptr;
}

mozilla::UniquePtr<void, JS::BufferContentsDeleter> ExternalData::pointer() {
  MOZ_ASSERT(!uniquePointerCreated_,
             "Not allowed to create multiple unique pointers to contents");
  uniquePointerCreated_ = true;
  return {contents_, {ExternalData::freeCallback, this}};
}

/* static */
void ExternalData::freeCallback(void* contents, void* userData) {
  auto self = static_cast<ExternalData*>(userData);
  MOZ_ASSERT(self->contents() == contents);
  self->free();
}

AutoGCParameter::AutoGCParameter(JSContext* cx, JSGCParamKey key,
                                 uint32_t value)
    : cx_(cx), key_(key), value_() {
  value_ = JS_GetGCParameter(cx, key);
  JS_SetGCParameter(cx, key, value);
}

AutoGCParameter::~AutoGCParameter() { JS_SetGCParameter(cx_, key_, value_); }

#ifdef JS_GC_ZEAL

AutoLeaveZeal::AutoLeaveZeal(JSContext* cx)
    : cx_(cx), zealBits_(0), frequency_(0) {
  uint32_t dummy;
  JS::GetGCZealBits(cx_, &zealBits_, &frequency_, &dummy);
  JS::SetGCZeal(cx_, 0, 0);
  JS::PrepareForFullGC(cx_);
  JS::NonIncrementalGC(cx_, JS::GCOptions::Normal, JS::GCReason::DEBUG_GC);
}

AutoLeaveZeal::~AutoLeaveZeal() {
  JS::SetGCZeal(cx_, 0, 0);
  for (size_t i = 0; i < sizeof(zealBits_) * 8; i++) {
    if (zealBits_ & (1 << i)) {
      JS::SetGCZeal(cx_, i, frequency_);
    }
  }

#  ifdef DEBUG
  uint32_t zealBitsAfter, frequencyAfter, dummy;
  JS::GetGCZealBits(cx_, &zealBitsAfter, &frequencyAfter, &dummy);
  MOZ_ASSERT(zealBitsAfter == zealBits_);
  MOZ_ASSERT(frequencyAfter == frequency_);
#  endif
}
#else

AutoLeaveZeal::AutoLeaveZeal(JSContext* cx) {}
AutoLeaveZeal::~AutoLeaveZeal() {}

#endif

struct CommandOptions {
  bool list = false;
  bool runRuntimeTests = true;
  bool runFrontendTests = true;
  bool help = false;
  const char* filter = nullptr;
};

static void PrintUsage() {
  printf("Usage: jsapi-tests [OPTIONS] [FILTER]\n");
  printf("\n");
  printf("Options:\n");
  printf("    -h, --help          Display this message\n");
  printf("        --list          List all tests\n");
  printf(
      "        --frontend-only Run tests for frontend-only APIs, with "
      "light-weight entry point\n");
}

static void ParseArgs(int argc, char* argv[], CommandOptions& options) {
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      options.help = true;
      continue;
    }

    if (strcmp(argv[i], "--list") == 0) {
      options.list = true;
      continue;
    }

    if (strcmp(argv[i], "--frontend-only") == 0) {
      options.runRuntimeTests = false;
      continue;
    }

    if (!options.filter) {
      options.filter = argv[i];
      continue;
    }

    printf("error: Unrecognized option: %s\n", argv[i]);
    options.help = true;
  }
}

static void NewHandler() {
  std::set_new_handler(nullptr);
  Die("Out of memory.");
}

template <typename TestT>
void PrintTests(TestList<TestT> list) {
  for (TestT* test = list.getFirst(); test; test = test->next) {
    printf("%s\n", test->name());
  }
}

void RunTests(int& total, int& failures, CommandOptions& options,
              TestList<TestBase> list) {
  // Reinitializing the global for every test is quite slow, due to having to
  // recompile all self-hosted builtins. Allow tests to opt-in to reusing the
  // context and global.
  JSContext* maybeReusedContext = nullptr;
  auto guard = mozilla::MakeScopeExit(
      [&]() { RuntimeTest::MaybeFreeContext(maybeReusedContext); });

  for (TestBase* test = list.getFirst(); test; test = test->next) {
    if ((test->isRuntimeTest() && !options.runRuntimeTests) ||
        (!test->isRuntimeTest() && !options.runFrontendTests)) {
      continue;
    }

    const char* name = test->name();
    if (options.filter && strstr(name, options.filter) == nullptr) {
      continue;
    }

    total += 1;

    printf("%s\n", name);

    // Make sure the test name is printed before we enter the test that can
    // crash on failure.
    fflush(stdout);

    if (test->isRuntimeTest() &&
        !RuntimeTest::From(test)->initContext(maybeReusedContext)) {
      printf("TEST-UNEXPECTED-FAIL | %s | Failed to set context.\n", name);
      failures++;
      continue;
    }

    if (!test->init()) {
      printf("TEST-UNEXPECTED-FAIL | %s | Failed to initialize.\n", name);
      failures++;
      test->uninit();
      continue;
    }

    if (test->run()) {
      printf("TEST-PASS | %s | ok\n", name);
    } else {
      std::string messages = test->messages();
      printf("%s | %s | %s\n",
             (test->knownFail ? "TEST-KNOWN-FAIL" : "TEST-UNEXPECTED-FAIL"),
             name, messages.c_str());
      if (!test->knownFail) {
        failures++;
      }
    }

    if (test->isRuntimeTest()) {
      // Return a non-nullptr pointer if the context & global can safely be
      // reused for the next test.
      maybeReusedContext = RuntimeTest::From(test)->maybeForgetContext();
    }

    test->uninit();
  }
}

int main(int argc, char* argv[]) {
  int total = 0;
  int failures = 0;
  CommandOptions options;
  ParseArgs(argc, argv, options);

  if (options.help) {
    PrintUsage();
    return 0;
  }

  // Ensure allocation failure in std::string gets reported as test failure.
  std::set_new_handler(NewHandler);

  // Override prefs for jsapi-tests.
  JS::Prefs::setAtStartup_experimental_weakrefs_expose_cleanupSome(true);
  JS::Prefs::setAtStartup_experimental_symbols_as_weakmap_keys(true);

  if (options.runRuntimeTests) {
    if (!JS_Init()) {
      Die("JS_Init() failed.");
    }
  } else if (options.runFrontendTests) {
    if (!JS_FrontendOnlyInit()) {
      Die("JS_FrontendOnlyInit() failed.");
    }
  }

  if (options.list) {
    PrintTests(testList);
    return 0;
  }

  RunTests(total, failures, options, testList);

  MOZ_RELEASE_ASSERT(!JSRuntime::hasLiveRuntimes());
  if (options.runRuntimeTests) {
    JS_ShutDown();
  } else if (options.runFrontendTests) {
    JS_FrontendOnlyShutDown();
  }

  if (failures) {
    printf("\n%d unexpected failure%s.\n", failures,
           (failures == 1 ? "" : "s"));
    return 1;
  }
  printf("\nPassed: ran %d tests.\n", total);
  return 0;
}
