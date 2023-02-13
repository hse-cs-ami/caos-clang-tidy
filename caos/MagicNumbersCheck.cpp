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
// variables as constants. Also integer literals may be permitted in some
// functions' parameters (e.g. `base` of `strtol`/`strtoll` or `mode` of `open`)

#include "MagicNumbersCheck.h"
#include "../clang-tidy/utils/OptionsUtils.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include <algorithm>

using namespace clang::ast_matchers;

namespace clang {

static bool isUsedToInitializeAConstant(
    const MatchFinder::MatchResult &Result, const DynTypedNode &Node,
    bool LangIsCpp,
    tidy::caos::MagicNumbersCheck::LiteralUsageInfo &UsageInfo) {
  using tidy::caos::MagicNumbersCheck;

  const auto *AsInitList = Node.get<InitListExpr>();
  if (AsInitList) {
    UsageInfo.IsUsedInInitializerList = true;
  } else {
    const auto *AsDecl = Node.get<DeclaratorDecl>();
    if (AsDecl) {
      if (AsDecl->getType().isConstQualified()) {
        UsageInfo.Category =
            LangIsCpp ? MagicNumbersCheck::ConstCategory::TRUE_CONST
                      : MagicNumbersCheck::ConstCategory::RUNTIME_CONST;
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

namespace tidy {
namespace caos {

const char DefaultIgnoredIntegerValues[] = "1;2;3;4;";
const char DefaultIgnoredFloatingPointValues[] = "1.0;100.0;";

// sequence of "function_name;arg_pos;bases"
// `arg_pos` starts from 1. `bases` is a concatenation of one or more chars from set {'d', 'o', 'x', 'b', 'a'} ('a' means "any")
// If you want to ignore multiple args of a function, use a separate item for each arg (with same function_name, but different arg_pos).
const char DefaultIgnoredFunctionArgs[] = "strtol;3;d;strtoll;3;d";

MagicNumbersCheck::MagicNumbersCheck(StringRef Name, ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context),
      IgnoreAllFloatingPointValues(
          Options.get("IgnoreAllFloatingPointValues", false)),
      IgnoreBitFieldsWidths(Options.get("IgnoreBitFieldsWidths", true)),
      IgnorePowersOf2IntegerValues(
          Options.get("IgnorePowersOf2IntegerValues", false)),
      IgnoreStrtolBases(Options.get("IgnoreStrtolBases", false)),
      RawIgnoredIntegerValues(
          Options.get("IgnoredIntegerValues", DefaultIgnoredIntegerValues)),
      RawIgnoredFloatingPointValues(Options.get(
          "IgnoredFloatingPointValues", DefaultIgnoredFloatingPointValues)),
      RawIgnoredFunctionArgs(
          Options.get("IgnoredFunctionArgs", DefaultIgnoredFunctionArgs)) {
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

  parseIgnoredFunctionArgs();
}

void MagicNumbersCheck::parseIgnoredFunctionArgs() {
  // Example:
  // IgnoredFunctionArgs:
  // "strtol;3;d;strtoll;3;d;open;3;o;creat;2;o;chmod;2;o;fchmod;2;o"
  const std::vector<StringRef> IgnoredFunctionArgsInput =
      utils::options::parseStringList(RawIgnoredFunctionArgs);
  if (IgnoredFunctionArgsInput.size() % 3 != 0) {
    configurationDiag("invalid IgnoredFunctionArgs option list '%0' (length is "
                      "not a multiple of 3)")
        << RawIgnoredFunctionArgs;
    return; // Don't even try to parse the list. If a value is missing from the
            // middle of the list, all following entries will be broken.
  }
  for (size_t i = 0; i < IgnoredFunctionArgsInput.size(); i += 3) {
    StringRef FunctionName =
        IgnoredFunctionArgsInput[i]; // Check if name is a valid identifier?
    StringRef PositionInput = IgnoredFunctionArgsInput[i + 1];
    unsigned Position;
    if (PositionInput.getAsInteger(10, Position)) {
      configurationDiag(
          "invalid arg_pos '%0' in item #%1 of IgnoredFunctionArgs option")
          << PositionInput << i / 3;
      continue;
    }
    StringRef BasesInput = IgnoredFunctionArgsInput[i + 2];
    int Bases = 0;
    bool BasesErr = false;
    for (char Base : BasesInput) {
      switch (Base) {
      case 'd':
        Bases |= IgnoredFunctionArg::Base::DEC;
        break;
      case 'o':
        Bases |= IgnoredFunctionArg::Base::OCT;
        break;
      case 'x':
        Bases |= IgnoredFunctionArg::Base::HEX;
        break;
      case 'b':
        Bases |= IgnoredFunctionArg::Base::BIN;
        break;
      case 'a':
        Bases = IgnoredFunctionArg::Base::ANY;
        break;
      default:
        // std::string is used, because char is formatted as an integer.
        configurationDiag("invalid char '%0' in allowed bases '%1' of item #%2 "
                          "of IgnoredFunctionArgs option")
            << std::string(1, Base) << BasesInput << i / 3;
        BasesErr = true; // Don't break out of inner loop, report all invalid chars
                         // (clang-tidy deduplicates diags, so we'll report only distinct chars)
      }
    }
    if (BasesErr) {
      continue;
    }
    assert(static_cast<int>(Bases) != 0);
    IgnoredFunctionArgs.push_back(
        {.FunctionName = FunctionName,
         .Position = Position,
         .Bases = static_cast<IgnoredFunctionArg::Base>(Bases)});
  }

  if (IgnoreStrtolBases) {
    // Duplicates are not checked (at this scale they shouldn't have any noticeable effect on performance).
    IgnoredFunctionArgs.push_back({.FunctionName = "strtol",
                                   .Position = 3,
                                   .Bases = IgnoredFunctionArg::Base::DEC});
    IgnoredFunctionArgs.push_back({.FunctionName = "strtoll",
                                   .Position = 3,
                                   .Bases = IgnoredFunctionArg::Base::DEC});
  }
  llvm::sort(IgnoredFunctionArgs);
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
  Options.store(Opts, "IgnoredFunctionArgs", RawIgnoredFunctionArgs);
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

bool MagicNumbersCheck::isIgnoredFunctionArg(
    const clang::ast_matchers::MatchFinder::MatchResult &Result,
    const IntegerLiteral &Literal) const {
  if (IgnoredFunctionArgs.empty()) {
    return false;
  }
  return llvm::any_of(Result.Context->getParents(Literal),
                      [&, this](const DynTypedNode &Parent) {
                        return isIgnoredFunctionArgImpl(
                            Result, Parent, DynTypedNode::create(Literal),
                            Literal);
                      });
}

bool MagicNumbersCheck::isIgnoredFunctionArgImpl(
    const MatchFinder::MatchResult &Result, const DynTypedNode &Node,
    const DynTypedNode &Child, const IntegerLiteral &Literal) const {
  const auto *AsCallExpr = Node.get<CallExpr>();
  if (!AsCallExpr) {
    // In some cases a node can have multiple parents, so it's better to check
    // all of them
    // https://github.com/llvm-mirror/clang-tools-extra/blob/5c40544fa40bfb85ec888b6a03421b3905e4a4e7/clang-tidy/utils/ExprSequence.cpp#L21
    return llvm::any_of(Result.Context->getParents(Node),
                        [&, this](const DynTypedNode &Parent) {
                          return isIgnoredFunctionArgImpl(Result, Parent, Node,
                                                          Literal);
                        });
  }
  const auto *FuncRef =
      dyn_cast<DeclRefExpr>(AsCallExpr->getCallee()->IgnoreImpCasts());
  if (!FuncRef) { // not sure if this can happen, better check to be safe
    return false;
  }

  IgnoredFunctionArg ArgInfo{
      .FunctionName = FuncRef->getDecl()->getName(),
      .Position = 0,
  };
  ArrayRef<const Expr *> Args{AsCallExpr->getArgs(), AsCallExpr->getNumArgs()};
  for (size_t i = 0; i < Args.size(); ++i) {
    if (DynTypedNode::create(*Args[i]) == Child) {
      ArgInfo.Position = i + 1;
    }
  }
  assert(ArgInfo.Position > 0);

  auto it = std::lower_bound(IgnoredFunctionArgs.begin(),
                             IgnoredFunctionArgs.end(), ArgInfo);
  if (it == IgnoredFunctionArgs.end() ||
      it->FunctionName != ArgInfo.FunctionName ||
      it->Position != ArgInfo.Position) {
    // (FunctionName, Position) is not in the list.
    return false;
  }

  llvm::SmallVector<char> LiteralBuf;
  SourceLocation Loc = Literal.getLocation();
  StringRef LiteralSpelling =
      Lexer::getSpelling(Loc, LiteralBuf, *Result.SourceManager, getLangOpts());
  auto Base = IgnoredFunctionArg::Base::DEC;
  // Can LiteralSpelling be empty? In this case, it's considered as a decimal literal.
  // Zero is allowed for any base (it's always ignored).
  // All other one-digit literals are decimal.
  if (LiteralSpelling.size() >= 2 && LiteralSpelling[0] == '0') {
    if (LiteralSpelling[1] == 'x' || LiteralSpelling[1] == 'X') {
      Base = IgnoredFunctionArg::Base::HEX;
    } else if (LiteralSpelling[1] == 'b') {
      Base = IgnoredFunctionArg::Base::BIN;
    } else {
      assert('0' <= LiteralSpelling[1] && LiteralSpelling[1] <= '9');
      Base = IgnoredFunctionArg::Base::OCT;
    }
  }
  return Base & it->Bases;
}

} // namespace caos
} // namespace tidy
} // namespace clang
