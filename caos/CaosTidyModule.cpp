//===--- PluginTidyModule.cpp - clang-tidy ----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "../clang-tidy/ClangTidy.h"
#include "../clang-tidy/ClangTidyModule.h"
#include "../clang-tidy/ClangTidyModuleRegistry.h"
#include "MagicNumbersCheck.h"
#include <iostream>

namespace clang {
namespace tidy {
namespace caos {

class CaosModule : public ClangTidyModule {
public:
  void addCheckFactories(ClangTidyCheckFactories &CheckFactories) override {
    CheckFactories.registerCheck<MagicNumbersCheck>(
        "caos-magic-numbers");
  }
};

} // namespace caos

// Register the CaosModule using this statically initialized variable.
static ClangTidyModuleRegistry::Add<caos::CaosModule>
X("caos-module", "Adds custom checks for CAOS course");

} // namespace tidy
} // namespace clang
