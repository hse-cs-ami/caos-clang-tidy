//===--- MagicNumbersCheck.h - clang-tidy-----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_READABILITY_MAGICNUMBERSCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_READABILITY_MAGICNUMBERSCHECK_H

#include <type_traits>

#include "../clang-tidy/ClangTidyCheck.h"
#include "clang/Lex/Lexer.h"
#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/SmallVector.h>

namespace clang {
namespace tidy {
namespace caos {

/// Detects magic numbers, integer and floating point literals embedded in code.
///
/// For the user-facing documentation see:
/// http://clang.llvm.org/extra/clang-tidy/checks/readability/magic-numbers.html
class MagicNumbersCheck : public ClangTidyCheck {
public:
  MagicNumbersCheck(StringRef Name, ClangTidyContext *Context);
  void storeOptions(ClangTidyOptions::OptionMap &Opts) override;
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;

  enum class ConstCategory {
    NONE,
    RUNTIME_CONST,
    TRUE_CONST,
  };

  struct LiteralUsageInfo {
    ConstCategory Category = ConstCategory::NONE;
    bool IsUsedInInitializerList = false;
  };

private:
  // For static_assert in constexpr if. See
  // https://en.cppreference.com/w/cpp/language/if#Constexpr_If
  template <class> inline static constexpr bool dependent_false_v = false;

  void parseIgnoredFunctionArgs();

  LiteralUsageInfo
  getUsageInfo(const clang::ast_matchers::MatchFinder::MatchResult &Result,
                                const clang::Expr &ExprResult) const;

  bool isIgnoredValue(const IntegerLiteral *Literal) const;
  bool isIgnoredValue(const FloatingLiteral *Literal) const;

  bool isSyntheticValue(const clang::SourceManager *SourceManager,
                        const IntegerLiteral *Literal) const;

  bool
  isBitFieldWidth(const clang::ast_matchers::MatchFinder::MatchResult &Result,
                       const IntegerLiteral &Literal) const;

  bool isIgnoredFunctionArg(const clang::ast_matchers::MatchFinder::MatchResult &Result,
                 const IntegerLiteral &Literal) const;

  bool isIgnoredFunctionArgImpl(const ast_matchers::MatchFinder::MatchFinder::MatchResult &Result,
                                const DynTypedNode &Node, const DynTypedNode &Child) const;

  template <typename L>
  void checkBoundMatch(const ast_matchers::MatchFinder::MatchResult &Result,
                       const char *BoundName) {
    const L *MatchedLiteral = Result.Nodes.getNodeAs<L>(BoundName);
    if (!MatchedLiteral)
      return;

    if (Result.SourceManager->isMacroBodyExpansion(
            MatchedLiteral->getLocation()))
      return;

    if (isIgnoredValue(MatchedLiteral))
      return;

    LiteralUsageInfo UsageInfo = getUsageInfo(Result, *MatchedLiteral);
    if (UsageInfo.Category == ConstCategory::TRUE_CONST ||
        (UsageInfo.Category == ConstCategory::RUNTIME_CONST &&
         UsageInfo.IsUsedInInitializerList))
      return;

    if constexpr (std::is_same_v<L, IntegerLiteral>) {
      if (isSyntheticValue(Result.SourceManager, MatchedLiteral))
        return;

      if (isBitFieldWidth(Result, *MatchedLiteral))
        return;

      if (isIgnoredFunctionArg(Result, *MatchedLiteral))
        return;
    }

    const StringRef LiteralSourceText = Lexer::getSourceText(
        CharSourceRange::getTokenRange(MatchedLiteral->getSourceRange()),
        *Result.SourceManager, getLangOpts());

    if (UsageInfo.Category == ConstCategory::RUNTIME_CONST) {
      if constexpr (std::is_same_v<L, IntegerLiteral>) {
        diag(MatchedLiteral->getLocation(),
             "'const' in C is not a compile-time constant; consider using an "
             "enum for integer constants");
      } else if constexpr (std::is_same_v<L, FloatingLiteral>) {
        diag(MatchedLiteral->getLocation(),
             "'const' in C is not a compile-time constant; consider using a "
             "#define for floating-point constants");
      } else {
        static_assert(dependent_false_v<L>, "Not implemented");
      }
    } else {
      diag(MatchedLiteral->getLocation(),
           "%0 is a magic number; consider replacing it with a named constant")
          << LiteralSourceText;
    }
  }

  struct IgnoredFunctionArg {
    enum Base {
      DEC = 1,
      OCT = 2,
      HEX = 4,
      BIN = 8,
      ANY = DEC | OCT | HEX | BIN,
    };

    StringRef FunctionName;
    // Single integer instead of an array, because in most cases literals are allowed only in 1 arg of a function.
    // Also, different arguments can have different allowed bases.
    unsigned Position;
    Base Bases;

    bool operator<(const IgnoredFunctionArg& other) const {
      return FunctionName < other.FunctionName || (FunctionName == other.FunctionName && Position < other.Position);
    }
  };

  const bool IgnoreAllFloatingPointValues;
  const bool IgnoreBitFieldsWidths;
  const bool IgnorePowersOf2IntegerValues;
  const bool IgnoreStrtolBases;  // Legacy option. Use IgnoredFunctionArgs instead
  const StringRef RawIgnoredIntegerValues;
  const StringRef RawIgnoredFloatingPointValues;
  const StringRef RawIgnoredFunctionArgs;

  constexpr static unsigned SensibleNumberOfMagicValueExceptions = 16;

  constexpr static llvm::APFloat::roundingMode DefaultRoundingMode =
      llvm::APFloat::rmNearestTiesToEven;

  llvm::SmallVector<int64_t, SensibleNumberOfMagicValueExceptions>
      IgnoredIntegerValues;
  llvm::SmallVector<float, SensibleNumberOfMagicValueExceptions>
      IgnoredFloatingPointValues;
  llvm::SmallVector<double, SensibleNumberOfMagicValueExceptions>
      IgnoredDoublePointValues;
  llvm::SmallVector<IgnoredFunctionArg, SensibleNumberOfMagicValueExceptions>
    IgnoredFunctionArgs;
};

} // namespace caos
} // namespace tidy
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_READABILITY_MAGICNUMBERSCHECK_H
