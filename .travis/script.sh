#!/usr/bin/env bash

set -eux

cd "$(dirname $0)/../mozjs/js/rust"

# Clear the ccache statistics.
ccache -z

# Build and test with `-vv`, aka "very verbose", to keep Travis CI from killing
# us due to no output (we can't use `travis_wait` here, as its an alias that
# isn't available to child shells). It's also useful when debugging, of course.
cargo build -vv $FEATURES
cargo test -vv $FEATURES

# Dump out the ccache statistics.
ccache -s
