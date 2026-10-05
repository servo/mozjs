/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "util/RandomSeed.h"

#include "mozilla/Array.h"
#include "mozilla/Maybe.h"
#include "mozilla/RandomNum.h"

#include <stdint.h>

#include "vm/Time.h"

uint64_t js::GenerateRandomSeed() {
  mozilla::Maybe<uint64_t> maybeSeed = mozilla::RandomUint64();

  return maybeSeed.valueOrFrom([] {
    // Use PRMJ_Now() in case we couldn't read random bits from the OS.
    uint64_t timestamp = PRMJ_Now();
    return timestamp ^ (timestamp << 32);
  });
}

void js::GenerateXorShift128PlusSeed(mozilla::Array<uint64_t, 2>& seed) {
  // XorShift128PlusRNG must be initialized with a non-zero seed.
  do {
    seed[0] = GenerateRandomSeed();
    seed[1] = GenerateRandomSeed();
  } while (seed[0] == 0 && seed[1] == 0);
}
