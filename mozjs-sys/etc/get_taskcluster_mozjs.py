#!/usr/bin/env python3

import sys
import tarfile

import requests
from urllib.request import urlretrieve

ESR = 140
REPO = f"mozilla-esr{ESR}"
HEADERS = {"User-Agent": "mozjs-sys/1.0 (https://github.com/servo/mozjs)"}


def download_artifact(task_id: str, artifact_name: str, dl_name: str) -> None:
    """Download `artifact_name` from `task_id`, scanning runs newest-first.

    A task can have multiple runs (presumably due to failures) - We must walk runs
    and look for the requested artifact.
    Scanning newest-first since presumably the latest run is a successfull one.
    """
    base = f"https://firefox-ci-tc.services.mozilla.com/api/queue/v1/task/{task_id}"
    status_response = requests.get(f"{base}/status")
    status_response.raise_for_status()
    runs = status_response.json()["status"]["runs"]
    for run in reversed(runs):
        i = run["runId"]
        response = requests.get(f"{base}/runs/{i}/artifacts")
        response.raise_for_status()
        for artifact in response.json()["artifacts"]:
            if artifact_name in artifact["name"]:
                url = f"{base}/runs/{i}/artifacts/{artifact['name']}"
                print(f"Downloading: {url}")
                urlretrieve(url, dl_name)
                return
    print(
        f"Error: Could not find {artifact_name} in any of {len(runs)} run(s) "
        f"of task {task_id}"
    )
    sys.exit(1)


def find_hazard_task_in_push(push_id: int) -> str | None:
    response = requests.get(
        f"https://treeherder.mozilla.org/api/jobs/?push_id={push_id}",
        headers=HEADERS,
    )
    response.raise_for_status()
    for result in response.json()["results"]:
        if "hazard-linux64-haz/debug" in result:
            return result[14]
    return None


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


def download_hazard_artifacts_from_taskcluster(commit: str) -> None:
    response = requests.get(
        f"https://treeherder.mozilla.org/api/project/{REPO}/push/?revision={commit}",
        headers=HEADERS,
    )
    response.raise_for_status()
    results = response.json()["results"]
    if not results:
        print(f"Error: No push found on {REPO} for commit {commit}")
        sys.exit(1)
    push_id = results[0]["id"]
    print(f"Looking for hazard task in push {push_id} ({commit[:12]})...")

    hazard_task_id = find_hazard_task_in_push(push_id)
    if hazard_task_id is None:
        print(
            f"Error: Could not find hazard-linux64-haz/debug task in push "
            f"{push_id} ({commit[:12]})"
        )
        sys.exit(1)
    print(f"Hazard task id {hazard_task_id}")
    download_artifact(hazard_task_id, "allFunctions.txt.gz", "allFunctions.txt.gz")
    download_artifact(hazard_task_id, "gcFunctions.txt.gz", "gcFunctions.txt.gz")
