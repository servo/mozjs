/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef OverrideBaseCallChecker_h_
#define OverrideBaseCallChecker_h_

#include "plugin.h"

class OverrideBaseCallChecker : public BaseCheck {
public:
  OverrideBaseCallChecker(StringRef CheckName, ContextType *Context = nullptr)
      : BaseCheck(CheckName, Context) {}
  void registerMatchers(MatchFinder *AstMatcher) override;
  void check(const MatchFinder::MatchResult &Result) override;
  bool isLanguageVersionSupported(const LangOptions &LangOpts) const override {
    return LangOpts.CPlusPlus;
  }

private:
  void evaluateExpression(const Stmt *StmtExpr,
                          std::list<const CXXMethodDecl *> &MethodList);
  void getRequiredBaseMethod(const CXXMethodDecl *Method,
                             std::list<const CXXMethodDecl *> &MethodsList);
  void findBaseMethodCall(const CXXMethodDecl *Method,
                          std::list<const CXXMethodDecl *> &MethodsList);
  bool isRequiredBaseMethod(const CXXMethodDecl *Method);
};

#endif
