/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NonParamInsideFunctionDeclChecker_h_
#define NonParamInsideFunctionDeclChecker_h_

#include "plugin.h"

class NonParamInsideFunctionDeclChecker : public BaseCheck {
public:
  NonParamInsideFunctionDeclChecker(StringRef CheckName,
                                    ContextType *Context = nullptr)
      : BaseCheck(CheckName, Context) {}
  void registerMatchers(MatchFinder *AstMatcher) override;
  void check(const MatchFinder::MatchResult &Result) override;
  bool isLanguageVersionSupported(const LangOptions &LangOpts) const override {
    return LangOpts.CPlusPlus;
  }
};

#endif
