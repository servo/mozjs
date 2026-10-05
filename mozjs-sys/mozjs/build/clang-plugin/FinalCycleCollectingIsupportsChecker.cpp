/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FinalCycleCollectingIsupportsChecker.h"
#include "CustomMatchers.h"
#include "clang/Lex/Lexer.h"

void FinalCycleCollectingIsupportsChecker::registerMatchers(
    MatchFinder *AstMatcher) {
  // Match final classes in DOM paths that have an AddRef method marked
  // `override` (not `final`) expanded from
  // NS_DECL_CYCLE_COLLECTING_ISUPPORTS_META. NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  // uses `override`; NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL uses `final`.
  AstMatcher->addMatcher(
      cxxRecordDecl(
          isFinal(), isInPath("dom/html"),
          has(cxxMethodDecl(hasName("AddRef"), isOverride(), unless(isFinal()),
                            isExpandedFromMacro(
                                "NS_DECL_CYCLE_COLLECTING_ISUPPORTS_META"))
                  .bind("addRef")))
          .bind("decl"),
      this);
}

void FinalCycleCollectingIsupportsChecker::check(
    const MatchFinder::MatchResult &Result) {
  const auto *D = Result.Nodes.getNodeAs<CXXRecordDecl>("decl");
  const auto *AddRef = Result.Nodes.getNodeAs<CXXMethodDecl>("addRef");

  SourceManager &SM = Result.Context->getSourceManager();

  SourceLocation AddRefLoc = AddRef->getLocation();

  // Walk up the macro expansion stack to find the file-level location of
  // the NS_DECL_CYCLE_COLLECTING_ISUPPORTS token.
  SourceLocation MacroCallLoc = SM.getImmediateMacroCallerLoc(AddRefLoc);
  while (MacroCallLoc.isMacroID()) {
    MacroCallLoc = SM.getImmediateMacroCallerLoc(MacroCallLoc);
  }

  // Get the source range of the macro name token for the fixit.
  CharSourceRange FixRange =
      Lexer::makeFileCharRange(CharSourceRange::getTokenRange(MacroCallLoc), SM,
                               Result.Context->getLangOpts());

  diag(D->getBeginLoc(),
       "final class %0 uses NS_DECL_CYCLE_COLLECTING_ISUPPORTS; "
       "use NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL instead",
       DiagnosticIDs::Error)
      << D;

  if (FixRange.isValid()) {
    diag(MacroCallLoc,
         "replace NS_DECL_CYCLE_COLLECTING_ISUPPORTS with "
         "NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL",
         DiagnosticIDs::Note)
        << FixItHint::CreateReplacement(
               FixRange, "NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL");
  }
}
