/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_glean_GleanTimingDistribution_h
#define mozilla_glean_GleanTimingDistribution_h

#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/glean/bindings/DistributionData.h"
#include "mozilla/glean/bindings/GleanMetric.h"  // GleanMetric
#include "mozilla/glean/bindings/TimingDistributionStandalone.h"
#include "mozilla/Maybe.h"
#include "mozilla/Result.h"
#include "nsTArray.h"

namespace mozilla::dom {
struct GleanDistributionData;
}  // namespace mozilla::dom

namespace mozilla::glean {
// Forward declaration to make the friend class below work.
class GleanTimingDistribution;

namespace impl {
class TimingDistributionMetric : public TimingDistributionStandalone {
 public:
  constexpr explicit TimingDistributionMetric(uint32_t aId)
      : TimingDistributionStandalone(aId) {}

  /**
   * **Test-only API**
   *
   * Gets the currently stored value as a DistributionData.
   *
   * This function will attempt to await the last parent-process task (if any)
   * writing to the the metric's storage engine before returning a value.
   * This function will not wait for data from child processes.
   *
   * This doesn't clear the stored value.
   * Parent process only. Panics in child processes.
   *
   * @param aPingName The (optional) name of the ping to retrieve the metric
   *        for. Defaults to the first value in `send_in_pings`.
   *
   * @return value of the stored metric, or Nothing() if there is no value.
   */
  Result<Maybe<DistributionData>, nsCString> TestGetValue(
      const nsACString& aPingName = nsCString()) const;

  using TimingDistributionStandalone::AutoTimer;

  friend class mozilla::glean::GleanTimingDistribution;
};
}  // namespace impl

class GleanTimingDistribution final : public GleanMetric {
 public:
  explicit GleanTimingDistribution(uint64_t aId, nsISupports* aParent)
      : GleanMetric(aParent), mTimingDist(aId) {}

  virtual JSObject* WrapObject(
      JSContext* aCx, JS::Handle<JSObject*> aGivenProto) override final;

  uint64_t Start();
  void StopAndAccumulate(uint64_t aId);
  void Cancel(uint64_t aId);
  void AccumulateSamples(const nsTArray<int64_t>& aSamples);
  void AccumulateSingleSample(int64_t aSample);

  void TestGetValue(const nsACString& aPingName,
                    dom::Nullable<dom::GleanDistributionData>& aRetval,
                    ErrorResult& aRv);

  void TestAccumulateRawMillis(uint64_t aSample);

 private:
  virtual ~GleanTimingDistribution() = default;

  const impl::TimingDistributionMetric mTimingDist;
};

}  // namespace mozilla::glean

#endif /* mozilla_glean_GleanTimingDistribution_h */
