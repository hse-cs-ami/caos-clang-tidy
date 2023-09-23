Custom clang-tidy module for HSE AMI CAOS course (2022-2023).

This repository is based on [AliceO2Group/O2CodeChecker](https://github.com/AliceO2Group/O2CodeChecker)

## Building

Prerequisites:
- `clang-15`
- `clang-tidy-15`
- `cmake`
- `libclang-15-dev`

```shell
./build.sh
```

## Installation (optional)
```shell
cd build
sudo make install
```

## Running

```shell
 clang-tidy-15 --load $PWD/build/caos/libclangTidyCaosModule.so \
  --config="{WarningsAsErrors: '*', CheckOptions: {caos-identifier-naming.UnionCase: CamelCase, caos-identifier-naming.StructCase: CamelCase}}" \
  --checks="caos-magic-numbers,caos-identifier-naming" \
  test/main.c
```

Or, if the plugin was installed via "make install":
```shell
# you can add this line to .bashrc
alias clang-tidy='clang-tidy-15 --load /usr/local/lib/libclangTidyCaosModule.so'

clang-tidy --config=... --checks="caos-magic-numbers,caos-identifier-naming" test/main.c

```

2023 update: `readability-identifier-naming` has been [fixed](https://github.com/llvm/llvm-project/commit/fa8e74073762300d07b02adec42c629daf82c44b) (probably will be included in 18.x release and will make `caos-identifier-naming` obsolete)
