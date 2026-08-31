# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import gzip
import json
import logging
import os
import os.path
import pprint
import re
import shutil
import sys
import tempfile
import time
import urllib.parse
from copy import deepcopy
from pathlib import Path
from statistics import median

# ruff linter deprecates Dict, List, Tuple required for Python 3.8 compatibility
from typing import Any, Callable, Dict, List, Literal, Tuple, Union, cast  # noqa UP035
from xmlrpc.client import Fault

import bugzilla
import mozci.data
import requests
from bugzilla.bug import Bug
from failedplatform import FailedPlatform
from manifestparser import ManifestParser
from manifestparser.toml import (
    Mode,
    add_skip_if,
    alphabetize_toml_str,
    replace_tbd_skip_if,
    sort_paths,
)
from mozci.push import Push
from mozci.task import Optional, TestTask
from mozci.util.taskcluster import get_task
from mozinfo.platforminfo import PlatformInfo
from taskcluster.exceptions import TaskclusterRestFailure
from wpt_path_utils import (
    WPT_META0,
    WPT_META0_CLASSIC,
    parse_wpt_path,
)
from yaml import load

# Use faster LibYAML, if installed: https://pyyaml.org/wiki/PyYAMLDocumentation
try:
    from yaml import CLoader as Loader
except ImportError:
    from yaml import Loader

ArtifactList = List[Dict[Literal["name"], str]]  # noqa UP006
CreateBug = Optional[Callable[[], Bug]]
DictStrList = Dict[str, List]  # noqa UP006
Extras = Dict[str, PlatformInfo]  # noqa UP006
FailedPlatforms = Dict[str, FailedPlatform]  # noqa UP006
GenBugComment = Tuple[  # noqa UP006
    CreateBug,  # noqa UP006
    str,  # bugid
    bool,  # meta_bug_blocked
    dict,  # attachments
    str,  # comment
    int,  # line_number
    str,  # summary
    str,  # description
    str,  # product
    str,  # component
]  # noqa UP006
JSONType = Union[
    None,
    bool,
    int,
    float,
    str,
    List["JSONType"],  # noqa UP006
    Dict[str, "JSONType"],  # noqa UP006
]
DictJSON = Dict[str, JSONType]  # noqa UP006
ListBug = List[Bug]  # noqa UP006
ListInt = List[int]  # noqa UP006
ListStr = List[str]  # noqa UP006
ManifestPaths = Dict[str, Dict[str, List[str]]]  # noqa UP006
OptBug = Optional[Bug]
BugsBySummary = Dict[str, OptBug]  # noqa UP006
OptDifferences = Optional[List[int]]  # noqa UP006
OptInt = Optional[int]
OptJs = Optional[Dict[str, bool]]  # noqa UP006
OptPlatformInfo = Optional[PlatformInfo]
OptStr = Optional[str]
OptTaskResult = Optional[Dict[str, Any]]  # noqa UP006
PlatformPermutations = Dict[  # noqa UP006
    str,  # Manifest
    Dict[  # noqa UP006
        str,  # OS
        Dict[  # noqa UP006
            str,  # OS Version
            Dict[  # noqa UP006
                str,  # Processor
                Dict[  # noqa UP006
                    str,  # Build type
                    Dict[  # noqa UP006
                        str,  # Test Variant
                        Dict[str, int],  # noqa UP006 {'pass': x, 'fail': y}
                    ],
                ],
            ],
        ],
    ],
]
Runs = Dict[str, Dict[str, Any]]  # noqa UP006
Suggestion = Tuple[OptInt, OptStr, OptStr]  # noqa UP006
TaskIdOrPlatformInfo = Union[str, PlatformInfo]
Tasks = List[TestTask]  # noqa UP006
TupleOptIntStrOptInt = Tuple[OptInt, str, OptInt]  # noqa UP006
WptPaths = Tuple[OptStr, OptStr, OptStr, OptStr]  # noqa UP006

BUGREF_REGEX = r"[Bb][Uu][Gg] ?([0-9]+|TBD)"
TASK_LOG = "live_backing.log"
TASK_ARTIFACT = "public/logs/" + TASK_LOG
ATTACHMENT_DESCRIPTION = "Compressed " + TASK_ARTIFACT + " for task "
ATTACHMENT_REGEX = (
    r".*Created attachment ([0-9]+)\n.*"
    + ATTACHMENT_DESCRIPTION
    + "([A-Za-z0-9_-]+)\n.*"
)

BUGZILLA_AUTHENTICATION_HELP = "Must create a Bugzilla API key per https://github.com/mozilla/mozci-tools/blob/main/citools/test_triage_bug_filer.py"
CACHE_EXPIRY = 45  # days to expire entries in .skip_fails_cache
CACHE_DIR = ".skip_fails_cache"

MS_PER_MINUTE = 60 * 1000  # ms per minute
DEBUG_THRESHOLD = 40 * MS_PER_MINUTE  # 40 minutes in ms
OPT_THRESHOLD = 20 * MS_PER_MINUTE  # 20 minutes in ms

ANYJS = "anyjs"
CC = "classification"
DEF = "DEFAULT"
DIFFERENCE = "difference"
DURATIONS = "durations"
EQEQ = "=="
ERROR = "error"
FAIL = "FAIL"
FAILED_RUNS = "runs_failed"
FAILURE_RATIO = 0.4  # more than this fraction of failures will disable
INTERMITTENT_RATIO_REFTEST = 0.4  # reftest low frequency intermittent
FAILURE_RATIO_REFTEST = 0.8  # disable ratio for reftest (high freq intermittent)
GROUP = "group"
KIND = "kind"
LINENO = "lineno"
LL = "label"
MEDIAN_DURATION = "duration_median"
MINIMUM_RUNS = 3  # mininum number of runs to consider success/failure
MOCK_BUG_DEFAULTS = {"blocks": [], "comments": []}
MOCK_TASK_DEFAULTS = {"extra": {}, "failure_types": {}, "results": []}
MOCK_TASK_INITS = ["results"]
MODIFIERS = "modifiers"
NOTEQ = "!="
OPT = "opt"
PASS = "PASS"
PIXELS = "pixels"
PP = "path"
QUERY = "query"
RR = "result"
RUNS = "runs"
STATUS = "status"
SUBTEST = "subtest"
SUBTEST_REGEX = (
    r"image comparison, max difference: ([0-9]+), number of differing pixels: ([0-9]+)"
)
SUM_BY_LABEL = "sum_by_label"
TEST = "test"
TEST_TYPES = [EQEQ, NOTEQ]
TOTAL_DURATION = "duration_total"
TOTAL_RUNS = "runs_total"


def read_json(filename: str):
    """read data as JSON from filename"""
    with open(filename, encoding="utf-8") as fp:
        data = json.load(fp)
    return data


def default_serializer(obj):
    if hasattr(obj, "to_dict"):
        return obj.to_dict()
    return str(obj)


def write_json(filename: str, data):
    """saves data as JSON to filename"""
    # ensure that (at most ONE) parent dir exists
    parent = os.path.dirname(filename)
    grandparent = os.path.dirname(parent)
    if not os.path.isdir(grandparent):
        raise NotADirectoryError(
            f"write_json: grand parent directory does not exist for: {filename}"
        )
    if not os.path.isdir(parent):
        os.mkdir(parent)
    with open(filename, "w", encoding="utf-8") as fp:
        s: str = json.dumps(data, indent=2, sort_keys=True, default=default_serializer)
        if s[-1] != "\n":
            s += "\n"  # end with newline to match JSON linter
        fp.write(s)


class Mock:
    def __init__(self, data, defaults={}, inits=[]):
        self._data = data
        self._defaults = defaults
        for name in inits:
            values = self._data.get(name, [])  # assume type is an array
            values = [Mock(value, defaults, inits) for value in values]
            self._data[name] = values

    def __getattr__(self, name):
        if name in self._data:
            return self._data[name]
        if name in self._defaults:
            return self._defaults[name]
        return ""


class Classification:
    "Classification of the failure (not the task result)"

    DISABLE_INTERMITTENT = "disable_intermittent"  # reftest [40%, 80%)
    DISABLE_FAILURE = "disable_failure"  # reftest (80%,100%] failure
    DISABLE_MANIFEST = "disable_manifest"  # crash found
    DISABLE_RECOMMENDED = "disable_recommended"  # disable first failing path
    DISABLE_TOO_LONG = "disable_too_long"  # runtime threshold exceeded
    INTERMITTENT = "intermittent"
    SECONDARY = "secondary"  # secondary failing path
    SUCCESS = "success"  # path always succeeds
    UNKNOWN = "unknown"


class Kind:
    "Kind of manifest"

    LIST = "list"
    TOML = "toml"
    UNKNOWN = "unknown"
    WPT = "wpt"


class SkipfailsMode(Mode):
    "Skipfails mode of operation"

    @classmethod
    def from_flags(
        cls,
        carryover_mode: bool,
        known_intermittents_mode: bool,
        new_failures_mode: bool,
        replace_tbd_mode: bool,
    ) -> int:
        if (
            sum([
                carryover_mode,
                known_intermittents_mode,
                new_failures_mode,
                replace_tbd_mode,
            ])
            > 1
        ):
            raise Exception(
                "may not specifiy more than one mode: --carryover --known-intermittents --new-failures --replace-tbd"
            )
        if carryover_mode:
            return cls.CARRYOVER
        elif known_intermittents_mode:
            return cls.KNOWN_INTERMITTENT
        elif new_failures_mode:
            return cls.NEW_FAILURE
        elif replace_tbd_mode:
            return cls.REPLACE_TBD
        return cls.NORMAL

    @classmethod
    def edits_bugzilla(cls, mode: int) -> bool:
        if mode in [cls.NORMAL, cls.REPLACE_TBD]:
            return True
        return False

    @classmethod
    def description(cls, mode: int) -> str:
        if mode == cls.CARRYOVER:
            return "Carryover mode: only platform match conditions considered, no bugs created or updated"
        elif mode == cls.KNOWN_INTERMITTENT:
            return "Known Intermittents mode: only failures with known intermittents considered, no bugs created or updated"
        elif mode == cls.NEW_FAILURE:
            return "New failures mode: Will only edit manifest skip-if conditions for new failures (i.e. not carryover nor known intermittents)"
        elif mode == cls.REPLACE_TBD:
            return "Replace TBD mode: Will only edit manifest skip-if conditions for new failures by filing new bugs and replacing TBD with actual bug number."
        return "Normal mode"

    @classmethod
    def name(cls, mode: int) -> str:
        if mode == cls.NORMAL:
            return "NORMAL"
        if mode == cls.CARRYOVER:
            return "CARRYOVER"
        elif mode == cls.KNOWN_INTERMITTENT:
            return "KNOWN_INTERMITTENT"
        elif mode == cls.NEW_FAILURE:
            return "NEW_FAILURE"
        elif mode == cls.REPLACE_TBD:
            return "REPLACE_TBD"
        if mode == cls.CARRYOVER_FILED:
            return "CARRYOVER_FILED"
        elif mode == cls.KNOWN_INTERMITTENT_FILED:
            return "KNOWN_INTERMITTENT_FILED"
        elif mode == cls.NEW_FAILURE_FILED:
            return "NEW_FAILURE_FILED"
        return ""

    @classmethod
    def bug_filed(cls, mode: int) -> int:
        if mode == cls.CARRYOVER:
            return cls.CARRYOVER_FILED
        elif mode == cls.KNOWN_INTERMITTENT:
            return cls.KNOWN_INTERMITTENT_FILED
        elif mode == cls.NEW_FAILURE:
            return cls.NEW_FAILURE_FILED
        else:
            raise Exception(
                f"Skipfails mode {cls.name(mode)} cannot be promoted to a _FILED mode"
            )
        return mode


class Action:
    """
    A defferred action to take for a failure as a result
    of running in --carryover, --known-intermittents or --new-failures mode
    to be acted upon in --replace-tbd mode
    """

    SENTINEL = "|"

    def __init__(self, **kwargs):
        self.bugid: str = str(kwargs.get("bugid", ""))
        self.comment: str = kwargs.get("comment", "")
        self.component: str = kwargs.get("component", "")
        self.description: str = kwargs.get("description", "")
        self.disposition: str = kwargs.get("disposition", Mode.NORMAL)
        self.label: str = kwargs.get("label", "")
        self.manifest: str = kwargs.get("manifest", "")
        self.path: str = kwargs.get("path", "")
        self.product: str = kwargs.get("product", "")
        self.revision: str = kwargs.get("revision", "")
        self.skip_if: str = kwargs.get("skip_if", "")
        self.summary: str = kwargs.get("summary", "")
        self.task_id: str = kwargs.get("task_id", "")

    @classmethod
    def make_key(cls, manifest: str, path: str, label: str) -> str:
        if not manifest:
            raise Exception(
                "cannot create a key for an Action if the manifest is not specified"
            )
        if not path:
            raise Exception(
                "cannot create a key for an Action if the path is not specified"
            )
        if not label:
            raise Exception(
                "cannot create a key for an Action if the label is not specified"
            )
        return manifest + Action.SENTINEL + path + Action.SENTINEL + label

    def key(self) -> str:
        return Action.make_key(self.manifest, self.path, self.label)

    def to_dict(self) -> Dict:  # noqa UP006
        return self.__dict__


DictAction = Dict[str, Action]  # noqa UP006
OptAction = Optional[Action]  # noqa UP006


class Skipfails:
    "mach manifest skip-fails implementation: Update manifests to skip failing tests"

    REPO = "repo"
    REVISION = "revision"
    TREEHERDER = "treeherder.mozilla.org"
    BUGZILLA_DISABLE = "disable"
    BUGZILLA_SERVER = "bugzilla.allizom.org"
    BUGZILLA_SERVER_DEFAULT = BUGZILLA_DISABLE

    def __init__(
        self,
        command_context=None,
        try_url="",
        verbose=True,
        bugzilla=None,
        dry_run=False,
        turbo=False,
        implicit_vars=False,
        new_version=None,
        task_id=None,
        user_agent=None,
        clear_cache=None,
    ):
        self.command_context = command_context
        if self.command_context is not None:
            self.topsrcdir = self.command_context.topsrcdir
        else:
            self.topsrcdir = Path(__file__).parent.parent.parent
        self.topsrcdir = os.path.normpath(self.topsrcdir)
        if isinstance(try_url, list) and len(try_url) == 1:
            self.try_url = try_url[0]
        else:
            self.try_url = try_url
        self.implicit_vars = implicit_vars
        self.new_version = new_version
        self.verbose = verbose
        self.turbo = turbo
        self.edit_bugzilla = True
        if bugzilla is None:
            if "BUGZILLA" in os.environ:
                self.bugzilla = os.environ["BUGZILLA"]
            else:
                self.bugzilla = Skipfails.BUGZILLA_SERVER_DEFAULT
        else:
            self.bugzilla = bugzilla
        if self.bugzilla == Skipfails.BUGZILLA_DISABLE:
            self.bugzilla = None  # Bug filing disabled
            self.edit_bugzilla = False
        self.dry_run = dry_run
        if self.dry_run:
            self.edit_bugzilla = False
        self.component = "skip-fails"
        self._bzapi = None
        self._attach_rx = None
        self.variants = {}
        self.tasks = {}
        self.pp = None
        self.headers = {}  # for Treeherder requests
        self.headers["Accept"] = "application/json"
        self.headers["User-Agent"] = "treeherder-pyclient"
        self.jobs_url = "https://treeherder.mozilla.org/api/jobs/"
        self.push_ids = {}
        self.job_ids = {}
        self.extras: Extras = {}
        self.bugs = []  # preloaded bugs, currently not an updated cache
        self.error_summary = {}
        self._subtest_rx = None
        self.lmp = None
        self.failure_types = None
        self.task_id: OptStr = task_id
        self.failed_platforms: FailedPlatforms = {}
        self.platform_permutations: PlatformPermutations = {}
        self.user_agent: OptStr = user_agent
        self.suggestions: DictJSON = {}
        self.clear_cache: OptStr = clear_cache
        self.mode: int = Mode.NORMAL
        self.bugs_by_summary: BugsBySummary = {}
        self.actions: DictAction = {}
        self._bugref_rx = None
        self.check_cache()

    def check_cache(self) -> None:
        """
        Will ensure that the cache directory is present
        And will clear any entries based upon --clear-cache
        Or revisions older than 45 days
        """
        cache_dir = self.full_path(CACHE_DIR)
        if not os.path.exists(cache_dir):
            self.vinfo(f"creating cache directory: {cache_dir}")
            os.mkdir(cache_dir)
            return
        self.vinfo(f"clearing cache for revisions older than {CACHE_EXPIRY} days")
        expiry: float = CACHE_EXPIRY * 24 * 3600
        for rev_dir in [e.name for e in os.scandir(cache_dir) if e.is_dir()]:
            rev_path = os.path.join(cache_dir, rev_dir)
            if (
                self.clear_cache is not None and self.clear_cache in (rev_dir, "all")
            ) or self.file_age(rev_path) > expiry:
                self.vinfo(f"clearing revision: {rev_path}")
                self.delete_dir(rev_path)

    def cached_path(self, revision: str, filename: str) -> str:
        """Return full path for a cached filename for revision"""
        cache_dir = self.full_path(CACHE_DIR)
        return os.path.join(cache_dir, revision, filename)

    def record_new_bug(self, revision: OptStr, bugid: int) -> None:
        """Records this bug id in the cache of created bugs for this revision"""
        if revision is not None:
            new_bugs_path = self.cached_path(revision, "new-bugs.json")
            new_bugs: ListInt = []
            if os.path.exists(new_bugs_path):
                self.vinfo(f"Reading cached new bugs for revision: {revision}")
                new_bugs = read_json(new_bugs_path)
            if bugid not in new_bugs:
                new_bugs.append(bugid)
                new_bugs.sort()
            write_json(new_bugs_path, new_bugs)

    def _initialize_bzapi(self):
        """Lazily initializes the Bugzilla API (returns True on success)"""
        if self._bzapi is None and self.bugzilla is not None:
            self._bzapi = bugzilla.Bugzilla(self.bugzilla)
            self._attach_rx = re.compile(ATTACHMENT_REGEX, flags=re.M)
        return self._bzapi is not None

    def pprint(self, obj):
        if self.pp is None:
            self.pp = pprint.PrettyPrinter(indent=4, stream=sys.stderr)
        self.pp.pprint(obj)
        sys.stderr.flush()

    def error(self, e):
        if self.command_context is not None:
            self.command_context.log(
                logging.ERROR, self.component, {ERROR: str(e)}, "ERROR: {error}"
            )
        else:
            print(f"ERROR: {e}", file=sys.stderr, flush=True)

    def warning(self, e):
        if self.command_context is not None:
            self.command_context.log(
                logging.WARNING, self.component, {ERROR: str(e)}, "WARNING: {error}"
            )
        else:
            print(f"WARNING: {e}", file=sys.stderr, flush=True)

    def info(self, e):
        if self.command_context is not None:
            self.command_context.log(
                logging.INFO, self.component, {ERROR: str(e)}, "INFO: {error}"
            )
        else:
            print(f"INFO: {e}", file=sys.stderr, flush=True)

    def vinfo(self, e):
        if self.verbose:
            self.info(e)

    def full_path(self, filename):
        """Returns full path for the relative filename"""

        return os.path.join(self.topsrcdir, os.path.normpath(filename))

    def isdir(self, filename):
        """Returns True if filename is a directory"""

        return os.path.isdir(self.full_path(filename))

    def exists(self, filename):
        """Returns True if filename exists"""

        return os.path.exists(self.full_path(filename))

    def file_age(self, path: str) -> float:
        """Returns age of filename in seconds"""

        age: float = 0.0
        if os.path.exists(path):
            stat: os.stat_result = os.stat(path)
            mtime: float = stat.st_mtime
            now: float = time.time()
            age = now - mtime
        return age

    def delete_dir(self, path: str) -> None:
        """Recursively deletes dir at path"""
        abs_path: str = os.path.abspath(path)
        # Safety: prevent root or empty deletions
        if abs_path in ("", os.path.sep):
            raise ValueError(f"Refusing to delete unsafe path: {path}")
        # Ensure path is inside topsrcdir
        if not abs_path.startswith(self.topsrcdir + os.path.sep):
            raise ValueError(
                f"Refusing to delete: {path} is not inside {self.topsrcdir}"
            )
        if not os.path.exists(abs_path):
            raise FileNotFoundError(f"Path does not exist: {abs_path}")
        if not os.path.isdir(abs_path):
            raise NotADirectoryError(f"Not a directory: {abs_path}")
        shutil.rmtree(abs_path)

    def run(
        self,
        meta_bug_id: OptInt = None,
        save_tasks: OptStr = None,
        use_tasks: OptStr = None,
        save_failures: OptStr = None,
        use_failures: OptStr = None,
        max_failures: int = -1,
        failure_ratio: float = FAILURE_RATIO,
        mode: int = Mode.NORMAL,
    ):
        "Run skip-fails on try_url, return True on success"

        self.mode = mode
        if self.mode != Mode.NORMAL and meta_bug_id is None:
            raise Exception(
                "must specifiy --meta-bug-id when using one of: --carryover --known-intermittents --new-failures --replace-tbd"
            )
        if self.mode != Mode.NORMAL:
            self.read_actions(meta_bug_id)
        self.vinfo(SkipfailsMode.description(self.mode))
        if not SkipfailsMode.edits_bugzilla(self.mode):
            self.edit_bugzilla = False
        if self.bugzilla is None:
            self.vinfo("Bugzilla has been disabled: bugs not created or updated.")
        elif self.dry_run:
            self.vinfo("Flag --dry-run: bugs not created or updated.")
        if meta_bug_id is None:
            self.edit_bugzilla = False
            self.vinfo("No --meta-bug-id specified: bugs not created or updated.")
        else:
            self.vinfo(f"meta-bug-id: {meta_bug_id}")
        if self.new_version is not None:
            self.vinfo(
                f"All skip-if conditions will use the --new-version for os_version: {self.new_version}"
            )
        if failure_ratio != FAILURE_RATIO:
            self.vinfo(f"Failure ratio for this run: {failure_ratio}")
        self.vinfo(
            f"skip-fails assumes implicit-vars for reftest: {self.implicit_vars}"
        )
        try_url = self.try_url
        revision, repo = self.get_revision(try_url)
        self.cached_job_ids(revision)
        use_tasks_cached = self.cached_path(revision, "tasks.json")
        use_failures_cached = self.cached_path(revision, "failures.json")
        if use_tasks is None and os.path.exists(use_tasks_cached):
            use_tasks = use_tasks_cached
        if save_tasks is None and use_tasks != use_tasks_cached:
            save_tasks = use_tasks_cached
        if use_failures is None and os.path.exists(use_failures_cached):
            use_failures = use_failures_cached
        if save_failures is None and use_failures != use_failures_cached:
            save_failures = use_failures_cached
        if use_tasks is not None:
            tasks = self.read_tasks(use_tasks)
            self.vinfo(f"use tasks: {use_tasks}")
            self.failure_types = None  # do NOT cache failure_types
        else:
            tasks = self.get_tasks(revision, repo)
            if len(tasks) > 0 and self.task_id is not None:
                tasks = [t for t in tasks if t.id == self.task_id]
            self.failure_types = {}  # cache failure_types
        if use_failures is not None:
            failures = self.read_failures(use_failures)
            self.vinfo(f"use failures: {use_failures}")
        else:
            failures = self.get_failures(tasks, failure_ratio)
            if save_failures is not None:
                write_json(save_failures, failures)
                self.vinfo(f"save failures: {save_failures}")
        if save_tasks is not None:
            self.write_tasks(save_tasks, tasks)
            self.vinfo(f"save tasks: {save_tasks}")
        num_failures = 0
        if self.mode == Mode.REPLACE_TBD:
            self.replace_tbd(meta_bug_id)
            failures = []
        for manifest in failures:
            kind = failures[manifest][KIND]
            for label in failures[manifest][LL]:
                for path in failures[manifest][LL][label][PP]:
                    classification = failures[manifest][LL][label][PP][path][CC]
                    if classification.startswith("disable_") or (
                        self.turbo and classification == Classification.SECONDARY
                    ):
                        anyjs = {}  # anyjs alternate basename = False
                        differences = []
                        pixels = []
                        status = FAIL
                        lineno = failures[manifest][LL][label][PP][path].get(LINENO, 0)
                        runs: Runs = failures[manifest][LL][label][PP][path][RUNS]
                        k = Action.make_key(manifest, path, label)
                        if (
                            self.mode in [Mode.KNOWN_INTERMITTENT, Mode.NEW_FAILURE]
                            and k in self.actions
                        ):
                            self.info(
                                f"\n\n===== Previously handled {SkipfailsMode.name(self.actions[k].disposition)} in manifest: {manifest} ====="
                            )
                            self.info(f"    path: {path}")
                            self.info(f"    label: {label}")
                            continue

                        # skip_failure only needs to run against one failing task for each path: first_task_id
                        first_task_id: OptStr = None
                        for task_id in runs:
                            if first_task_id is None and not runs[task_id].get(
                                RR, False
                            ):
                                first_task_id = task_id
                            if kind == Kind.TOML:
                                continue
                            elif kind == Kind.LIST:
                                difference = runs[task_id].get(DIFFERENCE, 0)
                                if difference > 0:
                                    differences.append(difference)
                                pixel = runs[task_id].get(PIXELS, 0)
                                if pixel > 0:
                                    pixels.append(pixel)
                                status = runs[task_id].get(STATUS, FAIL)
                            elif kind == Kind.WPT:
                                filename = os.path.basename(path)
                                anyjs[filename] = False
                                if QUERY in runs[task_id]:
                                    query = runs[task_id][QUERY]
                                    anyjs[filename + query] = False
                                else:
                                    query = None
                                if ANYJS in runs[task_id]:
                                    any_filename = os.path.basename(
                                        runs[task_id][ANYJS]
                                    )
                                    anyjs[any_filename] = False
                                    if query is not None:
                                        anyjs[any_filename + query] = False

                        self.skip_failure(
                            manifest,
                            kind,
                            path,
                            first_task_id,
                            None,  # platform_info
                            None,  # bug_id
                            False,  # high_freq
                            anyjs,
                            differences,
                            pixels,
                            lineno,
                            status,
                            label,
                            classification,
                            try_url,
                            revision,
                            repo,
                            meta_bug_id,
                        )
                        num_failures += 1
                        if max_failures >= 0 and num_failures >= max_failures:
                            self.warning(
                                f"max_failures={max_failures} threshold reached: stopping."
                            )
                            return True
        self.cache_job_ids(revision)
        if self.mode != Mode.NORMAL:
            self.write_actions(meta_bug_id)
        return True

    def get_revision(self, url):
        parsed = urllib.parse.urlparse(url)
        if parsed.scheme != "https":
            raise ValueError("try_url scheme not https")
        if parsed.netloc != Skipfails.TREEHERDER:
            raise ValueError(f"try_url server not {Skipfails.TREEHERDER}")
        if len(parsed.query) == 0:
            raise ValueError("try_url query missing")
        query = urllib.parse.parse_qs(parsed.query)
        if Skipfails.REVISION not in query:
            raise ValueError("try_url query missing revision")
        revision = query[Skipfails.REVISION][0]
        if Skipfails.REPO in query:
            repo = query[Skipfails.REPO][0]
        else:
            repo = "try"
        self.vinfo(f"considering {repo} revision={revision}")
        return revision, repo

    def get_tasks(self, revision, repo):
        self.vinfo(f"Retrieving tasks for revision: {revision} ...")
        push = Push(revision, repo)
        tasks = None
        try:
            tasks = push.tasks
        except requests.exceptions.HTTPError:
            n = len(mozci.data.handler.sources)
            self.error("Error querying mozci, sources are:")
            tcs = -1
            for i in range(n):
                source = mozci.data.handler.sources[i]
                self.error(f"  sources[{i}] is type {source.__class__.__name__}")
                if source.__class__.__name__ == "TreeherderClientSource":
                    tcs = i
            if tcs < 0:
                raise PermissionError("Error querying mozci with default User-Agent")
            msg = f"Error querying mozci with User-Agent: {mozci.data.handler.sources[tcs].session.headers['User-Agent']}"
            if self.user_agent is None:
                raise PermissionError(msg)
            else:
                self.error(msg)
                self.error(f"Re-try with User-Agent: {self.user_agent}")
                mozci.data.handler.sources[tcs].session.headers = {
                    "User-Agent": self.user_agent
                }
                tasks = push.tasks
        return tasks

    def get_kind_manifest(self, manifest: str):
        kind = Kind.UNKNOWN
        if manifest.endswith(".ini"):
            self.warning(f"cannot analyze skip-fails on INI manifests: {manifest}")
            return (None, None)
        elif manifest.endswith(".list"):
            kind = Kind.LIST
        elif manifest.endswith(".toml"):
            # NOTE: manifest may be a compound of manifest1:manifest2
            # where manifest1 includes manifest2
            # The skip-condition will need to be associated with manifest2
            includes = manifest.split(":")
            if len(includes) > 1:
                manifest = includes[-1]
                self.warning(
                    f"manifest '{manifest}' is included from {':'.join(includes[0:-1])}"
                )
            kind = Kind.TOML
        else:
            kind = Kind.WPT
            path, wpt_manifest, _query, _anyjs = self.wpt_paths(manifest)
            if path is None or wpt_manifest is None:  # not WPT
                self.warning(
                    f"cannot analyze skip-fails on unknown manifest type: {manifest}"
                )
                return (None, None)
            manifest = wpt_manifest
        return (kind, manifest)

    def get_task_config(self, task: TestTask):
        if task.label is None:
            self.warning(f"Cannot find task label for task: {task.id}")
            return None
        # strip chunk number - this finds failures across different chunks
        try:
            parts = task.label.split("-")
            int(parts[-1])
            return "-".join(parts[:-1])
        except ValueError:
            return task.label

    def get_failures(
        self,
        tasks: Tasks,
        failure_ratio: float = FAILURE_RATIO,
    ):
        """
        find failures and create structure comprised of runs by path:
           result:
            * False (failed)
            * True (passed)
           classification: Classification
            * unknown (default) < 3 runs
            * intermittent (not enough failures)
            * disable_recommended (enough repeated failures) >3 runs >= 4
            * disable_manifest (disable DEFAULT if no other failures)
            * secondary (not first failure in group)
            * success
        """

        failures = {}
        manifest_paths: ManifestPaths = {}
        manifest_ = {
            KIND: Kind.UNKNOWN,
            LL: {},
        }
        label_ = {
            DURATIONS: {},
            MEDIAN_DURATION: 0,
            OPT: False,
            PP: {},
            SUM_BY_LABEL: {},  # All sums implicitly zero
            TOTAL_DURATION: 0,
        }
        path_ = {
            CC: Classification.UNKNOWN,
            FAILED_RUNS: 0,
            RUNS: {},
            TOTAL_RUNS: 0,
        }
        run_ = {
            RR: False,
        }

        for task in tasks:  # add explicit failures
            config = self.get_task_config(task)
            if config is None:
                continue

            try:
                if len(task.results) == 0:
                    continue  # ignore aborted tasks
                _fail_types = task.failure_types  # call magic property once
                if _fail_types is None:
                    continue
                if self.failure_types is None:
                    self.failure_types = {}

                # This remove known failures
                failure_types = {}
                failing_groups = [r.group for r in task.results if not r.ok]
                for ft in _fail_types:
                    failtype = ft
                    kind, manifest = self.get_kind_manifest(ft)
                    if kind == Kind.WPT:
                        failtype = parse_wpt_path(ft)[0]

                    if [fg for fg in failing_groups if fg.endswith(failtype)]:
                        failure_types[ft] = _fail_types[ft]

                self.failure_types[task.id] = failure_types
                self.vinfo(f"Getting failure_types from task: {task.id}")
                for raw_manifest in failure_types:
                    kind, manifest = self.get_kind_manifest(raw_manifest)
                    if kind is None or manifest is None:
                        continue

                    if kind != Kind.WPT:
                        if manifest not in failures:
                            failures[manifest] = deepcopy(manifest_)
                            failures[manifest][KIND] = kind
                        if task.label not in failures[manifest][LL]:
                            failures[manifest][LL][task.label] = deepcopy(label_)

                    if manifest not in manifest_paths:
                        manifest_paths[manifest] = {}
                    if config not in manifest_paths[manifest]:
                        manifest_paths[manifest][config] = []

                    for included_path_type in failure_types[raw_manifest]:
                        included_path, _type = included_path_type
                        path = included_path.split(":")[
                            -1
                        ]  # strip 'included_from.toml:' prefix
                        query = None
                        anyjs = {}
                        allpaths = []
                        if kind == Kind.WPT:
                            path, mmpath, query, anyjs = self.wpt_paths(path)
                            if path is None or mmpath is None:
                                self.warning(
                                    f"non existant failure path: {included_path_type[0]}"
                                )
                                break
                            allpaths = [path]
                            manifest = os.path.dirname(mmpath)
                            if manifest not in manifest_paths:
                                # this can be a subdir, or translated path
                                manifest_paths[manifest] = {}
                            if config not in manifest_paths[manifest]:
                                manifest_paths[manifest][config] = []
                            if manifest not in failures:
                                failures[manifest] = deepcopy(manifest_)
                                failures[manifest][KIND] = kind
                            if task.label not in failures[manifest][LL]:
                                failures[manifest][LL][task.label] = deepcopy(label_)
                        elif kind == Kind.LIST:
                            words = path.split()
                            if len(words) != 3 or words[1] not in TEST_TYPES:
                                self.warning(f"reftest type not supported: {path}")
                                continue
                            allpaths = self.get_allpaths(task.id, manifest, path)
                        elif kind == Kind.TOML:
                            if path == manifest:
                                path = DEF  # refers to the manifest itself
                            allpaths = [path]
                        for path in allpaths:
                            if path not in manifest_paths[manifest].get(config, []):
                                manifest_paths[manifest][config].append(path)
                            self.vinfo(
                                f"Getting failure info in manifest: {manifest}, path: {path}"
                            )
                            task_path_object = failures[manifest][LL][task.label][PP]
                            if path not in task_path_object:
                                task_path_object[path] = deepcopy(path_)
                            task_path = task_path_object[path]
                            if task.id not in task_path[RUNS]:
                                task_path[RUNS][task.id] = deepcopy(run_)
                            else:
                                continue
                            result = task.result == "passed"
                            task_path[RUNS][task.id][RR] = result
                            if query is not None:
                                task_path[RUNS][task.id][QUERY] = query
                            if anyjs is not None and len(anyjs) > 0:
                                task_path[RUNS][task.id][ANYJS] = anyjs
                            task_path[TOTAL_RUNS] += 1
                            if not result:
                                task_path[FAILED_RUNS] += 1
                            if kind == Kind.LIST:
                                (
                                    lineno,
                                    difference,
                                    pixels,
                                    status,
                                ) = self.get_lineno_difference_pixels_status(
                                    task.id, manifest, path
                                )
                                if lineno > 0:
                                    task_path[LINENO] = lineno
                                else:
                                    self.vinfo(f"ERROR no lineno for {path}")
                                if status != FAIL:
                                    task_path[RUNS][task.id][STATUS] = status
                                if status == FAIL and difference == 0 and pixels == 0:
                                    # intermittent, not error
                                    task_path[RUNS][task.id][RR] = True
                                    task_path[FAILED_RUNS] -= 1
                                elif difference > 0:
                                    task_path[RUNS][task.id][DIFFERENCE] = difference
                                if pixels > 0:
                                    task_path[RUNS][task.id][PIXELS] = pixels
            except AttributeError:
                pass  # self.warning(f"unknown attribute in task (#1): {ae}")

        for task in tasks:  # add results
            config = self.get_task_config(task)
            if config is None:
                continue

            try:
                if len(task.results) == 0:
                    continue  # ignore aborted tasks
                if self.failure_types is None:
                    continue
                self.vinfo(f"Getting results from task: {task.id}")
                for result in task.results:
                    kind, manifest = self.get_kind_manifest(result.group)
                    if kind is None or manifest is None:
                        continue

                    if manifest not in manifest_paths:
                        continue
                    if config not in manifest_paths[manifest]:
                        continue
                    if manifest not in failures:
                        failures[manifest] = deepcopy(manifest_)
                    task_label_object = failures[manifest][LL]
                    if task.label not in task_label_object:
                        task_label_object[task.label] = deepcopy(label_)
                    task_label = task_label_object[task.label]
                    if task.id not in task_label[DURATIONS]:
                        # duration may be None !!!
                        task_label[DURATIONS][task.id] = result.duration or 0
                        if task_label[OPT] is None:
                            task_label[OPT] = self.get_opt_for_task(task.id)
                    for path in manifest_paths[manifest][config]:  # all known paths
                        # path can be one of any paths that have failed for the manifest/config
                        # ensure the path is in the specific task failure data
                        if path not in [
                            path
                            for path, type in self.failure_types.get(task.id, {}).get(
                                manifest, [("", "")]
                            )
                        ]:
                            result.ok = True

                        task_path_object = task_label[PP]
                        if path not in task_path_object:
                            task_path_object[path] = deepcopy(path_)
                        task_path = task_path_object[path]
                        if task.id not in task_path[RUNS]:
                            task_path[RUNS][task.id] = deepcopy(run_)
                            task_path[RUNS][task.id][RR] = result.ok
                            task_path[TOTAL_RUNS] += 1
                            if not result.ok:
                                task_path[FAILED_RUNS] += 1
                            if kind == Kind.LIST:
                                (
                                    lineno,
                                    difference,
                                    pixels,
                                    status,
                                ) = self.get_lineno_difference_pixels_status(
                                    task.id, manifest, path
                                )
                                if lineno > 0:
                                    task_path[LINENO] = lineno
                                else:
                                    self.vinfo(f"ERROR no lineno for {path}")
                                if status != FAIL:
                                    task_path[RUNS][task.id][STATUS] = status
                                if (
                                    status == FAIL
                                    and difference == 0
                                    and pixels == 0
                                    and not result.ok
                                ):
                                    # intermittent, not error
                                    task_path[RUNS][task.id][RR] = True
                                    task_path[FAILED_RUNS] -= 1
                                if difference > 0:
                                    task_path[RUNS][task.id][DIFFERENCE] = difference
                                if pixels > 0:
                                    task_path[RUNS][task.id][PIXELS] = pixels
            except AttributeError:
                pass  # self.warning(f"unknown attribute in task (#2): {ae}")

        for manifest in failures:  # determine classifications
            kind = failures[manifest][KIND]
            for label in failures[manifest][LL]:
                task_label = failures[manifest][LL][label]
                opt = task_label[OPT]
                durations = []  # summarize durations
                try:
                    first_task_id: str = next(iter(task_label.get(DURATIONS, {})))
                except StopIteration:
                    continue
                for task_id in task_label.get(DURATIONS, {}):
                    duration = task_label[DURATIONS][task_id]
                    durations.append(duration)
                if len(durations) > 0:
                    total_duration = sum(durations)
                    median_duration = median(durations)
                    task_label[TOTAL_DURATION] = total_duration
                    task_label[MEDIAN_DURATION] = median_duration
                    if (opt and median_duration > OPT_THRESHOLD) or (
                        (not opt) and median_duration > DEBUG_THRESHOLD
                    ):
                        if kind == Kind.TOML:
                            paths = [DEF]
                        else:
                            paths = task_label[PP].keys()
                        for path in paths:
                            task_path_object = task_label[PP]
                            if path not in task_path_object:
                                task_path_object[path] = deepcopy(path_)
                            task_path = task_path_object[path]
                            if first_task_id not in task_path[RUNS]:
                                task_path[RUNS][first_task_id] = deepcopy(run_)
                                task_path[RUNS][first_task_id][RR] = False
                                task_path[TOTAL_RUNS] += 1
                                task_path[FAILED_RUNS] += 1
                            task_path[CC] = Classification.DISABLE_TOO_LONG
                primary = True  # we have not seen the first failure
                for path in sort_paths(task_label[PP]):
                    task_path = task_label[PP][path]
                    classification = task_path[CC]
                    if classification == Classification.UNKNOWN:
                        failed_runs = task_path[FAILED_RUNS]
                        total_runs = task_path[TOTAL_RUNS]
                        status = FAIL  # default status, only one run could be PASS
                        for first_task_id in task_path[RUNS]:
                            status = task_path[RUNS][first_task_id].get(STATUS, status)
                        if kind == Kind.LIST:
                            ratio = INTERMITTENT_RATIO_REFTEST
                        else:
                            ratio = failure_ratio
                        if total_runs >= MINIMUM_RUNS:
                            if failed_runs / total_runs < ratio:
                                if failed_runs == 0:
                                    classification = Classification.SUCCESS
                                else:
                                    classification = Classification.INTERMITTENT
                            elif kind == Kind.LIST:
                                if failed_runs / total_runs < FAILURE_RATIO_REFTEST:
                                    classification = Classification.DISABLE_INTERMITTENT
                                else:
                                    classification = Classification.DISABLE_FAILURE
                            elif primary:
                                if path == DEF:
                                    classification = Classification.DISABLE_MANIFEST
                                else:
                                    classification = Classification.DISABLE_RECOMMENDED
                                primary = False
                            else:
                                classification = Classification.SECONDARY
                        elif self.task_id is not None:
                            classification = Classification.DISABLE_RECOMMENDED
                        task_path[CC] = classification
                    if classification not in task_label[SUM_BY_LABEL]:
                        task_label[SUM_BY_LABEL][classification] = 0
                    task_label[SUM_BY_LABEL][classification] += 1

        return failures

    def bugid_from_reference(self, bug_reference) -> str:
        if self._bugref_rx is None:
            self._bugref_rx = re.compile(BUGREF_REGEX)
        m = self._bugref_rx.findall(bug_reference)
        if len(m) == 1:
            bugid = m[0]
        else:
            raise Exception("Carryover bug reference does not include a bug id")
        return bugid

    def get_bug_by_id(self, id) -> OptBug:
        """Get bug by bug id"""

        bug: OptBug = None
        for b in self.bugs:
            if b.id == id:
                bug = b
                break
        if bug is None and self._initialize_bzapi():
            self.vinfo(f"Retrieving bug id: {id} ...")
            bug = self._bzapi.getbug(id)
        return bug

    def get_bugs_by_summary(self, summary, meta_bug_id: OptInt = None) -> ListBug:
        """Get bug by bug summary"""

        bugs: ListBug = []
        for b in self.bugs:
            if b.summary == summary:
                bugs.append(b)
        if (
            not self.edit_bugzilla
            and len(bugs) == 0
            and summary in self.bugs_by_summary
        ):
            bug = self.bugs_by_summary[summary]
            if bug is not None:
                bugs.append(bug)
            return bugs
        if len(bugs) == 0 and self.bugzilla is not None and self._initialize_bzapi():
            self.vinfo(f"Retrieving bugs by summary: {summary} ...")
            query = self._bzapi.build_query(short_desc=summary)
            query["include_fields"] = [
                "id",
                "product",
                "component",
                "status",
                "resolution",
                "summary",
                "blocks",
            ]
            try:
                bugs = self._bzapi.query(query)
            except requests.exceptions.HTTPError:
                raise
        if len(bugs) > 0 and meta_bug_id is not None:
            # narrow results to those blocking meta_bug_id
            i = 0
            while i < len(bugs):
                if meta_bug_id not in bugs[i].blocks:
                    del bugs[i]
                else:
                    i += 1
        if not self.edit_bugzilla:
            self.bugs_by_summary[summary] = bugs[0] if len(bugs) > 0 else None
        return bugs

    def create_bug(
        self,
        summary: str = "Bug short description",
        description: str = "Bug description",
        product: str = "Testing",
        component: str = "General",
        version: str = "unspecified",
        bugtype: str = "task",
        revision: OptStr = None,
    ) -> OptBug:
        """Create a bug"""

        bug: OptBug = None
        if self._initialize_bzapi():
            if not self._bzapi.logged_in:
                self.error(
                    "Must create a Bugzilla API key per https://github.com/mozilla/mozci-tools/blob/main/citools/test_triage_bug_filer.py"
                )
                raise PermissionError(f"Not authenticated for Bugzilla {self.bugzilla}")
            createinfo = self._bzapi.build_createbug(
                product=product,
                component=component,
                summary=summary,
                version=version,
                description=description,
            )
            createinfo["type"] = bugtype
            bug = self._bzapi.createbug(createinfo)
            if bug is not None:
                self.vinfo(f'Created Bug {bug.id} {product}::{component} : "{summary}"')
                self.record_new_bug(revision, bug.id)
        return bug

    def add_bug_comment(self, id: int, comment: str, meta_bug_id: OptInt = None):
        """Add a comment to an existing bug"""

        if self._initialize_bzapi():
            if not self._bzapi.logged_in:
                self.error(BUGZILLA_AUTHENTICATION_HELP)
                raise PermissionError("Not authenticated for Bugzilla")
            if meta_bug_id is not None:
                blocks_add = [meta_bug_id]
            else:
                blocks_add = None
            updateinfo = self._bzapi.build_update(
                comment=comment, blocks_add=blocks_add
            )
            self._bzapi.update_bugs([id], updateinfo)

    def generate_bugzilla_comment(
        self,
        manifest: str,
        kind: str,
        path: str,
        skip_if: str,
        filename: str,
        anyjs: OptJs,
        lineno: OptInt,
        label: OptStr,
        classification: OptStr,
        task_id: OptStr,
        try_url: OptStr,
        revision: OptStr,
        repo: OptStr,
        meta_bug_id: OptInt = None,
    ) -> GenBugComment:
        """
        Will create a comment for the failure details provided as arguments.
        Will determine if a bug exists already for this manifest and meta-bug-id.
        If exactly one bug is found, then set
           bugid
           meta_bug_blocked
           attachments (to determine if the task log is in the attachments)
        else
           set bugid to TBD
           if we should edit_bugzilla (not --dry-run) then we return a lambda function
              to lazily create a bug later
        Returns a tuple with
          create_bug_lambda -- lambda function to create a bug (or None)
          bugid -- id of bug found, or TBD
          meta_bug_blocked -- True if bug found and meta_bug_id is already blocked
          attachments -- dictionary of attachments if bug found
          comment -- comment to add to the bug (if we should edit_bugzilla)
        """

        create_bug_lambda: CreateBug = None
        bugid: str = "TBD"
        meta_bug_blocked: bool = False
        attachments: dict = {}
        comment: str = ""
        line_number: OptInt = None
        if classification == Classification.DISABLE_MANIFEST:
            comment = "Disabled entire manifest due to crash result"
        elif classification == Classification.DISABLE_TOO_LONG:
            comment = "Disabled entire manifest due to excessive run time"
        else:
            comment = f'Disabled test due to failures in test file: "{filename}"'
            if classification == Classification.SECONDARY:
                comment += " (secondary)"
        if kind == Kind.WPT and anyjs is not None and len(anyjs) > 1:
            comment += "\nAdditional WPT wildcard paths:"
            for p in sorted(anyjs.keys()):
                if p != filename:
                    comment += f'\n  "{p}"'
        if task_id is not None:
            if kind != Kind.LIST:
                if revision is not None and repo is not None:
                    push_id = self.get_push_id(revision, repo)
                    if push_id is not None:
                        job_id = self.get_job_id(push_id, task_id)
                        if job_id is not None:
                            (
                                line_number,
                                line,
                                log_url,
                            ) = self.get_bug_suggestions(
                                repo, revision, job_id, path, anyjs
                            )
                            if log_url is not None:
                                comment += f"\nError log line {line_number}: {log_url}"
        summary: str = f"MANIFEST {manifest}"
        bugs: ListBug = []  # performance optimization when not editing bugzilla
        if self.edit_bugzilla:
            bugs = self.get_bugs_by_summary(summary, meta_bug_id)
        if len(bugs) == 0:
            description = (
                f"This bug covers excluded failing tests in the MANIFEST {manifest}"
            )
            description += "\n(generated by `mach manifest skip-fails`)"
            product, component = self.get_file_info(path)
            if self.edit_bugzilla:
                create_bug_lambda = cast(
                    CreateBug,
                    lambda: self.create_bug(
                        summary,
                        description,
                        product,
                        component,
                        "unspecified",
                        "task",
                        revision,
                    ),
                )
        elif len(bugs) == 1:
            bugid = str(bugs[0].id)
            product = bugs[0].product
            component = bugs[0].component
            self.vinfo(f'Found Bug {bugid} {product}::{component} "{summary}"')
            if meta_bug_id is not None:
                if meta_bug_id in bugs[0].blocks:
                    self.vinfo(f"  Bug {bugid} already blocks meta bug {meta_bug_id}")
                    meta_bug_blocked = True
            comments = bugs[0].getcomments()
            for i in range(len(comments)):
                text = comments[i]["text"]
                attach_rx = self._attach_rx
                if attach_rx is not None:
                    m = attach_rx.findall(text)
                    if len(m) == 1:
                        a_task_id = m[0][1]
                        attachments[a_task_id] = m[0][0]
                        if a_task_id == task_id:
                            self.vinfo(
                                f"  Bug {bugid} already has the compressed log attached for this task"
                            )
        elif meta_bug_id is None:
            raise Exception(f'More than one bug found for summary: "{summary}"')
        else:
            raise Exception(
                f'More than one bug found for summary: "{summary}" for meta-bug-id: {meta_bug_id}'
            )
        if kind == Kind.LIST:
            comment += f"\nfuzzy-if condition on line {lineno}: {skip_if}"
        return (
            create_bug_lambda,
            bugid,
            meta_bug_blocked,
            attachments,
            comment,
            line_number,
            summary,
            description,
            product,
            component,
        )

    def resolve_failure_filename(self, path: str, kind: str, manifest: str) -> str:
        filename = DEF
        if kind == Kind.TOML:
            filename = self.get_filename_in_manifest(manifest.rsplit(":", 1)[-1], path)
        elif kind == Kind.WPT:
            filename = os.path.basename(path)
        elif kind == Kind.LIST:
            filename = [
                am
                for am in self.error_summary.get(manifest, "")
                if self.error_summary[manifest][am]["test"].endswith(path)
            ]
            if filename:
                filename = filename[0]
            else:
                filename = path
        return filename

    def resolve_failure_manifest(self, path: str, kind: str, manifest: str) -> str:
        if kind == Kind.WPT:
            _path, resolved_manifest, _query, _anyjs = self.wpt_paths(path)
            if resolved_manifest:
                return resolved_manifest
            raise Exception(f"Could not resolve WPT manifest for path {path}")
        return manifest

    def skip_failure(
        self,
        manifest: str,
        kind: str,
        path: str,
        task_id: OptStr,
        platform_info: OptPlatformInfo = None,
        bug_id: OptStr = None,
        high_freq: bool = False,
        anyjs: OptJs = None,
        differences: OptDifferences = None,
        pixels: OptDifferences = None,
        lineno: OptInt = None,
        status: OptStr = None,
        label: OptStr = None,
        classification: OptStr = None,
        try_url: OptStr = None,
        revision: OptStr = None,
        repo: OptStr = None,
        meta_bug_id: OptInt = None,
    ):
        """
        Skip a failure (for TOML, WPT and REFTEST manifests)
        For wpt anyjs is a dictionary mapping from alternate basename to
        a boolean (indicating if the basename has been handled in the manifest)
        """

        path: str = path.rsplit(":", 1)[-1]
        self.info(f"\n\n===== Skip failure in manifest: {manifest} =====")
        self.info(f"    path: {path}")
        self.info(f"    label: {label}")
        action: OptAction = None

        if self.mode != Mode.NORMAL:
            if bug_id is not None:
                self.warning(
                    f"skip_failure with bug_id specified not supported in {Mode.name(self.mode)} mode"
                )
                return
            if kind != Kind.TOML:
                self.warning(
                    f"skip_failure in {SkipfailsMode.name(self.mode)} mode only supported for TOML manifests"
                )
                return

        skip_if: OptStr
        if task_id is None:
            skip_if = "true"
        else:
            skip_if = self.task_to_skip_if(
                manifest,
                platform_info if platform_info is not None else task_id,
                kind,
                path,
                high_freq,
            )
        if skip_if is None:
            self.info("Not adding skip-if condition")
            return
        self.vinfo(f"proposed skip-if: {skip_if}")

        filename: str = self.resolve_failure_filename(path, kind, manifest)
        manifest: str = self.resolve_failure_manifest(path, kind, manifest)
        manifest_path: str = self.full_path(manifest)
        manifest_str: str = ""
        comment: str = ""
        line_number: OptInt = None
        additional_comment: str = ""
        meta_bug_blocked: bool = False
        create_bug_lambda: CreateBug = None
        bugid: OptInt

        if bug_id is None:
            if self.mode == Mode.KNOWN_INTERMITTENT and kind == Kind.TOML:
                (bugid, comment, line_number) = self.find_known_intermittent(
                    repo, revision, task_id, manifest, filename, skip_if
                )
                if bugid is None:
                    self.info("Ignoring failure as it is not a known intermittent")
                    return
                self.vinfo(f"Found known intermittent: {bugid}")
                action = Action(
                    manifest=manifest,
                    path=path,
                    label=label,
                    revision=revision,
                    disposition=self.mode,
                    bugid=bugid,
                    task_id=task_id,
                    skip_if=skip_if,
                )
            else:
                (
                    create_bug_lambda,
                    bugid,
                    meta_bug_blocked,
                    attachments,
                    comment,
                    line_number,
                    summary,
                    description,
                    product,
                    component,
                ) = self.generate_bugzilla_comment(
                    manifest,
                    kind,
                    path,
                    skip_if,
                    filename,
                    anyjs,
                    lineno,
                    label,
                    classification,
                    task_id,
                    try_url,
                    revision,
                    repo,
                    meta_bug_id,
                )
            bug_reference: str = f"Bug {bugid}"
            if classification == Classification.SECONDARY and kind != Kind.WPT:
                bug_reference += " (secondary)"
            if self.mode == Mode.NEW_FAILURE:
                action = Action(
                    manifest=manifest,
                    path=path,
                    label=label,
                    revision=revision,
                    disposition=self.mode,
                    bugid=bugid,
                    task_id=task_id,
                    skip_if=skip_if,
                    summary=summary,
                    description=description,
                    product=product,
                    component=component,
                )
        else:
            bug_reference = f"Bug {bug_id}"

        if kind == Kind.WPT:
            if os.path.exists(manifest_path):
                manifest_str = open(manifest_path, encoding="utf-8").read()
            else:
                # ensure parent directories exist
                os.makedirs(os.path.dirname(manifest_path), exist_ok=True)
            if bug_id is None and create_bug_lambda is not None:
                bug = create_bug_lambda()
                if bug is not None:
                    bug_reference = f"Bug {bug.id}"
            manifest_str, additional_comment = self.wpt_add_skip_if(
                manifest_str, anyjs, skip_if, bug_reference
            )
        elif kind == Kind.TOML:
            mp = ManifestParser(use_toml=True, document=True)
            try:
                mp.read(manifest_path)
            except OSError:
                raise Exception(f"Unable to find path: {manifest_path}")

            document = mp.source_documents[manifest_path]
            try:
                additional_comment, carryover, bug_reference = add_skip_if(
                    document,
                    filename,
                    skip_if,
                    bug_reference,
                    create_bug_lambda,
                    self.mode,
                )
            except Exception:
                # Note: this fails to find a comment at the desired index
                # Note: manifestparser len(skip_if) yields: TypeError: object of type 'bool' has no len()
                additional_comment = ""
                carryover = False
            if bug_reference is None:  # skip-if was ignored
                self.warning(
                    f'Did NOT add redundant skip-if to: ["{filename}"] in manifest: "{manifest}"'
                )
                return
            if self.mode == Mode.CARRYOVER:
                if not carryover:
                    self.vinfo(
                        f'No --carryover in: ["{filename}"] in manifest: "{manifest}"'
                    )
                    return
                bugid = self.bugid_from_reference(bug_reference)
                action = Action(
                    manifest=manifest,
                    path=path,
                    label=label,
                    revision=revision,
                    disposition=self.mode,
                    bugid=bugid,
                    task_id=task_id,
                    skip_if=skip_if,
                )
            manifest_str = alphabetize_toml_str(document)
        elif kind == Kind.LIST:
            if lineno == 0:
                self.error(
                    f"cannot determine line to edit in manifest: {manifest_path}"
                )
            elif not os.path.exists(manifest_path):
                self.error(f"manifest does not exist: {manifest_path}")
            else:
                manifest_str = open(manifest_path, encoding="utf-8").read()
                if status == PASS:
                    self.info(f"Unexpected status: {status}")
                if (
                    status == PASS
                    or classification == Classification.DISABLE_INTERMITTENT
                ):
                    zero = True  # refest lower ranges should include zero
                else:
                    zero = False
                if bug_id is None and create_bug_lambda is not None:
                    bug = create_bug_lambda()
                    if bug is not None:
                        bug_reference = f"Bug {bug.id}"
                manifest_str, additional_comment = self.reftest_add_fuzzy_if(
                    manifest_str,
                    filename,
                    skip_if,
                    differences,
                    pixels,
                    lineno,
                    zero,
                    bug_reference,
                )
                if not manifest_str and additional_comment:
                    self.warning(additional_comment)
        if manifest_str:
            if line_number is not None:
                comment += "\n" + self.error_log_context(revision, task_id, line_number)
            if additional_comment:
                comment += "\n" + additional_comment
            if action is not None:
                action.comment = comment
                self.actions[action.key()] = action
            if self.dry_run:
                prefix = "Would have (--dry-run): "
            else:
                prefix = ""
                with open(manifest_path, "w", encoding="utf-8", newline="\n") as fp:
                    fp.write(manifest_str)
            self.info(f'{prefix}Edited ["{filename}"] in manifest: "{manifest}"')
            if kind != Kind.LIST:
                self.info(
                    f'{prefix}Added {SkipfailsMode.name(self.mode)} skip-if condition: "{skip_if} # {bug_reference}"'
                )
            if bug_id is None:
                return
            if self.mode == Mode.NORMAL:
                if self.bugzilla is None:
                    self.vinfo(
                        f"Bugzilla has been disabled: comment not added to Bug {bugid}:\n{comment}"
                    )
                elif meta_bug_id is None:
                    self.vinfo(
                        f"No --meta-bug-id specified: comment not added to Bug {bugid}:\n{comment}"
                    )
                elif self.dry_run:
                    self.vinfo(
                        f"Flag --dry-run: comment not added to Bug {bugid}:\n{comment}"
                    )
                else:
                    self.add_bug_comment(
                        bugid, comment, None if meta_bug_blocked else meta_bug_id
                    )
                    if meta_bug_id is not None:
                        self.info(f"  Bug {bugid} blocks meta Bug: {meta_bug_id}")
                    self.info(f"Added comment to Bug {bugid}:\n{comment}")
            else:
                self.vinfo(f"New comment for Bug {bugid}:\n{comment}")
        else:
            self.error(f'Error editing ["{filename}"] in manifest: "{manifest}"')

    def replace_tbd(self, meta_bug_id: int):
        # First pass: file new bugs for TBD, collect comments by bugid
        comments_by_bugid: DictStrList = {}
        for k in self.actions:
            action: Action = self.actions[k]
            self.info(f"\n\n===== Action in manifest: {action.manifest} =====")
            self.info(f"    path: {action.path}")
            self.info(f"    label: {action.label}")
            self.info(f"    skip_if: {action.skip_if}")
            self.info(f"    disposition: {SkipfailsMode.name(action.disposition)}")
            self.info(f"    bug_id: {action.bugid}")

            kind: Kind = Kind.TOML
            if not action.manifest.endswith(".toml"):
                raise Exception(
                    f'Only TOML manifests supported for --replace-tbd: "{action.manifest}"'
                )
            if action.disposition == Mode.NEW_FAILURE:
                if self.bugzilla is None:
                    self.vinfo(
                        f"Bugzilla has been disabled: new bug not created for Bug {action.bugid}"
                    )
                elif self.dry_run:
                    self.vinfo(
                        f"Flag --dry-run: new bug not created for Bug {action.bugid}"
                    )
                else:
                    # Check for existing bug
                    bugs = self.get_bugs_by_summary(action.summary, meta_bug_id)
                    if len(bugs) == 0:
                        bug = self.create_bug(
                            action.summary,
                            action.description,
                            action.product,
                            action.component,
                        )
                        if bug is not None:
                            action.bugid = str(bug.id)
                    elif len(bugs) == 1:
                        action.bugid = str(bugs[0].id)
                        self.vinfo(f'Found Bug {action.bugid} "{action.summary}"')
                    else:
                        raise Exception(
                            f'More than one bug found for summary: "{action.summary}"'
                        )
                    manifest_path: str = self.full_path(action.manifest)
                    filename: str = self.resolve_failure_filename(
                        action.path, kind, action.manifest
                    )

                    mp = ManifestParser(use_toml=True, document=True)
                    mp.read(manifest_path)
                    document = mp.source_documents[manifest_path]
                    updated = replace_tbd_skip_if(
                        document, filename, action.skip_if, action.bugid
                    )
                    if updated:
                        manifest_str = alphabetize_toml_str(document)
                        with open(
                            manifest_path, "w", encoding="utf-8", newline="\n"
                        ) as fp:
                            fp.write(manifest_str)
                        self.info(
                            f'Edited ["{filename}"] in manifest: "{action.manifest}"'
                        )
                    else:
                        self.error(
                            f'Error editing ["{filename}"] in manifest: "{action.manifest}"'
                        )
                    self.actions[k] = action
            comments: ListStr = comments_by_bugid.get(action.bugid, [])
            comments.append(action.comment)
            comments_by_bugid[action.bugid] = comments
        # Second pass: Add combined comment for each bugid
        for k in self.actions:
            action: Action = self.actions[k]
            comments: ListStr = comments_by_bugid.get(action.bugid, [])
            if self.bugzilla is not None and not self.dry_run:
                action.disposition = SkipfailsMode.bug_filed(action.disposition)
                self.actions[k] = action
            if len(comments) > 0:  # comments not yet added
                self.info(
                    f"\n\n===== Filing Combined Comment for Bug {action.bugid} ====="
                )
                comment: str = ""
                for c in comments:
                    comment += c + "\n"
                if self.bugzilla is None:
                    self.vinfo(
                        f"Bugzilla has been disabled: comment not added to Bug {action.bugid}:\n{comment}"
                    )
                elif self.dry_run:
                    self.vinfo(
                        f"Flag --dry-run: comment not added to Bug {action.bugid}:\n{comment}"
                    )
                else:
                    self.add_bug_comment(int(action.bugid), comment)
                    self.info(f"Added comment to Bug {action.bugid}:\n{comment}")
                comments_by_bugid[action.bugid] = []

    def get_variants(self):
        """Get mozinfo for each test variants"""

        if len(self.variants) == 0:
            variants_file = "taskcluster/test_configs/variants.yml"
            variants_path = self.full_path(variants_file)
            with open(variants_path, encoding="utf-8") as fp:
                raw_variants = load(fp, Loader=Loader)
            for k, v in raw_variants.items():
                mozinfo = k
                if "mozinfo" in v:
                    mozinfo = v["mozinfo"]
                self.variants[k] = mozinfo
        return self.variants

    def get_task_details(self, task_id):
        """Download details for task task_id"""

        if task_id in self.tasks:  # if cached
            task = self.tasks[task_id]
        else:
            self.vinfo(f"get_task_details for task: {task_id}")
            try:
                task = get_task(task_id)
            except TaskclusterRestFailure:
                self.warning(f"Task {task_id} no longer exists.")
                return None
            self.tasks[task_id] = task
        return task

    def get_extra(self, task_id):
        """Calculate extra for task task_id"""

        if task_id in self.extras:  # if cached
            platform_info = self.extras[task_id]
        else:
            self.get_variants()
            task = self.get_task_details(task_id) or {}
            test_setting = task.get("extra", {}).get("test-setting", {})
            platform = test_setting.get("platform", {})
            platform_os = platform.get("os", {})
            if self.new_version:
                platform_os["version"] = self.new_version
            if not test_setting:
                return None
            platform_info = PlatformInfo(test_setting)
        self.extras[task_id] = platform_info
        return platform_info

    def get_opt_for_task(self, task_id):
        extra = self.get_extra(task_id)
        return extra.opt

    def _fetch_platform_permutations(self):
        self.info("Fetching platform permutations...")
        import taskcluster

        url: OptStr = None
        index = taskcluster.Index({
            "rootUrl": "https://firefox-ci-tc.services.mozilla.com",
        })
        route = "gecko.v2.mozilla-central.latest.source.test-info-all"
        queue = taskcluster.Queue({
            "rootUrl": "https://firefox-ci-tc.services.mozilla.com",
        })

        # Typing from findTask is wrong, so we need to convert to Any
        result: OptTaskResult = index.findTask(route)
        if result is not None:
            task_id: str = result["taskId"]
            result = queue.listLatestArtifacts(task_id)
            if result is not None and task_id is not None:
                artifact_list: ArtifactList = result["artifacts"]
                for artifact in artifact_list:
                    artifact_name = artifact["name"]
                    if artifact_name.endswith("test-info-testrun-matrix.json"):
                        url = queue.buildUrl(
                            "getLatestArtifact", task_id, artifact_name
                        )
                        break

        if url is not None:
            self.vinfo("Retrieving platform permutations ...")
            response = requests.get(url, headers={"User-agent": "mach-test-info/1.0"})
            self.platform_permutations = response.json()
        else:
            self.info("Failed fetching platform permutations...")

    def _get_list_skip_if(self, platform_info: PlatformInfo):
        aa = "&&"
        nn = "!"

        os = platform_info.os
        build_type = platform_info.build_type
        runtimes = platform_info.test_variant.split("+")

        skip_if = None
        if os == "linux":
            skip_if = "gtkWidget"
        elif os == "win":
            skip_if = "winWidget"
        elif os == "mac":
            skip_if = "cocoaWidget"
        elif os == "android":
            skip_if = "Android"
        else:
            self.error(f"cannot calculate skip-if for unknown OS: '{os}'")
        if skip_if is not None:
            ccov = "ccov" in build_type
            asan = "asan" in build_type
            tsan = "tsan" in build_type
            optimized = (
                (not platform_info.debug) and (not ccov) and (not asan) and (not tsan)
            )
            skip_if += aa
            if optimized:
                skip_if += "optimized"
            elif platform_info.debug:
                skip_if += "isDebugBuild"
            elif ccov:
                skip_if += "isCoverageBuild"
            elif asan:
                skip_if += "AddressSanitizer"
            elif tsan:
                skip_if += "ThreadSanitizer"
            # See implicit VARIANT_DEFAULTS in
            # https://searchfox.org/firefox-main/source/layout/tools/reftest/manifest.sys.mjs#30
            no_fission = "!fission" not in runtimes
            snapshot = "snapshot" in runtimes
            swgl = "swgl" in runtimes
            nogpu = "nogpu" in runtimes
            if not self.implicit_vars and no_fission:
                skip_if += aa + "fission"
            elif not no_fission:  # implicit default: fission
                skip_if += aa + nn + "fission"
            if platform_info.bits is not None:
                if platform_info.bits == "32":
                    skip_if += aa + nn + "is64Bit"  # override implicit is64Bit
                elif not self.implicit_vars and os == "winWidget":
                    skip_if += aa + "is64Bit"
            if not self.implicit_vars and not swgl:
                skip_if += aa + nn + "swgl"
            elif swgl:  # implicit default: !swgl
                skip_if += aa + "swgl"

            if not self.implicit_vars and not nogpu:
                skip_if += aa + nn + "nogpu"
            elif nogpu:  # implicit default: !swgl
                skip_if += aa + "nogpu"

            if os == "gtkWidget":
                if not self.implicit_vars and not snapshot:
                    skip_if += aa + nn + "useDrawSnapshot"
                elif snapshot:  # implicit default: !useDrawSnapshot
                    skip_if += aa + "useDrawSnapshot"
        return skip_if

    def task_to_skip_if(
        self,
        manifest: str,
        task: TaskIdOrPlatformInfo,
        kind: str,
        file_path: str,
        high_freq: bool,
    ) -> OptStr:
        """Calculate the skip-if condition for failing task task_id"""
        if isinstance(task, str):
            extra = self.get_extra(task)
        else:
            extra = task
        if kind == Kind.WPT:
            qq = '"'
            aa = " and "
        else:
            qq = "'"
            aa = " && "
        eq = " == "
        skip_if = None
        os = extra.os
        os_version = extra.os_version
        if os is not None:
            if kind == Kind.LIST:
                skip_if = self._get_list_skip_if(extra)
            else:
                skip_if = "os" + eq + qq + os + qq
                if os_version is not None:
                    skip_if += aa + "os_version" + eq + qq + os_version + qq
        arch = extra.arch
        if arch is not None and skip_if is not None and kind != Kind.LIST:
            skip_if += aa + "arch" + eq + qq + arch + qq
            if high_freq:
                failure_key = os + os_version + arch + manifest + file_path
                if self.failed_platforms.get(failure_key) is None:
                    if not self.platform_permutations:
                        self._fetch_platform_permutations()
                    permutations = (
                        self.platform_permutations
                        .get(manifest, {})
                        .get(os, {})
                        .get(os_version, {})
                        .get(arch, None)
                    )
                    self.failed_platforms[failure_key] = FailedPlatform(
                        permutations, high_freq
                    )
                skip_cond = self.failed_platforms[failure_key].get_skip_string(
                    aa, extra.build_type, extra.test_variant
                )
                if skip_cond is not None:
                    skip_if += skip_cond
                else:
                    skip_if = None
            else:  # not high_freq
                skip_if += aa + extra.build_type
                variants = extra.test_variant.split("+")
                if len(variants) >= 3:
                    self.warning(
                        f'Removing all variants "{" ".join(variants)}" from skip-if condition in manifest={manifest} and file={file_path}'
                    )
                else:
                    for tv in variants:
                        if tv != "no_variant":
                            skip_if += aa + tv
        elif skip_if is None:
            raise Exception(
                f"Unable to calculate skip-if condition from manifest={manifest} and file={file_path}"
            )
        if skip_if is not None and kind == Kind.WPT:
            # ensure ! -> 'not', primarily fission and e10s
            skip_if = skip_if.replace("!", "not ")
        return skip_if

    def get_file_info(self, path, product="Testing", component="General"):
        """
        Get bugzilla product and component for the path.
        Provide defaults (in case command_context is not defined
        or there isn't file info available).
        """
        if path != DEF and self.command_context is not None:
            reader = self.command_context.mozbuild_reader(config_mode="empty")
            info = reader.files_info([path])
            try:
                cp = info[path]["BUG_COMPONENT"]
            except TypeError:
                # TypeError: BugzillaComponent.__new__() missing 2 required positional arguments: 'product' and 'component'
                pass
            else:
                product = cp.product
                component = cp.component
        return product, component

    def get_filename_in_manifest(self, manifest: str, path: str) -> str:
        """return relative filename for path in manifest"""

        filename = os.path.basename(path)
        if filename == DEF:
            return filename
        manifest_dir = os.path.dirname(manifest)
        i = 0
        j = min(len(manifest_dir), len(path))
        while i < j and manifest_dir[i] == path[i]:
            i += 1
        if i < len(manifest_dir):
            for _ in range(manifest_dir.count("/", i) + 1):
                filename = "../" + filename
        elif i < len(path):
            filename = path[i + 1 :]
        return filename

    def get_push_id(self, revision: str, repo: str):
        """Return the push_id for revision and repo (or None)"""

        if revision in self.push_ids:  # if cached
            self.vinfo(f"Getting push_id for {repo} revision: {revision} ...")
            push_id = self.push_ids[revision]
        else:
            push_id = None
            push_url = f"https://treeherder.mozilla.org/api/project/{repo}/push/"
            params = {}
            params["full"] = "true"
            params["count"] = 10
            params["revision"] = revision
            self.vinfo(f"Retrieving push_id for {repo} revision: {revision} ...")
            r = requests.get(push_url, headers=self.headers, params=params)
            if r.status_code != 200:
                self.warning(f"FAILED to query Treeherder = {r} for {r.url}")
            else:
                response = r.json()
                if "results" in response:
                    results = response["results"]
                    if len(results) > 0:
                        r0 = results[0]
                        if "id" in r0:
                            push_id = r0["id"]
            self.push_ids[revision] = push_id
        return push_id

    def cached_job_ids(self, revision):
        if len(self.push_ids) == 0 and len(self.job_ids) == 0:
            # no pre-caching for tests
            job_ids_cached = self.cached_path(revision, "job_ids.json")
            if os.path.exists(job_ids_cached):
                self.job_ids = read_json(job_ids_cached)
                for k in self.job_ids:
                    push_id, _task_id = k.split(":")
                    self.push_ids[revision] = push_id
                    break

    def cache_job_ids(self, revision):
        job_ids_cached = self.cached_path(revision, "job_ids.json")
        if not os.path.exists(job_ids_cached):
            write_json(job_ids_cached, self.job_ids)

    def get_job_id(self, push_id, task_id):
        """Return the job_id for push_id, task_id (or None)"""

        k = f"{push_id}:{task_id}"
        if k in self.job_ids:  # if cached
            self.vinfo(f"Getting job_id for push_id: {push_id}, task_id: {task_id} ...")
            job_id = self.job_ids[k]
        else:
            job_id = None
            params = {}
            params["push_id"] = push_id
            self.vinfo(
                f"Retrieving job_id for push_id: {push_id}, task_id: {task_id} ..."
            )
            r = requests.get(self.jobs_url, headers=self.headers, params=params)
            if r.status_code != 200:
                self.warning(f"FAILED to query Treeherder = {r} for {r.url}")
            else:
                response = r.json()
                if "results" in response:
                    results = response["results"]
                    if len(results) > 0:
                        for result in results:
                            if len(result) > 14:
                                if result[14] == task_id:
                                    job_id = result[1]
                                    break
            self.job_ids[k] = job_id
        return job_id

    def cached_bug_suggestions(self, repo, revision, job_id) -> JSONType:
        """
        Return the bug_suggestions JSON for the job_id
        Use the cache, if present, else download from treeherder
        """

        if job_id in self.suggestions:
            self.vinfo(
                f"Getting bug_suggestions for {repo} revision: {revision} job_id: {job_id}"
            )
            suggestions = self.suggestions[job_id]
        else:
            suggestions_path = self.cached_path(revision, f"suggest-{job_id}.json")
            if os.path.exists(suggestions_path):
                self.vinfo(
                    f"Reading cached bug_suggestions for {repo} revision: {revision} job_id: {job_id}"
                )
                suggestions = read_json(suggestions_path)
            else:
                suggestions_url = f"https://treeherder.mozilla.org/api/project/{repo}/jobs/{job_id}/bug_suggestions/"
                self.vinfo(
                    f"Retrieving bug_suggestions for {repo} revision: {revision} job_id: {job_id}"
                )
                r = requests.get(suggestions_url, headers=self.headers)
                if r.status_code != 200:
                    self.warning(f"FAILED to query Treeherder = {r} for {r.url}")
                    return None
                suggestions = r.json()
                write_json(suggestions_path, suggestions)
            self.suggestions[job_id] = suggestions
        return suggestions

    def get_bug_suggestions(
        self, repo, revision, job_id, path, anyjs=None
    ) -> Suggestion:
        """
        Return the (line_number, line, log_url)
        for the given repo and job_id
        """
        line_number: int = None
        line: str = None
        log_url: str = None
        suggestions: JSONType = self.cached_bug_suggestions(repo, revision, job_id)
        if suggestions is not None:
            paths: ListStr
            if anyjs is not None and len(anyjs) > 0:
                pathdir: str = os.path.dirname(path) + "/"
                paths = [pathdir + f for f in anyjs.keys()]
            else:
                paths = [path]
            if len(suggestions) > 0:
                for suggestion in suggestions:
                    for p in paths:
                        path_end = suggestion.get("path_end", None)
                        # handles WPT short paths
                        if path_end is not None and p.endswith(path_end):
                            line_number = suggestion["line_number"] + 1
                            line = suggestion["search"]
                            log_url = f"https://treeherder.mozilla.org/logviewer?repo={repo}&job_id={job_id}&lineNumber={line_number}"
                            break
        return (line_number, line, log_url)

    def read_tasks(self, filename):
        """read tasks as JSON from filename"""

        if not os.path.exists(filename):
            msg = f"use-tasks JSON file does not exist: {filename}"
            raise OSError(2, msg, filename)
        tasks = read_json(filename)
        tasks = [Mock(task, MOCK_TASK_DEFAULTS, MOCK_TASK_INITS) for task in tasks]
        for task in tasks:
            if len(task.extra) > 0:  # pre-warm cache for extra information
                platform_info = PlatformInfo()
                extra: Any = task.extra
                platform_info.from_dict(extra)
                self.extras[task.id] = platform_info
        return tasks

    def read_failures(self, filename):
        """read failures as JSON from filename"""

        if not os.path.exists(filename):
            msg = f"use-failures JSON file does not exist: {filename}"
            raise OSError(2, msg, filename)
        failures = read_json(filename)
        return failures

    def read_bugs(self, filename):
        """read bugs as JSON from filename"""

        if not os.path.exists(filename):
            msg = f"bugs JSON file does not exist: {filename}"
            raise OSError(2, msg, filename)
        bugs = read_json(filename)
        bugs = [Mock(bug, MOCK_BUG_DEFAULTS) for bug in bugs]
        return bugs

    def write_tasks(self, save_tasks, tasks):
        """saves tasks as JSON to save_tasks"""
        jtasks = []
        for task in tasks:
            extras = self.get_extra(task.id)
            if not extras:
                continue

            jtask = {}
            jtask["id"] = task.id
            jtask["label"] = task.label
            jtask["duration"] = task.duration
            jtask["result"] = task.result
            jtask["state"] = task.state
            jtask["extra"] = extras.to_dict()
            jtags = {}
            for k, v in task.tags.items():
                if k == "createdForUser":
                    jtags[k] = "ci@mozilla.com"
                else:
                    jtags[k] = v
            jtask["tags"] = jtags
            jtask["tier"] = task.tier
            jtask["results"] = [
                {"group": r.group, "ok": r.ok, "duration": r.duration}
                for r in task.results
            ]
            jtask["errors"] = None  # Bug with task.errors property??
            jft = {}
            if self.failure_types is not None and task.id in self.failure_types:
                failure_types = self.failure_types[task.id]  # use cache
            else:
                try:
                    failure_types = task.failure_types
                except requests.exceptions.HTTPError:
                    continue
                except TaskclusterRestFailure:
                    continue
            for k in failure_types:
                if isinstance(task, TestTask):
                    jft[k] = [[f[0], f[1].value] for f in task.failure_types[k]]
                else:
                    jft[k] = [[f[0], f[1]] for f in task.failure_types[k]]
            jtask["failure_types"] = jft
            jtasks.append(jtask)
        write_json(save_tasks, jtasks)

    def add_attachment_log_for_task(self, bugid: str, task_id: str):
        """Adds compressed log for this task to bugid"""

        log_url = f"https://firefox-ci-tc.services.mozilla.com/api/queue/v1/task/{task_id}/artifacts/public/logs/live_backing.log"
        self.vinfo(f"Retrieving full log for task: {task_id}")
        r = requests.get(log_url, headers=self.headers)
        if r.status_code != 200:
            self.error(f"Unable to get log for task: {task_id}")
            return
        attach_fp = tempfile.NamedTemporaryFile()
        with gzip.open(attach_fp, "wb") as fp:
            fp.write(r.text.encode("utf-8"))
        if self._initialize_bzapi():
            description = ATTACHMENT_DESCRIPTION + task_id
            file_name = TASK_LOG + ".gz"
            comment = "Added compressed log"
            content_type = "application/gzip"
            try:
                self._bzapi.attachfile(
                    [int(bugid)],
                    attach_fp.name,
                    description,
                    file_name=file_name,
                    comment=comment,
                    content_type=content_type,
                    is_private=False,
                )
            except Fault:
                pass  # Fault expected: Failed to fetch key 9372091 from network storage: The specified key does not exist.

    def wpt_paths(self, shortpath: str) -> WptPaths:
        """
        Analyzes the WPT short path for a test and returns
        (path, manifest, query, anyjs) where
        path is the relative path to the test file
        manifest is the relative path to the file metadata
        query is the test file query paramters (or None)
        anyjs is the html test file as reported by mozci (or None)
        """
        path, manifest, query, anyjs = parse_wpt_path(shortpath, self.isdir)
        if manifest and manifest.startswith(WPT_META0):
            manifest_classic = manifest.replace(WPT_META0, WPT_META0_CLASSIC, 1)
            if self.exists(manifest_classic):
                if self.exists(manifest):
                    self.warning(
                        f"Both classic {manifest_classic} and metadata {manifest} manifests exist"
                    )
                else:
                    self.warning(
                        f"Using the classic {manifest_classic} manifest as the metadata manifest {manifest} does not exist"
                    )
                    manifest = manifest_classic

        self.vinfo(f"wpt_paths::{path},{manifest}")

        if path and not self.exists(path):
            return (None, None, None, None)
        return (path, manifest, query, anyjs)

    def wpt_add_skip_if(self, manifest_str, anyjs, skip_if, bug_reference):
        """
        Edits a WPT manifest string to add disabled condition
        anyjs is a dictionary mapping from filename and any alternate basenames to
        a boolean (indicating if the file has been handled in the manifest).
        Returns additional_comment (if any)
        """

        additional_comment = ""
        disabled_key = False
        disabled = "  disabled:"
        condition_start = "    if "
        condition = condition_start + skip_if + ": " + bug_reference
        lines = manifest_str.splitlines()
        section = None  # name of the section
        i = 0
        n = len(lines)
        while i < n:
            line = lines[i]
            if line.startswith("["):
                if section is not None and not anyjs[section]:  # not yet handled
                    if not disabled_key:
                        lines.insert(i, disabled)
                        i += 1
                    lines.insert(i, condition)
                    lines.insert(i + 1, "")  # blank line after condition
                    i += 2
                    n += 2
                    anyjs[section] = True
                section = line[1:-1]
                if section and anyjs and section in anyjs and not anyjs[section]:
                    disabled_key = False
                else:
                    section = None  # ignore section we are not interested in
            elif section is not None:
                if line == disabled:
                    disabled_key = True
                elif line.startswith("  ["):
                    if i > 0 and i - 1 < n and lines[i - 1] == "":
                        del lines[i - 1]
                        i -= 1
                        n -= 1
                    if not disabled_key:
                        lines.insert(i, disabled)
                        i += 1
                        n += 1
                    lines.insert(i, condition)
                    lines.insert(i + 1, "")  # blank line after condition
                    i += 2
                    n += 2
                    anyjs[section] = True
                    section = None
                elif line.startswith("  ") and not line.startswith("    "):
                    if disabled_key:  # insert condition above new key
                        lines.insert(i, condition)
                        i += 1
                        n += 1
                        anyjs[section] = True
                        section = None
                        disabled_key = False
                elif line.startswith("    "):
                    if disabled_key and line == condition:
                        anyjs[section] = True  # condition already present
                        section = None
            i += 1
        if section is not None and not anyjs[section]:  # not yet handled
            if i > 0 and i - 1 < n and lines[i - 1] == "":
                del lines[i - 1]
            if not disabled_key:
                lines.append(disabled)
                i += 1
                n += 1
            lines.append(condition)
            lines.append("")  # blank line after condition
            i += 2
            n += 2
            anyjs[section] = True
        if len(anyjs) > 0:
            for section in anyjs:
                if not anyjs[section]:
                    if i > 0 and i - 1 < n and lines[i - 1] != "":
                        lines.append("")  # blank line before condition
                        i += 1
                        n += 1
                    lines.append("[" + section + "]")
                    lines.append(disabled)
                    lines.append(condition)
                    lines.append("")  # blank line after condition
                    i += 4
                    n += 4
        manifest_str = "\n".join(lines) + "\n"
        return manifest_str, additional_comment

    def reftest_add_fuzzy_if(
        self,
        manifest_str,
        filename,
        fuzzy_if,
        differences,
        pixels,
        lineno,
        zero,
        bug_reference,
    ):
        """
        Edits a reftest manifest string to add disabled condition
        """

        if self.lmp is None:
            from parse_reftest import ListManifestParser

            self.lmp = ListManifestParser(
                self.implicit_vars, self.verbose, self.error, self.warning, self.info
            )
        manifest_str, additional_comment = self.lmp.reftest_add_fuzzy_if(
            manifest_str,
            filename,
            fuzzy_if,
            differences,
            pixels,
            lineno,
            zero,
            bug_reference,
        )
        return manifest_str, additional_comment

    def get_lineno_difference_pixels_status(self, task_id, manifest, allmods):
        """
        Returns
        - lineno in manifest
        - image comparison, max *difference*
        - number of differing *pixels*
        - status (PASS or FAIL)
        as cached from reftest_errorsummary.log for a task
        """
        manifest_obj = self.error_summary.get(manifest, {})
        # allmods_obj: manifest_test_name: {test: allmods, error: ...}
        allmods_obj = manifest_obj[
            [am for am in manifest_obj if manifest_obj[am]["test"].endswith(allmods)][0]
        ]
        lineno = allmods_obj.get(LINENO, 0)
        runs_obj = allmods_obj.get(RUNS, {})
        task_obj = runs_obj.get(task_id, {})
        difference = task_obj.get(DIFFERENCE, 0)
        pixels = task_obj.get(PIXELS, 0)
        status = task_obj.get(STATUS, FAIL)
        return lineno, difference, pixels, status

    def reftest_find_lineno(self, manifest, modifiers, allmods):
        """
        Return the line number with modifiers in manifest (else 0)
        """

        lineno = 0
        mods = []
        prefs = []
        for i in range(len(modifiers)):
            if modifiers[i].find("pref(") >= 0 or modifiers[i].find("skip-if(") >= 0:
                prefs.append(modifiers[i])
            else:
                mods.append(modifiers[i])
        m = len(mods)
        manifest_str = open(manifest, encoding="utf-8").read()
        lines = manifest_str.splitlines()
        defaults = []
        found = False
        alt_lineno = 0
        for linenum in range(len(lines)):
            line = lines[linenum]
            if len(line) > 0 and line[0] == "#":
                continue
            comment_start = line.find(" #")  # MUST NOT match anchors!
            if comment_start > 0:
                line = line[0:comment_start].strip()
            words = line.split()
            n = len(words)
            if n > 1 and words[0] == "defaults":
                defaults = words[1:].copy()
                continue
            line_defaults = defaults.copy()
            i = 0
            while i < n:
                if words[i].find("pref(") >= 0 or words[i].find("skip-if(") >= 0:
                    line_defaults.append(words[i])
                    del words[i]
                    n -= 1
                else:
                    i += 1
            if (len(prefs) == 0 or prefs == line_defaults) and words == mods:
                found = True
                lineno = linenum + 1
                break
            elif m > 2 and n > 2:
                if words[-3:] == mods[-3:]:
                    alt_lineno = linenum + 1
                else:
                    bwords = [os.path.basename(f) for f in words[-2:]]
                    bmods = [os.path.basename(f) for f in mods[-2:]]
                    if bwords == bmods:
                        alt_lineno = linenum + 1
        if not found:
            if alt_lineno > 0:
                lineno = alt_lineno
                self.warning(
                    f"manifest '{manifest}' found lineno: {lineno}, but it does not contain all the prefs from modifiers,\nSEARCH: {allmods}\nFOUND : {lines[alt_lineno - 1]}"
                )
            else:
                lineno = 0
                self.error(
                    f"manifest '{manifest}' does not contain line with modifiers: {allmods}"
                )
        return lineno

    def get_allpaths(self, task_id, manifest, path):
        """
        Looks up the reftest_errorsummary.log for a task
        and caches the details in self.error_summary by
           task_id, manifest, allmods
        where allmods is the concatenation of all modifiers
        and the details include
        - image comparison, max *difference*
        - number of differing *pixels*
        - status: unexpected PASS or FAIL

        The list iof unique modifiers (allmods) for the given path are returned
        """

        allpaths = []
        words = path.split()
        if len(words) != 3 or words[1] not in TEST_TYPES:
            self.warning(
                f"reftest_errorsummary.log for task: {task_id} has unsupported test type '{path}'"
            )
            return allpaths
        if manifest in self.error_summary:
            for allmods in self.error_summary[manifest]:
                if self.error_summary[manifest][allmods][
                    TEST
                ] == path and task_id in self.error_summary[manifest][allmods].get(
                    RUNS, {}
                ):
                    allpaths.append(path)
            if len(allpaths) > 0:
                return allpaths  # cached (including self tests)
        error_url = f"https://firefox-ci-tc.services.mozilla.com/api/queue/v1/task/{task_id}/artifacts/public/test_info/reftest_errorsummary.log"
        self.vinfo(f"Retrieving reftest_errorsummary.log for task: {task_id}")
        r = requests.get(error_url, headers=self.headers)
        if r.status_code != 200:
            self.error(f"Unable to get reftest_errorsummary.log for task: {task_id}")
            return allpaths
        for line in r.text.splitlines():
            summary = json.loads(line)
            group = summary.get(GROUP, "")
            if not group or not os.path.exists(group):  # not error line
                continue
            test = summary.get(TEST, None)
            if test is None:
                continue
            if not MODIFIERS in summary:
                self.warning(
                    f"reftest_errorsummary.log for task: {task_id} does not have modifiers for '{test}'"
                )
                continue
            words = test.split()
            if len(words) != 3 or words[1] not in TEST_TYPES:
                self.warning(
                    f"reftest_errorsummary.log for task: {task_id} has unsupported test '{test}'"
                )
                continue
            status = summary.get(STATUS, "")
            if status not in [FAIL, PASS]:
                self.warning(
                    f"reftest_errorsummary.log for task: {task_id} has unknown status: {status} for '{test}'"
                )
                continue
            error = summary.get(SUBTEST, "")
            mods = summary[MODIFIERS]
            allmods = " ".join(mods)
            if group not in self.error_summary:
                self.error_summary[group] = {}
            if allmods not in self.error_summary[group]:
                self.error_summary[group][allmods] = {}
            self.error_summary[group][allmods][TEST] = test
            lineno = self.error_summary[group][allmods].get(LINENO, 0)
            if lineno == 0:
                lineno = self.reftest_find_lineno(group, mods, allmods)
                if lineno > 0:
                    self.error_summary[group][allmods][LINENO] = lineno
            if RUNS not in self.error_summary[group][allmods]:
                self.error_summary[group][allmods][RUNS] = {}
            if task_id not in self.error_summary[group][allmods][RUNS]:
                self.error_summary[group][allmods][RUNS][task_id] = {}
            self.error_summary[group][allmods][RUNS][task_id][ERROR] = error
            if self._subtest_rx is None:
                self._subtest_rx = re.compile(SUBTEST_REGEX)
            m = self._subtest_rx.findall(error)
            if len(m) == 1:
                difference = int(m[0][0])
                pixels = int(m[0][1])
            else:
                difference = 0
                pixels = 0
            if difference > 0:
                self.error_summary[group][allmods][RUNS][task_id][DIFFERENCE] = (
                    difference
                )
            if pixels > 0:
                self.error_summary[group][allmods][RUNS][task_id][PIXELS] = pixels
            if status != FAIL:
                self.error_summary[group][allmods][RUNS][task_id][STATUS] = status
            if test == path:
                allpaths.append(test)
        return allpaths

    def find_known_intermittent(
        self,
        repo: str,
        revision: str,
        task_id: str,
        manifest: str,
        filename: str,
        skip_if: str,
    ) -> TupleOptIntStrOptInt:
        """
        Returns bugid if a known intermittent is found.
        Also returns a suggested comment to be added to the known intermittent
        bug... (currently not implemented). The args
          manifest, filename, skip_if
        are only used to create the comment
        """
        bugid = None
        suggestions: JSONType = None
        line_number: OptInt = None
        comment: str = f'Intermittent failure in manifest: "{manifest}"'
        comment += f'\n  in test: "[{filename}]"'
        comment += f'\n     added skip-if: "{skip_if}"'
        if revision is not None and repo is not None:
            push_id = self.get_push_id(revision, repo)
            if push_id is not None:
                job_id = self.get_job_id(push_id, task_id)
                if job_id is not None:
                    suggestions: JSONType = self.cached_bug_suggestions(
                        repo, revision, job_id
                    )
        if suggestions is not None:
            top: JSONType = None
            for suggestion in suggestions:
                path_end = suggestion.get("path_end", None)
                search: str = suggestion.get("search", "")
                if (
                    path_end is not None
                    and path_end.endswith(filename)
                    and (
                        search.startswith("PROCESS-CRASH")
                        or (search.startswith("TEST-UNEXPECTED") and top is None)
                    )
                ):
                    top = suggestion
            if top is not None:
                recent_bugs = top.get("bugs", {}).get("open_recent", [])
                for bug in recent_bugs:
                    summary: str = bug.get("summary", "")
                    if summary.endswith("single tracking bug"):
                        bugid: int = bug.get("id", None)
                        line_number = top["line_number"] + 1
                        log_url: str = f"https://treeherder.mozilla.org/logviewer?repo={repo}&job_id={job_id}&lineNumber={line_number}"
                        comment += f"\nError log line {line_number}: {log_url}"
        return (bugid, comment, line_number)

    def error_log_context(self, revision: str, task_id: str, line_number: int) -> str:
        context: str = ""
        context_path: str = self.cached_path(
            revision, f"context-{task_id}-{line_number}.txt"
        )
        path = Path(context_path)
        if path.exists():
            self.vinfo(
                f"Reading cached error log context for revision: {revision} task-id: {task_id} line: {line_number}"
            )
            context = path.read_text(encoding="utf-8")
        else:
            delta: int = 10
            log_url = f"https://firefoxci.taskcluster-artifacts.net/{task_id}/0/public/logs/live_backing.log"
            self.vinfo(
                f"Retrieving error log context for revision: {revision} task-id: {task_id} line: {line_number}"
            )
            r = requests.get(log_url, headers=self.headers)
            if r.status_code != 200:
                self.warning(f"Unable to get log for task: {task_id}")
                return context
            log: str = r.text
            n: int = len(log)
            i: int = 0
            j: int = log.find("\n", i)
            if j < 0:
                j = n
            line: int = 1
            prefix: str
            while i < n:
                if line >= line_number - delta and line <= line_number + delta:
                    prefix = f"{line:6d}"
                    if line == line_number:
                        prefix = prefix.replace(" ", ">")
                    context += f"{prefix}: {log[i:j]}\n"
                i = j + 1
                j = log.find("\n", i)
                if j < 0:
                    j = n
                line += 1
            path.write_text(context, encoding="utf-8")
        return context

    def read_actions(self, meta_bug_id: int):
        cache_dir = self.full_path(CACHE_DIR)
        meta_dir = os.path.join(cache_dir, str(meta_bug_id))
        actions_path = os.path.join(meta_dir, "actions.json")
        if not os.path.exists(meta_dir):
            self.vinfo(f"creating meta_bug_id cache dir: {meta_dir}")
            os.mkdir(meta_dir)
        actions: DictAction = {}
        if os.path.exists(actions_path):
            actions = read_json(actions_path)
        for k in actions:
            if k not in self.actions:  # do not supercede newly created actions
                self.actions[k] = Action(**actions[k])

    def write_actions(self, meta_bug_id: int):
        cache_dir = self.full_path(CACHE_DIR)
        meta_dir = os.path.join(cache_dir, str(meta_bug_id))
        actions_path = os.path.join(meta_dir, "actions.json")
        if not os.path.exists(meta_dir):
            self.vinfo(f"creating meta_bug_id cache dir: {meta_dir}")
            os.mkdir(meta_dir)
        write_json(actions_path, self.actions)
