#!/usr/bin/env python3

import sys
import os
import tarfile

import requests
from urllib.parse import quote
from urllib.request import urlretrieve

ESR = 140
REPO = f"mozilla-esr{ESR}"
HEADERS = {"User-Agent": "mozjs-sys/1.0 (https://github.com/servo/mozjs)"}


def download_artifact(task_id: str, artifact_name: str, dl_name: str, i=0):
    response = requests.get(
        f"https://firefox-ci-tc.services.mozilla.com/api/queue/v1/task/{task_id}/runs/{i}/artifacts",
    )
    response.raise_for_status()
    artifacts = response.json()["artifacts"]
    if not artifacts:
        if i < 5:
            download_artifact(task_id, artifact_name, dl_name, i + 1)
            return
        else:
            print(f"Error: No artifacts found for task {task_id} after {i} attempts")
            sys.exit(1)
    file = None
    for artifact in artifacts:
        if artifact_name in artifact["name"]:
            file = artifact["name"]
            break

    if file is None:
        print(f"Error: Could not find {artifact_name} artifact")
        sys.exit(1)

    url = f"https://firefox-ci-tc.services.mozilla.com/api/queue/v1/task/{task_id}/runs/{i}/artifacts/{file}"
    print(f"Downloading: {url}")

    urlretrieve(url, dl_name)


def find_sm_pkg_and_hazard_task_in_push(push_id: int) -> tuple[str | None, str | None]:
    response = requests.get(
        f"https://treeherder.mozilla.org/api/jobs/?push_id={push_id}",
        headers=HEADERS,
    )
    response.raise_for_status()
    sm_pkg_task_id = None
    hazard_task_id = None
    for result in response.json()["results"]:
        if "spidermonkey-sm-package-linux64/opt" in result:
            sm_pkg_task_id = result[14]
        elif "hazard-linux64-haz/debug" in result:
            hazard_task_id = result[14]
    return sm_pkg_task_id, hazard_task_id


def get_ancestor_revisions(commit: str, limit: int) -> set[str]:
    revset = quote(f"ancestors({commit})")
    response = requests.get(
        f"https://hg.mozilla.org/releases/{REPO}/json-log"
        f"?rev={revset}&limit={limit}",
        headers=HEADERS,
    )
    response.raise_for_status()
    return {entry["node"] for entry in response.json().get("entries", [])}


def get_previous_pushes(commit: str, count: int):
    # Multiple changesets can map to the same push, so we fetch more.
    ancestors = get_ancestor_revisions(commit, limit=count * 20)

    response = requests.get(
        f"https://treeherder.mozilla.org/api/project/{REPO}/push/?revision={commit}",
        headers=HEADERS,
    )
    response.raise_for_status()
    initial_push = response.json()["results"][0]
    push_timestamp = initial_push["push_timestamp"]

    response = requests.get(
        f"https://treeherder.mozilla.org/api/project/{REPO}/push/"
        f"?push_timestamp__lte={push_timestamp}&count={count * 4}",
        headers=HEADERS,
    )
    response.raise_for_status()
    all_pushes = response.json()["results"]

    # Keep only pushes whose tip changeset is an ancestor of `commit`.
    on_branch = [p for p in all_pushes if p["revision"] in ancestors]
    return on_branch[:count]

def read_milestone_from_tarball(tarball_path: str) -> str:
    """Extract `config/milestone.txt` from the SM source tarball and return
    the milestone version string (e.g. '140.9.0')."""
    with tarfile.open(tarball_path, "r:*") as tar:
        milestone_member = None
        for member in tar:
            if member.isfile() and member.name.endswith("/config/milestone.txt"):
                milestone_member = member
                break
        if milestone_member is None:
            raise RuntimeError(
                f"config/milestone.txt not found in {tarball_path}"
            )
        f = tar.extractfile(milestone_member)
        if f is None:
            raise RuntimeError(
                f"Could not read {milestone_member.name} from {tarball_path}"
            )
        content = f.read().decode("utf-8")

    for line in content.splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            return line
    raise RuntimeError(
        f"Could not parse milestone version from {milestone_member.name}"
    )


def verify_tarball_version(tarball_path: str, expected_version: str) -> None:
    """Verify the tarball's milestone matches the expected tag version."""
    milestone = read_milestone_from_tarball(tarball_path)
    if milestone != expected_version:
        raise RuntimeError(
            f"SpiderMonkey version mismatch: tarball milestone is {milestone} "
            f"but the resolved tag implies {expected_version}."
        )
    print(f"Milestone verified: tarball is SpiderMonkey {milestone}")


def download_from_taskcluster(commit: str, look_back_for_artifacts: int = 50):
    pushes = get_previous_pushes(commit, look_back_for_artifacts + 1)

    sm_pkg_task_id = None
    hazard_task_id = None

    # SM tasks are only run for pushes that modify SM related files:
    # https://searchfox.org/firefox-main/rev/98bf4b92d3f5d7a9855281df4bf333210bcfbbc4/taskcluster/kinds/spidermonkey/kind.yml#30-60
    # so we need to find last commit that had such task
    for i, push in enumerate(pushes):
        push_id = push["id"]
        push_revision = push["revision"][:12]
        if i == 0:
            print(f"Checking initial push {push_id} ({push_revision})...")
        else:
            print(
                f"No SpiderMonkey tasks found, checking previous push {push_id} ({push_revision})..."
            )

        sm_pkg_task_id, hazard_task_id = find_sm_pkg_and_hazard_task_in_push(push_id)

        if sm_pkg_task_id is not None and hazard_task_id is not None:
            print(f"Found tasks in push {push_id} ({push_revision})")
            break

    if sm_pkg_task_id is None:
        print(
            f"Error: Could not find spidermonkey-sm-package-linux64/opt task after checking {len(pushes)} pushes"
        )
        sys.exit(1)
    else:
        print(f"Spidermonkey package task id {sm_pkg_task_id}")
        download_artifact(sm_pkg_task_id, "tar.xz", "mozjs.tar.xz")
    if hazard_task_id is None:
        print("Error: Could not find hazard-linux64-haz/debug task")
        sys.exit(1)
    else:
        print(f"Hazard task id {hazard_task_id}")
        download_artifact(hazard_task_id, "allFunctions.txt.gz", "allFunctions.txt.gz")
        download_artifact(hazard_task_id, "gcFunctions.txt.gz", "gcFunctions.txt.gz")


if __name__ == "__main__":
    script_dir = os.path.dirname(os.path.abspath(__file__))
    commit_file = os.path.join(script_dir, "COMMIT")
    with open(commit_file, "r") as f:
        commit = f.read().strip()
    print(f"Commit: {commit}")
    download_from_taskcluster(commit)
