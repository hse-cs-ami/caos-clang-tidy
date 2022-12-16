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
#include "FooCheck.h"
#include "MagicNumbersCheck.h"
#include <iostream>

namespace clang {
namespace tidy {
namespace plugin {

class PluginModule : public ClangTidyModule {
public:
  void addCheckFactories(ClangTidyCheckFactories &CheckFactories) override {
    CheckFactories.registerCheck<FooCheck>(
        "plugin-Foo");
    CheckFactories.registerCheck<MagicNumbersCheck>(
        "plugin-magic-numbers");
  }
};

} // namespace plugin

// Register the PluginTidyModule using this statically initialized variable.
static ClangTidyModuleRegistry::Add<plugin::PluginModule>
X("pluginO2-module", "Adds Plugin specific checks");

} // namespace tidy
} // namespace clang
