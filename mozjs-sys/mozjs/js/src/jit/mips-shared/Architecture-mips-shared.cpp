/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/mips-shared/Architecture-mips-shared.h"

#include "jit/FlushICache.h"  // js::jit::FlushICache
#include "jit/RegisterSets.h"
#include "jit/Simulator.h"

#if defined(__linux__)
#  include <fcntl.h>
#  include <unistd.h>

#  if !defined(JS_SIMULATOR)
#    include <sys/cachectl.h>
#  endif
#endif

#define HWCAP_LOONGSON (1 << 27)
#define HWCAP_R2 (1 << 26)
#define HWCAP_FPU (1 << 0)

namespace js {
namespace jit {

static uint32_t get_mips_flags() {
  uint32_t flags = 0;

#if defined(JS_SIMULATOR_MIPS64)
  flags |= HWCAP_FPU;
  if (!getenv("MIPS_PRE_R2")) {
    flags |= HWCAP_R2;
  }
#else
#  ifdef __linux__
  FILE* fp = fopen("/proc/cpuinfo", "r");
  if (fp) {
    char buf[1024] = {};
    size_t len = fread(buf, sizeof(char), sizeof(buf) - 1, fp);
    fclose(fp);
    buf[len] = 0;
    if (strstr(buf, "FPU")) {
      flags |= HWCAP_FPU;
    }
    if (strstr(buf, "Loongson")) {
      flags |= HWCAP_LOONGSON;
    }
    if (strstr(buf, "mips32r2") || strstr(buf, "mips64r2")) {
      flags |= HWCAP_R2;
    }
  }
#  endif
#endif  // JS_SIMULATOR_MIPS64
  return flags;
}

void MIPSFlags::Init() {
  MOZ_ASSERT(!IsInitialized());

  flags = get_mips_flags();
  hasFPU = flags & HWCAP_FPU;
  hasR2 = flags & HWCAP_R2;
  isLoongson = flags & HWCAP_LOONGSON;
  initialized = true;
}

bool CPUFlagsHaveBeenComputed() { return MIPSFlags::IsInitialized(); }

Registers::Code Registers::FromName(const char* name) {
  for (size_t i = 0; i < Total; i++) {
    if (strcmp(GetName(i), name) == 0) {
      return Code(i);
    }
  }

  return Invalid;
}

void FlushICache(void* code, size_t size) {
#if defined(JS_SIMULATOR)
  js::jit::SimulatorProcess::FlushICache(code, size);

#elif defined(_MIPS_ARCH_LOONGSON3A)
  // On Loongson3-CPUs, The cache flushed automatically
  // by hardware. Just need to execute an instruction hazard.
  uintptr_t tmp;
  asm volatile(
      ".set   push \n"
      ".set   noreorder \n"
      "move   %[tmp], $ra \n"
      "bal    1f \n"
      "daddiu $ra, 8 \n"
      "1: \n"
      "jr.hb  $ra \n"
      "move   $ra, %[tmp] \n"
      ".set   pop\n"
      : [tmp] "=&r"(tmp));

#elif defined(__GNUC__)
  intptr_t end = reinterpret_cast<intptr_t>(code) + size;
  __builtin___clear_cache(reinterpret_cast<char*>(code),
                          reinterpret_cast<char*>(end));

#else
  _flush_cache(reinterpret_cast<char*>(code), size, BCACHE);

#endif
}

}  // namespace jit
}  // namespace js
