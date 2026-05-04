#!/usr/bin/env python3

import os
import sys
import subprocess

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from get_latest_mozjs import get_latest_mozjs_tag_changeset
from get_taskcluster_mozjs import download_from_taskcluster, ESR, REPO, verify_tarball_version
from get_mozjs import download_gh_artifact
from update import main

script_dir = os.path.dirname(os.path.abspath(__file__))

minor, patch, tag, changeset = get_latest_mozjs_tag_changeset()
print(f"Latest tag: {tag}, changeset: {changeset}")

try:
    subprocess.check_call(
        ["gh", "release", "view", f"mozjs-source-{changeset}", "--repo", "servo/mozjs"],
    )
    print(f"Release mozjs-source-{changeset} already exists, skipping SM bumps")
    sys.exit(0)
except subprocess.CalledProcessError:
    pass

if GITHUB_OUTPUT := os.getenv("GITHUB_OUTPUT"):
    with open(GITHUB_OUTPUT, "a") as github_output_file:
        print(f"tag={tag}", file=github_output_file)
        print(f"changeset={changeset}", file=github_output_file)
        print(f"version={ESR}.{minor}.{patch}", file=github_output_file)
        print(f"esr={ESR}", file=github_output_file)


download_from_taskcluster(changeset)

verify_tarball_version("mozjs.tar.xz", f"{ESR}.{minor}.{patch}")

subprocess.check_call(
    [
        "gh",
        "release",
        "create",
        f"mozjs-source-{changeset}",
        "mozjs.tar.xz",
        "allFunctions.txt.gz",
        "gcFunctions.txt.gz",
        "--repo",
        "servo/mozjs",
        "--title",
        f"SpiderMonkey {tag}",
        "--latest=false",
        "--notes",
        f"Source code for SpiderMonkey {tag} (changeset: [{changeset}](https://hg.mozilla.org/releases/{REPO}/rev/{changeset}))",
    ]
)

os.remove("mozjs.tar.xz")
os.remove("allFunctions.txt.gz")
os.remove("gcFunctions.txt.gz")

commit_file = os.path.join(script_dir, "COMMIT")
with open(commit_file, "w") as f:
    f.write(changeset)

subprocess.check_call(["git", "add", f"{commit_file}"])
subprocess.check_call(["git", "commit", "-m", "Update COMMIT", "--signoff"])

download_gh_artifact("mozjs.tar.xz")

main(["mozjs.tar.xz"])

os.remove("mozjs.tar.xz")

subprocess.check_call(["git", "add", "mozjs-sys/mozjs"])
subprocess.check_call(["git", "commit", "-m", "Apply patches", "--signoff"])

version = f"{ESR}.{minor}.{patch}-0"
print(f"Updating to version {version}")

cargo_toml_file = os.path.join(script_dir, "..", "Cargo.toml")
with open(cargo_toml_file, "r") as f:
    cargo_toml = f.readlines()
    for i in range(len(cargo_toml)):
        if cargo_toml[i].startswith("version"):
            cargo_toml[i] = f'version = "{version}"\n'
            break
with open(cargo_toml_file, "w") as f:
    f.writelines(cargo_toml)

mozjs_cargo_toml_file = os.path.join(script_dir, "..", "..", "mozjs", "Cargo.toml")
with open(mozjs_cargo_toml_file, "r") as f:
    mozjs_cargo_toml = f.readlines()
    for i in range(len(mozjs_cargo_toml)):
        if mozjs_cargo_toml[i].startswith("version"):
            # Bump patch version, we assume that security bumps are backwards compatible
            major, minor, patch = (
                mozjs_cargo_toml[i]
                .removeprefix('version = "')
                .strip()
                .removesuffix('"')
                .split(".")
            )
            print(f"Current version: {major}.{minor}.{patch}")
            mozjs_cargo_toml[i] = f'version = "{major}.{minor}.{int(patch) + 1}"\n'
        if mozjs_cargo_toml[i].startswith("mozjs_sys"):
            mozjs_cargo_toml[i] = (
                f'mozjs_sys = {{ version = "={version}", path = "../mozjs-sys" }}\n'
            )
with open(mozjs_cargo_toml_file, "w") as f:
    f.writelines(mozjs_cargo_toml)

subprocess.check_call(["git", "add", cargo_toml_file, mozjs_cargo_toml_file])
subprocess.check_call(["git", "commit", "-m", f"Bump crate versions", "--signoff"])
