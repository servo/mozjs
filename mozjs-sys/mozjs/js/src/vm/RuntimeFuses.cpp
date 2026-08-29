/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "vm/RuntimeFuses.h"

#include <stddef.h>
#include <stdint.h>

#include "builtin/String.h"
#include "js/friend/UsageStatistics.h"
#include "vm/GlobalObject.h"
#include "vm/JSContext.h"
#include "vm/Runtime.h"

using namespace js;

int32_t js::RuntimeFuses::fuseOffsets[uint8_t(
    RuntimeFuses::FuseIndex::LastFuseIndex)] = {
#define FUSE(Name, LowerName) offsetof(RuntimeFuses, LowerName),
    FOR_EACH_RUNTIME_FUSE(FUSE)
#undef FUSE
};

// static
int32_t js::RuntimeFuses::offsetOfFuseWordRelativeToRuntime(
    RuntimeFuses::FuseIndex index) {
  int32_t base_offset = offsetof(JSRuntime, runtimeFuses);
  int32_t fuse_offset = RuntimeFuses::fuseOffsets[uint8_t(index)];
  int32_t fuseWordOffset = GuardFuse::fuseOffset();

  return base_offset + fuse_offset + fuseWordOffset;
}

const char* js::RuntimeFuses::fuseNames[] = {
#define FUSE(Name, LowerName) #LowerName,
    FOR_EACH_RUNTIME_FUSE(FUSE)
#undef FUSE
};

// TODO: It is not elegant that we have both this mechanism, but also
// GuardFuse::name, and all the overrides for naming fuses. The issue is
// that this method is static to handle consumers that don't have a
// RuntimeFuses around but work with indexes (e.g. spew code).
//
// I'd love it if we had a better answer.
const char* js::RuntimeFuses::getFuseName(RuntimeFuses::FuseIndex index) {
  uint8_t rawIndex = uint8_t(index);
  MOZ_ASSERT(index < RuntimeFuses::FuseIndex::LastFuseIndex);
  return fuseNames[rawIndex];
}

void js::HasSeenObjectEmulateUndefinedFuse::popFuse(JSContext* cx) {
  js::InvalidatingRuntimeFuse::popFuse(cx);
  MOZ_ASSERT(cx->global());
  cx->runtime()->setUseCounter(cx->global(), JSUseCounter::ISHTMLDDA_FUSE);
}

void js::HasSeenArrayExceedsInt32LengthFuse::popFuse(JSContext* cx) {
  js::InvalidatingRuntimeFuse::popFuse(cx);
}

bool js::DefaultLocaleHasDefaultCaseMappingFuse::checkInvariant(JSContext* cx) {
#if JS_HAS_INTL_API
  auto locale = cx->runtime()->getDefaultLocaleIfInitialized();
  return LocaleHasDefaultCaseMapping(locale);
#else
  return true;
#endif
}

void js::DefaultLocaleHasDefaultCaseMappingFuse::popFuse(JSContext* cx) {
  js::InvalidatingRuntimeFuse::popFuse(cx);
}
