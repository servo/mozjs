/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_glean_DistributionData_h
#define mozilla_glean_DistributionData_h

#include "nsTHashMap.h"
#include "mozilla/glean/fog_ffi_generated.h"

namespace mozilla::glean {

struct DistributionData final {
  uint64_t sum;
  uint64_t count;
  nsTHashMap<nsUint64HashKey, uint64_t> values;

  /**
   * Create distribution data from the buckets, counts and sum,
   * as returned by `fog_*_distribution_test_get_value`.
   */
  DistributionData(const nsTArray<uint64_t>& aBuckets,
                   const nsTArray<uint64_t>& aCounts, uint64_t aSum,
                   uint64_t aCount)
      : sum(aSum), count(aCount) {
    for (size_t i = 0; i < aBuckets.Length(); ++i) {
      this->values.InsertOrUpdate(aBuckets[i], aCounts[i]);
    }
  }

  /**
   * Create distribution data from an FfiDistributionData instance.
   */
  explicit DistributionData(const impl::FfiDistributionData& aData)
      : sum(aData.sum), count(aData.count) {
    for (size_t i = 0; i < aData.keys.Length(); ++i) {
      this->values.InsertOrUpdate(aData.keys[i], aData.values[i]);
    }
  }

  friend std::ostream& operator<<(std::ostream& aStream,
                                  const DistributionData& aDist) {
    aStream << "DistributionData(";
    aStream << "sum=" << aDist.sum << ", ";
    aStream << "count=" << aDist.count << ", ";
    aStream << "values={";
    bool first = true;
    for (const auto& entry : aDist.values) {
      if (!first) {
        aStream << ", ";
      }
      first = false;

      const uint64_t bucket = entry.GetKey();
      const uint64_t count = entry.GetData();
      aStream << bucket << "=" << count;
    }
    aStream << "}";
    aStream << ")";
    return aStream;
  }

  static void fromFFIArray(
      const nsTArray<impl::FfiDistributionData>& aDataArray,
      nsTArray<DistributionData>& aResultArray) {
    for (const auto& d : aDataArray) {
      aResultArray.AppendElement(DistributionData(d));
    }
  }
};

}  // namespace mozilla::glean

#endif /* mozilla_glean_DistributionData_h */
