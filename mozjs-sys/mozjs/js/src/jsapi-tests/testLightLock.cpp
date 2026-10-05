/*
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/XorShift128PlusRNG.h"

#include "gc/LightLock.h"
#include "js/Vector.h"
#include "jsapi-tests/tests.h"
#include "threading/Thread.h"
#include "util/RandomSeed.h"

using namespace js;

class MOZ_RAII AutoLockLightLock {
  JSRuntime* runtime;
  LightLock& lock;

 public:
  AutoLockLightLock(JSRuntime* runtime, LightLock& lock)
      : runtime(runtime), lock(lock) {
    lock.lock(runtime);
  }
  ~AutoLockLightLock() { lock.unlock(runtime); }
};

BEGIN_TEST(testLightLock_basic) {
  JSRuntime* rt = cx->runtime();

  LightLock lock;
  CHECK(!lock.isLocked());
  lock.lock(rt);
  CHECK(lock.isLocked());
  lock.unlock(rt);
  CHECK(!lock.isLocked());

  {
    AutoLockLightLock guard(rt, lock);
    CHECK(lock.isLocked());
  }
  CHECK(!lock.isLocked());

  return true;
}
END_TEST(testLightLock_basic)

BEGIN_TEST(testLightLock_withThread) {
  JSRuntime* rt = cx->runtime();

  using LogVector = Vector<int32_t, 3, SystemAllocPolicy>;
  LogVector log;

  LightLock lock;
  lock.lock(rt);

  Thread thread;
  CHECK(thread.init(
      [](JSRuntime* rt, LightLock* lock, LogVector* log) {
        AutoLockLightLock guard(rt, *lock);
        MOZ_ALWAYS_TRUE(log->append(1));
      },
      rt, &lock, &log));

  ThisThread::SleepMilliseconds(1);

  CHECK(log.append(0));

  lock.unlock(rt);

  thread.join();

  CHECK(log.append(2));

  CHECK(log.length() == 3);
  for (int32_t i = 0; i < 3; i++) {
    CHECK(log[i] == i);
  }

  return true;
}
END_TEST(testLightLock_withThread)

struct SharedData {
  LightLock lock;
  uint64_t randomSeed[2][2];
  bool lockedBy[2] = {false, false};
  int64_t count = 0;
};

static void LightLockStressThread(JSRuntime* rt, size_t thisThreadIndex,
                                  SharedData* data, size_t iterations,
                                  int64_t update) {
  size_t otherThreadIndex = 1 - thisThreadIndex;

  mozilla::non_crypto::XorShift128PlusRNG randomState(
      data->randomSeed[thisThreadIndex][0],
      data->randomSeed[thisThreadIndex][1]);

  for (size_t i = 0; i < iterations; i++) {
    uint64_t randomBits = randomState.next();

    AutoLockLightLock guard(rt, data->lock);
    MOZ_RELEASE_ASSERT(!data->lockedBy[thisThreadIndex]);
    MOZ_RELEASE_ASSERT(!data->lockedBy[otherThreadIndex]);
    data->lockedBy[thisThreadIndex] = true;
    int64_t oldCount = data->count;
    if (randomBits % 10 == 0) {
      ThisThread::SleepMilliseconds(1);
    }
    data->count = oldCount + update;
    data->lockedBy[thisThreadIndex] = false;
  }
}

BEGIN_TEST(testLightLock_stress) {
  const size_t Iterations = 10000;

  JSRuntime* rt = cx->runtime();

  SharedData data;

  for (size_t i = 0; i < 4; i++) {
    uint64_t seed = js::GenerateRandomSeed();
    printf("Random seed %zu: 0x%" PRIx64 "\n", i, seed);
    data.randomSeed[i / 2][i % 2] = seed;
  }

  Thread thread1;
  Thread thread2;
  CHECK(thread1.init(LightLockStressThread, rt, 0, &data, Iterations, 1));
  CHECK(thread2.init(LightLockStressThread, rt, 1, &data, Iterations, -1));

  thread1.join();
  thread2.join();

  CHECK(!data.lockedBy[0]);
  CHECK(!data.lockedBy[1]);
  CHECK(data.count == 0);
  return true;
}
END_TEST(testLightLock_stress)
