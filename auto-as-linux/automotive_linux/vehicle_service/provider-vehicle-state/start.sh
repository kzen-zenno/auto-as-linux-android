#!/bin/bash

set -e

SRC_DIR=/app/src
BUILD_DIR=/app/build

conan profile new default --detect --force
conan profile update settings.compiler.libcxx=libstdc++11 default

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

conan install "$SRC_DIR" \
    --build=missing \
    -s build_type=Release

source activate.sh

cmake -S "$SRC_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release

cmake --build "$BUILD_DIR" --target someip_feeder --target vsomeip3-cfg --target vsomeip3-sd -j"$(nproc)"

export LD_LIBRARY_PATH="$BUILD_DIR/_deps/vsomeip3-build:${LD_LIBRARY_PATH}"

exec "$BUILD_DIR/src/someip_feeder/someip_feeder"
