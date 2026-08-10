/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ParamTraitsEnumChecker.h"
#include "CustomMatchers.h"

void ParamTraitsEnumChecker::registerMatchers(MatchFinder *AstMatcher) {
  AstMatcher->addMatcher(
      classTemplateSpecializationDecl(
          hasName("ParamTraits"), isDefinition(),
          unless(anyOf(
              // Exclude ParamTraits that derive from EnumSerializer (e.g.,
              // ContiguousEnumSerializer) as these are the recommended way to
              // serialize enums.
              isDerivedFrom("EnumSerializer"),
              // Exclude nsresult specifically, which has a legitimate
              // ParamTraitsMozilla specialization.
              hasTemplateArgument(
                  0, refersToType(hasDeclaration(namedDecl(hasName("nsresult"))))))))
          .bind("decl"),
      this);
}

void ParamTraitsEnumChecker::check(const MatchFinder::MatchResult &Result) {
  const ClassTemplateSpecializationDecl *Decl =
      Result.Nodes.getNodeAs<ClassTemplateSpecializationDecl>("decl");

  const TemplateArgumentList &ArgumentList = Decl->getTemplateArgs();
  if (ArgumentList.size() != 1) {
    diag(Decl->getBeginLoc(),
         "ParamTraits specialization should have exactly one template argument",
         DiagnosticIDs::Error);
    return;
  }

  QualType ArgType = ArgumentList[0].getAsType();
  if (const clang::Type *TypePtr = ArgType.getTypePtrOrNull()) {
    if (TypePtr->isEnumeralType()) {
      diag(Decl->getBeginLoc(),
           "Custom ParamTraits implementation for an enum type",
           DiagnosticIDs::Error);
      diag(Decl->getBeginLoc(),
           "Please use a helper class for example ContiguousEnumSerializer",
           DiagnosticIDs::Note);
    }
  }
}
