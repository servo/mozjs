#!/usr/bin/env bash

set -eo pipefail

script_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

# # Get github repository url
gh_url='https://github.'$(cd "$script_dir" && git remote get-url origin | cut -f2 -d. | tr ':' /)

mode="release"
if [[ $1 == "debug" ]]
then
  mode="debug"
fi

git_rev="$(git -C "$script_dir" rev-parse HEAD)"
file="spidermonkey-wasm-static-lib_${mode}.tar.gz"
bundle_url="${gh_url}/releases/download/rev_${git_rev}/${file}"

curl --fail -L -O "$bundle_url"
tar xf "$file"
rm "$file"
