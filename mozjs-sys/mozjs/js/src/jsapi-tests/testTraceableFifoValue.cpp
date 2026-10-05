/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ds/TraceableFifo.h"
#include "js/PropertyAndElement.h"  // JS_DefineProperty, JS_GetProperty
#include "js/RootingAPI.h"

#include "jsapi-tests/tests.h"

using namespace js;

BEGIN_TEST(testTraceableFifoValueBasic) {
  JS::Rooted<TraceableFifo<JS::Value>> fifo(cx, TraceableFifo<JS::Value>(cx));

  // Test empty state
  CHECK(fifo.empty());
  CHECK(fifo.length() == 0);

  // Test pushBack with various JS::Value types
  CHECK(fifo.pushBack(JS::UndefinedValue()));
  CHECK(fifo.pushBack(JS::NullValue()));
  CHECK(fifo.pushBack(JS::Int32Value(42)));
  CHECK(fifo.pushBack(JS::BooleanValue(true)));
  CHECK(fifo.pushBack(JS::DoubleValue(3.14)));

  CHECK(!fifo.empty());
  CHECK(fifo.length() == 5);

  // Test FIFO behavior - first in, first out
  CHECK(fifo.front().isUndefined());
  fifo.popFront();

  CHECK(fifo.front().isNull());
  fifo.popFront();

  CHECK(fifo.front().isInt32() && fifo.front().toInt32() == 42);
  fifo.popFront();

  CHECK(fifo.front().isBoolean() && fifo.front().toBoolean() == true);
  fifo.popFront();

  CHECK(fifo.front().isDouble() && fifo.front().toDouble() == 3.14);
  fifo.popFront();

  CHECK(fifo.empty());
  CHECK(fifo.length() == 0);

  return true;
}
END_TEST(testTraceableFifoValueBasic)

BEGIN_TEST(testTraceableFifoValueGCSurvival) {
  JS::Rooted<TraceableFifo<JS::Value>> fifo(cx, TraceableFifo<JS::Value>(cx));

  // Create objects and add them to fifo
  const size_t numObjects = 15;
  for (size_t i = 0; i < numObjects; ++i) {
    JS::RootedObject obj(cx, JS_NewPlainObject(cx));
    CHECK(obj);

    // Add a property to make objects identifiable
    JS::RootedValue indexVal(cx, JS::NumberValue(static_cast<double>(i)));
    CHECK(JS_DefineProperty(cx, obj, "testIndex", indexVal, 0));

    JS::RootedValue objVal(cx, JS::ObjectValue(*obj));
    CHECK(fifo.pushBack(objVal));
  }

  CHECK(fifo.length() == numObjects);

  // Trigger multiple GC cycles to ensure objects are properly traced
  JS_GC(cx);
  JS_GC(cx);
  JS_GC(cx);

  // Verify all objects survived and have correct properties
  for (size_t i = 0; i < numObjects; ++i) {
    CHECK(!fifo.empty());
    CHECK(fifo.front().isObject());

    JS::RootedObject obj(cx, &fifo.front().toObject());
    CHECK(obj);

    JS::RootedValue indexVal(cx);
    CHECK(JS_GetProperty(cx, obj, "testIndex", &indexVal));
    CHECK(indexVal.isNumber() && indexVal.toNumber() == static_cast<double>(i));

    fifo.popFront();
  }

  CHECK(fifo.empty());

  return true;
}
END_TEST(testTraceableFifoValueGCSurvival)

BEGIN_TEST(testTraceableFifoValueStrings) {
  JS::Rooted<TraceableFifo<JS::Value>> fifo(cx, TraceableFifo<JS::Value>(cx));

  // Test with various string types
  const char* testStrings[] = {"hello",
                               "world",
                               "TraceableFifo",
                               "JavaScript",
                               "garbage collection",
                               "SpiderMonkey",
                               "test string with spaces"};

  // Add strings to fifo
  for (const char* str : testStrings) {
    JS::RootedString jsStr(cx, JS_NewStringCopyZ(cx, str));
    CHECK(jsStr);
    JS::RootedValue strVal(cx, JS::StringValue(jsStr));
    CHECK(fifo.pushBack(strVal));
  }

  CHECK(fifo.length() == std::size(testStrings));

  // Trigger GC to ensure strings survive
  JS_GC(cx);

  // Verify strings in FIFO order
  for (const char* expected : testStrings) {
    CHECK(!fifo.empty());
    CHECK(fifo.front().isString());

    JS::RootedString str(cx, fifo.front().toString());
    CHECK(str);

    bool match = false;
    CHECK(JS_StringEqualsAscii(cx, str, expected, strlen(expected), &match));
    CHECK(match);

    fifo.popFront();
  }

  CHECK(fifo.empty());

  return true;
}
END_TEST(testTraceableFifoValueStrings)

BEGIN_TEST(testTraceableFifoValueMixed) {
  JS::Rooted<TraceableFifo<JS::Value>> fifo(cx, TraceableFifo<JS::Value>(cx));

  // Mix different value types
  CHECK(fifo.pushBack(JS::Int32Value(100)));

  JS::RootedString str(cx, JS_NewStringCopyZ(cx, "mixed"));
  CHECK(str);
  CHECK(fifo.pushBack(JS::StringValue(str)));

  JS::RootedObject obj(cx, JS_NewPlainObject(cx));
  CHECK(obj);
  CHECK(fifo.pushBack(JS::ObjectValue(*obj)));

  CHECK(fifo.pushBack(JS::BooleanValue(false)));
  CHECK(fifo.pushBack(JS::UndefinedValue()));

  CHECK(fifo.length() == 5);

  // Force GC between operations
  JS_GC(cx);

  // Verify mixed types maintain order and survive GC
  CHECK(fifo.front().isInt32() && fifo.front().toInt32() == 100);
  fifo.popFront();

  CHECK(fifo.front().isString());
  JS::RootedString retrievedStr(cx, fifo.front().toString());
  bool match = false;
  CHECK(JS_StringEqualsAscii(cx, retrievedStr, "mixed", 5, &match));
  CHECK(match);
  fifo.popFront();

  CHECK(fifo.front().isObject());
  CHECK(&fifo.front().toObject() == obj);
  fifo.popFront();

  CHECK(fifo.front().isBoolean() && !fifo.front().toBoolean());
  fifo.popFront();

  CHECK(fifo.front().isUndefined());
  fifo.popFront();

  CHECK(fifo.empty());

  return true;
}
END_TEST(testTraceableFifoValueMixed)

BEGIN_TEST(testTraceableFifoValueEmplaceBack) {
  JS::Rooted<TraceableFifo<JS::Value>> fifo(cx, TraceableFifo<JS::Value>(cx));

  // Test emplaceBack with different value construction
  CHECK(fifo.emplaceBack(JS::UndefinedValue()));
  CHECK(fifo.emplaceBack(JS::Int32Value(999)));
  CHECK(fifo.emplaceBack(JS::DoubleValue(2.718)));

  CHECK(fifo.length() == 3);

  // Verify emplaced values
  CHECK(fifo.front().isUndefined());
  fifo.popFront();

  CHECK(fifo.front().isInt32() && fifo.front().toInt32() == 999);
  fifo.popFront();

  CHECK(fifo.front().isDouble() && fifo.front().toDouble() == 2.718);
  fifo.popFront();

  CHECK(fifo.empty());

  return true;
}
END_TEST(testTraceableFifoValueEmplaceBack)

BEGIN_TEST(testTraceableFifoValueClear) {
  JS::Rooted<TraceableFifo<JS::Value>> fifo(cx, TraceableFifo<JS::Value>(cx));

  // Fill with many values
  for (int i = 0; i < 30; ++i) {
    if (i % 3 == 0) {
      CHECK(fifo.pushBack(JS::Int32Value(i)));
    } else if (i % 3 == 1) {
      JS::RootedObject obj(cx, JS_NewPlainObject(cx));
      CHECK(obj);
      CHECK(fifo.pushBack(JS::ObjectValue(*obj)));
    } else {
      JS::RootedString str(cx, JS_NewStringCopyZ(cx, "test"));
      CHECK(str);
      CHECK(fifo.pushBack(JS::StringValue(str)));
    }
  }

  CHECK(fifo.length() == 30);
  CHECK(!fifo.empty());

  // Clear the fifo
  fifo.clear();

  CHECK(fifo.length() == 0);
  CHECK(fifo.empty());

  // Verify we can still use it after clear
  CHECK(fifo.pushBack(JS::Int32Value(42)));
  CHECK(fifo.length() == 1);
  CHECK(fifo.front().isInt32() && fifo.front().toInt32() == 42);

  return true;
}
END_TEST(testTraceableFifoValueClear)

BEGIN_TEST(testTraceableFifoValueLargeScale) {
  JS::Rooted<TraceableFifo<JS::Value>> fifo(cx, TraceableFifo<JS::Value>(cx));

  // Large scale test with GC pressure
  const size_t largeCount = 100;

  for (size_t i = 0; i < largeCount; ++i) {
    // Create different types of values
    switch (i % 4) {
      case 0: {
        CHECK(fifo.pushBack(JS::Int32Value(static_cast<int32_t>(i))));
        break;
      }
      case 1: {
        JS::RootedObject obj(cx, JS_NewPlainObject(cx));
        CHECK(obj);
        CHECK(fifo.pushBack(JS::ObjectValue(*obj)));
        break;
      }
      case 2: {
        JS::RootedString str(cx, JS_NewStringCopyZ(cx, "large"));
        CHECK(str);
        CHECK(fifo.pushBack(JS::StringValue(str)));
        break;
      }
      case 3: {
        CHECK(fifo.pushBack(JS::DoubleValue(static_cast<double>(i) + 0.5)));
        break;
      }
    }

    // Periodic GC to test tracing under pressure
    if (i % 25 == 0) {
      JS_GC(cx);
    }
  }

  CHECK(fifo.length() == largeCount);

  // Final GC sweep
  JS_GC(cx);
  JS_GC(cx);

  // Verify all values are intact
  for (size_t i = 0; i < largeCount; ++i) {
    CHECK(!fifo.empty());

    switch (i % 4) {
      case 0:
        CHECK(fifo.front().isInt32());
        CHECK(fifo.front().toInt32() == static_cast<int32_t>(i));
        break;
      case 1:
        CHECK(fifo.front().isObject());
        break;
      case 2:
        CHECK(fifo.front().isString());
        break;
      case 3:
        CHECK(fifo.front().isDouble());
        CHECK(fifo.front().toDouble() == static_cast<double>(i) + 0.5);
        break;
    }

    fifo.popFront();
  }

  CHECK(fifo.empty());

  return true;
}
END_TEST(testTraceableFifoValueLargeScale)
