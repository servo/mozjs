/* * This Source Code Form is subject to the terms of the Mozilla Public
 * * License, v. 2.0. If a copy of the MPL was not distributed with this
 * * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef FuzzerRegistry_h_
#define FuzzerRegistry_h_

#include <cstdint>
#include <map>
#include <string>
#include <utility>

#include "mozilla/Types.h"

typedef int (*FuzzerInitFunc)(int*, char***);
typedef int (*FuzzerTestingFunc)(const uint8_t*, size_t);

typedef int (*LibFuzzerDriver)(int*, char***, FuzzerTestingFunc);

namespace mozilla {

typedef std::pair<FuzzerInitFunc, FuzzerTestingFunc> FuzzerFunctions;

class FuzzerRegistry {
 public:
  MOZ_EXPORT static FuzzerRegistry& getInstance();
  MOZ_EXPORT void registerModule(std::string moduleName,
                                 FuzzerInitFunc initFunc,
                                 FuzzerTestingFunc testingFunc);
  MOZ_EXPORT FuzzerFunctions getModuleFunctions(std::string& moduleName);
  MOZ_EXPORT void printModuleNames();

  FuzzerRegistry(FuzzerRegistry const&) = delete;
  void operator=(FuzzerRegistry const&) = delete;

 private:
  FuzzerRegistry() {};
  std::map<std::string, FuzzerFunctions> moduleMap;
};

}  // namespace mozilla

#endif  // FuzzerRegistry_h_
