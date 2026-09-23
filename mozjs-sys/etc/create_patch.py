#!/usr/bin/env python3

import argparse
import io
import pathlib
import re
import subprocess
import sys
import tarfile
import tempfile

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent
PATCH_DIR = SCRIPT_DIR / "patches"
TARGET = pathlib.Path("mozjs-sys/mozjs")
PATCH_PREFIX_RE = re.compile(r"^(\d+)-")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Create a mozjs patch file from a commit, filtering the diff to "
            "mozjs-sys/mozjs and formatting it for mozjs-sys/etc/update.py."
        )
    )
    parser.add_argument("commit", help="commit to export")
    parser.add_argument(
        "-o",
        "--output",
        type=pathlib.Path,
        help="patch path to write; defaults to the next numbered file in mozjs-sys/etc/patches",
    )
    parser.add_argument(
        "--stdout",
        action="store_true",
        help="write the patch to stdout instead of a file",
    )
    parser.add_argument(
        "--no-check",
        action="store_true",
        help="skip git apply --check validation",
    )
    return parser.parse_args()


def git(*args: str) -> str:
    return subprocess.check_output(["git", *args], text=True, cwd=REPO_ROOT)


def ensure_single_parent(commit: str) -> tuple[str, str]:
    rev = git("rev-parse", "--verify", commit).strip()
    parents = git("show", "--no-patch", "--format=%P", rev).strip().split()
    if len(parents) != 1:
        raise SystemExit("commit must have exactly one parent")
    return rev, parents[0]


def next_patch_number() -> int:
    highest = 0
    for path in PATCH_DIR.glob("*.patch"):
        match = PATCH_PREFIX_RE.match(path.name)
        if match:
            highest = max(highest, int(match.group(1)))
    return highest + 1


def default_output_path(commit: str) -> pathlib.Path:
    subject = git("show", "--no-patch", "--format=%f", commit).strip()
    patch_number = next_patch_number()
    # If we ever hit this, we probably should just increase the patch formatting to 5 digits.
    if patch_number > 9999:
        raise RuntimeError("too many patches already in mozjs-sys/etc/patches. Please adapt the script patch formatting.")
    return PATCH_DIR / f"{patch_number:04d}-{subject}.patch"


def build_patch(commit: str, parent: str) -> str:
    return git(
        "diff",
        "--patch",
        "--full-index",
        "--binary",
        f"--relative={TARGET}",
        parent,
        commit,
        "--",
        str(TARGET),
    )


def validate_patch(patch_path: pathlib.Path, parent: str) -> None:
    try:
        with tempfile.NamedTemporaryFile("w", suffix=".tar") as archive_file:
            subprocess.check_call(
                ["git", "archive", "--format=tar", f"--output={archive_file.name}", parent, str(TARGET)],
                cwd=REPO_ROOT,
            )
            with tempfile.TemporaryDirectory(prefix="mozjs-patch-check-") as tempdir:
                with tarfile.open(name=archive_file.name, mode="r") as tar:
                    tar.extractall(tempdir)

                subprocess.check_call(
                    [
                        "git",
                        "apply",
                        "--check",
                        "--directory=" + str(TARGET),
                        str(patch_path.resolve()),
                    ],
                    cwd=tempdir,
                )
    except subprocess.CalledProcessError as e:
        print(f"patch {patch_path} is invalid: {e}", file=sys.stderr)
        print("Check the patch and manually remove it before rerunning the script", file=sys.stderr)


def main() -> int:
    args = parse_args()
    commit, parent = ensure_single_parent(args.commit)
    patch = build_patch(commit, parent)
    if not patch.strip():
        raise SystemExit(f"commit {commit} has no changes under {TARGET}")

    if args.stdout:
        sys.stdout.write(patch)
        return 0
    else:
        output = args.output or default_output_path(commit)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(patch)

    if not args.no_check:
        validate_patch(output, parent)

    print(output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
