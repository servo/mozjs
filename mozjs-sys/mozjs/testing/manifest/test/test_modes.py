# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
from pathlib import Path

import mozunit
import pytest
from manifestparser.toml import Mode
from skipfails import FAILURE_RATIO, Skipfails, read_json, write_json

DATA_PATH = Path(__file__).with_name("data")
REVISION: str = "7cf7a9720f4ead03213f1799f3dcc00a413c7a02"
RETRIEVING = "Retrieving"
TRY_URL: str = f"https://treeherder.mozilla.org/jobs?repo=try&revision={REVISION}"
META_BUG_ID: str = "1991977"
TEXT_FILE = 0
JSON_FILE = 1
TASKS_FILE = 1


def clear_cache(sf: Skipfails):
    sf.clear_cache = META_BUG_ID
    sf.check_cache()
    sf.clear_cache = REVISION
    sf.check_cache()


def copy_to_cache(skipfails: Skipfails, filename: str, kind: int = JSON_FILE):
    from_path = DATA_PATH.joinpath(filename)
    to_path = skipfails.cached_path(REVISION, filename)
    if kind == TEXT_FILE:
        with open(from_path) as from_fp, open(to_path, "w") as to_fp:
            to_fp.write(from_fp.read())
    elif kind == JSON_FILE:
        data = read_json(from_path)
        write_json(to_path, data)
    else:  # TASKS_FILE
        data = skipfails.read_tasks(from_path)
        skipfails.write_tasks(to_path, data)


def cache_vs_expected(
    skipfails: Skipfails,
    to_filename: str,
    from_filename: str = "",
    cache_dir: str = REVISION,
):
    from_path = DATA_PATH.joinpath(from_filename if from_filename else to_filename)
    to_path = skipfails.cached_path(cache_dir, to_filename)
    with open(from_path) as from_fp, open(to_path) as to_fp:
        from_data = from_fp.read()
        to_data = to_fp.read()
    return (to_data, from_data)


@pytest.fixture(scope="session")
def skipfails():
    sf = Skipfails(
        None,  # command_context
        TRY_URL,
        True,  # verbose
        "disable",  # bugzilla
        False,  # dry_run
        False,  # turbo
        False,  # implicit_vars
        None,  # new_version
        None,  # task_id
        None,  # user_agent
        None,  # clear_cache
    )
    clear_cache(sf)
    yield sf


def test_carryover_mode(skipfails: Skipfails, capsys):
    "Test --carryover"

    copy_to_cache(skipfails, "tasks.json", TASKS_FILE)
    copy_to_cache(skipfails, "job_ids.json")
    copy_to_cache(skipfails, "browser.toml", TEXT_FILE)
    copy_to_cache(skipfails, "suggest-531522970.json")
    copy_to_cache(skipfails, "suggest-531522979.json")
    copy_to_cache(skipfails, "suggest-531523119.json")
    copy_to_cache(skipfails, "context-O304PG2lSOuef7JzFoSaow-12581.txt", TEXT_FILE)
    copy_to_cache(skipfails, "context-BarnNoFwSCGQtnNGH-o08w-4028.txt", TEXT_FILE)

    mode: int = Mode.CARRYOVER
    skipfails.run(
        META_BUG_ID,
        None,  # save_tasks
        None,  # use_tasks
        None,  # save_failures
        None,  # use_failures
        -1,  # max_failures
        FAILURE_RATIO,  # failure_ratio: float = FAILURE_RATIO,
        mode,
    )

    out, err = capsys.readouterr()
    # save STDERR for debugging (don't clear cache at the end)
    err_path = skipfails.cached_path(REVISION, "err-carryover.log")
    with open(err_path, "w") as fp:
        fp.write(err)
    mode_string = "Carryover mode: only platform match conditions considered, no bugs created or updated"
    assert mode_string in err
    assert RETRIEVING not in err

    failures, failures_expected = cache_vs_expected(skipfails, "failures.json")
    assert failures == failures_expected

    manifest, manifest_expected = cache_vs_expected(
        skipfails, "browser.toml", "browser-carryover.toml"
    )
    assert manifest == manifest_expected

    actions, actions_expected = cache_vs_expected(
        skipfails, "actions.json", "actions-carryover.json", META_BUG_ID
    )
    assert actions == actions_expected


def test_known_intermittents_mode(skipfails: Skipfails, capsys):
    "Test --known-intermittents"

    mode: int = Mode.KNOWN_INTERMITTENT
    skipfails.run(
        META_BUG_ID,
        None,  # save_tasks
        None,  # use_tasks
        None,  # save_failures
        None,  # use_failures
        -1,  # max_failures
        FAILURE_RATIO,  # failure_ratio: float = FAILURE_RATIO,
        mode,
    )

    out, err = capsys.readouterr()
    # save STDERR for debugging (don't clear cache at the end)
    err_path = skipfails.cached_path(REVISION, "err-known.log")
    with open(err_path, "w") as fp:
        fp.write(err)
    mode_string = "Known Intermittents mode: only failures with known intermittents considered, no bugs created or updated"
    assert mode_string in err
    assert RETRIEVING not in err

    manifest, manifest_expected = cache_vs_expected(
        skipfails, "browser.toml", "browser-known.toml"
    )
    assert manifest == manifest_expected

    actions, actions_expected = cache_vs_expected(
        skipfails, "actions.json", "actions-known.json", META_BUG_ID
    )
    assert actions == actions_expected


def test_new_failures_mode(skipfails: Skipfails, capsys):
    "Test --new-failures"

    mode: int = Mode.NEW_FAILURE
    skipfails.run(
        META_BUG_ID,
        None,  # save_tasks
        None,  # use_tasks
        None,  # save_failures
        None,  # use_failures
        -1,  # max_failures
        FAILURE_RATIO,  # failure_ratio: float = FAILURE_RATIO,
        mode,
    )

    out, err = capsys.readouterr()
    # save STDERR for debugging (don't clear cache at the end)
    err_path = skipfails.cached_path(REVISION, "err-new.log")
    with open(err_path, "w") as fp:
        fp.write(err)
    mode_string = "New failures mode: Will only edit manifest skip-if conditions for new failures (i.e. not carryover nor known intermittents)"
    assert mode_string in err
    assert RETRIEVING not in err

    manifest, manifest_expected = cache_vs_expected(
        skipfails, "browser.toml", "browser-new.toml"
    )
    assert manifest == manifest_expected

    actions, actions_expected = cache_vs_expected(
        skipfails, "actions.json", "actions-new.json", META_BUG_ID
    )
    assert actions == actions_expected


def test_replace_tbd_mode(skipfails: Skipfails, capsys):
    "Test --replace-tbd"

    mode: int = Mode.REPLACE_TBD
    skipfails.run(
        META_BUG_ID,
        None,  # save_tasks
        None,  # use_tasks
        None,  # save_failures
        None,  # use_failures
        -1,  # max_failures
        FAILURE_RATIO,  # failure_ratio: float = FAILURE_RATIO,
        mode,
    )

    out, err = capsys.readouterr()
    # save STDERR for debugging (don't clear cache at the end)
    err_path = skipfails.cached_path(REVISION, "err-replace.log")
    with open(err_path, "w") as fp:
        fp.write(err)
    mode_string = "Replace TBD mode: Will only edit manifest skip-if conditions for new failures by filing new bugs and replacing TBD with actual bug number."
    assert mode_string in err
    assert RETRIEVING not in err

    carryover = "Bugzilla has been disabled: comment not added to Bug 1111111"
    assert carryover in err

    intermittent = "Error log line 4028: https://treeherder.mozilla.org/logviewer?repo=try&job_id=531522970&lineNumber=4028"
    assert intermittent in err

    new = "Error log line 12581: https://treeherder.mozilla.org/logviewer?repo=try&job_id=531523119&lineNumber=12581"
    assert new in err


def test_cleanup(skipfails: Skipfails):
    clear_cache(skipfails)
    tasks_cached = skipfails.cached_path(REVISION, "tasks.json")
    assert not os.path.exists(tasks_cached)


if __name__ == "__main__":
    mozunit.main()
