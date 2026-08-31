/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_glean_GleanMemoryDistribution_h
#define mozilla_glean_GleanMemoryDistribution_h

#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/glean/bindings/DistributionData.h"
#include "mozilla/glean/bindings/GleanMetric.h"
#include "mozilla/glean/bindings/MemoryDistributionStandalone.h"
#include "mozilla/Maybe.h"
#include "nsTArray.h"

namespace mozilla::dom {
struct GleanDistributionData;
}  // namespace mozilla::dom

namespace mozilla::glean {

namespace impl {

class MemoryDistributionMetric : public MemoryDistributionStandalone {
 public:
  constexpr explicit MemoryDistributionMetric(uint32_t aId)
      : MemoryDistributionStandalone(aId) {}

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
};
}  // namespace impl

class GleanMemoryDistribution final : public GleanMetric {
 public:
  explicit GleanMemoryDistribution(uint64_t aId, nsISupports* aParent)
      : GleanMetric(aParent), mMemoryDist(aId) {}

  virtual JSObject* WrapObject(
      JSContext* aCx, JS::Handle<JSObject*> aGivenProto) override final;

  void Accumulate(uint64_t aSample);

  void TestGetValue(const nsACString& aPingName,
                    dom::Nullable<dom::GleanDistributionData>& aRetval,
                    ErrorResult& aRv);

 private:
  virtual ~GleanMemoryDistribution() = default;

  const impl::MemoryDistributionMetric mMemoryDist;
};

}  // namespace mozilla::glean

#endif /* mozilla_glean_GleanMemoryDistribution_h */
