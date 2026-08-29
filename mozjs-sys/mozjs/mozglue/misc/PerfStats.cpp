/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/PerfStats.h"

namespace mozilla::detail {
MFBT_DATA Atomic<uint64_t, MemoryOrdering::Relaxed> sPerfStatsCollectionMask{0};
MFBT_DATA Atomic<PerfStats*, MemoryOrdering::SequentiallyConsistent>
    sPerfStatsSingleton{nullptr};
}  // namespace mozilla::detail
