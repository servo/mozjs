#!/usr/bin/env bash

# We always want backtraces for everything.
export RUST_BACKTRACE=1

if [[ "$TRAVIS_OS_NAME" == "osx" ]]; then
    brew install autoconf@2.13 ccache llvm@3.9 yasm
    export LIBCLANG_PATH=$(find /usr/local/Cellar/llvm -type f -name libclang.dylib)
    export LIBCLANG_PATH=$(dirname $LIBCLANG_PATH)
elif [[ "$TRAVIS_OS_NAME" == "linux" ]]; then
    export CC="gcc-5"
    export CXX="g++-5"
    export LIBCLANG_PATH=/usr/lib/llvm-3.9/lib
else
    echo "Error: unknown \$TRAVIS_OS_NAME: $TRAVIS_OS_NAME"
    exit 1
fi

export CCACHE=$(which ccache)
