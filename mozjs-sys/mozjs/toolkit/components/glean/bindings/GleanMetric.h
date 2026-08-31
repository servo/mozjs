/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_glean_GleanMetric_h
#define mozilla_glean_GleanMetric_h

#include "mozilla/glean/bindings/CommonStandalone.h"
// The following include provides GleanWebidlEnabled(), used by generated
// GleanMetricsBinding.cpp
#include "mozilla/glean/bindings/GleanWebIDL.h"
#include "nsWrapperCache.h"
#include "nsClassHashtable.h"
#include "nsTHashMap.h"
#include "mozilla/DataMutex.h"

namespace mozilla::Telemetry {
enum class ScalarID : uint32_t;
enum HistogramID : uint32_t;
}  // namespace mozilla::Telemetry

namespace mozilla::glean {

class GleanMetric : public nsISupports, public nsWrapperCache {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS;
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(GleanMetric);

  nsISupports* GetParentObject() const { return mParent; }

 protected:
  GleanMetric(nsISupports* aParent) : mParent(aParent) {}
  virtual ~GleanMetric() = default;
  nsCOMPtr<nsISupports> mParent;
};

typedef nsUint32HashKey SubmetricIdHashKey;
typedef nsTHashMap<SubmetricIdHashKey,
                   std::tuple<Telemetry::ScalarID, nsString>>
    SubmetricToLabeledMirrorMapType;
typedef StaticDataMutex<UniquePtr<SubmetricToLabeledMirrorMapType>>
    SubmetricToMirrorMutex;

Maybe<SubmetricToMirrorMutex::AutoLock> GetLabeledMirrorLock();

typedef nsTHashMap<SubmetricIdHashKey,
                   std::tuple<Telemetry::HistogramID, nsCString>>
    SubmetricToLabeledDistributionMirrorMapType;
typedef StaticDataMutex<UniquePtr<SubmetricToLabeledDistributionMirrorMapType>>
    SubmetricToDistributionMirrorMutex;

Maybe<SubmetricToDistributionMirrorMutex::AutoLock>
GetLabeledDistributionMirrorLock();

typedef nsTHashMap<SubmetricIdHashKey,
                   std::tuple<Telemetry::HistogramID, nsCString, nsCString>>
    SubmetricToDualLabeledDistributionMirrorMapType;
typedef StaticDataMutex<
    UniquePtr<SubmetricToDualLabeledDistributionMirrorMapType>>
    SubmetricToDualLabeledDistributionMirrorMutex;

Maybe<SubmetricToDualLabeledDistributionMirrorMutex::AutoLock>
GetDualLabeledDistributionMirrorLock();

}  // namespace mozilla::glean

#endif /* mozilla_glean_GleanMetric_h */
