/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ds/Fifo.h"

#include "jsapi-tests/tests.h"

// Test with primitive type (int)
BEGIN_TEST(testFifoPrimitive) {
  js::Fifo<int, 0, js::SystemAllocPolicy> fifo;

  // Test initial state
  CHECK(fifo.empty());
  CHECK(fifo.length() == 0);

  // Test emplaceBack
  CHECK(fifo.emplaceBack(1));
  CHECK(!fifo.empty());
  CHECK(fifo.length() == 1);
  CHECK(fifo.front() == 1);

  CHECK(fifo.emplaceBack(2));
  CHECK(fifo.emplaceBack(3));
  CHECK(fifo.length() == 3);
  CHECK(fifo.front() == 1);

  // Test popFront
  fifo.popFront();
  CHECK(fifo.length() == 2);
  CHECK(fifo.front() == 2);

  // Test emplaceFront
  CHECK(fifo.emplaceFront(10));
  CHECK(fifo.length() == 3);
  CHECK(fifo.front() == 10);

  // Fill to 5 elements
  CHECK(fifo.emplaceBack(4));
  CHECK(fifo.emplaceBack(5));
  CHECK(fifo.length() == 5);

  // Verify order by popping all
  CHECK(fifo.front() == 10);
  fifo.popFront();
  CHECK(fifo.front() == 2);
  fifo.popFront();
  CHECK(fifo.front() == 3);
  fifo.popFront();
  CHECK(fifo.front() == 4);
  fifo.popFront();
  CHECK(fifo.front() == 5);
  fifo.popFront();

  CHECK(fifo.empty());
  CHECK(fifo.length() == 0);

  // Test clear
  CHECK(fifo.emplaceBack(100));
  CHECK(fifo.emplaceBack(200));
  CHECK(fifo.length() == 2);
  fifo.clear();
  CHECK(fifo.empty());
  CHECK(fifo.length() == 0);

  return true;
}
END_TEST(testFifoPrimitive)

// Simple copyable class for testing
struct SimpleCopyable {
  int value;
  bool* destructorCalled;

  explicit SimpleCopyable(int v, bool* dc = nullptr)
      : value(v), destructorCalled(dc) {}

  SimpleCopyable(const SimpleCopyable& other) = default;
  SimpleCopyable& operator=(const SimpleCopyable& other) = default;

  ~SimpleCopyable() {
    if (destructorCalled) {
      *destructorCalled = true;
    }
  }
};

BEGIN_TEST(testFifoCopyable) {
  js::Fifo<SimpleCopyable, 0, js::SystemAllocPolicy> fifo;

  // Test initial state
  CHECK(fifo.empty());
  CHECK(fifo.length() == 0);

  // Test emplaceBack with constructor args
  CHECK(fifo.emplaceBack(1, nullptr));
  CHECK(fifo.emplaceBack(2, nullptr));
  CHECK(fifo.length() == 2);
  CHECK(fifo.front().value == 1);

  // Test emplaceFront
  CHECK(fifo.emplaceFront(0, nullptr));
  CHECK(fifo.length() == 3);
  CHECK(fifo.front().value == 0);

  // Pop and verify order
  fifo.popFront();
  CHECK(fifo.front().value == 1);
  fifo.popFront();
  CHECK(fifo.front().value == 2);
  CHECK(fifo.length() == 1);

  // Test destructor is called on clear
  bool destructorCalled = false;
  CHECK(fifo.emplaceBack(99, &destructorCalled));
  CHECK(fifo.length() == 2);
  CHECK(!destructorCalled);

  fifo.clear();
  CHECK(destructorCalled);
  CHECK(fifo.empty());

  // Test with 5 elements
  CHECK(fifo.emplaceBack(10, nullptr));
  CHECK(fifo.emplaceBack(20, nullptr));
  CHECK(fifo.emplaceBack(30, nullptr));
  CHECK(fifo.emplaceFront(5, nullptr));
  CHECK(fifo.emplaceBack(40, nullptr));
  CHECK(fifo.length() == 5);

  // Verify FIFO order
  CHECK(fifo.front().value == 5);
  fifo.popFront();
  CHECK(fifo.front().value == 10);
  fifo.popFront();
  CHECK(fifo.front().value == 20);
  fifo.popFront();
  CHECK(fifo.front().value == 30);
  fifo.popFront();
  CHECK(fifo.front().value == 40);
  fifo.popFront();
  CHECK(fifo.empty());

  return true;
}
END_TEST(testFifoCopyable)

// Test mixing emplaceBack and emplaceFront operations
BEGIN_TEST(testFifoMixedOperations) {
  js::Fifo<int, 0, js::SystemAllocPolicy> fifo;

  // Interleave front and back operations
  CHECK(fifo.emplaceBack(3));   // Queue: [3]
  CHECK(fifo.emplaceFront(1));  // Queue: [1, 3]
  CHECK(fifo.emplaceBack(5));   // Queue: [1, 3, 5]
  CHECK(fifo.emplaceFront(0));  // Queue: [0, 1, 3, 5]
  CHECK(fifo.emplaceBack(7));   // Queue: [0, 1, 3, 5, 7]

  CHECK(fifo.length() == 5);

  // Verify order
  CHECK(fifo.front() == 0);
  fifo.popFront();
  CHECK(fifo.front() == 1);
  fifo.popFront();
  CHECK(fifo.front() == 3);
  fifo.popFront();
  CHECK(fifo.front() == 5);
  fifo.popFront();
  CHECK(fifo.front() == 7);
  fifo.popFront();

  CHECK(fifo.empty());

  return true;
}
END_TEST(testFifoMixedOperations)
