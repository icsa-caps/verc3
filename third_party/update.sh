#!/usr/bin/env bash
set -e

git submodule update --init

cd "${0%/*}"

# Google Test
cd googletest
git clean -fdx
cmake .
make

cd ../gflags
git clean -fdx
cmake .
make

