#!/usr/bin/env python3

import sys
import os
import requests
from urllib.request import urlretrieve

ESR = 140
REPO = f"mozilla-esr{ESR}"
HEADERS = {"User-Agent": "Mozilla/5.0 (X11; Linux x86_64) mozjs-sys/1.0"}


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


def get_previous_pushes(commit: str, count: int):
    response = requests.get(
        f"https://treeherder.mozilla.org/api/project/{REPO}/push/?revision={commit}",
        headers=HEADERS,
    )
    response.raise_for_status()
    initial_push = response.json()["results"][0]
    push_timestamp = initial_push["push_timestamp"]

    # this is how treeherders get n more pushes works
    response = requests.get(
        f"https://treeherder.mozilla.org/api/project/{REPO}/push/?push_timestamp__lte={push_timestamp}&count={count}",
        headers=HEADERS,
    )
    response.raise_for_status()
    return response.json()["results"]


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
