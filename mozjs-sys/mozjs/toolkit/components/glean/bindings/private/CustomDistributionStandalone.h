/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_glean_CustomDistributionStandalone_h
#define mozilla_glean_CustomDistributionStandalone_h

#include <cstdint>  // uint32_t

namespace mozilla::glean::impl {

class CustomDistributionStandalone {
 public:
  constexpr explicit CustomDistributionStandalone(uint32_t aId) : mId(aId) {}

  /**
   * Accumulates the provided sample in the metric.
   *
   * @param aSamples The sample to be recorded by the metric.
   */
  void AccumulateSingleSample(uint64_t aSample) const;

  /**
   * Accumulates the provided sample in the metric.
   *
   * @param aSamples The signed integer sample to be recorded by the
   *                 metric.
   */
  void AccumulateSingleSampleSigned(int64_t aSample) const;

 protected:
  const uint32_t mId;
};

}  // namespace mozilla::glean::impl

#endif /* mozilla_glean_CustomDistributionStandalone_h */
