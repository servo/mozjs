# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import subprocess
from pathlib import Path

import mozunit
import pytest

from mozversioncontrol import get_repository_object


def setup_hg_test_paths(working_dir):
    """Set up hg paths for testing."""
    hgrc_path = Path(working_dir) / ".hg" / "hgrc"
    with open(hgrc_path, "w") as f:
        f.write("[paths]\n")
        f.write("default = https://example.com/fake-repo\n")
        f.write("default-push = ssh://example.com/fake-repo\n")
        f.write("try = https://example.com/fake-try-repo\n")
        f.write("try-push = ssh://example.com/fake-try-repo\n")


STEPS = {
    "hg": [setup_hg_test_paths],
    "git": [
        """
        git checkout -b feature-branch
        git remote add origin https://example.com/fake-repo.git
        git remote set-url --push origin git@example.com:fake-repo.git
        git config remote.pushDefault origin
        git fetch origin || true
        git branch --set-upstream-to=upstream/master feature-branch
        """,
        """
        git remote add fork https://example.com/fake-fork-repo.git
        git remote set-url --push fork git@example.com:fake-fork-repo.git
        """,
    ],
    "jj": [
        r"""
        jj config set --repo git.fetch "[\"origin\"]"
        jj config set --repo git.push fork
        jj git remote add origin https://example.com/fake-repo.git
        jj git remote add fork git@example.com:fake-fork-repo.git
        """,
    ],
    "src": [],
}


def test_get_remote_url_default(repo):
    vcs = get_repository_object(repo.dir)

    if vcs.name == "src":
        assert vcs.get_remote_url() is None
        return

    repo.execute_next_step()

    url = vcs.get_remote_url()
    expected_urls = {
        "hg": "https://example.com/fake-repo",
        "git": "../remoterepo",  # Git tracks upstream/master from conftest setup
        "jj": "https://example.com/fake-repo.git",
    }
    assert url == expected_urls[vcs.name]


def test_get_remote_url_push(repo):
    vcs = get_repository_object(repo.dir)

    if vcs.name == "src":
        assert vcs.get_remote_url(push=True) is None
        return

    # Execute setup step to configure remotes
    repo.execute_next_step()

    # Test getting push URL
    push_url = vcs.get_remote_url(push=True)

    expected_push_urls = {
        "hg": "ssh://example.com/fake-repo",
        "git": "git@example.com:fake-repo.git",
        "jj": "git@example.com:fake-fork-repo.git",
    }
    assert push_url == expected_push_urls[vcs.name]


@pytest.mark.parametrize(
    "remote,push,vcs_expected",
    [
        # Test various remotes for each VCS with dummy URLs
        ("try", False, {"hg": "https://example.com/fake-try-repo"}),
        ("try", True, {"hg": "ssh://example.com/fake-try-repo"}),
        (
            "fork",
            False,
            {
                "git": "https://example.com/fake-fork-repo.git",
                "jj": "git@example.com:fake-fork-repo.git",
            },
        ),
        ("fork", True, {"git": "git@example.com:fake-fork-repo.git"}),
        (
            "origin",
            False,
            {
                "git": "https://example.com/fake-repo.git",
                "jj": "https://example.com/fake-repo.git",
            },
        ),
        ("origin", True, {"git": "git@example.com:fake-repo.git"}),
        ("nonexistent", False, {"hg": None, "git": None, "jj": None}),
    ],
)
def test_get_remote_url_specific(repo, remote, push, vcs_expected):
    vcs = get_repository_object(repo.dir)

    if vcs.name == "src":
        pytest.skip("src doesn't support remotes")

    if vcs.name not in vcs_expected:
        pytest.skip(f"Remote '{remote}' not tested for {vcs.name}")

    repo.execute_next_step()

    if vcs.name == "git" and "fork" in remote:
        repo.execute_next_step()

    url = vcs.get_remote_url(remote, push=push)
    assert url == vcs_expected[vcs.name]


def test_get_remote_url_unconfigured(repo):
    vcs = get_repository_object(repo.dir)

    if vcs.name == "src":
        assert vcs.get_remote_url() is None
        return

    url = vcs.get_remote_url()
    assert url is None or "remoterepo" in url


def test_get_remote_url_with_no_branch_tracking(repo):
    vcs = get_repository_object(repo.dir)

    if vcs.name != "git":
        pytest.skip("Test only relevant for git")

    repo.execute_next_step()
    subprocess.check_call(["git", "checkout", "-b", "orphan-branch"], cwd=repo.dir)

    # Should return None when no remote is tracked
    url = vcs.get_remote_url()
    assert url is None

    # But specifying a remote explicitly should work
    url = vcs.get_remote_url("origin")
    assert url == "https://example.com/fake-repo.git"


if __name__ == "__main__":
    mozunit.main()
