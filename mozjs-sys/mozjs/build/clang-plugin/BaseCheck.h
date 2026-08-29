/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BaseCheck_h_
#define BaseCheck_h_

class MozContext {};
typedef MozContext ContextType;

class BaseCheck : public MatchFinder::MatchCallback {
public:
  BaseCheck(StringRef CheckName, ContextType *Context) {}
  virtual void registerMatchers(MatchFinder *Finder) {}
  virtual void registerPPCallbacks(CompilerInstance &CI) {}
  virtual void check(const MatchFinder::MatchResult &Result) {}
  virtual bool isLanguageVersionSupported(const LangOptions &LangOpts) const {
    return true;
  }
  DiagnosticBuilder diag(SourceLocation Loc, StringRef Description,
                         DiagnosticIDs::Level Level = DiagnosticIDs::Warning) {
    DiagnosticsEngine &Diag = Context->getDiagnostics();
    unsigned ID = Diag.getDiagnosticIDs()->getCustomDiagID(Level, Description);
    return Diag.Report(Loc, ID);
  }

private:
  void run(const MatchFinder::MatchResult &Result) override {
    Context = Result.Context;
    check(Result);
  }

private:
  ASTContext *Context;
};

#endif
