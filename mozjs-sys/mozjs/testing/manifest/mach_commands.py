# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# ruff linter deprecates List, required for Python 3.8 compatibility
from typing import List, Optional  # noqa UP035

from mach.decorators import Command, CommandArgument, SubCommand


@Command(
    "manifest",
    category="testing",
    description="Manifest operations",
    virtualenv_name="manifest",
)
def manifest(_command_context):
    """
    All functions implemented as subcommands.
    """


@SubCommand(
    "manifest",
    "skip-fails",
    description="Update manifests to skip failing tests",
)
@CommandArgument("try_url", nargs=1, help="Treeherder URL for try (please use quotes)")
@CommandArgument(
    "-b",
    "--bugzilla",
    default=None,
    help="Bugzilla instance [disable]",
)
@CommandArgument(
    "-C",
    "--clear-cache",
    nargs="?",
    const="all",
    default=None,
    help="clear cache REVISION (or all)",
)
@CommandArgument(
    "-c",
    "--carryover",
    action="store_true",
    help="Set carryover mode (only skip failures for platform matches)",
)
@CommandArgument(
    "-d",
    "--dry-run",
    action="store_true",
    help="Determine manifest changes, but do not write them",
)
@CommandArgument(
    "-F",
    "--use-failures",
    default=None,
    help="Use failures from file",
)
@CommandArgument(
    "-f",
    "--save-failures",
    default=None,
    help="Save failures to file",
)
@CommandArgument(
    "-I",
    "--implicit-vars",
    action="store_true",
    help="Use implicit variables in reftest manifests",
)
@CommandArgument(
    "-i",
    "--task-id",
    default=None,
    help="Task id to write a condition for instead of all tasks from the push",
)
@CommandArgument(
    "-k",
    "--known-intermittents",
    action="store_true",
    help="Set known intermittents mode (only skip failures known intermittents)",
)
@CommandArgument(
    "-M",
    "--max-failures",
    type=int,
    default=-1,
    help="Maximum number of failures to skip (-1 == no limit)",
)
@CommandArgument("-m", "--meta-bug-id", type=int, default=None, help="Meta Bug id")
@CommandArgument(
    "-n",
    "--new-version",
    default=None,
    help="New version to use for annotations",
)
@CommandArgument(
    "-N",
    "--new-failures",
    action="store_true",
    help="Set new failures mode (only add conditions for new failures)",
)
@CommandArgument(
    "-r",
    "--failure-ratio",
    type=float,
    default=0.4,
    help="Ratio of test failures/total to skip [0.4]",
)
@CommandArgument(
    "-R",
    "--replace-tbd",
    action="store_true",
    help="Replace Bug TBD in manifests by filing new bugs",
)
@CommandArgument(
    "-s",
    "--turbo",
    action="store_true",
    help="Skip all secondary failures",
)
@CommandArgument("-T", "--use-tasks", default=None, help="Use tasks from file")
@CommandArgument("-t", "--save-tasks", default=None, help="Save tasks to file")
@CommandArgument(
    "-u",
    "--user-agent",
    default=None,
    help="User-Agent to use for mozci if queries are forbidden from treeherder",
)
@CommandArgument("-v", "--verbose", action="store_true", help="Verbose mode")
def skipfails(
    command_context,
    try_url,
    bugzilla: Optional[str] = None,
    meta_bug_id: Optional[int] = None,
    turbo: bool = False,
    save_tasks: Optional[str] = None,
    use_tasks: Optional[str] = None,
    save_failures: Optional[str] = None,
    use_failures: Optional[str] = None,
    max_failures: int = -1,
    verbose: bool = False,
    dry_run: bool = False,
    implicit_vars: bool = False,
    new_version: Optional[str] = None,
    task_id: Optional[str] = None,
    user_agent: Optional[str] = None,
    failure_ratio: float = 0.4,
    clear_cache: Optional[str] = None,
    carryover: bool = False,
    known_intermittents: bool = False,
    new_failures: bool = False,
    replace_tbd: bool = False,
):
    from skipfails import Skipfails, SkipfailsMode

    mode: int = SkipfailsMode.from_flags(
        carryover,
        known_intermittents,
        new_failures,
        replace_tbd,
    )
    Skipfails(
        command_context,
        try_url,
        verbose,
        bugzilla,
        dry_run,
        turbo,
        implicit_vars,
        new_version,
        task_id,
        user_agent,
        clear_cache,
    ).run(
        meta_bug_id,
        save_tasks,
        use_tasks,
        save_failures,
        use_failures,
        max_failures,
        failure_ratio,
        mode,
    )


@SubCommand(
    "manifest",
    "high-freq-skip-fails",
    description="Update manifests to skip failing tests",
)
@CommandArgument(
    "-f",
    "--failures",
    default="30",
    dest="failures",
    help="Minimum number of failures for the bug to be skipped",
)
@CommandArgument(
    "-d",
    "--days",
    default="7",
    dest="days",
    help="Number of days to look for failures since now",
)
def high_freq_skipfails(command_context, failures: str, days: str):
    from high_freq_skipfails import HighFreqSkipfails

    try:
        failures_num = int(failures)
    except ValueError:
        failures_num = 30
    try:
        days_num = int(days)
    except ValueError:
        days_num = 7
    HighFreqSkipfails(command_context, failures_num, days_num).run()


@SubCommand(
    "manifest",
    "clean-skip-fails",
    description="Update manifests to remove skip-if conditions for a specific platform. Only works for TOML manifests.",
)
@CommandArgument(
    "manifest_search_path",
    nargs=1,
    help="Path to the folder containing the manifests to update, or the path to a single manifest",
)
@CommandArgument(
    "-o",
    "--os",
    default=None,
    dest="os_name",
    help="OS to remove (linux, mac, win)",
)
@CommandArgument(
    "-s",
    "--os_version",
    default=None,
    dest="os_version",
    help="Version of the OS to remove (eg: 18.04 for linux)",
)
@CommandArgument(
    "-p",
    "--processor",
    default=None,
    dest="processor",
    help="Type of processor architecture to remove (eg: x86)",
)
def clean_skipfails(
    command_context,
    manifest_search_path: List[str],  # noqa UP006
    os_name: Optional[str] = None,
    os_version: Optional[str] = None,
    processor: Optional[str] = None,
):
    from clean_skipfails import CleanSkipfails

    CleanSkipfails(
        command_context, manifest_search_path[0], os_name, os_version, processor
    ).run()
