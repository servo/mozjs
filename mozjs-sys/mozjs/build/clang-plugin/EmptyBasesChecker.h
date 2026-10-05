/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef EmptyBasesChecker_h_
#define EmptyBasesChecker_h_

#include "plugin.h"

class EmptyBasesChecker : public BaseCheck {
public:
  EmptyBasesChecker(StringRef CheckName, ContextType *Context = nullptr)
      : BaseCheck(CheckName, Context), CI(nullptr) {}
  void registerMatchers(MatchFinder *AstMatcher) override;
  void check(const MatchFinder::MatchResult &Result) override;
  bool isLanguageVersionSupported(const LangOptions &LangOpts) const override {
    return LangOpts.CPlusPlus;
  }

private:
  const CompilerInstance *CI;
};

#endif
