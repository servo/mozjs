/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/glean/bindings/MetricStandaloneTypes.h"

namespace mozilla::glean::impl {

void BooleanStandalone::Set(bool aValue) const { (void)mId; }

template <>
void CounterStandalone<CounterType::eBaseOrLabeled>::Add(
    int32_t aAmount) const {
  (void)mId;
}

template <>
void CounterStandalone<CounterType::eDualLabeled>::Add(int32_t aAmount) const {
  (void)mId;
}

void CustomDistributionStandalone::AccumulateSingleSample(
    uint64_t aSample) const {
  (void)mId;
}

void CustomDistributionStandalone::AccumulateSingleSampleSigned(
    int64_t aSample) const {}

void DenominatorStandalone::Add(int32_t aAmount) const { (void)mId; }

void MemoryDistributionStandalone::Accumulate(size_t) const { (void)mId; }

void NumeratorStandalone::AddToNumerator(int32_t) const { (void)mId; }

void QuantityStandalone::Set(int64_t) const { (void)mId; }

void RateStandalone::AddToNumerator(int32_t) const { (void)mId; }

void RateStandalone::AddToDenominator(int32_t) const {}

void TimespanStandalone::Start() const { (void)mId; }

void TimespanStandalone::Stop() const {}

void TimespanStandalone::Cancel() const {}

void TimespanStandalone::SetRaw(uint32_t) const {}

TimerId TimingDistributionStandalone::Start() const {
  (void)mId;
  return 0;
}

void TimingDistributionStandalone::StopAndAccumulate(
    const TimerId&& aId) const {}

void TimingDistributionStandalone::AccumulateRawDuration(
    const TimeDuration& aDuration) const {}

void TimingDistributionStandalone::Cancel(const TimerId&&) const {}

TimingDistributionStandalone::AutoTimer TimingDistributionStandalone::Measure()
    const {
  return AutoTimer(mId, this->Start());
}

void TimingDistributionStandalone::AutoTimer::Cancel() {
  (void)mMetricId;
  (void)mTimerId;
}

TimingDistributionStandalone::AutoTimer::~AutoTimer() = default;

}  // namespace mozilla::glean::impl
