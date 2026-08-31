/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef LoadLibraryUsageChecker_h_
#define LoadLibraryUsageChecker_h_

#include "plugin.h"

class LoadLibraryUsageChecker : public BaseCheck {
public:
  LoadLibraryUsageChecker(StringRef CheckName, ContextType *Context = nullptr)
      : BaseCheck(CheckName, Context) {}
  void registerMatchers(MatchFinder *AstMatcher) override;
  void check(const MatchFinder::MatchResult &Result) override;
};

#endif // !defined(LoadLibraryUsageChecker_h_)
