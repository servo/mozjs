# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Support for running tasks that are invoked via the `run-task` script.
"""

import dataclasses
import os

from voluptuous import Any, Optional, Required

from taskgraph.transforms.run import run_task_using
from taskgraph.transforms.run.common import (
    support_caches,
    support_vcs_checkout,
)
from taskgraph.transforms.task import taskref_or_string
from taskgraph.util import path, taskcluster
from taskgraph.util.caches import CACHES
from taskgraph.util.schema import Schema

EXEC_COMMANDS = {
    "bash": ["bash", "-cx"],
    "powershell": ["powershell.exe", "-ExecutionPolicy", "Bypass"],
}

run_task_schema = Schema(
    {
        Required("using"): "run-task",
        # Which caches to use. May take a boolean in which case either all
        # (True) or no (False) caches will be used. Alternatively, it can
        # accept a list of caches to enable. Defaults to only the checkout cache
        # enabled.
        Optional("use-caches", "caches"): Any(bool, list(CACHES.keys())),
        # if true (the default), perform a checkout on the worker
        Required("checkout"): Any(bool, {str: dict}),
        Optional(
            "cwd",
            description="Path to run command in. If a checkout is present, the path "
            "to the checkout will be interpolated with the key `checkout`",
        ): str,
        # The sparse checkout profile to use. Value is the filename relative to the
        # directory where sparse profiles are defined (build/sparse-profiles/).
        Required("sparse-profile"): Any(str, None),
        # The command arguments to pass to the `run-task` script, after the
        # checkout arguments.  If a list, it will be passed directly; otherwise
        # it will be included in a single argument to the command specified by
        # `exec-with`.
        Required("command"): Any([taskref_or_string], taskref_or_string),
        # What to execute the command with in the event command is a string.
        Optional("exec-with"): Any(*list(EXEC_COMMANDS)),
        # Command used to invoke the `run-task` script. Can be used if the script
        # or Python installation is in a non-standard location on the workers.
        Optional("run-task-command"): list,
        # Base work directory used to set up the task.
        Required("workdir"): str,
        # Whether to run as root. (defaults to False)
        Optional("run-as-root"): bool,
    }
)


def common_setup(config, task, taskdesc, command):
    run = task["run"]
    if run["checkout"]:
        repo_configs = config.repo_configs
        if len(repo_configs) > 1 and run["checkout"] is True:
            raise Exception("Must explicitly specify checkouts with multiple repos.")
        elif run["checkout"] is not True:
            repo_configs = {
                repo: dataclasses.replace(repo_configs[repo], **config)
                for (repo, config) in run["checkout"].items()
            }

        vcs_path = support_vcs_checkout(
            config,
            task,
            taskdesc,
            repo_configs=repo_configs,
            sparse=bool(run["sparse-profile"]),
        )

        for repo_config in repo_configs.values():
            checkout_path = path.join(vcs_path, repo_config.path)
            command.append(f"--{repo_config.prefix}-checkout={checkout_path}")

        if run["sparse-profile"]:
            command.append(
                "--{}-sparse-profile=build/sparse-profiles/{}".format(
                    repo_config.prefix,  # type: ignore
                    run["sparse-profile"],
                )
            )

        if "cwd" in run:
            run["cwd"] = path.normpath(run["cwd"].format(checkout=vcs_path))
    elif "cwd" in run and "{checkout}" in run["cwd"]:
        raise Exception(
            "Found `{{checkout}}` interpolation in `cwd` for task {name} "
            "but the task doesn't have a checkout: {cwd}".format(
                cwd=run["cwd"], name=task.get("name", task.get("label"))
            )
        )

    if "cwd" in run:
        command.extend(("--task-cwd", run["cwd"]))

    support_caches(config, task, taskdesc)
    taskdesc["worker"].setdefault("env", {})["MOZ_SCM_LEVEL"] = config.params["level"]


worker_defaults = {
    "checkout": True,
    "sparse-profile": None,
    "run-as-root": False,
}


def script_url(config, script):
    if "MOZ_AUTOMATION" in os.environ and "TASK_ID" not in os.environ:
        raise Exception("TASK_ID must be defined to use run-task on generic-worker")
    task_id = os.environ.get("TASK_ID", "<TASK_ID>")
    # use_proxy = False to avoid having all generic-workers turn on proxy
    # Assumes the cluster allows anonymous downloads of public artifacts
    tc_url = taskcluster.get_root_url(False)
    # TODO: Use util/taskcluster.py:get_artifact_url once hack for Bug 1405889 is removed
    return f"{tc_url}/api/queue/v1/task/{task_id}/artifacts/public/{script}"


@run_task_using(
    "docker-worker", "run-task", schema=run_task_schema, defaults=worker_defaults
)
def docker_worker_run_task(config, task, taskdesc):
    run = task["run"]
    worker = taskdesc["worker"] = task["worker"]
    command = run.pop("run-task-command", ["/usr/local/bin/run-task"])
    common_setup(config, task, taskdesc, command)

    run_command = run["command"]

    # dict is for the case of `{'task-reference': str}`.
    if isinstance(run_command, str) or isinstance(run_command, dict):
        exec_cmd = EXEC_COMMANDS[run.pop("exec-with", "bash")]
        run_command = exec_cmd + [run_command]
    if run["run-as-root"]:
        command.extend(("--user", "root", "--group", "root"))
    command.append("--")
    command.extend(run_command)
    worker["command"] = command


@run_task_using(
    "generic-worker", "run-task", schema=run_task_schema, defaults=worker_defaults
)
def generic_worker_run_task(config, task, taskdesc):
    run = task["run"]
    worker = taskdesc["worker"] = task["worker"]
    is_win = worker["os"] == "windows"
    is_bitbar = worker["os"] == "linux-bitbar"

    command = run.pop("run-task-command", None)
    if not command:
        if is_win:
            command = ["C:/mozilla-build/python3/python3.exe", "run-task"]
        else:
            command = ["./run-task"]

    common_setup(config, task, taskdesc, command)

    worker.setdefault("mounts", [])
    worker["mounts"].append(
        {
            "content": {
                "url": script_url(config, "run-task"),
            },
            "file": "./run-task",
        }
    )
    if worker.get("env", {}).get("MOZ_FETCHES"):
        worker["mounts"].append(
            {
                "content": {
                    "url": script_url(config, "fetch-content"),
                },
                "file": "./fetch-content",
            }
        )

    run_command = run["command"]

    if isinstance(run_command, str):
        if is_win:
            run_command = f'"{run_command}"'
        exec_cmd = EXEC_COMMANDS[run.pop("exec-with", "bash")]
        run_command = exec_cmd + [run_command]

    if run["run-as-root"]:
        command.extend(("--user", "root", "--group", "root"))
    command.append("--")
    if is_bitbar:
        # Use the bitbar wrapper script which sets up the device and adb
        # environment variables
        command.append("/builds/taskcluster/script.py")
    command.extend(run_command)

    if is_win:
        worker["command"] = [" ".join(command)]
    else:
        worker["command"] = [
            ["chmod", "+x", "run-task"],
            command,
        ]
