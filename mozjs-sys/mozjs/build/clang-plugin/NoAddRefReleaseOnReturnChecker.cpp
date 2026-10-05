/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "NoAddRefReleaseOnReturnChecker.h"
#include "CustomMatchers.h"

void NoAddRefReleaseOnReturnChecker::registerMatchers(MatchFinder *AstMatcher) {
  // Look for all of the calls to AddRef() or Release()
  AstMatcher->addMatcher(
      memberExpr(
          isAddRefOrRelease(),
          hasObjectExpression(ignoringImplicit(
              callExpr(callee(functionDecl(hasNoAddRefReleaseOnReturnAttr())
                                  .bind("callee")))
                  .bind("call"))),
          hasParent(callExpr()))
          .bind("member"),
      this);
}

void NoAddRefReleaseOnReturnChecker::check(
    const MatchFinder::MatchResult &Result) {
  const MemberExpr *Member = Result.Nodes.getNodeAs<MemberExpr>("member");
  const CallExpr *Call = Result.Nodes.getNodeAs<CallExpr>("call");
  const FunctionDecl *Callee = Result.Nodes.getNodeAs<FunctionDecl>("callee");

  // Check if the call to AddRef() or Release() was made on the result of a call
  // to a MOZ_NO_ADDREF_RELEASE_ON_RETURN function or method.
  diag(Call->getBeginLoc(),
       "%1 must not be called on the return value of '%0' which is marked with "
       "MOZ_NO_ADDREF_RELEASE_ON_RETURN",
       DiagnosticIDs::Error)
      << Callee->getQualifiedNameAsString()
      << dyn_cast<CXXMethodDecl>(Member->getMemberDecl());
}
