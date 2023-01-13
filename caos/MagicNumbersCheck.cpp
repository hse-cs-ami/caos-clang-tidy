//===--- MagicNumbersCheck.cpp - clang-tidy-------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A checker for magic numbers: integer or floating point literals embedded
// in the code, outside the definition of a constant or an enumeration.
//
//===----------------------------------------------------------------------===//

// This is a modified version of MagicNumbersCheck.cpp from
// clang-tools-extra/clang-tidy release 15.0.6 If this check is used for C, it
// If this check is used for C, it doesn't consider const-qualified numeric
// variables as constants. Also integer literals may be permitter in third
// parameter (base) of strtol(l)

#include "MagicNumbersCheck.h"
#include "../clang-tidy/utils/OptionsUtils.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/ADT/STLExtras.h"
#include <algorithm>

using namespace clang::ast_matchers;

namespace clang {

static bool isUsedToInitializeAConstant(
    const MatchFinder::MatchResult &Result, const DynTypedNode &Node,
                                        bool LangIsCpp,
                                        tidy::caos::MagicNumbersCheck::LiteralUsageInfo& UsageInfo) {
  using tidy::caos::MagicNumbersCheck;

  const auto *AsDecl = Node.get<DeclaratorDecl>();
  if (AsDecl) {
    if (AsDecl->getType().isConstQualified()) {
      UsageInfo.Category =
          LangIsCpp ? MagicNumbersCheck::ConstCategory::TRUE_CONST
                    : MagicNumbersCheck::ConstCategory::RUNTIME_CONST;  // TODO check initializer lists
      return true;
    }

    if (AsDecl->isImplicit()) {
      UsageInfo.Category = MagicNumbersCheck::ConstCategory::TRUE_CONST;
      return true;
    } else {
      return false;
    }
  }

  if (Node.get<EnumConstantDecl>()) {
    UsageInfo.Category = MagicNumbersCheck::ConstCategory::TRUE_CONST;
    return true;
  }

  return llvm::any_of(
      Result.Context->getParents(Node),
                      [&Result, &UsageInfo, LangIsCpp](const DynTypedNode &Parent) {
        return isUsedToInitializeAConstant(Result, Parent, LangIsCpp,
                                           UsageInfo);
                      });
}

static bool isUsedToDefineABitField(const MatchFinder::MatchResult &Result,
                                    const DynTypedNode &Node) {
  const auto *AsFieldDecl = Node.get<FieldDecl>();
  if (AsFieldDecl && AsFieldDecl->isBitField())
    return true;

  return llvm::any_of(Result.Context->getParents(Node),
                      [&Result](const DynTypedNode &Parent) {
                        return isUsedToDefineABitField(Result, Parent);
                      });
}

static bool isUsedAsStrtolBase(const MatchFinder::MatchResult &Result,
                               const DynTypedNode &Node) {
  const auto *AsCallExpr = Node.get<CallExpr>();
  if (!AsCallExpr) {
    // In some cases a node can have multiple parents, so it's better to check
    // all of them
    // https://github.com/llvm-mirror/clang-tools-extra/blob/5c40544fa40bfb85ec888b6a03421b3905e4a4e7/clang-tidy/utils/ExprSequence.cpp#L21
    return llvm::any_of(Result.Context->getParents(Node),
                        [&Result](const DynTypedNode &Parent) {
                          return isUsedAsStrtolBase(Result, Parent);
                        });
  }
  const auto *FuncRef =
      dyn_cast<DeclRefExpr>(AsCallExpr->getCallee()->IgnoreImpCasts());
  if (!FuncRef) { // not sure if this can happen, better check to be safe
    return false;
  }
  StringRef FuncName = FuncRef->getDecl()->getName();
  // literals are allowed in any argument of strtol(l) to simplify
  // implementation.
  return FuncName == "strtol" || FuncName == "strtoll";
}

namespace tidy {
namespace caos {

const char DefaultIgnoredIntegerValues[] = "1;2;3;4;";
const char DefaultIgnoredFloatingPointValues[] = "1.0;100.0;";

MagicNumbersCheck::MagicNumbersCheck(StringRef Name, ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context),
      IgnoreAllFloatingPointValues(
          Options.get("IgnoreAllFloatingPointValues", false)),
      IgnoreBitFieldsWidths(Options.get("IgnoreBitFieldsWidths", true)),
      IgnorePowersOf2IntegerValues(
          Options.get("IgnorePowersOf2IntegerValues", false)),
      IgnoreStrtolBases(Options.get("IgnoreStrtolBases", true)),
      RawIgnoredIntegerValues(
          Options.get("IgnoredIntegerValues", DefaultIgnoredIntegerValues)),
      RawIgnoredFloatingPointValues(Options.get(
          "IgnoredFloatingPointValues", DefaultIgnoredFloatingPointValues)) {
  // Process the set of ignored integer values.
  const std::vector<StringRef> IgnoredIntegerValuesInput =
      utils::options::parseStringList(RawIgnoredIntegerValues);
  IgnoredIntegerValues.resize(IgnoredIntegerValuesInput.size());
  llvm::transform(IgnoredIntegerValuesInput, IgnoredIntegerValues.begin(),
                  [](StringRef Value) {
                    int64_t Res;
                    Value.getAsInteger(10, Res);
                    return Res;
                  });
  llvm::sort(IgnoredIntegerValues);

  if (!IgnoreAllFloatingPointValues) {
    // Process the set of ignored floating point values.
    const std::vector<StringRef> IgnoredFloatingPointValuesInput =
        utils::options::parseStringList(RawIgnoredFloatingPointValues);
    IgnoredFloatingPointValues.reserve(IgnoredFloatingPointValuesInput.size());
    IgnoredDoublePointValues.reserve(IgnoredFloatingPointValuesInput.size());
    for (const auto &InputValue : IgnoredFloatingPointValuesInput) {
      llvm::APFloat FloatValue(llvm::APFloat::IEEEsingle());
      auto StatusOrErr =
          FloatValue.convertFromString(InputValue, DefaultRoundingMode);
      assert(StatusOrErr && "Invalid floating point representation");
      consumeError(StatusOrErr.takeError());
      IgnoredFloatingPointValues.push_back(FloatValue.convertToFloat());

      llvm::APFloat DoubleValue(llvm::APFloat::IEEEdouble());
      StatusOrErr =
          DoubleValue.convertFromString(InputValue, DefaultRoundingMode);
      assert(StatusOrErr && "Invalid floating point representation");
      consumeError(StatusOrErr.takeError());
      IgnoredDoublePointValues.push_back(DoubleValue.convertToDouble());
    }
    llvm::sort(IgnoredFloatingPointValues);
    llvm::sort(IgnoredDoublePointValues);
  }
}

void MagicNumbersCheck::storeOptions(ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "IgnoreAllFloatingPointValues",
                IgnoreAllFloatingPointValues);
  Options.store(Opts, "IgnoreBitFieldsWidths", IgnoreBitFieldsWidths);
  Options.store(Opts, "IgnorePowersOf2IntegerValues",
                IgnorePowersOf2IntegerValues);
  Options.store(Opts, "IgnoreStrtolBases", IgnoreStrtolBases);
  Options.store(Opts, "IgnoredIntegerValues", RawIgnoredIntegerValues);
  Options.store(Opts, "IgnoredFloatingPointValues",
                RawIgnoredFloatingPointValues);
}

void MagicNumbersCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(integerLiteral().bind("integer"), this);
  if (!IgnoreAllFloatingPointValues)
    Finder->addMatcher(floatLiteral().bind("float"), this);
}

void MagicNumbersCheck::check(const MatchFinder::MatchResult &Result) {

  TraversalKindScope RAII(*Result.Context, TK_AsIs);

  checkBoundMatch<IntegerLiteral>(Result, "integer");
  checkBoundMatch<FloatingLiteral>(Result, "float");
}

MagicNumbersCheck::LiteralUsageInfo MagicNumbersCheck::getUsageInfo(
    const clang::ast_matchers::MatchFinder::MatchResult &Result,
                                const clang::Expr &ExprResult) const {
  LiteralUsageInfo UsageInfo;

  llvm::any_of(
      Result.Context->getParents(ExprResult),
      [this, &Result, &UsageInfo](const DynTypedNode &Parent) {
        if (isUsedToInitializeAConstant(Result, Parent, getLangOpts().CPlusPlus,
                                        UsageInfo))
          return true;

        // Some checks from original readability-magic-numbers.
        // If any of them returns true, the constant is considered a "true"
        // (compile-time) constant. This may not always be the case, but
        // distinction between categories is used only to ban numeric runtime
        // constants.
        if (std::invoke([&]() {
          // Ignore this instance, because this matches an
          // expanded class enumeration value.
          if (Parent.get<CStyleCastExpr>() &&
              llvm::any_of(
                  Result.Context->getParents(Parent),
                  [](const DynTypedNode &GrandParent) {
                        return GrandParent
                                   .get<SubstNonTypeTemplateParmExpr>() !=
                           nullptr;
                  }))
            return true;

          // Ignore this instance, because this match reports the
          // location where the template is defined, not where it
          // is instantiated.
          if (Parent.get<SubstNonTypeTemplateParmExpr>())
            return true;

          // Don't warn on string user defined literals:
          // std::string s = "Hello World"s;
          if (const auto *UDL = Parent.get<UserDefinedLiteral>())
                if (UDL->getLiteralOperatorKind() ==
                    UserDefinedLiteral::LOK_String)
              return true;

          return false;
        })) {
          UsageInfo.Category = ConstCategory::TRUE_CONST;
          return true;
        }
        return false;
      });

    return UsageInfo;
}

bool MagicNumbersCheck::isIgnoredValue(const IntegerLiteral *Literal) const {
  const llvm::APInt IntValue = Literal->getValue();
  const int64_t Value = IntValue.getZExtValue();
  if (Value == 0)
    return true;

  if (IgnorePowersOf2IntegerValues && IntValue.isPowerOf2())
    return true;

  return std::binary_search(IgnoredIntegerValues.begin(),
                            IgnoredIntegerValues.end(), Value);
}

bool MagicNumbersCheck::isIgnoredValue(const FloatingLiteral *Literal) const {
  const llvm::APFloat FloatValue = Literal->getValue();
  if (FloatValue.isZero())
    return true;

  if (&FloatValue.getSemantics() == &llvm::APFloat::IEEEsingle()) {
    const float Value = FloatValue.convertToFloat();
    return std::binary_search(IgnoredFloatingPointValues.begin(),
                              IgnoredFloatingPointValues.end(), Value);
  }

  if (&FloatValue.getSemantics() == &llvm::APFloat::IEEEdouble()) {
    const double Value = FloatValue.convertToDouble();
    return std::binary_search(IgnoredDoublePointValues.begin(),
                              IgnoredDoublePointValues.end(), Value);
  }

  return false;
}

bool MagicNumbersCheck::isSyntheticValue(const SourceManager *SourceManager,
                                         const IntegerLiteral *Literal) const {
  const std::pair<FileID, unsigned> FileOffset =
      SourceManager->getDecomposedLoc(Literal->getLocation());
  if (FileOffset.first.isInvalid())
    return false;

  const StringRef BufferIdentifier =
      SourceManager->getBufferOrFake(FileOffset.first).getBufferIdentifier();

  return BufferIdentifier.empty();
}

bool MagicNumbersCheck::isBitFieldWidth(
    const clang::ast_matchers::MatchFinder::MatchResult &Result,
    const IntegerLiteral &Literal) const {
  return IgnoreBitFieldsWidths &&
         llvm::any_of(Result.Context->getParents(Literal),
                      [&Result](const DynTypedNode &Parent) {
                        return isUsedToDefineABitField(Result, Parent);
                      });
}

bool MagicNumbersCheck::isStrtolBase(
    const clang::ast_matchers::MatchFinder::MatchResult &Result,
    const IntegerLiteral &Literal) const {
  if (!IgnoreStrtolBases) {
    return false;
  }
  return llvm::any_of(Result.Context->getParents(Literal),
                      [&Result](const DynTypedNode &Parent) {
                        return isUsedAsStrtolBase(Result, Parent);
                      });
}

} // namespace caos
} // namespace tidy
} // namespace clang
