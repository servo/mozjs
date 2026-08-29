/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/riscv64/Architecture-riscv64.h"

#include "mozilla/TextUtils.h"

#include <algorithm>
#include <cstdlib>

#include "jit/FlushICache.h"  // js::jit::FlushICache
#include "jit/RegisterSets.h"
#include "jit/riscv64/Assembler-riscv64.h"
#include "jit/Simulator.h"

#if defined(__linux__) && !defined(JS_SIMULATOR_RISCV64) && \
    __has_include(<asm/hwprobe.h>)
#  define USE_HWPROBE
#endif

#ifdef USE_HWPROBE
#  include <asm/hwprobe.h>
#  include <sys/syscall.h>
#endif

namespace js {
namespace jit {
Registers::Code Registers::FromName(const char* name) {
  for (size_t i = 0; i < Total; i++) {
    if (strcmp(GetName(i), name) == 0) {
      return Code(i);
    }
  }

  return Invalid;
}

FloatRegisters::Code FloatRegisters::FromName(const char* name) {
  for (size_t i = 0; i < Total; i++) {
    if (strcmp(GetName(i), name) == 0) {
      return Code(i);
    }
  }

  return Invalid;
}

FloatRegisterSet FloatRegister::ReduceSetForPush(const FloatRegisterSet& s) {
#ifdef ENABLE_WASM_SIMD
#  error "Needs more careful logic if SIMD is enabled"
#endif

  LiveFloatRegisterSet mod;
  for (FloatRegisterIterator iter(s); iter.more(); ++iter) {
    if ((*iter).isSingle()) {
      // Even for single size registers save complete double register.
      mod.addUnchecked((*iter).doubleOverlay());
    } else {
      mod.addUnchecked(*iter);
    }
  }
  return mod.set();
}

FloatRegister FloatRegister::singleOverlay() const {
  MOZ_ASSERT(!isInvalid());
  if (kind_ == Codes::Double) {
    return FloatRegister(encoding_, Codes::Single);
  }
  return *this;
}

FloatRegister FloatRegister::doubleOverlay() const {
  MOZ_ASSERT(!isInvalid());
  if (kind_ != Codes::Double) {
    return FloatRegister(encoding_, Codes::Double);
  }
  return *this;
}

uint32_t FloatRegister::GetPushSizeInBytes(
    const TypedRegisterSet<FloatRegister>& s) {
#ifdef ENABLE_WASM_SIMD
#  error "Needs more careful logic if SIMD is enabled"
#endif

  return s.size() * sizeof(double);
}
void FlushICache(void* code, size_t size) {
#if defined(JS_SIMULATOR)
  js::jit::SimulatorProcess::FlushICache(code, size);

#elif defined(__linux__) || defined(__OpenBSD__)
#  if defined(__GNUC__)
  intptr_t end = reinterpret_cast<intptr_t>(code) + size;
  __builtin___clear_cache(reinterpret_cast<char*>(code),
                          reinterpret_cast<char*>(end));

#  else
  _flush_cache(reinterpret_cast<char*>(code), size, BCACHE);
#  endif
#else
#  error "Unsupported platform"
#endif
}

static const char* gRiscvExtensionsString = nullptr;

void SetRISCV64ExtensionsString(const char* extensions) {
  MOZ_ASSERT(!RVFlags::IsInitialized());
  gRiscvExtensionsString = extensions;
}

enum class RVProfile {
  // General-purpose computing base extension (I,M,A,F,D)
  //
  // These extensions are assumed to be always supported.
  RV64G,

  // RVA20U64 profile
  //
  // https://riscv.github.io/riscv-isa-manual/snapshot/spec/#_rva20u64_profile
  RVA20U64,

  // RVA22U64 profile
  //
  // https://riscv.github.io/riscv-isa-manual/snapshot/spec/#_rva22u64_profile
  RVA22U64,

  // RVA23U64 profile
  //
  // https://riscv.github.io/riscv-isa-manual/snapshot/spec/#_rva23u64_profile
  RVA23U64,
};

static RVExtensions ExtensionsFromProfile(RVProfile profile) {
  RVExtensions result{};
  switch (profile) {
    case RVProfile::RVA23U64:
      result += {
          RVExtension::Zfa,
          RVExtension::Zicond,
      };
      [[fallthrough]];
    case RVProfile::RVA22U64:
      result += {
          RVExtension::Zba,
          RVExtension::Zbb,
          RVExtension::Zbs,
          RVExtension::Zfhmin,
      };
      [[fallthrough]];
    case RVProfile::RVA20U64:
      // No additional extensions.
      //
      // Compressed instructions are mandatory for RVA20U64, but we don't
      // currently support them.
      [[fallthrough]];
    case RVProfile::RV64G:
      break;
  }
  return result;
}

/**
 * Parse extensions similar to the -march flag for GCC/Clang.
 */
static RVExtensions ParseRVExtensions(std::string_view sv) {
  struct NameToProfile {
    std::string_view name;
    RVProfile profile;
  } profiles[] = {
      {"rv64g", RVProfile::RV64G},
      {"rva20u64", RVProfile::RVA20U64},
      {"rva22u64", RVProfile::RVA22U64},
      {"rva23u64", RVProfile::RVA23U64},
  };

  mozilla::Maybe<RVProfile> profile{};
  for (const auto& e : profiles) {
    if (sv.starts_with(e.name)) {
      sv.remove_prefix(e.name.length());
      profile = mozilla::Some(e.profile);
      break;
    }
  }
  if (!profile) {
    fprintf(stderr, "missing or unknown ISA or profile: %.*s\n",
            int(sv.length()), sv.data());
    return {};
  }

  auto result = ExtensionsFromProfile(*profile);

  // "rv64g" can be directly followed by single letter extension names.
  bool needSeparator = *profile != RVProfile::RV64G;

  // Parse additional extensions.
  //
  // Error handling:
  // - Print a warning for unsupported, unknown, or duplicated extensions.
  // - Stop parsing if syntax errors were encountered.
  while (!sv.empty()) {
    bool hasSeparator = sv[0] == '_';
    if (hasSeparator) {
      sv.remove_prefix(1);
    } else if (!needSeparator) {
      needSeparator = true;
    } else {
      fprintf(stderr, "missing '_' separator: %.*s\n", int(sv.length()),
              sv.data());
      break;
    }

    auto name = sv.substr(0, sv.find('_'));
    if (name.empty()) {
      fprintf(stderr, "unexpected empty extension\n");
      break;
    }
    sv.remove_prefix(name.length());

    // Stop parsing if the extension name contains any non-alphanumeric letters.
    if (!std::all_of(name.begin(), name.end(),
                     mozilla::IsAsciiAlphanumeric<char>)) {
      fprintf(stderr, "invalid extension name: '%.*s'\n", int(name.length()),
              name.data());
      break;
    }

    // Multiletter extension names require a leading '_' separator.
    if (!hasSeparator && (name[0] == 's' || name[0] == 'x' || name[0] == 'z')) {
      fprintf(stderr, "missing '_' separator before '%.*s'\n",
              int(name.length()), name.data());
      break;
    }

    // Skip over supervisor or vendor extensions.
    if (name[0] == 's' || name[0] == 'x') {
      fprintf(stderr, "unsupported or unknown extension: %.*s\n",
              int(name.length()), name.data());
      continue;
    }

    // Multiletter extension names start with 'z'.
    if (name[0] == 'z') {
      RVExtension extension;
      if (name == "zba") {
        extension = RVExtension::Zba;
      } else if (name == "zbb") {
        extension = RVExtension::Zbb;
      } else if (name == "zbs") {
        extension = RVExtension::Zbs;
      } else if (name == "zfhmin") {
        extension = RVExtension::Zfhmin;
      } else if (name == "zfa") {
        extension = RVExtension::Zfa;
      } else if (name == "zicond") {
        extension = RVExtension::Zicond;
      } else {
        fprintf(stderr, "unsupported or unknown extension: %.*s\n",
                int(name.length()), name.data());
        continue;
      }
      if (result.contains(extension)) {
        fprintf(stderr, "duplicate extension: %.*s\n", int(name.length()),
                name.data());
        continue;
      }
      result += extension;
      continue;
    }

    // Single letter extension names.
    for (auto c : name) {
      RVExtensions extensions;
      switch (c) {
        case 'b':
          extensions = {
              RVExtension::Zba,
              RVExtension::Zbb,
              RVExtension::Zbs,
          };
          break;
        default:
          fprintf(stderr, "unsupported or unknown extension: %c\n", c);
          continue;
      }

      if (!(result & extensions).isEmpty()) {
        fprintf(stderr, "duplicate extension: %c\n", c);
        continue;
      }
      result += extensions;
    }
  }

  return result;
}

static RVExtensions ComputeRVExtensions() {
  RVExtensions extensions{};

#if defined(JS_SIMULATOR_RISCV64)
  // Simulator supports all RVA23U64 extensions.
  extensions += ExtensionsFromProfile(RVProfile::RVA23U64);
#elif defined(USE_HWPROBE)
  riscv_hwprobe probe[1] = {{RISCV_HWPROBE_KEY_IMA_EXT_0, 0}};
  if (syscall(__NR_riscv_hwprobe, probe, 1, 0, nullptr, 0) == 0) {
    if (probe[0].value & RISCV_HWPROBE_EXT_ZBA) {
      extensions += RVExtension::Zba;
    }
    if (probe[0].value & RISCV_HWPROBE_EXT_ZBB) {
      extensions += RVExtension::Zbb;
    }
    if (probe[0].value & RISCV_HWPROBE_EXT_ZBS) {
      extensions += RVExtension::Zbs;
    }
    if (probe[0].value & RISCV_HWPROBE_EXT_ZFHMIN) {
      extensions += RVExtension::Zfhmin;
    }
    if (probe[0].value & RISCV_HWPROBE_EXT_ZFA) {
      extensions += RVExtension::Zfa;
    }
    if (probe[0].value & RISCV_HWPROBE_EXT_ZICOND) {
      extensions += RVExtension::Zicond;
    }
  }
#endif

  return extensions;
}

// static
void RVFlags::Init() {
  MOZ_ASSERT(!IsInitialized());

  RVExtensions supported = ComputeRVExtensions();

  auto requested = supported;
  if (const auto* ext = std::getenv("RISCV_EXT")) {
    requested = ParseRVExtensions(ext);
  } else if (gRiscvExtensionsString) {
    requested = ParseRVExtensions(gRiscvExtensionsString);
  }

  // Enable requested extensions if and only if they're also supported.
  auto actual = requested & supported;
  MOZ_ASSERT(!actual.contains(RVExtension::Initialized));
  actual += RVExtension::Initialized;

  extensions = actual;
}

bool CPUFlagsHaveBeenComputed() { return RVFlags::IsInitialized(); }

}  // namespace jit
}  // namespace js

#undef USE_HWPROBE
