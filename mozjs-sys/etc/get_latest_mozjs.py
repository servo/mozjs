#!/usr/bin/env python3

import os
import sys
import re
import requests

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from get_taskcluster_mozjs import download_from_taskcluster, ESR, REPO


# Returns minor, patch, tag, changeset.
def get_latest_mozjs_tag_changeset() -> tuple[str, str, str, str]:
    # Obtain latest released tag
    response = requests.get(f"https://hg.mozilla.org/releases/{REPO}/json-tags")
    response.raise_for_status()

    tags = response.json()["tags"]

    matching = []
    for tag_info in tags:
        tag: str = tag_info["tag"]
        if re.match(rf"^FIREFOX_{ESR}_.*esr_RELEASE$", tag):
            minor_patch = tag.removeprefix(f"FIREFOX_{ESR}_").removesuffix("esr_RELEASE").split("_", 1)
            minor = minor_patch[0]
            if len(minor_patch) == 2:
                patch = minor_patch[1]
            elif len(minor_patch) == 1:
                patch = "0"
            else:
                raise ValueError(f"Invalid tag format: {tag}")
            matching.append(
                (
                    minor,
                    patch,
                    tag,
                    tag_info["node"],
                )
            )

    if not matching:
        print("Error: No matching FIREFOX_*_RELEASE tags found")
        sys.exit(1)

    # Sort by version (first minor, then patch) and get the latest
    matching.sort(key=lambda x: (int(x[0]), int(x[1])))
    minor, patch, tag, changeset = matching[-1]

    return minor, patch, tag, changeset


if __name__ == "__main__":
    minor, patch, tag, changeset = get_latest_mozjs_tag_changeset()
    print(f"Latest tag: {tag}, changeset: {changeset}")

    download_from_taskcluster(changeset)
