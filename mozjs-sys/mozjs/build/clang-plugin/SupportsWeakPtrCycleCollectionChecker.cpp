/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SupportsWeakPtrCycleCollectionChecker.h"
#include "CustomMatchers.h"

void SupportsWeakPtrCycleCollectionChecker::registerMatchers(
    MatchFinder *AstMatcher) {
  auto UnlinkMatcher = cxxRecordDecl(
      hasName("cycleCollection"), hasDefinition(),
      has(cxxMethodDecl(hasName("Unlink"), hasMethodDefinition()).bind("unlink")));

  AstMatcher->addMatcher(
      cxxRecordDecl(
          hasDefinition(), isFirstParty(),
          hasDirectBase(hasType(cxxRecordDecl(hasName("SupportsWeakPtr")))),
          has(UnlinkMatcher))
          .bind("weakPtrClass"),
      this);

  AstMatcher->addMatcher(
      cxxRecordDecl(
          hasDefinition(), isFirstParty(),
          hasDirectBase(
              hasType(cxxRecordDecl(hasName("nsSupportsWeakReference")))),
          has(UnlinkMatcher))
          .bind("weakRefClass"),
      this);
}

namespace {

class WeakUnlinkFinder : public RecursiveASTVisitor<WeakUnlinkFinder> {
public:
  bool Found = false;
  llvm::SmallPtrSet<const FunctionDecl *, 8> Visited;
  StringRef MethodName;

  explicit WeakUnlinkFinder(StringRef MethodName) : MethodName(MethodName) {}

  bool TraverseFunction(const FunctionDecl *Def) {
    if (Visited.insert(Def).second) {
      return TraverseStmt(Def->getBody());
    }
    return true;
  }

  bool VisitCXXMemberCallExpr(CXXMemberCallExpr *CE) {
    if (const CXXMethodDecl *MD = CE->getMethodDecl()) {
      if (IdentifierInfo* Identifier = MD->getIdentifier()) {
        if (Identifier->getName() == MethodName) {
          Found = true;
          return false;
        }
      }
    }
    return true;
  }

  // Also recurse into directly-called functions resolvable within this TU,
  // so that the cleanup call in a parent's Unlink (called via the INHERITED
  // variant) is not missed.
  bool VisitCallExpr(CallExpr *CE) {
    if (Found) {
      return false;
    }
    const FunctionDecl *Callee = CE->getDirectCallee();
    if (!Callee) {
      return true;
    }
    const FunctionDecl *Def = Callee->getDefinition();
    if (!Def) {
      return true;
    }
    return TraverseFunction(Def);
  }
};

} // anonymous namespace

void SupportsWeakPtrCycleCollectionChecker::check(
    const MatchFinder::MatchResult &Result) {
  const auto *D = Result.Nodes.getNodeAs<CXXRecordDecl>("weakPtrClass");
  StringRef BaseClass, RequiredMethod, Macro;
  if (D) {
    BaseClass = "SupportsWeakPtr";
    RequiredMethod = "DetachWeakPtr";
    Macro = "NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_PTR";
  } else {
    D = Result.Nodes.getNodeAs<CXXRecordDecl>("weakRefClass");
    BaseClass = "nsSupportsWeakReference";
    RequiredMethod = "ClearWeakReferences";
    Macro = "NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_REFERENCE";
  }

  const auto *UnlinkDecl = Result.Nodes.getNodeAs<CXXMethodDecl>("unlink");
  // Get the out-of-class definition (expanded from NS_IMPL_CYCLE_COLLECTION_*
  // macros in the .cpp)
  const auto *UnlinkDef =
      dyn_cast<CXXMethodDecl>(UnlinkDecl->getDefinition());

  WeakUnlinkFinder Finder(RequiredMethod);
  Finder.TraverseFunction(UnlinkDef);
  if (!Finder.Found) {
    diag(D->getBeginLoc(),
         "Cycle-collected class %0 inherits from '%1' but "
         "its cycle collection Unlink does not call '%2'()",
         DiagnosticIDs::Error)
        << D << BaseClass << RequiredMethod;
    diag(UnlinkDef->getBeginLoc(), "Unlink defined here; add '%0'",
         DiagnosticIDs::Note)
        << Macro;
  }
}
