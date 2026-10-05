# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import subprocess

import mozunit
import pytest

from mozversioncontrol import get_repository_object


def test_add_note(repo):
    if repo.vcs not in ("git", "jj"):
        pytest.skip("add_note only implemented for git and jj")

    vcs = get_repository_object(repo.dir)

    vcs.add_note("refs/notes/test", "hello from test")

    commit = vcs.head_rev
    result = subprocess.run(
        ["git", "notes", "--ref", "refs/notes/test", "show", commit],
        cwd=str(repo.dir),
        capture_output=True,
        text=True,
        check=True,
    )
    assert result.stdout.strip() == "hello from test"


if __name__ == "__main__":
    mozunit.main()
