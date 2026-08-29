import mozunit
import pytest

from mozversioncontrol import get_repository_object
from mozversioncontrol.errors import MissingVCSExtension


def test_prepare_try_push_cleanup_restores_state(repo):
    commit_message = "try: test cleanup"
    vcs = get_repository_object(repo.dir)
    initial_head = vcs.head_ref

    try:
        head, cleanup_fn = vcs.prepare_try_push(
            commit_message, {"test.txt": "test content"}
        )

        if vcs.name != "src":
            assert head != initial_head, "Head should have changed"

        cleanup_fn()

        assert vcs.head_ref == initial_head, (
            "Head should be restored to initial state after cleanup"
        )
    except MissingVCSExtension:
        pytest.xfail("Requires the Mercurial evolve extension.")


def test_prepare_try_push_cleanup_idempotent(repo):
    commit_message = "try: idempotent cleanup"
    vcs = get_repository_object(repo.dir)
    initial_head = vcs.head_ref

    try:
        head, cleanup_fn = vcs.prepare_try_push(commit_message)

        cleanup_fn()
        assert vcs.head_ref == initial_head

        cleanup_fn()
        assert vcs.head_ref == initial_head, (
            "Cleanup should be idempotent and safe to call multiple times"
        )
    except MissingVCSExtension:
        pytest.xfail("Requires the Mercurial evolve extension.")


if __name__ == "__main__":
    mozunit.main()
