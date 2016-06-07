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

cd ../glog
git clean -fdx
autoreconf --force --install --verbose .
./configure --enable-shared=false \
	LDFLAGS="-L$(cd ../gflags/lib && pwd)" \
	CPPFLAGS="-DGOOGLE_GLOG_DLL_DECL= -I$(cd ../gflags/include && pwd)"
make
mkdir dst && make DESTDIR="$(pwd)/dst" install
make clean
git checkout -- .

