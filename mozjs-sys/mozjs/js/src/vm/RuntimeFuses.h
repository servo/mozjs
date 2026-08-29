/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_RuntimeFuses_h
#define vm_RuntimeFuses_h

#include <stdint.h>

#include "vm/InvalidatingFuse.h"

struct JS_PUBLIC_API JSContext;

namespace js {

class HasSeenObjectEmulateUndefinedFuse final : public InvalidatingRuntimeFuse {
  const char* name() override { return "HasSeenObjectEmulateUndefinedFuse"; }

  bool checkInvariant(JSContext* cx) override {
    // Without traversing the GC heap I don't think it's possible to assert
    // this invariant directly.
    return true;
  }

 public:
  void popFuse(JSContext* cx) override;
};

class HasSeenArrayExceedsInt32LengthFuse final
    : public InvalidatingRuntimeFuse {
  const char* name() override { return "HasSeenArrayExceedsInt32LengthFuse"; }

  bool checkInvariant(JSContext* cx) override { return true; }

 public:
  void popFuse(JSContext* cx) override;
};

class DefaultLocaleHasDefaultCaseMappingFuse final
    : public InvalidatingRuntimeFuse {
  const char* name() override {
    return "DefaultLocaleHasDefaultCaseMappingFuse";
  }

  bool checkInvariant(JSContext* cx) override;

 public:
  void popFuse(JSContext* cx) override;
};

#define FOR_EACH_RUNTIME_FUSE(FUSE)                                            \
  FUSE(HasSeenObjectEmulateUndefinedFuse, hasSeenObjectEmulateUndefinedFuse)   \
  FUSE(HasSeenArrayExceedsInt32LengthFuse, hasSeenArrayExceedsInt32LengthFuse) \
  FUSE(DefaultLocaleHasDefaultCaseMappingFuse,                                 \
       defaultLocaleHasDefaultCaseMappingFuse)

struct RuntimeFuses {
  RuntimeFuses() = default;

#define FUSE(Name, LowerName) Name LowerName{};
  FOR_EACH_RUNTIME_FUSE(FUSE)
#undef FUSE

  void assertInvariants(JSContext* cx) {
// Generate the invariant checking calls.
#define FUSE(Name, LowerName) LowerName.assertInvariant(cx);
    FOR_EACH_RUNTIME_FUSE(FUSE)
#undef FUSE
  }

  // Code Generation Code:
  enum class FuseIndex : uint8_t {
  // Generate Fuse Indexes
#define FUSE(Name, LowerName) Name,
    FOR_EACH_RUNTIME_FUSE(FUSE)
#undef FUSE
        LastFuseIndex
  };

  GuardFuse* getFuseByIndex(FuseIndex index) {
    switch (index) {
      // Return fuses.
#define FUSE(Name, LowerName) \
  case FuseIndex::Name:       \
    return &this->LowerName;
      FOR_EACH_RUNTIME_FUSE(FUSE)
#undef FUSE
      case FuseIndex::LastFuseIndex:
        break;
    }
    MOZ_CRASH("Fuse Not Found");
  }

  static int32_t fuseOffsets[];
  static const char* fuseNames[];

  static int32_t offsetOfFuseWordRelativeToRuntime(FuseIndex index);
  static const char* getFuseName(FuseIndex index);

#ifdef DEBUG
  static bool isInvalidatingFuse(FuseIndex index) {
    switch (index) {
#  define FUSE(Name, LowerName)                                        \
    case FuseIndex::Name:                                              \
      static_assert(std::is_base_of_v<InvalidatingRuntimeFuse, Name>); \
      return true;
      FOR_EACH_RUNTIME_FUSE(FUSE)
#  undef FUSE
      case FuseIndex::LastFuseIndex:
        break;
    }
    MOZ_CRASH("Fuse Not Found");
  }
#endif
};

}  // namespace js

#endif
