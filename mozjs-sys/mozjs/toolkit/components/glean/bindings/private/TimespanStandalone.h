/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_glean_TimespanStandalone_h
#define mozilla_glean_TimespanStandalone_h

#include <cstdint>  // uint32_t

namespace mozilla::glean::impl {

class TimespanStandalone {
 public:
  constexpr explicit TimespanStandalone(uint32_t aId) : mId(aId) {}

  /**
   * Start tracking time for the provided metric.
   *
   * This records an error if it’s already tracking time (i.e. start was already
   * called with no corresponding [stop]): in that case the original
   * start time will be preserved.
   */
  void Start() const;

  /**
   * Stop tracking time for the provided metric.
   *
   * Sets the metric to the elapsed time, but does not overwrite an already
   * existing value.
   * This will record an error if no [start] was called or there is an already
   * existing value.
   */
  void Stop() const;

  /**
   * Abort a previous Start.
   *
   * No error will be recorded if no Start was called.
   */
  void Cancel() const;

  /**
   * Explicitly sets the timespan value
   *
   * This API should only be used if you cannot make use of
   * `start`/`stop`/`cancel`.
   *
   * @param aDuration The duration of this timespan, in units matching the
   *        `time_unit` of this metric's definition.
   */
  void SetRaw(uint32_t aDuration) const;

 protected:
  const uint32_t mId;
};

}  // namespace mozilla::glean::impl

#endif /* mozilla_glean_TimespanStandalone_h */
