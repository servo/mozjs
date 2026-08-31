# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import subprocess
import tempfile

from mach.func_cache import mach_func_cache


def _extract_resources(tasks_data):
    resources = set()
    for t in tasks_data.values():
        resources.update(t.get("attributes", {}).get("toolchain-resources", []))
    return sorted(resources)


@mach_func_cache(
    inputs=["taskcluster"],
    dynamic_inputs=_extract_resources,
    env_vars=["TASKCLUSTER_ROOT_URL", "MOZ_SCM_LEVEL"],
)
def toolchain_task_definitions():
    root_dir = os.path.join(os.path.dirname(__file__), "..", "..", "..")
    mach = os.path.join(root_dir, "mach")

    env = os.environ.copy()
    env.pop("MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE", None)
    params = {"level": os.environ.get("MOZ_SCM_LEVEL", "3"), "files_changed": []}

    import sys

    with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as f:
        output_file = f.name

    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".json", delete=False
    ) as params_file:
        params_path = params_file.name
        json.dump(params, params_file)

    try:
        result = subprocess.run(
            [
                sys.executable,
                mach,
                "taskgraph",
                "tasks",
                "-k",
                "fetch",
                "-k",
                "toolchain",
                "-J",
                "--output-file",
                output_file,
                "-p",
                params_path,
            ],
            check=False,
            cwd=root_dir,
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        if result.returncode != 0:
            raise RuntimeError(
                f"mach taskgraph failed in toolchain.py (exit {result.returncode})"
            )

        with open(output_file) as f:
            tasks_data = json.load(f)
    finally:
        os.unlink(output_file)
        os.unlink(params_path)
    for label, data in tasks_data.items():
        data["label"] = label
        data["kind"] = data["attributes"]["kind"]

    aliased = {}
    for label, t in tasks_data.items():
        aliases = t["attributes"].get(f"{t['kind']}-alias")
        if not aliases:
            aliases = []
        if isinstance(aliases, str):
            aliases = [aliases]
        for alias in aliases:
            aliased[f"{t['kind']}-{alias}"] = t
    tasks_data.update(aliased)

    return tasks_data
