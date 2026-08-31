/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_glean_TimingDistributionStandalone_h
#define mozilla_glean_TimingDistributionStandalone_h

#include "mozilla/glean/bindings/CommonStandalone.h"  // TimerId
#include "mozilla/TimeStamp.h"                        // TimeDuration

namespace mozilla::glean {

namespace impl {
class TimingDistributionStandalone {
 public:
  constexpr explicit TimingDistributionStandalone(uint32_t aId) : mId(aId) {}

  /*
   * Starts tracking time for the provided metric.
   *
   * @returns A unique TimerId for the new timer
   */
  TimerId Start() const;

  /*
   * Stops tracking time for the provided metric and associated timer id.
   *
   * Adds a count to the corresponding bucket in the timing distribution.
   * This will record an error if no `Start` was called on this TimerId or
   * if this TimerId was used to call `Cancel`.
   *
   * @param aId The TimerId to associate with this timing. This allows for
   *            concurrent timing of events associated with different ids.
   */
  void StopAndAccumulate(const TimerId&& aId) const;

  /*
   * Adds a duration sample to a timing distribution metric.
   *
   * Adds a count to the corresponding bucket in the timing distribution.
   * Prefer Start() and StopAndAccumulate() where possible.
   * Users of this API are responsible for ensuring the timing source used
   * to calculate the TimeDuration is monotonic and consistent accross
   * platforms.
   *
   * NOTE: Negative durations are not handled and will saturate to INT64_MAX
   *       nanoseconds.
   *
   * @param aDuration The duration of the sample to add to the distribution.
   */
  void AccumulateRawDuration(const TimeDuration& aDuration) const;

  /*
   * Aborts a previous `Start` call. No error is recorded if no `Start` was
   * called.
   *
   * @param aId The TimerId whose `Start` you wish to abort.
   */
  void Cancel(const TimerId&& aId) const;

  class MOZ_RAII AutoTimer {
   public:
    void Cancel();
    ~AutoTimer();  // NOLINT(performance-trivially-destructible)

   private:
    AutoTimer(uint32_t aMetricId, TimerId aTimerId)
        : mMetricId(aMetricId), mTimerId(aTimerId) {}
    AutoTimer(AutoTimer& aOther) = delete;

    const uint32_t mMetricId;
    TimerId mTimerId;

    friend class TimingDistributionStandalone;
  };

  AutoTimer Measure() const;

 protected:
  const uint32_t mId;
};
}  // namespace impl

}  // namespace mozilla::glean

#endif /* mozilla_glean_TimingDistributionStandalone_h */
