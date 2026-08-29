/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EmptyBasesChecker.h"
#include "CustomMatchers.h"

namespace clang {
namespace ast_matchers {

AST_MATCHER_P(CXXRecordDecl, emptyBaseCountAtLeast, unsigned, N) {
  unsigned NumEmptyBases = 0;
  for(const auto & Base : Node.bases()) {
    if(const Type* BaseType = Base.getType().getTypePtrOrNull()) {
      if(const CXXRecordDecl*  CRD = BaseType->getAsCXXRecordDecl(); CRD && CRD->isEmpty())
        NumEmptyBases += 1;
    }
  }
  return NumEmptyBases >= N;
}

AST_MATCHER(CXXRecordDecl, hasMozEmptyBasesAttr) {
  return Node.hasAttr<EmptyBasesAttr>();
}

}

}

void EmptyBasesChecker::registerMatchers(MatchFinder *AstMatcher) {
  AstMatcher->addMatcher(
      cxxRecordDecl(isDefinition(), isFirstParty(), emptyBaseCountAtLeast(2), unless(hasMozEmptyBasesAttr()))
          .bind("class"),
      this);
}

void EmptyBasesChecker::check(const MatchFinder::MatchResult &Result) {
  const CXXRecordDecl *Cls = Result.Nodes.getNodeAs<CXXRecordDecl>("class");
    diag(Cls->getBeginLoc(),
        "Missing MOZ_EMPTY_BASES",
        DiagnosticIDs::Error);
}
