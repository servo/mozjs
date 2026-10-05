/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef PerfStats_h
#define PerfStats_h

#include "mozilla/Atomics.h"
#include "mozilla/RefPtr.h"
#include "mozilla/TimeStamp.h"
#include <limits>
#include <cstdint>

#include <string>
#include <vector>

#ifdef MOZILLA_INTERNAL_API
extern "C" bool NS_IsMainThread();
#endif

// PerfStats
//
// Framework for low overhead selective collection of internal performance
// metrics through ChromeUtils.
//
// Gathering: in C++, wrap execution in an RAII class
// PerfStats::AutoMetricRecording<PerfStats::Metric::MyMetric> or call
// PerfStats::RecordMeasurement{Start,End} manually. Use
// RecordMeasurementCount() for incrementing counters.
//
// Controlling: Use ChromeUtils.setPerfStatsFeatures(array), where an empty
// array disables all metrics. Pass metric names as strings, e.g.
// ["LayerTransactions", "Rasterizing"]. To enable all features, use
// ChromeUtils.enableAllPerfStatsFeatures();
//
// Reporting: Results can be accessed with ChromeUtils.CollectPerfStats().
// Browsertime will sum results across processes and report them.

// Define a new metric by adding it to this list. It will be created as a class
// enum value mozilla::PerfStats::Metric::MyMetricName.
#define FOR_EACH_PERFSTATS_METRIC(MACRO)          \
  MACRO(DisplayListBuilding)                      \
  MACRO(Rasterizing)                              \
  MACRO(WrDisplayListBuilding)                    \
  MACRO(LayerTransactions)                        \
  MACRO(FrameBuilding)                            \
  MACRO(Compositing)                              \
  MACRO(Reflowing)                                \
  MACRO(Styling)                                  \
  MACRO(HttpChannelCompletion)                    \
  MACRO(HttpChannelCompletion_Network)            \
  MACRO(HttpChannelCompletion_Cache)              \
  MACRO(HttpChannelAsyncOpenToTransactionPending) \
  MACRO(HttpChannelResponseStartParentToContent)  \
  MACRO(HttpChannelResponseEndParentToContent)    \
  MACRO(HttpTransactionWaitTime)                  \
  MACRO(ResponseEndSocketToParent)                \
  MACRO(OnStartRequestSocketToParent)             \
  MACRO(OnDataAvailableSocketToParent)            \
  MACRO(OnStopRequestSocketToParent)              \
  MACRO(OnStartRequestToContent)                  \
  MACRO(OnDataAvailableToContent)                 \
  MACRO(OnStopRequestToContent)                   \
  MACRO(JSBC_Compression)                         \
  MACRO(JSBC_Decompression)                       \
  MACRO(JSBC_IO_Read)                             \
  MACRO(JSBC_IO_Write)                            \
  MACRO(MinorGC)                                  \
  MACRO(MajorGC)                                  \
  MACRO(NonIdleMajorGC)                           \
  MACRO(A11Y_DoInitialUpdate)                     \
  MACRO(A11Y_ProcessQueuedCacheUpdate)            \
  MACRO(A11Y_ContentRemovedNode)                  \
  MACRO(A11Y_ContentRemovedAcc)                   \
  MACRO(A11Y_PruneOrInsertSubtree)                \
  MACRO(A11Y_ShutdownChildrenInSubtree)           \
  MACRO(A11Y_ShowEvent)                           \
  MACRO(A11Y_RecvCache)                           \
  MACRO(A11Y_ProcessShowEvent)                    \
  MACRO(A11Y_CoalesceEvents)                      \
  MACRO(A11Y_CoalesceMutationEvents)              \
  MACRO(A11Y_ProcessHideEvent)                    \
  MACRO(A11Y_SendCache)                           \
  MACRO(A11Y_WillRefresh)                         \
  MACRO(A11Y_AccessibilityServiceInit)            \
  MACRO(A11Y_PlatformShowHideEvent)               \
  MACRO(UrlClassifierCheckChannel)

namespace mozilla {

template <typename ResolveValueT, typename RejectValueT, bool IsExclusive>
class MozPromise;

namespace dom {
// Forward declaration.
class ContentParent;
}  // namespace dom

// Forward declaration needed by namespace detail below.
class PerfStats;

namespace detail {
// Exposed as free variables so that GCC generates proper weak GOT relocations
// when accessed from inline function bodies in shared libraries. GCC does not
// propagate the weak attribute from class static member declarations to inline
// code accesses, unlike for free extern variables.
MFBT_DATA extern Atomic<uint64_t, MemoryOrdering::Relaxed>
    sPerfStatsCollectionMask;
MFBT_DATA extern Atomic<PerfStats*, MemoryOrdering::SequentiallyConsistent>
    sPerfStatsSingleton;
}  // namespace detail

class PerfStats {
 public:
  using PerfStatsPromise = MozPromise<std::string, bool, true>;

  // MetricMask is a bitmask based on 'Metric', i.e. Metric::LayerBuilding (2)
  // is synonymous to 1 << 2 in MetricMask.
  using MetricMask = uint64_t;
  using MetricCounter = uint32_t;

  enum class Metric : MetricMask {
#define DECLARE_ENUM(metric) metric,
    FOR_EACH_PERFSTATS_METRIC(DECLARE_ENUM)
#undef DECLARE_ENUM
        Max
  };

  // Main thread only
  static MFBT_API void RecordMeasurementStart(Metric aMetric) {
    // Since the Start/End calls use a single variable to track the time,
    // multiple calls to Start from multiple threads would cause race
    // conditions. When needing to record PerfStats off the main thread use the
    // atomic RecordMeasurement call.
#ifdef MOZILLA_INTERNAL_API
    MOZ_ASSERT(NS_IsMainThread());
#endif
    if (!(detail::sPerfStatsCollectionMask &
          (MetricMask(1) << static_cast<MetricMask>(aMetric)))) {
      return;
    }
    RecordMeasurementStartInternal(aMetric);
  }

  // Main thread only
  static MFBT_API void RecordMeasurementEnd(Metric aMetric) {
#ifdef MOZILLA_INTERNAL_API
    MOZ_ASSERT(NS_IsMainThread());
#endif
    if (!(detail::sPerfStatsCollectionMask &
          (MetricMask(1) << static_cast<MetricMask>(aMetric)))) {
      return;
    }
    RecordMeasurementEndInternal(aMetric);
  }

  // This may be called off the main thread.
  static MFBT_API void RecordMeasurement(Metric aMetric,
                                         TimeDuration aDuration) {
    if (!(detail::sPerfStatsCollectionMask &
          (MetricMask(1) << static_cast<MetricMask>(aMetric)))) {
      return;
    }
    RecordMeasurementInternal(aMetric, aDuration);
  }

  // This may be called off the main thread.
  static MFBT_API void RecordMeasurementCounter(Metric aMetric,
                                                MetricMask aIncrementAmount) {
    if (!(detail::sPerfStatsCollectionMask &
          (MetricMask(1) << static_cast<MetricMask>(aMetric)))) {
      return;
    }
    RecordMeasurementCounterInternal(aMetric, aIncrementAmount);
  }

  template <Metric N>
  class AutoMetricRecording {
   public:
    AutoMetricRecording() {
      if (detail::sPerfStatsCollectionMask &
          (MetricMask(1) << static_cast<MetricMask>(N))) {
        mStart = TimeStamp::Now();
      }
    }
    ~AutoMetricRecording() {
      if (!mStart.IsNull()) {
        RecordMeasurementInternal(N, TimeStamp::Now() - mStart);
      }
    }

   private:
    TimeStamp mStart;
  };

  static void SetCollectionMask(MetricMask aMask);
  static MetricMask GetCollectionMask();

  static RefPtr<PerfStatsPromise> CollectPerfStatsJSON();
  static std::string CollectLocalPerfStatsJSON();
  static void StorePerfStats(dom::ContentParent* aParent,
                             const std::string& aPerfStats);

  // Returns the mask with the bit set for a given metric name, or 0 if not
  // found
  static MetricMask GetFeatureMask(const char* aMetricName);

 private:
  static PerfStats* GetSingleton() {
    if (!detail::sPerfStatsSingleton) {
      static PerfStats sInstance;
      detail::sPerfStatsSingleton.compareExchange(nullptr, &sInstance);
    }
    return detail::sPerfStatsSingleton;
  }

  static void RecordMeasurementStartInternal(Metric aMetric) {
    GetSingleton()->mRecordedStarts[static_cast<size_t>(aMetric)] =
        TimeStamp::Now();
  }

  static void RecordMeasurementEndInternal(Metric aMetric) {
    PerfStats* singleton = GetSingleton();
    auto idx = static_cast<MetricMask>(aMetric);
    singleton->mRecordedTimes[idx].fetch_add(
        (TimeStamp::Now() - singleton->mRecordedStarts[idx]).ToMilliseconds(),
        std::memory_order_relaxed);
    ++singleton->mRecordedCounts[idx];
  }

  static void RecordMeasurementInternal(Metric aMetric,
                                        TimeDuration aDuration) {
    PerfStats* singleton = GetSingleton();
    auto idx = static_cast<MetricMask>(aMetric);
    singleton->mRecordedTimes[idx].fetch_add(aDuration.ToMilliseconds(),
                                             std::memory_order_relaxed);
    ++singleton->mRecordedCounts[idx];
  }

  static void RecordMeasurementCounterInternal(Metric aMetric,
                                               MetricCounter aIncrementAmount) {
    PerfStats* singleton = GetSingleton();
    auto idx = static_cast<MetricMask>(aMetric);
    singleton->mRecordedTimes[idx].fetch_add(double(aIncrementAmount),
                                             std::memory_order_relaxed);
    ++singleton->mRecordedCounts[idx];
  }

  void ResetCollection();
  void StorePerfStatsInternal(dom::ContentParent* aParent,
                              const std::string& aPerfStats);
  RefPtr<PerfStatsPromise> CollectPerfStatsJSONInternal();
  std::string CollectLocalPerfStatsJSONInternal();

  TimeStamp mRecordedStarts[static_cast<MetricMask>(Metric::Max)];
  std::atomic<double> mRecordedTimes[static_cast<MetricMask>(Metric::Max)];
  Atomic<MetricCounter, MemoryOrdering::Relaxed>
      mRecordedCounts[static_cast<MetricMask>(Metric::Max)];
  std::vector<std::string> mStoredPerfStats;
};

static_assert(static_cast<PerfStats::MetricMask>(1)
                      << (static_cast<uint64_t>(PerfStats::Metric::Max) - 1) <=
                  std::numeric_limits<PerfStats::MetricMask>::max(),
              "More metrics than can fit into sCollectionMask bitmask");

}  // namespace mozilla

#endif  // PerfStats_h
