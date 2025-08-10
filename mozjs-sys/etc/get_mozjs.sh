#!/usr/bin/env bash

set -o errexit
set -o nounset
set -o pipefail

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
# get commit and appropriate mozjs tar
COMMIT=$( cat $SCRIPT_DIR/COMMIT )
echo "Commit $COMMIT"
tar_file=$(curl -L -H "Accept: application/vnd.github+json" -H "X-GitHub-Api-Version: 2022-11-28" https://api.github.com/repos/servo/mozjs/releases/tags/mozjs-source-$COMMIT | jq -r '.assets[] | select(.name | contains("tar.xz")) | .browser_download_url')
echo "Tar at $tar_file"
curl -L --output mozjs.tar.xz $tar_file
