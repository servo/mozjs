/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef gc_ParallelMarking_h
#define gc_ParallelMarking_h

#include "mozilla/Atomics.h"
#include "mozilla/BitSet.h"
#include "mozilla/Maybe.h"
#include "mozilla/TimeStamp.h"

#include "gc/GCMarker.h"
#include "gc/GCParallelTask.h"
#include "js/HeapAPI.h"
#include "js/SliceBudget.h"
#include "threading/ConditionVariable.h"
#include "threading/ProtectedData.h"

namespace js {

class AutoLockHelperThreadState;

namespace gc {

class ParallelMarker;

using ParallelTaskBitset = mozilla::BitSet<MaxParallelWorkers, uint32_t>;

// A helper thread task that performs parallel marking.
class alignas(TypicalCacheLineSize) ParallelMarkTask : public GCParallelTask {
 public:
  friend class ParallelMarker;

  ParallelMarkTask(ParallelMarker* pm, GCMarker* marker, MarkColor color,
                   uint32_t id, const JS::SliceBudget& budget);
  ~ParallelMarkTask();

  void run(AutoLockHelperThreadState& lock) override;

  using AtomicCount = mozilla::Atomic<uint32_t, mozilla::Relaxed>;
  AtomicCount& waitingTaskCountRef();

  void donateWork();

  void recordDuration() override;

 private:
  bool tryMarking(AutoLockHelperThreadState& lock);
  bool requestWork(AutoLockHelperThreadState& lock);
  void resumeWaitingTasks(AutoLockHelperThreadState& lock);
  void markDeferredWeakmaps(AutoLockHelperThreadState& lock);

  void waitUntilResumed(AutoLockHelperThreadState& lock);
  void resume();
  void resumeOnFinish(const AutoLockHelperThreadState& lock);

  bool hasWork() const;

  // The following fields are only accessed by the marker thread:
  ParallelMarker* const pm;
  GCMarker* const marker;
  AutoSetMarkColor color;
  JS::SliceBudget budget;
  ConditionVariable resumed;

  const uint32_t id;

  HelperThreadLockData<bool> isWaiting;

  // Length of time this task spent blocked waiting for work.
  MainThreadOrGCTaskData<mozilla::TimeDuration> markTime;
  MainThreadOrGCTaskData<mozilla::TimeDuration> waitTime;
};

// Per-runtime parallel marking state.
//
// This class is used on the main thread and coordinates parallel marking using
// several helper threads running ParallelMarkTasks.
//
// This uses a work-requesting approach. Threads mark until they run out of
// work and then add themselves to a list of waiting tasks and block. Running
// tasks with enough work may donate work to a waiting task and resume it.
class MOZ_STACK_CLASS ParallelMarker {
 public:
  static bool mark(GCRuntime* gc, const JS::SliceBudget& sliceBudget);

  bool hasWaitingTasks() const { return !waitingTasks.IsEmpty(); }

  void donateWorkFrom(GCMarker* src);

 private:
  static bool markOneColor(GCRuntime* gc, MarkColor color,
                           const JS::SliceBudget& sliceBudget);

  explicit ParallelMarker(GCRuntime* gc, MarkColor color);

  bool mark(const JS::SliceBudget& sliceBudget);

  bool anyMarkerHasEntries() const;

  void addTask(ParallelMarkTask* task, const AutoLockHelperThreadState& lock);

  void addTaskToWaitingList(ParallelMarkTask* task,
                            const AutoLockHelperThreadState& lock);
#ifdef DEBUG
  bool isTaskInWaitingList(const ParallelMarkTask* task,
                           const AutoLockHelperThreadState& lock) const;
#endif
  ParallelMarkTask* takeWaitingTask();

#ifdef DEBUG
  // True while a task is marking deferred weakmaps. During this the lock is
  // released and no task is active, but the marking task will resume or finish
  // any waiting tasks once it completes, so waiting tasks must not treat this
  // as a lost wakeup.
  bool isMarkingDeferredWeakmaps(const AutoLockHelperThreadState& lock) const {
    return markingDeferredWeakmaps.ref();
  }
#endif

  bool hasActiveTasks(const AutoLockHelperThreadState& lock) const {
    return !activeTasks.ref().IsEmpty();
  }
  void setTaskActive(ParallelMarkTask* task,
                     const AutoLockHelperThreadState& lock);
  void setTaskInactive(ParallelMarkTask* task,
                       const AutoLockHelperThreadState& lock);

  size_t workerCount() const;

  friend class ParallelMarkTask;

  GCRuntime* const gc;

  mozilla::Maybe<ParallelMarkTask> tasks[MaxParallelWorkers];

  // waitingTasks is written to with the lock held but can be read without.
  using WaitingTaskSet =
      mozilla::BitSet<MaxParallelWorkers,
                      mozilla::Atomic<uint32_t, mozilla::Relaxed>>;
  WaitingTaskSet waitingTasks;

  HelperThreadLockData<ParallelTaskBitset> activeTasks;

#ifdef DEBUG
  // Set while a task marks deferred weakmaps with the lock released.
  HelperThreadLockData<bool> markingDeferredWeakmaps;
#endif

  const MarkColor color;
};

inline ParallelMarkTask::AtomicCount& ParallelMarkTask::waitingTaskCountRef() {
  return pm->waitingTasks.Storage()[0];
}

}  // namespace gc
}  // namespace js

#endif /* gc_ParallelMarking_h */
