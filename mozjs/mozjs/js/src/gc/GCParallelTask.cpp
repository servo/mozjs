/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gc/GCParallelTask.h"

#include "mozilla/Maybe.h"
#include "mozilla/TimeStamp.h"

#include "gc/GCContext.h"
#include "gc/GCInternals.h"
#include "gc/ParallelWork.h"
#include "vm/HelperThreadState.h"
#include "vm/Runtime.h"
#include "vm/Time.h"

using namespace js;
using namespace js::gc;

using mozilla::Maybe;
using mozilla::TimeDuration;
using mozilla::TimeStamp;

js::GCParallelTask::~GCParallelTask() {
  // The LinkedListElement destructor will remove us from any list we are part
  // of without synchronization, so ensure that doesn't happen.
  MOZ_DIAGNOSTIC_ASSERT(!isInList());

  // Only most-derived classes' destructors may do the join: base class
  // destructors run after those for derived classes' members, so a join in a
  // base class can't ensure that the task is done using the members. All we
  // can do now is check that someone has previously stopped the task.
  assertIdle();
}

void js::GCParallelTask::startWithLockHeld(AutoLockHelperThreadState& lock) {
  MOZ_ASSERT(CanUseExtraThreads());
  MOZ_ASSERT(HelperThreadState().isInitialized(lock));
  assertIdle();

  setDispatched(lock);
  HelperThreadState().submitTask(this, lock);
}

void js::GCParallelTask::start() {
  if (!CanUseExtraThreads()) {
    runFromMainThread();
    return;
  }

  AutoLockHelperThreadState lock;
  startWithLockHeld(lock);
}

void js::GCParallelTask::startOrRunIfIdle(AutoLockHelperThreadState& lock) {
  if (wasStarted(lock)) {
    return;
  }

  // Join the previous invocation of the task. This will return immediately
  // if the thread has never been started.
  joinWithLockHeld(lock);

  if (!CanUseExtraThreads()) {
    AutoUnlockHelperThreadState unlock(lock);
    runFromMainThread();
    return;
  }

  startWithLockHeld(lock);
}

void js::GCParallelTask::cancelAndWait() {
  MOZ_ASSERT(!isCancelled());
  cancel_ = true;
  join();
  cancel_ = false;
}

void js::GCParallelTask::join(Maybe<TimeStamp> deadline) {
  AutoLockHelperThreadState lock;
  joinWithLockHeld(lock, deadline);
}

void js::GCParallelTask::joinWithLockHeld(AutoLockHelperThreadState& lock,
                                          Maybe<TimeStamp> deadline) {
  // Task has not been started; there's nothing to do.
  if (isIdle(lock)) {
    return;
  }

  if (isDispatched(lock) && deadline.isNothing()) {
    // If the task was dispatched but has not yet started then cancel the task
    // and run it from the main thread. This stops us from blocking here when
    // the helper threads are busy with other tasks.
    cancelDispatchedTask(lock);
    AutoUnlockHelperThreadState unlock(lock);
    runFromMainThread();
  } else {
    // Otherwise wait for the task to complete.
    joinNonIdleTask(deadline, lock);
  }

  if (isIdle(lock)) {
    if (phaseKind != gcstats::PhaseKind::NONE) {
      gc->stats().recordParallelPhase(phaseKind, duration());
    }
  }
}

void js::GCParallelTask::joinNonIdleTask(Maybe<TimeStamp> deadline,
                                         AutoLockHelperThreadState& lock) {
  MOZ_ASSERT(!isIdle(lock));

  while (!isFinished(lock)) {
    TimeDuration timeout = TimeDuration::Forever();
    if (deadline) {
      TimeStamp now = TimeStamp::Now();
      if (*deadline <= now) {
        break;
      }
      timeout = *deadline - now;
    }

    HelperThreadState().wait(lock, timeout);
  }

  if (isFinished(lock)) {
    setIdle(lock);
  }
}

void js::GCParallelTask::cancelDispatchedTask(AutoLockHelperThreadState& lock) {
  MOZ_ASSERT(isDispatched(lock));
  MOZ_ASSERT(isInList());
  remove();
  setIdle(lock);
}

static inline TimeDuration TimeSince(TimeStamp prev) {
  TimeStamp now = TimeStamp::Now();
  // Sadly this happens sometimes.
  MOZ_ASSERT(now >= prev);
  if (now < prev) {
    now = prev;
  }
  return now - prev;
}

void js::GCParallelTask::runFromMainThread() {
  assertIdle();
  MOZ_ASSERT(js::CurrentThreadCanAccessRuntime(gc->rt));
  AutoLockHelperThreadState lock;
  state_ = State::Running;
  runTask(gc->rt->gcContext(), lock);
  state_ = State::Idle;
}

class MOZ_RAII AutoGCContext {
  JS::GCContext context;

 public:
  explicit AutoGCContext(JSRuntime* runtime) : context(runtime) {
    MOZ_RELEASE_ASSERT(TlsGCContext.init(),
                       "Failed to initialize TLS for GC context");

    MOZ_ASSERT(!TlsGCContext.get());
    TlsGCContext.set(&context);
  }

  ~AutoGCContext() {
    MOZ_ASSERT(TlsGCContext.get() == &context);
    TlsGCContext.set(nullptr);
  }

  JS::GCContext* get() { return &context; }
};

void js::GCParallelTask::runHelperThreadTask(AutoLockHelperThreadState& lock) {
  setRunning(lock);

  AutoGCContext gcContext(gc->rt);

  runTask(gcContext.get(), lock);

  setFinished(lock);
}

void GCParallelTask::runTask(JS::GCContext* gcx,
                             AutoLockHelperThreadState& lock) {
  // Run the task from either the main thread or a helper thread.

  AutoSetThreadGCUse setUse(gcx, use);

  // The hazard analysis can't tell what the call to func_ will do but it's not
  // allowed to GC.
  JS::AutoSuppressGCAnalysis nogc;

  TimeStamp timeStart = TimeStamp::Now();
  run(lock);
  duration_ = TimeSince(timeStart);
}

bool js::GCParallelTask::isIdle() const {
  AutoLockHelperThreadState lock;
  return isIdle(lock);
}

bool js::GCParallelTask::wasStarted() const {
  AutoLockHelperThreadState lock;
  return wasStarted(lock);
}

/* static */
size_t js::gc::GCRuntime::parallelWorkerCount() const {
  return std::min(helperThreadCount.ref(), MaxParallelWorkers);
}
