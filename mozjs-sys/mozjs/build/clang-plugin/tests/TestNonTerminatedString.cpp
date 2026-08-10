#define MOZ_NON_TERMINATED_STRING __attribute__((annotate("moz_non_terminated_string")))

#include "mozilla/Casting.h"

#include <cstdio>
#include <sstream>
#include <string>

#define MOZ_FORMAT_PRINTF(stringIndex, firstToCheck) \
  __attribute__((format(printf, stringIndex, firstToCheck)))

const char *getNotTerminated() MOZ_NON_TERMINATED_STRING;
const char *getTerminated();

void myPrintf(const char *fmt, ...) MOZ_FORMAT_PRINTF(1, 2);

struct S {
  size_t size();
  const char *data() MOZ_NON_TERMINATED_STRING;
  const char *c_str();
};

void testPrintf() {
  printf("%s", getNotTerminated()); // expected-error{{MOZ_NON_TERMINATED_STRING return value from 'getNotTerminated' passed as an argument to printf-like function 'printf'}}
  printf("%s", getTerminated());
  printf("hello %s world", getNotTerminated()); // expected-error{{MOZ_NON_TERMINATED_STRING return value from 'getNotTerminated' passed as an argument to printf-like function 'printf'}}
}

void testSnprintf(char *buf, int size) {
  snprintf(buf, size, "%s", getNotTerminated()); // expected-error{{MOZ_NON_TERMINATED_STRING return value from 'getNotTerminated' passed as an argument to printf-like function 'snprintf'}}
  snprintf(buf, size, "%s", getTerminated());
}

void testFprintf() {
  fprintf(stderr, "%s", getNotTerminated()); // expected-error{{MOZ_NON_TERMINATED_STRING return value from 'getNotTerminated' passed as an argument to printf-like function 'fprintf'}}
  fprintf(stderr, "%s", getTerminated());
}

void testCustomPrintf() {
  myPrintf("%s", getNotTerminated()); // expected-error{{MOZ_NON_TERMINATED_STRING return value from 'getNotTerminated' passed as an argument to printf-like function 'myPrintf'}}
  myPrintf("%s", getTerminated());
}

void testMethod() {
  S s;
  printf("%s", s.data()); // expected-error{{MOZ_NON_TERMINATED_STRING return value from 'S::data' passed as an argument to printf-like function 'printf'}}
  printf("%s", s.c_str());
}

struct PrintfCtor {
  explicit PrintfCtor(const char *fmt, ...) MOZ_FORMAT_PRINTF(2, 3);
};

void testConstructor() {
  S s;
  PrintfCtor("hello %s", s.data()); // expected-error{{MOZ_NON_TERMINATED_STRING return value from 'S::data' passed as an argument to printf-like function 'PrintfCtor::PrintfCtor'}}
  PrintfCtor("hello %s", s.c_str());
  PrintfCtor("hello %s", getNotTerminated()); // expected-error{{MOZ_NON_TERMINATED_STRING return value from 'getNotTerminated' passed as an argument to printf-like function 'PrintfCtor::PrintfCtor'}}
  PrintfCtor("hello %s", getTerminated());
}

void takesOneArg(const char *s);
void takesTwoArgs(const char *s, int n);
void takesOneVoidArg(const void *p);

struct SingleArgCtor {
  explicit SingleArgCtor(const char *s);
};

struct TwoArgCtor {
  explicit TwoArgCtor(const char *s, int n);
};

struct SingleVoidArgCtor {
  explicit SingleVoidArgCtor(const void *p);
};

void testSingleArg() {
  S s;
  takesOneArg(getNotTerminated()); // expected-error{{MOZ_NON_TERMINATED_STRING return value from 'getNotTerminated' passed as an argument to single-argument function 'takesOneArg'}}
  takesOneArg(getTerminated());
  takesOneArg(s.data()); // expected-error{{MOZ_NON_TERMINATED_STRING return value from 'S::data' passed as an argument to single-argument function 'takesOneArg'}}
  takesOneArg(s.c_str());
  takesTwoArgs(getNotTerminated(), 0);
  takesOneVoidArg(getNotTerminated());
  (void)mozilla::BitwiseCast<const uint8_t*>(getNotTerminated());
  SingleArgCtor{getNotTerminated()}; // expected-error{{MOZ_NON_TERMINATED_STRING return value from 'getNotTerminated' passed as an argument to single-argument function 'SingleArgCtor::SingleArgCtor'}}
  SingleArgCtor{getTerminated()};
  SingleArgCtor{s.data()}; // expected-error{{MOZ_NON_TERMINATED_STRING return value from 'S::data' passed as an argument to single-argument function 'SingleArgCtor::SingleArgCtor'}}
  TwoArgCtor{getNotTerminated(), 0};
  SingleVoidArgCtor{getNotTerminated()};
}

void testStreams() {
  S s;
  std::stringstream ss;
  ss << s.data(); // expected-error-re{{MOZ_NON_TERMINATED_STRING return value from 'S::data' passed as an argument to operator function '{{.+}}'}}
  ss << s.c_str();
}

void testStringCtor() {
  S s;
  std::string s1(s.data()); // expected-error-re{{MOZ_NON_TERMINATED_STRING return value from 'S::data' passed as an argument to single-argument function '{{.+}}'}}
  std::string s2(s.data(), s.size());
  std::string s3(s.c_str());
}

void testNonPrintfUsage() {
  const char *p = getNotTerminated();
  (void)p;

  S s;
  const char *q = s.data();
  (void)q;
}
