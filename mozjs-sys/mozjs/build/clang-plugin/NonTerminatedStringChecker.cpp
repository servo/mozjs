/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "NonTerminatedStringChecker.h"
#include "CustomMatchers.h"

void NonTerminatedStringChecker::registerMatchers(MatchFinder *AstMatcher) {
  auto HasAnyCallArgument = hasAnyArgument(callExpr(callee(functionDecl(isMarkedNonTerminatedString()))));
  AstMatcher->addMatcher(callExpr(HasAnyCallArgument).bind("call"), this);
  AstMatcher->addMatcher(cxxConstructExpr(HasAnyCallArgument).bind("construct"), this);
}

template <typename CallExprT>
static void CheckArgs(BaseCheck &Check, const FunctionDecl *Func,
                      const CallExprT *Call) {
  if (!Func) {
    return;
  }

  unsigned NonDefaultArgCount = 0;
  for (unsigned I = 0; I < Call->getNumArgs(); I++) {
    if (!isa<CXXDefaultArgExpr>(Call->getArg(I))) {
      NonDefaultArgCount++;
    }
  }

  const char *Kind = nullptr;
  if (isa<CXXOperatorCallExpr>(Call)) {
    Kind = "operator";
  } else if (Func->hasAttr<FormatAttr>()) {
    Kind = "printf-like";
  } else if (NonDefaultArgCount == 1) {
    auto *Ident = Func->getIdentifier();
    if (Ident && (Ident->isStr("BitwiseCast") || Ident->isStr("bit_cast"))) {
      return; // These are casts, and don't look at the string
    }

    Kind = "single-argument";
  }
  if (!Kind) {
    return;
  }

  for (unsigned I = 0; I < Call->getNumArgs(); I++) {
    const Expr *BareArg = Call->getArg(I);
    const Expr *Arg = IgnoreTrivials(BareArg);

    const CallExpr *ArgCall = dyn_cast<CallExpr>(Arg);
    if (!ArgCall) {
      continue;
    }

    const FunctionDecl *ArgCallee = ArgCall->getDirectCallee();
    if (!ArgCallee) {
      continue;
    }

    // We may have been cast to another type like `void*`, in whichc ase we
    // shouldn't fire the diagnostic.
    const clang::PointerType *PT =
        BareArg->getType()->template getAs<clang::PointerType>();
    if (!PT) {
      continue;
    }

    QualType Pointee = PT->getPointeeType().getUnqualifiedType();
    if (!Pointee->isCharType() && !Pointee->isChar16Type()) {
      continue;
    }

    if (hasCustomAttribute<moz_non_terminated_string>(ArgCallee)) {
      Check.diag(ArgCall->getBeginLoc(),
                 "MOZ_NON_TERMINATED_STRING return value from '%0' passed as "
                 "an argument to %1 function '%2'",
                 DiagnosticIDs::Error)
          << ArgCallee->getQualifiedNameAsString() << Kind
          << Func->getQualifiedNameAsString();
    }
  }
}

void NonTerminatedStringChecker::check(const MatchFinder::MatchResult &Result) {
  if (auto *Call = Result.Nodes.getNodeAs<CallExpr>("call")) {
    CheckArgs(*this, Call->getDirectCallee(), Call);
  }

  if (auto *Construct = Result.Nodes.getNodeAs<CXXConstructExpr>("construct")) {
    CheckArgs(*this, Construct->getConstructor(), Construct);
  }
}
