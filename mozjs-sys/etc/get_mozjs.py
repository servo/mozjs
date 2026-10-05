#!/usr/bin/env python3

import os
from urllib.request import urlretrieve


def download_gh_artifact(name: str):
    script_dir = os.path.dirname(os.path.abspath(__file__))
    commit_file = os.path.join(script_dir, "COMMIT")
    with open(commit_file, "r") as f:
        commit = f.read().strip()
    urlretrieve(
        f"https://github.com/servo/mozjs/releases/download/mozjs-source-{commit}/{name}",
        name,
    )


if __name__ == "__main__":
    download_gh_artifact("mozjs.tar.xz")
