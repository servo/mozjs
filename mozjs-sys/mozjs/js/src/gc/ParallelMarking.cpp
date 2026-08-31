/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gc/ParallelMarking.h"

#include "gc/GCInternals.h"
#include "gc/GCLock.h"
#include "vm/GeckoProfiler.h"
#include "vm/HelperThreadState.h"
#include "vm/Runtime.h"

#include "gc/WeakMap-inl.h"

using namespace js;
using namespace js::gc;

using mozilla::TimeDuration;
using mozilla::TimeStamp;

using JS::SliceBudget;

class AutoAddTimeDuration {
  TimeStamp start_;
  TimeDuration& result_;

 public:
  explicit AutoAddTimeDuration(TimeDuration& result)
      : start_(TimeStamp::Now()), result_(result) {}
  TimeStamp start() const { return start_; }
  ~AutoAddTimeDuration() { result_ += TimeSince(start_); }
};

/* static */
bool ParallelMarker::mark(GCRuntime* gc, const SliceBudget& sliceBudget) {
  if (!markOneColor(gc, MarkColor::Black, sliceBudget) ||
      !markOneColor(gc, MarkColor::Gray, sliceBudget)) {
    return false;
  }

  // Handle any delayed marking, which is not performed in parallel.
  if (gc->hasDelayedMarking()) {
    gc->markAllDelayedChildren(ReportMarkTime);
  }

  return true;
}

/* static */
bool ParallelMarker::markOneColor(GCRuntime* gc, MarkColor color,
                                  const SliceBudget& sliceBudget) {
  ParallelMarker pm(gc, color);
  return pm.mark(sliceBudget);
}

ParallelMarker::ParallelMarker(GCRuntime* gc, MarkColor color)
    : gc(gc), color(color) {
  // There should always be enough parallel tasks to run our marking work.
  MOZ_ASSERT(workerCount() <= gc->getMaxParallelThreads());
}

size_t ParallelMarker::workerCount() const { return gc->markers.length(); }

bool ParallelMarker::mark(const SliceBudget& sliceBudget) {
  // Run a marking slice for a single color and return whether it is complete.

  if (!gc->hasDeferredWeakMaps(color) && !anyMarkerHasEntries()) {
    return true;
  }

  gcstats::AutoPhase ap(gc->stats(), gcstats::PhaseKind::PARALLEL_MARK);

  MOZ_ASSERT(workerCount() <= MaxParallelWorkers);

  for (size_t i = 0; i < workerCount(); i++) {
    GCMarker* marker = gc->markers[i].get();
    tasks[i].emplace(this, marker, color, i, sliceBudget);

    // Attempt to populate empty mark stacks.
    //
    // TODO: When tuning for more than two markers we may need to adopt a more
    // sophisticated approach.
    if (!marker->hasEntriesForCurrentColor() && gc->marker().canDonateWork()) {
      GCMarker::moveSomeWork(marker, &gc->marker(), false);
    }
  }

  AutoLockHelperThreadState lock;

  MOZ_ASSERT(!hasActiveTasks(lock));
  for (size_t i = 0; i < workerCount(); i++) {
    ParallelMarkTask& task = *tasks[i];
    if (task.hasWork()) {
      setTaskActive(&task, lock);
    }
  }

  // Run the parallel tasks, using the main thread for the first one.
  for (size_t i = 1; i < workerCount(); i++) {
    gc->startTask(*tasks[i], lock);
  }
  tasks[0]->runFromMainThread(lock);
  tasks[0]->recordDuration();  // Record stats as if it used a helper thread.
  for (size_t i = 1; i < workerCount(); i++) {
    gc->joinTask(*tasks[i], lock);
  }

  MOZ_ASSERT(!hasWaitingTasks());
  MOZ_ASSERT(!hasActiveTasks(lock));

  return !gc->hasDeferredWeakMaps(color) && !anyMarkerHasEntries();
}

bool ParallelMarker::anyMarkerHasEntries() const {
  for (const auto& marker : gc->markers) {
    if (marker->hasEntries(color)) {
      return true;
    }
  }

  return false;
}

ParallelMarkTask::ParallelMarkTask(ParallelMarker* pm, GCMarker* marker,
                                   MarkColor color, uint32_t id,
                                   const SliceBudget& budget)
    : GCParallelTask(pm->gc, gcstats::PhaseKind::PARALLEL_MARK, GCUse::Marking),
      pm(pm),
      marker(marker),
      color(*marker, color),
      budget(budget),
      id(id) {
  marker->enterParallelMarkingMode();
}

ParallelMarkTask::~ParallelMarkTask() {
  MOZ_ASSERT(!isWaiting.refNoCheck());
  marker->leaveParallelMarkingMode();
}

bool ParallelMarkTask::hasWork() const {
  return marker->hasEntriesForCurrentColor();
}

void ParallelMarkTask::recordDuration() {
  // Record times separately to avoid double counting when these are summed.
  gc->stats().recordParallelPhase(gcstats::PhaseKind::PARALLEL_MARK_MARK,
                                  markTime.ref());
  gc->stats().recordParallelPhase(gcstats::PhaseKind::PARALLEL_MARK_WAIT,
                                  waitTime.ref());
  TimeDuration other = duration() - markTime.ref() - waitTime.ref();
  if (other < TimeDuration::Zero()) {
    other = TimeDuration::Zero();
  }
  gc->stats().recordParallelPhase(gcstats::PhaseKind::PARALLEL_MARK_OTHER,
                                  other);
}

void ParallelMarkTask::run(AutoLockHelperThreadState& lock) {
  AutoUpdateMarkStackRanges updateRanges(*marker);

  for (;;) {
    if (hasWork()) {
      // There are entries on the mark stack. Mark them.
      if (!tryMarking(lock)) {
        // Marking stopped without finishing.
        break;
      }
    } else if (pm->hasActiveTasks(lock)) {
      // Any active task can produce more work for this task.
      if (!requestWork(lock)) {
        break;  // Over budget.
      }
    } else if (gc->hasDeferredWeakMaps(pm->color)) {
      // All marking is done, but there are deferred weakmaps to process.
      markDeferredWeakmaps(lock);
    } else {
      // No work remaining of any kind.
      break;
    }
  }

  // Allow other tasks to exit.
  resumeWaitingTasks(lock);
  MOZ_ASSERT(!isWaiting);
}

bool ParallelMarkTask::tryMarking(AutoLockHelperThreadState& lock) {
  MOZ_ASSERT(hasWork());
  MOZ_ASSERT(marker->isParallelMarking());

  // Mark until budget exceeded or we run out of work.
  bool finished;
  {
    AutoUnlockHelperThreadState unlock(lock);

    AutoAddTimeDuration time(markTime.ref());
    finished = marker->markCurrentColorInParallel(this, budget);

    GeckoProfilerRuntime& profiler = gc->rt->geckoProfiler();
    if (profiler.enabled()) {
      profiler.markInterval("Parallel marking ran", time.start(), nullptr,
                            JS::ProfilingCategoryPair::GCCC);
    }
  }

  MOZ_ASSERT_IF(finished, !hasWork());
  pm->setTaskInactive(this, lock);

  return finished;
}

void ParallelMarkTask::markDeferredWeakmaps(AutoLockHelperThreadState& lock) {
  MOZ_ASSERT(!pm->hasActiveTasks(lock));

  // Transfer the list to a local while the lock is still held.
  WeakMapList deferred(std::move(gc->deferredMapsList(pm->color)));

#ifdef DEBUG
  // Record that we are marking deferred weakmaps so that waiting tasks know
  // they will still be resumed even though no task is active while the lock is
  // released below.
  pm->markingDeferredWeakmaps = true;
#endif
  {
    // No other marking threads are running. Unlock helper thread state.
    AutoUnlockHelperThreadState unlock(lock);
    marker->markDeferredWeakMapChildren(deferred);
  }
#ifdef DEBUG
  pm->markingDeferredWeakmaps = false;
#endif

  // All deferred weakmaps should have been moved to the marked list.
  MOZ_ASSERT(deferred.isEmpty());

  if (hasWork()) {
    pm->setTaskActive(this, lock);
  }
}

bool ParallelMarkTask::requestWork(AutoLockHelperThreadState& lock) {
  MOZ_ASSERT(!hasWork());
  MOZ_ASSERT(pm->hasActiveTasks(lock));

  budget.forceCheck();
  if (budget.isOverBudget()) {
    return false;  // Over budget or interrupted.
  }

  // Add ourselves to the waiting list and wait for another task to give us
  // work. The task with work calls ParallelMarker::donateWorkFrom.
  waitUntilResumed(lock);

  return true;
}

void ParallelMarkTask::resumeWaitingTasks(AutoLockHelperThreadState& lock) {
  while (pm->hasWaitingTasks()) {
    auto* task = pm->takeWaitingTask();
    task->resumeOnFinish(lock);
  }
}

void ParallelMarkTask::waitUntilResumed(AutoLockHelperThreadState& lock) {
  AutoAddTimeDuration time(waitTime.ref());

  pm->addTaskToWaitingList(this, lock);

  // Set isWaiting flag and wait for another thread to clear it and resume us.
  MOZ_ASSERT(!isWaiting);
  isWaiting = true;

  do {
    // An active task clears isWaiting before calling resumed.notify*(), so
    // normally we can check that we're not hung by asserting that at least one
    // active task exists. But a spurious wakeup during deferred weakmap marking
    // could result in executing this body with no active marking tasks.
    MOZ_ASSERT(pm->hasActiveTasks(lock) || pm->isMarkingDeferredWeakmaps(lock));
    resumed.wait(lock);
  } while (isWaiting);

  MOZ_ASSERT(!pm->isTaskInWaitingList(this, lock));

  GeckoProfilerRuntime& profiler = gc->rt->geckoProfiler();
  if (profiler.enabled()) {
    char details[32];
    SprintfLiteral(details, "markers=%zu", pm->workerCount());
    profiler.markInterval("Parallel marking wait", time.start(), details,
                          JS::ProfilingCategoryPair::GCCC);
  }
}

void ParallelMarkTask::resume() {
  {
    AutoLockHelperThreadState lock;
    MOZ_ASSERT(isWaiting);

    isWaiting = false;

    // Increment the active task count before donateWorkFrom() returns so this
    // can't reach zero before the waiting task runs again.
    if (hasWork()) {
      pm->setTaskActive(this, lock);
    }
  }

  resumed.notify_all();
}

void ParallelMarkTask::resumeOnFinish(const AutoLockHelperThreadState& lock) {
  MOZ_ASSERT(isWaiting);
  MOZ_ASSERT(!hasWork());

  isWaiting = false;
  resumed.notify_all();
}

void ParallelMarker::addTaskToWaitingList(
    ParallelMarkTask* task, const AutoLockHelperThreadState& lock) {
  MOZ_ASSERT(!task->hasWork());
  MOZ_ASSERT(hasActiveTasks(lock));
  MOZ_ASSERT(!isTaskInWaitingList(task, lock));

  uint32_t id = task->id;
  MOZ_ASSERT(id < workerCount());
  MOZ_ASSERT(!waitingTasks[id]);
  waitingTasks[id] = true;
}

#ifdef DEBUG
bool ParallelMarker::isTaskInWaitingList(
    const ParallelMarkTask* task, const AutoLockHelperThreadState& lock) const {
  uint32_t id = task->id;
  MOZ_ASSERT(id < workerCount());
  return waitingTasks[id];
}
#endif

ParallelMarkTask* ParallelMarker::takeWaitingTask() {
  MOZ_ASSERT(hasWaitingTasks());
  uint32_t id = waitingTasks.FindFirst();
  MOZ_ASSERT(id < workerCount());

  MOZ_ASSERT(waitingTasks[id]);
  waitingTasks[id] = false;
  return &*tasks[id];
}

void ParallelMarker::setTaskActive(ParallelMarkTask* task,
                                   const AutoLockHelperThreadState& lock) {
  MOZ_ASSERT(task->hasWork());

  uint32_t id = task->id;
  MOZ_ASSERT(id < workerCount());
  MOZ_ASSERT(!activeTasks.ref()[id]);
  activeTasks.ref()[id] = true;
}

void ParallelMarker::setTaskInactive(ParallelMarkTask* task,
                                     const AutoLockHelperThreadState& lock) {
  MOZ_ASSERT(hasActiveTasks(lock));

  uint32_t id = task->id;
  MOZ_ASSERT(id < workerCount());
  MOZ_ASSERT(activeTasks.ref()[id]);
  activeTasks.ref()[id] = false;
}

void ParallelMarkTask::donateWork() { pm->donateWorkFrom(marker); }

void ParallelMarker::donateWorkFrom(GCMarker* src) {
  GeckoProfilerRuntime& profiler = gc->rt->geckoProfiler();

  if (!gHelperThreadLock.tryLock()) {
    if (profiler.enabled()) {
      profiler.markEvent("Parallel marking donate failed", "lock already held");
    }
    return;
  }

  // Check there are tasks waiting for work while holding the lock.
  if (!hasWaitingTasks()) {
    gHelperThreadLock.unlock();
    if (profiler.enabled()) {
      profiler.markEvent("Parallel marking donate failed", "no tasks waiting");
    }
    return;
  }

  // Take a waiting task off the list.
  ParallelMarkTask* waitingTask = takeWaitingTask();

  // |task| is not running so it's safe to move work to it.
  MOZ_ASSERT(waitingTask->isWaiting);

  gHelperThreadLock.unlock();

  // Move some work from this thread's mark stack to the waiting task.
  MOZ_ASSERT(!waitingTask->hasWork());
  size_t wordsMoved = GCMarker::moveSomeWork(waitingTask->marker, src, true);

  gc->stats().count(gcstats::COUNT_PARALLEL_MARK_INTERRUPTIONS);

  if (profiler.enabled()) {
    char details[32];
    SprintfLiteral(details, "words=%zu", wordsMoved);
    profiler.markEvent("Parallel marking donated work", details,
                       JS::ProfilingCategoryPair::GCCC);
  }

  // Resume waiting task.
  waitingTask->resume();
}
