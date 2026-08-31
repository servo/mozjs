#!/usr/bin/env python3
"""
Build mozjs.tar.xz locally from upstream.
Since not all upstream ESR releases build mozjs.tar.xz in CI,
this script checks out the firefox source code from upstream at the relevant tag
and builds the archive using the upstream script.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path
from urllib.request import urlretrieve

REPO = "mozilla-firefox/firefox"


def download_source_tarball(tag: str, dest: Path) -> None:
    url = f"https://codeload.github.com/{REPO}/tar.gz/refs/tags/{tag}"
    print(f"Downloading: {url}")
    urlretrieve(url, dest)


def extract_tarball(tarball: Path, dest_dir: Path) -> Path:
    print(f"Extracting {tarball.name} into {dest_dir}")
    with tarfile.open(tarball, "r:gz") as tar:
        tar.extractall(dest_dir, filter="data")
    entries = [p for p in dest_dir.iterdir() if p.is_dir()]
    if len(entries) != 1:
        raise RuntimeError(
            f"Expected a single top-level directory in {tarball}, found {entries}"
        )
    return entries[0]


def run_make_source_package(source_root: Path, dist_dir: Path) -> Path:
    script = source_root / "js" / "src" / "make-source-package.py"
    if not script.is_file():
        raise RuntimeError(f"{script} not found in extracted source tree")
    env = os.environ.copy()
    env["DIST"] = str(dist_dir)
    print(f"Running {script.relative_to(source_root)}")
    subprocess.check_call([sys.executable, str(script)], env=env, cwd=str(source_root))
    matches = sorted(dist_dir.glob("mozjs-*.tar.xz"))
    if len(matches) != 1:
        raise RuntimeError(
            f"Expected a single mozjs-*.tar.xz in {dist_dir}, found {matches}"
        )
    return matches[0]


def build_sm_package_from_git(tag: str, output: Path) -> None:
    """Produce a SpiderMonkey source tarball at `output` from the upstream git
    tag `tag` (e.g. "FIREFOX_140_11_0esr_RELEASE")."""
    with tempfile.TemporaryDirectory(prefix="mozjs-git-") as tmp_str:
        tmp = Path(tmp_str)
        tarball = tmp / "firefox.tar.gz"
        download_source_tarball(tag, tarball)
        extract_dir = tmp / "src"
        extract_dir.mkdir()
        source_root = extract_tarball(tarball, extract_dir)
        tarball.unlink()
        dist_dir = tmp / "dist"
        dist_dir.mkdir()
        pkg = run_make_source_package(source_root, dist_dir)
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(pkg), str(output))
        print(f"Wrote {output}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Build a SpiderMonkey source tarball from the mozilla-firefox/firefox "
            "git repository at a given Firefox release tag."
        ),
    )
    parser.add_argument(
        "tag",
        help='Firefox release tag, e.g. "FIREFOX_140_11_0esr_RELEASE"',
    )
    parser.add_argument(
        "-o",
        "--output",
        default="mozjs.tar.xz",
        help="Output path (default: %(default)s)",
    )
    args = parser.parse_args()
    build_sm_package_from_git(args.tag, Path(args.output))


if __name__ == "__main__":
    main()
