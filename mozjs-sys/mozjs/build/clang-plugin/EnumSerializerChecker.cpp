/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EnumSerializerChecker.h"
#include "CustomMatchers.h"

#include <set>

// Heuristically identifies enumerator names used as sentinel or boundary values.
static bool isSentinelName(StringRef Name) {
  std::string Lower = Name.lower();
  StringRef L(Lower);
  static constexpr StringRef ContainsPatterns[] = {"count", "invalid",
                                                   "sentinel", "bound",
                                                   "limit", "num", "max", "_end", "end_"};
  return llvm::any_of(ContainsPatterns,
                      [L](StringRef P) { return L.contains(P); }) ||
          L == "end";
}

void EnumSerializerChecker::registerMatchers(MatchFinder *AstMatcher) {
  AstMatcher->addMatcher(
      cxxRecordDecl(
          hasDefinition(), isFirstParty(),
          hasDirectBase(hasType(
              classTemplateSpecializationDecl(
                  anyOf(hasName("ContiguousEnumSerializer"),
                        hasName("ContiguousEnumSerializerInclusive")),
                  templateArgumentCountIs(3),
                  hasTemplateArgument(
                      0,
                      refersToType(hasDeclaration(enumDecl().bind("enum")))),
                  hasTemplateArgument(1, isIntegral()),
                  hasTemplateArgument(2, isIntegral()))
                  .bind("serializer"))))
          .bind("derived"),
      this);
}

void EnumSerializerChecker::check(const MatchFinder::MatchResult &Result) {
  const auto *Derived = Result.Nodes.getNodeAs<CXXRecordDecl>("derived");
  const auto *Serializer =
      Result.Nodes.getNodeAs<ClassTemplateSpecializationDecl>("serializer");
  const auto *ED = Result.Nodes.getNodeAs<EnumDecl>("enum")->getDefinition();
  if (!ED) {
    return;
  }

  StringRef Name = Serializer->getName();
  bool IsInclusive = Name == "ContiguousEnumSerializerInclusive";

  const TemplateArgumentList &Args = Serializer->getTemplateArgs();
  llvm::APSInt MinVal = Args[1].getAsIntegral();
  llvm::APSInt BoundVal = Args[2].getAsIntegral();

  // Collect all enumerators with their values.
  SmallVector<std::pair<int64_t, const EnumConstantDecl *>, 32> Enumerators;
  for (const auto *Enumerator : ED->enumerators()) {
    Enumerators.push_back(
        {Enumerator->getInitVal().getExtValue(), Enumerator});
  }

  if (Enumerators.empty()) {
    return;
  }

  llvm::sort(Enumerators,
             [](const auto &A, const auto &B) { return A.first < B.first; });

  int64_t MinI = MinVal.getExtValue();
  int64_t BoundI = BoundVal.getExtValue();

  // Find enumerators corresponding to the min and bound values.
  const EnumConstantDecl *MinEnumerator = nullptr;
  const EnumConstantDecl *BoundEnumerator = nullptr;
  for (const auto &[Val, ECD] : Enumerators) {
    if (Val == MinI && !MinEnumerator) {
      MinEnumerator = ECD;
    }
    if (Val == BoundI && !BoundEnumerator) {
      BoundEnumerator = ECD;
    }
  }

  SourceLocation Loc = Derived->getLocation();

  // Check 1: ContiguousEnumSerializerInclusive with a sentinel-like max value.
  if (IsInclusive && BoundEnumerator &&
      !hasCustomAttribute<moz_enum_serializer_allow_sentinel_upper_bound>(
          Derived)) {
    StringRef BoundName = BoundEnumerator->getName();
    if (isSentinelName(BoundName)) {
      diag(Loc,
           "ContiguousEnumSerializerInclusive includes sentinel value '%0' "
           "as valid; use ContiguousEnumSerializer with an exclusive upper "
           "bound instead",
           DiagnosticIDs::Warning)
          << BoundName;
    }
  }

  // Check 2: Min value doesn't match the first (smallest) enumerator.
  int64_t FirstVal = Enumerators.front().first;
  if (MinI != FirstVal &&
      !hasCustomAttribute<moz_enum_serializer_allow_min_mismatch>(Derived)) {
    diag(Loc,
         "%0 min value '%1' does not match the first enumerator '%2' "
         "(value %3); the range excludes valid enum values",
         DiagnosticIDs::Warning)
        << Name << (MinEnumerator ? MinEnumerator->getName() : StringRef("?"))
        << Enumerators.front().second->getName()
        << static_cast<int>(FirstVal);
  }

  // Check 3: Bound value doesn't match the last (highest) enumerator.
  int64_t LastVal = Enumerators.back().first;
  if (BoundI != LastVal) {
    diag(Loc,
         "%0 upper bound does not match the last enumerator '%1' "
         "(value %2); the range may exclude valid enum values",
         DiagnosticIDs::Warning)
        << Name << Enumerators.back().second->getName()
        << static_cast<int>(LastVal);
  }

  // Check 4: Non-contiguous range detection.
  int64_t RangeSize = IsInclusive ? (BoundI - MinI + 1) : (BoundI - MinI);

  // Count unique enumerator values that fall within the accepted range.
  std::set<int64_t> UniqueValsInRange;
  for (const auto &[Val, ECD] : Enumerators) {
    bool InRange =
        IsInclusive ? (Val >= MinI && Val <= BoundI) : (Val >= MinI && Val < BoundI);
    if (InRange) {
      UniqueValsInRange.insert(Val);
    }
  }

  int64_t EnumeratorCount = static_cast<int64_t>(UniqueValsInRange.size());
  if (EnumeratorCount < RangeSize) {
    int64_t GapCount = RangeSize - EnumeratorCount;
    diag(Loc,
         "%0 used with non-contiguous enum; range accepts %1 values but "
         "only %2 enumerators exist (%3 invalid values accepted)",
         DiagnosticIDs::Error)
        << Name << static_cast<int>(RangeSize)
        << static_cast<int>(EnumeratorCount) << static_cast<int>(GapCount);
  }
}
