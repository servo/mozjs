/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_glean_MemoryDistributionStandalone_h
#define mozilla_glean_MemoryDistributionStandalone_h

#include <cstddef>  // size_t
#include <cstdint>  // uint32_t

namespace mozilla::glean::impl {

class MemoryDistributionStandalone {
 public:
  constexpr explicit MemoryDistributionStandalone(uint32_t aId) : mId(aId) {}

  /*
   * Accumulates the provided sample in the metric.
   *
   * @param aSample The sample to be recorded by the metric. The sample is
   *                assumed to be in the confgured memory unit of the metric.
   *
   * Notes: Values bigger than 1 Terabyte (2^40 bytes) are truncated and an
   * InvalidValue error is recorded.
   */
  void Accumulate(size_t aSample) const;

 protected:
  const uint32_t mId;
};

}  // namespace mozilla::glean::impl

#endif /* mozilla_glean_MemoryDistributionStandalone_h */
