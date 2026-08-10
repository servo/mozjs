/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef util_RandomSeed_h
#define util_RandomSeed_h

#include "mozilla/Array.h"

#include <stdint.h>

namespace js {

extern uint64_t GenerateRandomSeed();

// Fill |seed[0]| and |seed[1]| with random bits, suitable for
// seeding a XorShift128+ random number generator.
extern void GenerateXorShift128PlusSeed(mozilla::Array<uint64_t, 2>& seed);

}  // namespace js

#endif  // util_RandomSeed_h
