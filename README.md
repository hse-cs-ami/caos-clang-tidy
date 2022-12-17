Custom clang-tidy module for HSE AMI CAOS course (2022-2023).

This repository is based on [AliceO2Group/O2CodeChecker](https://github.com/AliceO2Group/O2CodeChecker)

## Building

### Prerequisites

- clang-15
- clang-tidy-15
- libclang-15-dev

```shell
./prepare.sh
./build.sh
```

## Running

```shell
LD_PRELOAD=$PWD/build/caos/libclangTidyCaosModule.so clang-tidy --config="{WarningsAsErrors: '*'}" --checks="caos-magic-numbers" test/main.c
```
