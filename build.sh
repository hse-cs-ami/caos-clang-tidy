#!/bin/bash

set -eu

cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cd build
make -j2 clangTidyPluginModule
