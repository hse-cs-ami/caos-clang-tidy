#!/bin/bash

set -eu

# g++-11 doesn't work, because it uses a mangling scheme different from the one used in libclangTidy.a
# (clang::tidy::ClangTidyCheck::OptionsView::(store,get)<bool> are not found by linker)
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cd build
make -j $(nproc) clangTidyCaosModule
