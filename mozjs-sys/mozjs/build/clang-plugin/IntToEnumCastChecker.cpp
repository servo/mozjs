/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "IntToEnumCastChecker.h"
#include "CustomMatchers.h"

void IntToEnumCastChecker::registerMatchers(MatchFinder *AstMatcher) {
  // Match any explicit cast from a builtin type to an enum type, inside
  // methods of IPC actor classes (those deriving from mozilla::ipc::IProtocol).
  // This covers static_cast, C-style casts, and functional casts.
  //
  // We anchor on the method and descend into its whole body with
  // forEachDescendant, rather than using forCallable on the cast itself:
  // forCallable only matches the immediately enclosing callable, so casts
  // inside a lambda (which IPC code uses heavily for promises) would have the
  // lambda's operator() as their callable and be missed.
  AstMatcher->addMatcher(
      cxxMethodDecl(
          ofClass(isDerivedFrom(hasName("mozilla::ipc::IProtocol"))),
          isFirstParty(),
          forEachDescendant(
              explicitCastExpr(
                  hasDestinationType(qualType(hasCanonicalType(enumType()))),
                  hasSourceExpression(expr(
                      hasType(qualType(hasCanonicalType(builtinType()))))))
                  .bind("cast"))),
      this);
}

void IntToEnumCastChecker::check(const MatchFinder::MatchResult &Result) {
  const ExplicitCastExpr *Cast =
      Result.Nodes.getNodeAs<ExplicitCastExpr>("cast");

  // Skip casts in gtest code, which exercises IPC actors with synthetic values
  // and would otherwise be flagged by this checker.
  StringRef FileName =
      getFilename(Result.Context->getSourceManager(), Cast->getBeginLoc());
  for (auto Begin = llvm::sys::path::rbegin(FileName),
            End = llvm::sys::path::rend(FileName);
       Begin != End; ++Begin) {
    if (*Begin == "gtest") {
      return;
    }
  }

  // Compiler-inserted casts (e.g. from non-type template parameter
  // substitution) share the same source range as their sub-expression,
  // since no cast syntax was written. Skip those.
  const Expr *Sub = Cast->getSubExpr()->IgnoreImpCasts();
  if (Cast->getBeginLoc() == Sub->getBeginLoc() &&
      Cast->getEndLoc() == Sub->getEndLoc()) {
    return;
  }

  QualType DestType = Cast->getTypeAsWritten();
  QualType SrcType = Cast->getSubExpr()->getType();

  const char *CastName = "cast";
  if (isa<CXXStaticCastExpr>(Cast)) {
    CastName = "static_cast";
  } else if (isa<CXXFunctionalCastExpr>(Cast)) {
    CastName = "functional cast";
  } else if (isa<CStyleCastExpr>(Cast)) {
    CastName = "C-style cast";
  }

  diag(Cast->getBeginLoc(),
       "%2 from builtin type %0 to enum type %1 in an IPC actor method may "
       "produce an out-of-range enum value",
       DiagnosticIDs::Error)
      << SrcType << DestType << CastName;
  diag(Cast->getBeginLoc(),
       "consider using the enum type directly in the IPDL definition with a "
       "validated EnumSerializer, or use a clamping function",
       DiagnosticIDs::Note);
}
