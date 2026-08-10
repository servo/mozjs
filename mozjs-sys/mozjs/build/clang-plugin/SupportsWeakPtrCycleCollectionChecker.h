/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BUILD_CLANG_PLUGIN_SUPPORTSWEAKPTRCYCLECOLLECTIONCHECKER_H_
#define BUILD_CLANG_PLUGIN_SUPPORTSWEAKPTRCYCLECOLLECTIONCHECKER_H_

#include "plugin.h"

class SupportsWeakPtrCycleCollectionChecker : public BaseCheck {
public:
  SupportsWeakPtrCycleCollectionChecker(StringRef CheckName,
                                        ContextType *Context = nullptr)
      : BaseCheck(CheckName, Context) {}
  void registerMatchers(MatchFinder *AstMatcher) override;
  void check(const MatchFinder::MatchResult &Result) override;
  bool isLanguageVersionSupported(const LangOptions &LangOpts) const override {
    return LangOpts.CPlusPlus;
  }
};

#endif  // BUILD_CLANG_PLUGIN_SUPPORTSWEAKPTRCYCLECOLLECTIONCHECKER_H_
