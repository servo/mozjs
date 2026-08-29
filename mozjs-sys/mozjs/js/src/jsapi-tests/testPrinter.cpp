/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "js/Printer.h"  // FixedBufferPrinter

#include "jsapi-tests/tests.h"

using namespace js;

struct TestBuffer {
  const char ASCII_ACK = (char)6;

  char* buffer;
  size_t size;

  // len is the buffer size, including terminating null
  explicit TestBuffer(const size_t len) : buffer(new char[len + 3]), size(len) {
    buffer[size] = ASCII_ACK;  // to detect overflow
    buffer[size + 1] = ASCII_ACK;
    buffer[size + 2] = ASCII_ACK;
  }

  ~TestBuffer() { delete[] buffer; }

  // test has written past the end of the buffer
  bool hasOverflowed() {
    return buffer[size] != ASCII_ACK || buffer[size + 1] != ASCII_ACK ||
           buffer[size + 2] != ASCII_ACK;
  }

  bool matches(const char* expected) { return strcmp(buffer, expected) == 0; }
};

BEGIN_TEST(testFixedBufferPrinter) {
  // empty buffer
  {
    TestBuffer actual(0);
    FixedBufferPrinter fbp(actual.buffer, 0);
    fbp.put("will not fit");
    CHECK(!actual.hasOverflowed());
  }
  // buffer is initially null-terminated
  {
    TestBuffer actual(10);
    // make sure the buffer is not null-terminated
    memset(actual.buffer, '!', actual.size);
    FixedBufferPrinter fbp(actual.buffer, 10);
    CHECK(!actual.hasOverflowed());
    CHECK(actual.matches(""));
  }
  // one put that fits
  {
    TestBuffer actual(50);
    FixedBufferPrinter fbp(actual.buffer, actual.size);
    const char* expected = "expected string fits";
    fbp.put(expected);
    CHECK(!actual.hasOverflowed());
    CHECK(actual.matches(expected));
  }
  // unterminated string in put
  {
    TestBuffer actual(50);
    FixedBufferPrinter fbp(actual.buffer, actual.size);
    const char* expected = "okBAD";
    fbp.put(expected, 2);
    CHECK(!actual.hasOverflowed());
    CHECK(actual.matches("ok"));
  }
  // one put that more than fills the buffer
  {
    TestBuffer actual(16);
    FixedBufferPrinter fbp(actual.buffer, actual.size);
    const char* expected = "expected string overflow";
    fbp.put(expected);
    CHECK(!actual.hasOverflowed());
    CHECK(actual.matches("expected string"));
  }
  // maintains position over multiple puts that fit
  {
    TestBuffer actual(16);
    FixedBufferPrinter fbp(actual.buffer, actual.size);
    fbp.put("expected ");
    fbp.put("string");
    CHECK(actual.matches("expected string"));
  }
  // multiple puts, last one more than fills the buffer
  {
    TestBuffer actual(9);
    FixedBufferPrinter fbp(actual.buffer, actual.size);
    fbp.put("expected");
    fbp.put("overflow");
    CHECK(!actual.hasOverflowed());
    CHECK(actual.matches("expected"));
  }
  // put after buffer is full doesn't overflow
  {
    TestBuffer actual(2);
    FixedBufferPrinter fbp(actual.buffer, actual.size);
    fbp.put("exp");
    fbp.put("overflow");
    CHECK(!actual.hasOverflowed());
    CHECK(actual.matches("e"));
  }

  return true;
}
END_TEST(testFixedBufferPrinter)
