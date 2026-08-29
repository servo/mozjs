# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import buildconfig
import mozunit
import mozversioncontrol

# List of moz.build that currently hold a LegacyTest
# *Don't touch that dial!*

allowlist = {
    "ipc/ipdl/test/ipdl/moz.build",
    "js/src/moz.build",
    "js/src/build/moz.build",
    "memory/replace/logalloc/replay/moz.build",
    "toolkit/xre/test/win/moz.build",
}


def test_extra_legacy_tests():
    repo = mozversioncontrol.get_repository_object(buildconfig.topsrcdir)
    for src, fd in repo.get_tracked_files_finder().find("**/moz.build"):
        if src in allowlist:
            continue
        assert b"LegacyTest(" not in fd.read(), (
            f"LegacyTest used in {src}, please refrain and use another test kind."
        )


if __name__ == "__main__":
    mozunit.main()
