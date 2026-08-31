# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import os
import unittest

import mozunit
from mozpack.manifests import InstallManifest
from mozpack.test.test_files import TestWithTmpDir

from mozbuild.testing import install_test_files


@unittest.skipIf(
    "MOZ_AUTOMATION" in os.environ, "stamp optimization disabled in automation"
)
class TestInstallTestFiles(TestWithTmpDir):
    def _setup_manifest(self, source_file):
        """Write a minimal _test_files manifest symlinking source_file to tests/file."""
        manifests_dir = self.tmppath("obj/_build_manifests/install")
        os.makedirs(manifests_dir)
        m = InstallManifest()
        m.add_link(source_file, "tests/file")
        m.write(path=os.path.join(manifests_dir, "_test_files"))
        return manifests_dir

    def _make_source(self):
        src = self.tmppath("src/file")
        os.makedirs(os.path.dirname(src))
        with open(src, "w") as f:
            f.write("data")
        return src

    def test_no_stamp_for_empty_manifest(self):
        # An empty manifest should not write a stamp.
        manifests_dir = self.tmppath("obj/_build_manifests/install")
        os.makedirs(manifests_dir)
        InstallManifest().write(path=os.path.join(manifests_dir, "_test_files"))
        install_test_files(self.tmppath("src"), self.tmppath("obj"), "_tests")
        self.assertFalse(
            os.path.exists(os.path.join(manifests_dir, ".test_install_stamp"))
        )

    def test_stamp_written_on_symlink_success(self):
        # Stamp is written after a successful install on a symlink-capable filesystem.
        if not self.symlink_supported:
            return
        src = self._make_source()
        manifests_dir = self._setup_manifest(src)
        install_test_files(self.tmppath("src"), self.tmppath("obj"), "_tests")
        self.assertTrue(
            os.path.exists(os.path.join(manifests_dir, ".test_install_stamp"))
        )

    def test_skip_on_second_run(self):
        # Second run with a current stamp returns True to signal the caller to skip.
        if not self.symlink_supported:
            return
        src = self._make_source()
        self._setup_manifest(src)
        install_test_files(self.tmppath("src"), self.tmppath("obj"), "_tests")
        self.assertTrue(
            install_test_files(self.tmppath("src"), self.tmppath("obj"), "_tests")
        )

    def test_force_bypasses_stamp(self):
        # --force forces reinstall even with a current stamp.
        if not self.symlink_supported:
            return
        src = self._make_source()
        self._setup_manifest(src)
        install_test_files(self.tmppath("src"), self.tmppath("obj"), "_tests")
        dest_file = self.tmppath("obj/_tests/tests/file")
        os.remove(dest_file)
        self.assertFalse(os.path.exists(dest_file))
        install_test_files(
            self.tmppath("src"), self.tmppath("obj"), "_tests", force=True
        )
        self.assertTrue(os.path.exists(dest_file))

    def test_pattern_change_invalidates_stamp(self):
        # Adding a file to a pattern source directory changes the expansion hash,
        # so the next run should re-install rather than skip.
        if not self.symlink_supported:
            return
        src_dir = self.tmppath("src")
        os.makedirs(src_dir)
        with open(os.path.join(src_dir, "file"), "w") as f:
            f.write("data")
        manifests_dir = self.tmppath("obj/_build_manifests/install")
        os.makedirs(manifests_dir)
        m = InstallManifest()
        m.add_pattern_link(src_dir, "**", "tests")
        m.write(path=os.path.join(manifests_dir, "_test_files"))
        install_test_files(self.tmppath("src"), self.tmppath("obj"), "_tests")
        with open(os.path.join(src_dir, "new_file"), "w") as f:
            f.write("data")
        self.assertFalse(
            install_test_files(self.tmppath("src"), self.tmppath("obj"), "_tests")
        )

    def test_no_restore_without_force_flag(self):
        # Without --force, a current stamp skips the install and a
        # deleted symlink stays missing.
        if not self.symlink_supported:
            return
        src = self._make_source()
        self._setup_manifest(src)
        install_test_files(self.tmppath("src"), self.tmppath("obj"), "_tests")
        dest_file = self.tmppath("obj/_tests/tests/file")
        os.remove(dest_file)
        install_test_files(self.tmppath("src"), self.tmppath("obj"), "_tests")
        self.assertFalse(os.path.exists(dest_file))


if __name__ == "__main__":
    mozunit.main()
