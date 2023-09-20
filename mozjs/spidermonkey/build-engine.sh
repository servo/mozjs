#!/usr/bin/env bash

set -euo pipefail
set -x

working_dir="$(pwd)"
script_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

mode="${1:-release}"
mozconfig="${working_dir}/mozconfig-${mode}"
objdir="obj-$mode"
outdir="$mode"

cat << EOF > "$mozconfig"
ac_add_options --enable-project=js
ac_add_options --enable-application=js
ac_add_options --target=wasm32-unknown-wasi
ac_add_options --without-system-zlib
ac_add_options --without-intl-api
ac_add_options --disable-jit
ac_add_options --disable-shared-js
ac_add_options --disable-shared-memory
ac_add_options --disable-tests
ac_add_options --disable-clang-plugin
ac_add_options --enable-jitspew
ac_add_options --enable-optimize
ac_add_options --enable-js-streams
ac_add_options --prefix=${working_dir}/${objdir}/dist
mk_add_options MOZ_OBJDIR=${working_dir}/${objdir}
mk_add_options AUTOCLOBBER=1
EOF

target="$(uname)"
case "$target" in
  Linux)
    echo "ac_add_options --disable-stdcxx-compat" >> "$mozconfig"
    ;;

  Darwin)
    echo "ac_add_options --host=aarch64-apple-darwin" >> "$mozconfig"
    ;;

  *)
    echo "Unsupported build target: $target"
    exit 1
    ;;
esac

case "$mode" in
  release)
    echo "ac_add_options --disable-debug" >> "$mozconfig"
    ;;

  debug)
    echo "ac_add_options --enable-debug" >> "$mozconfig"
    ;;

  *)
    echo "Unknown build type: $mode"
    exit 1
    ;;
esac


# Ensure the Rust version matches that used by Gecko, and can compile to WASI
rustup target add wasm32-wasi

fetch_commits=
if [[ ! -a gecko-dev ]]; then

  # Clone Gecko repository at the required revision
  mkdir gecko-dev

  git -C gecko-dev init
  git -C gecko-dev remote add --no-tags -t wasi-embedding \
    origin "$(cat "$script_dir/gecko-repository")"

  fetch_commits=1
fi

target_rev="$(cat "$script_dir/gecko-revision")"
if [[ -n "$fetch_commits" ]] || \
  [[ "$(git -C gecko-dev rev-parse HEAD)" != "$target_rev" ]]; then
  git -C gecko-dev fetch --depth 1 origin "$target_rev"
  git -C gecko-dev checkout FETCH_HEAD
fi

# Use Gecko's build system bootstrapping to ensure all dependencies are
# installed
cd gecko-dev
./mach --no-interactive bootstrap --application-choice=js --no-system-changes

# ... except, that doesn't install the wasi-sysroot, which we need, so we do
# that manually.
cd ~/.mozbuild
python3 \
  "${working_dir}/gecko-dev/mach" \
  --no-interactive \
  artifact \
  toolchain \
  --bootstrap \
  --from-build \
  sysroot-wasm32-wasi

cd "$working_dir"

# Build SpiderMonkey for WASI
MOZCONFIG="${mozconfig}" \
MOZ_FETCHES_DIR=~/.mozbuild \
CC=~/.mozbuild/clang/bin/clang \
  python3 "${working_dir}/gecko-dev/mach" \
  --no-interactive \
    build

# Copy header, object, and static lib files
rm -rf "$outdir"
mkdir -p "$outdir/lib"

cd "$objdir"
cp -Lr dist/include "../$outdir"

while read -r file; do
  cp "$file" "../$outdir/lib"
done < "$script_dir/object-files.list"

cp js/src/build/libjs_static.a "wasm32-wasi/${mode}/libjsrust.a" "../$outdir/lib"
