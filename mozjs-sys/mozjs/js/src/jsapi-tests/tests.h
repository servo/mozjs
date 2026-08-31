/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jsapi_tests_tests_h
#define jsapi_tests_tests_h

#include <string>
#include <type_traits>

#include "jsapi.h"

#include "gc/GC.h"
#include "js/AllocPolicy.h"
#include "js/ArrayBuffer.h"  // BufferContentsDeleter
#include "js/Principals.h"   // JSPrincipals
#include "js/RegExpFlags.h"  // JS::RegExpFlags
#include "js/RootingAPI.h"   // JS::PersistentRootedObject
#include "js/Value.h"
#include "js/Vector.h"
#include "vm/JSContext.h"

namespace jsapitest {

enum class TestKind { Runtime, Frontend };

class TestBase {
 public:
  TestBase* next = nullptr;
  const TestKind kind;
  bool knownFail = false;
  std::string msgs;

  TestBase(TestKind kind);
  virtual ~TestBase() {}

  bool isRuntimeTest() const { return kind == TestKind::Runtime; }

  virtual const char* name() = 0;
  virtual bool run() = 0;

  // These methods may be overridden by the test to perform additional
  // initialization after any JSContext and global have been created.
  virtual bool init() { return true; }
  virtual void uninit() {}

  virtual void maybeAppendException(std::string& message) {}

  bool fail(const std::string& msg = std::string(), const char* filename = "-",
            int lineno = 0);

  std::string messages() const { return msgs; }
};

class RuntimeTest : public TestBase {
 public:
  JSContext* cx = nullptr;
  JS::PersistentRootedObject global;

  // Whether this test is willing to skip its init() and reuse a global (and
  // JSContext etc.) from a previous test that also has reuseGlobal=true. It
  // also means this test is willing to skip its uninit() if it is followed by
  // another reuseGlobal test.
  bool reuseGlobal = false;

  // Downcase TestBase to RuntimeTest.
  static RuntimeTest* From(TestBase* test);

  RuntimeTest();
  virtual ~RuntimeTest();

  // Initialize the context, possibly with one from a previously run test.
  bool initContext(JSContext* maybeReusedContext);

  // If this test is ok with its cx and global being reused, release this
  // test's cx to be reused by another test.
  JSContext* maybeForgetContext();

  static void MaybeFreeContext(JSContext* maybeCx);

  void uninit() override;

#define EXEC(s)                         \
  do {                                  \
    if (!exec(s, __FILE__, __LINE__)) { \
      return false;                     \
    }                                   \
  } while (false)

  bool exec(const char* utf8, const char* filename, int lineno);

  // Like exec(), but doesn't call fail() if JS::Evaluate returns false.
  bool execDontReport(const char* utf8, const char* filename, int lineno);

#define EVAL(s, vp)                             \
  do {                                          \
    if (!evaluate(s, __FILE__, __LINE__, vp)) { \
      return false;                             \
    }                                           \
  } while (false)

  bool evaluate(const char* utf8, const char* filename, int lineno,
                JS::MutableHandleValue vp);

  std::string jsvalToSource(JS::HandleValue v);

  std::string toSource(char c);
  std::string toSource(long v);
  std::string toSource(unsigned long v);
  std::string toSource(long long v);
  std::string toSource(unsigned long long v);
  std::string toSource(double d);
  std::string toSource(unsigned int v);
  std::string toSource(int v);
  std::string toSource(bool v);
  std::string toSource(JS::RegExpFlags flags);
  std::string toSource(JSAtom* v);

  // Note that in some still-supported GCC versions (we think anything before
  // GCC 4.6), this template does not work when the second argument is
  // nullptr. It infers type U = long int. Use CHECK_NULL instead.
  template <typename T, typename U>
  bool checkEqual(const T& actual, const U& expected, const char* actualExpr,
                  const char* expectedExpr, const char* filename, int lineno) {
    static_assert(std::is_signed_v<T> == std::is_signed_v<U>,
                  "using CHECK_EQUAL with different-signed inputs triggers "
                  "compiler warnings");
    static_assert(
        std::is_unsigned_v<T> == std::is_unsigned_v<U>,
        "using CHECK_EQUAL with different-signed inputs triggers compiler "
        "warnings");

    if (actual == expected) {
      return true;
    }

    fail(std::string("CHECK_EQUAL failed: expected (") + expectedExpr +
             ") = " + toSource(expected) + ", got (" + actualExpr +
             ") = " + toSource(actual),
         filename, lineno);
    return false;
  }

#define CHECK_EQUAL(actual, expected)                               \
  do {                                                              \
    if (!checkEqual(actual, expected, #actual, #expected, __FILE__, \
                    __LINE__)) {                                    \
      return false;                                                 \
    }                                                               \
  } while (false)

  template <typename T>
  bool checkNull(const T* actual, const char* actualExpr, const char* filename,
                 int lineno) {
    if (actual == nullptr) {
      return true;
    }

    fail(std::string("CHECK_NULL failed: expected nullptr, got (") +
             actualExpr + ") = " + toSource(actual),
         filename, lineno);
    return false;
  }

#define CHECK_NULL(actual)                                 \
  do {                                                     \
    if (!checkNull(actual, #actual, __FILE__, __LINE__)) { \
      return false;                                        \
    }                                                      \
  } while (false)

  bool checkSame(const JS::Value& actualArg, const JS::Value& expectedArg,
                 const char* actualExpr, const char* expectedExpr,
                 const char* filename, int lineno);

#define CHECK_SAME(actual, expected)                               \
  do {                                                             \
    if (!checkSame(actual, expected, #actual, #expected, __FILE__, \
                   __LINE__)) {                                    \
      return false;                                                \
    }                                                              \
  } while (false)

#define CHECK(expr)                                                         \
  do {                                                                      \
    if (!(expr)) {                                                          \
      return fail(std::string("CHECK failed: " #expr), __FILE__, __LINE__); \
    }                                                                       \
  } while (false)

  void maybeAppendException(std::string& message) override;

  static const JSClass* basicGlobalClass();

 protected:
  static void reportWarning(JSContext* cx, JSErrorReport* report);

  static bool print(JSContext* cx, unsigned argc, JS::Value* vp);

  bool definePrint();

  virtual JSContext* createContext();

  virtual const JSClass* getGlobalClass() { return basicGlobalClass(); }

  virtual JSObject* createGlobal(JSPrincipals* principals = nullptr);
};

class FrontendTest : public TestBase {
 public:
  FrontendTest();
  virtual ~FrontendTest() {}
};

}  // namespace jsapitest

#define BEGIN_TEST_WITH_ATTRIBUTES_AND_EXTRA(testname, attrs, extra) \
  class cls_##testname : public jsapitest::RuntimeTest {             \
   public:                                                           \
    virtual const char* name() override { return #testname; }        \
    extra bool run() override attrs

#define BEGIN_TEST_WITH_ATTRIBUTES(testname, attrs) \
  BEGIN_TEST_WITH_ATTRIBUTES_AND_EXTRA(testname, attrs, )

#define BEGIN_TEST(testname) BEGIN_TEST_WITH_ATTRIBUTES(testname, )

#define BEGIN_FRONTEND_TEST_WITH_ATTRIBUTES_AND_EXTRA(testname, attrs, extra) \
  class cls_##testname : public jsapitest::FrontendTest {                     \
   public:                                                                    \
    virtual const char* name() override { return #testname; }                 \
    extra bool run() override attrs

#define BEGIN_FRONTEND_TEST_WITH_ATTRIBUTES(testname, attrs) \
  BEGIN_FRONTEND_TEST_WITH_ATTRIBUTES_AND_EXTRA(testname, attrs, )

#define BEGIN_FRONTEND_TEST(testname) \
  BEGIN_FRONTEND_TEST_WITH_ATTRIBUTES(testname, )

#define BEGIN_REUSABLE_TEST(testname)   \
  BEGIN_TEST_WITH_ATTRIBUTES_AND_EXTRA( \
      testname, , cls_##testname() : RuntimeTest() { reuseGlobal = true; })

#define END_TEST(testname) \
  }                        \
  ;                        \
  MOZ_RUNINIT static cls_##testname cls_##testname##_instance;

/*
 * A "fixture" is a subclass of RuntimeTest that holds common definitions
 * for a set of tests. Each test that wants to use the fixture should use
 * BEGIN_FIXTURE_TEST and END_FIXTURE_TEST, just as one would use BEGIN_TEST and
 * END_TEST, but include the fixture class as the first argument. The fixture
 * class's declarations are then in scope for the test bodies.
 */

#define BEGIN_FIXTURE_TEST(fixture, testname)                 \
  class cls_##testname : public fixture {                     \
   public:                                                    \
    virtual const char* name() override { return #testname; } \
    bool run() override

#define END_FIXTURE_TEST(fixture, testname) \
  }                                         \
  ;                                         \
  MOZ_RUNINIT static cls_##testname cls_##testname##_instance;

/*
 * A class for creating and managing one temporary file.
 *
 * We could use the ISO C temporary file functions here, but those try to
 * create files in the root directory on Windows, which fails for users
 * without Administrator privileges.
 */
class TempFile {
  const char* name;
  FILE* stream;

 public:
  TempFile();
  ~TempFile();

  /*
   * Return a stream for a temporary file named |fileName|. Infallible.
   * Use only once per TempFile instance. If the file is not explicitly
   * closed and deleted via the member functions below, this object's
   * destructor will clean them up.
   */
  FILE* open(const char* fileName);

  /* Close the temporary file's stream. */
  void close();

  /* Delete the temporary file. */
  void remove();
};

// Just a wrapper around JSPrincipals that allows static construction.
class TestJSPrincipals : public JSPrincipals {
 public:
  explicit TestJSPrincipals(int rc = 0);

  bool write(JSContext* cx, JSStructuredCloneWriter* writer) override;

  bool isSystemPrincipal() override { return true; }
  bool isAddonPrincipal() override { return true; }
};

// A class that simulates externally memory-managed data, for testing with
// array buffers.
class ExternalData {
  char* contents_;
  size_t len_;
  bool uniquePointerCreated_ = false;

 public:
  explicit ExternalData(const char* str);

  size_t len() const { return len_; }
  void* contents() const { return contents_; }
  char* asString() const { return contents_; }
  bool wasFreed() const { return !contents_; }

  void free();
  mozilla::UniquePtr<void, JS::BufferContentsDeleter> pointer();

  static void freeCallback(void* contents, void* userData);
};

class AutoGCParameter {
  JSContext* cx_;
  JSGCParamKey key_;
  uint32_t value_;

 public:
  AutoGCParameter(JSContext* cx, JSGCParamKey key, uint32_t value);
  ~AutoGCParameter();
};

/*
 * Temporarily disable the GC zeal setting. This is only useful in tests that
 * need very explicit GC behavior and should not be used elsewhere.
 */
class AutoLeaveZeal {
#ifdef JS_GC_ZEAL
  JSContext* cx_;
  uint32_t zealBits_;
  uint32_t frequency_;
#endif

 public:
  explicit AutoLeaveZeal(JSContext* cx);
  ~AutoLeaveZeal();
};

#endif /* jsapi_tests_tests_h */
