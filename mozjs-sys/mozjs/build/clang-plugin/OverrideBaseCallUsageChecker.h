/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef OverrideBaseCallUsageChecker_h_
#define OverrideBaseCallUsageChecker_h_

#include "plugin.h"

/*
 *  This is a companion checker for OverrideBaseCallChecker that rejects
 *  the usage of MOZ_REQUIRED_BASE_METHOD on non-virtual base methods.
 */
class OverrideBaseCallUsageChecker : public BaseCheck {
public:
  OverrideBaseCallUsageChecker(StringRef CheckName = "override-base-call-usage",
                               ContextType *Context = nullptr)
      : BaseCheck(CheckName, Context) {}
  void registerMatchers(MatchFinder *AstMatcher) override;
  void check(const MatchFinder::MatchResult &Result) override;
  bool isLanguageVersionSupported(const LangOptions &LangOpts) const override {
    return LangOpts.CPlusPlus;
  }
};

#endif
