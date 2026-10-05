/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "NaNExprChecker.h"
#include "CustomMatchers.h"

void NaNExprChecker::registerMatchers(MatchFinder *AstMatcher) {
  AstMatcher->addMatcher(
      binaryOperator(
          binaryEqualityOperator(),
          hasLHS(
            ignoringParenImpCasts(
                    declRefExpr(to(varDecl(hasType(qualType((isFloat())))).bind("var"))))),
          hasRHS(
            ignoringParenImpCasts(
                    declRefExpr(to(varDecl(equalsBoundNode("var")))))),
          isFirstParty(),
          unless(isInWhitelistForNaNExpr()))
          .bind("node"),
      this);
}

void NaNExprChecker::check(const MatchFinder::MatchResult &Result) {
  const BinaryOperator *Expression =
      Result.Nodes.getNodeAs<BinaryOperator>("node");
  diag(Expression->getBeginLoc(),
       "comparing a floating point value to itself for "
       "NaN checking can lead to incorrect results",
       DiagnosticIDs::Error);
  diag(Expression->getBeginLoc(), "consider using std::isnan instead",
       DiagnosticIDs::Note);
}
