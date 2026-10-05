# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this,
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import subprocess

import mozunit
import pytest

from mozversioncontrol import get_repository_object

STEPS = {
    "hg": [
        """
        echo "second" > second
        hg add second
        hg commit -m "second commit"
        """,
    ],
    "git": [
        """
        echo "second" > second
        git add second
        git commit -m "second commit"
        """,
    ],
    "jj": [
        """
        echo "second" > second
        jj commit -m "second commit"
        jj bookmark create test-bookmark -r @-
        jj bookmark track test-bookmark --remote upstream
        """,
    ],
}


def verify_push_succeeded(repo):
    if repo.vcs == "hg":
        result = subprocess.run(
            ["hg", "log", "-r", "tip", "-T", "{desc}"],
            cwd=str(repo.dir.parent / "remoterepo"),
            capture_output=True,
            text=True,
            check=True,
        )
        assert "second commit" in result.stdout
    elif repo.vcs == "git":
        subprocess.run(
            ["git", "fetch"],
            cwd=str(repo.dir.parent / "remoterepo"),
            check=True,
        )
        result = subprocess.run(
            ["git", "log", "master", "-1", "--format=%s"],
            cwd=str(repo.dir.parent / "remoterepo"),
            capture_output=True,
            text=True,
            check=True,
        )
        assert "second commit" in result.stdout
    elif repo.vcs == "jj":
        subprocess.run(
            ["jj", "git", "fetch", "--remote", "upstream"],
            cwd=str(repo.dir),
            check=True,
        )
        result = subprocess.run(
            [
                "jj",
                "bookmark",
                "list",
                "--remote",
                "upstream",
                "test-bookmark",
            ],
            cwd=str(repo.dir),
            capture_output=True,
            text=True,
            check=True,
        )
        assert "second commit" in result.stdout


@pytest.mark.parametrize(
    "remote,ref,kwargs",
    [
        pytest.param(None, None, {}, id="no_args"),
        pytest.param("remote", None, {}, id="with_remote"),
        pytest.param("remote", "ref", {}, id="with_remote_and_ref"),
        pytest.param("remote", "ref", {"force": True}, id="with_force"),
    ],
)
def test_push(repo, remote, ref, kwargs):
    vcs = get_repository_object(repo.dir)

    repo.execute_next_step()

    if remote == "remote":
        if repo.vcs == "hg":
            remote = "../remoterepo"
        elif repo.vcs == "git":
            remote = "upstream"
        elif repo.vcs == "jj":
            remote = "upstream"

    if ref == "ref":
        if repo.vcs == "hg":
            ref = "."
        elif repo.vcs == "git":
            ref = "master"
        elif repo.vcs == "jj":
            ref = "test-bookmark"

    if ref is None and repo.vcs == "jj":
        pytest.skip("jj requires a bookmark ref to push")

    vcs.push(remote=remote, ref=ref, **kwargs)
    verify_push_succeeded(repo)


def test_push_ref_without_remote_raises(repo):
    vcs = get_repository_object(repo.dir)

    with pytest.raises(
        ValueError, match="Cannot specify ref without specifying remote"
    ):
        vcs.push(ref="some-ref")


def test_jj_push_url_to_name_translation(repo):
    """Test that jj translates git URLs to remote names"""
    if repo.vcs != "jj":
        pytest.skip("Only relevant for jj repos")

    vcs = get_repository_object(repo.dir)
    repo.execute_next_step()

    # Get the actual remote URL
    result = subprocess.run(
        ["jj", "git", "remote", "list"],
        cwd=str(repo.dir),
        capture_output=True,
        text=True,
        check=True,
    )

    # Extract the upstream URL from output
    for line in result.stdout.strip().splitlines():
        if line.startswith("upstream "):
            upstream_url = line.split(" ", 1)[1]
            break

    # Push using URL should work (it gets translated to "upstream")
    vcs.push(remote=upstream_url, ref="test-bookmark")


@pytest.mark.parametrize(
    "with_dest",
    [False, True],
)
def test_push_dest_branch(repo, with_dest):
    if repo.vcs == "hg":
        pytest.skip("Mercurial ignores dest_branch")

    vcs = get_repository_object(repo.dir)

    if not with_dest:
        with pytest.raises(
            ValueError, match="Cannot specify dest_branch without specifying ref"
        ):
            vcs.push(remote="upstream", dest_branch="try")
        return

    repo.execute_next_step()

    if repo.vcs == "git":
        vcs.push(remote="upstream", ref="HEAD", dest_branch="try")
        subprocess.run(
            ["git", "fetch"],
            cwd=str(repo.dir.parent / "remoterepo"),
            check=True,
        )
        result = subprocess.run(
            ["git", "log", "try", "-1", "--format=%s"],
            cwd=str(repo.dir.parent / "remoterepo"),
            capture_output=True,
            text=True,
            check=True,
        )
        assert "second commit" in result.stdout
    elif repo.vcs == "jj":
        change_id = vcs._resolve_to_change("test-bookmark")
        subprocess.run(
            ["jj", "bookmark", "create", "try", "-r", change_id],
            cwd=str(repo.dir),
            check=True,
        )
        subprocess.run(
            ["jj", "bookmark", "track", "try", "--remote", "upstream"],
            cwd=str(repo.dir),
            check=True,
        )
        vcs.push(remote="upstream", ref="test-bookmark", dest_branch="try")
        subprocess.run(
            ["jj", "git", "fetch", "--remote", "upstream"],
            cwd=str(repo.dir),
            check=True,
        )
        result = subprocess.run(
            ["jj", "bookmark", "list", "--remote", "upstream", "try"],
            cwd=str(repo.dir),
            capture_output=True,
            text=True,
            check=True,
        )
        assert "second commit" in result.stdout


def test_jj_push_change_id_with_dest_branch(repo):
    """Simulate push_to_try: a change ID as ref is resolved to a commit SHA
    when dest_branch is set, allowing git to push it to a named remote branch.
    """
    if repo.vcs != "jj":
        pytest.skip("Only relevant for jj repos")

    vcs = get_repository_object(repo.dir)
    repo.execute_next_step()

    change_id = vcs._resolve_to_change(vcs.HEAD_REVSET)
    vcs.push(remote="upstream", ref=change_id, dest_branch="test-bookmark")

    subprocess.run(
        ["jj", "git", "fetch", "--remote", "upstream"],
        cwd=str(repo.dir),
        check=True,
    )
    result = subprocess.run(
        ["jj", "bookmark", "list", "--remote", "upstream", "test-bookmark"],
        cwd=str(repo.dir),
        capture_output=True,
        text=True,
        check=True,
    )
    assert "second commit" in result.stdout


if __name__ == "__main__":
    mozunit.main()
