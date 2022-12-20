#!/bin/bash

set -eu

LLVM_VERSION=${LLVM_VERSION:-15.0.6}

wget "https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/clang-tools-extra-${LLVM_VERSION}.src.tar.xz" -O clang-tools-extra.tar.xz
tar xf clang-tools-extra.tar.xz
rm clang-tools-extra.tar.xz

for file in $(find clang-tidy -name '*.h'); do
    cp clang-tools-extra*/$file $file
done

rm -r clang-tools-extra*
