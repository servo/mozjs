# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import unittest
import zipfile
from shutil import rmtree
from tempfile import mkdtemp

import mozunit

from mozbuild.action.zip import main as zip_main


class TestZipAction(unittest.TestCase):
    def setUp(self):
        self.tmpdir = mkdtemp()

    def tearDown(self):
        rmtree(self.tmpdir)

    def _make_file(self, name, content=b""):
        path = os.path.join(self.tmpdir, name)
        with open(path, "wb") as fh:
            fh.write(content)
        return path

    def _zip_contents(self, zip_path):
        with zipfile.ZipFile(zip_path) as z:
            return sorted(z.namelist())

    def test_files_from_basic(self):
        self._make_file("a.txt", b"a")
        self._make_file("b.txt", b"b")
        self._make_file("c.txt", b"c")

        manifest = os.path.join(self.tmpdir, "manifest.list")
        with open(manifest, "w", encoding="utf-8") as fh:
            fh.write("a.txt\n")
            fh.write("c.txt\n")

        out = os.path.join(self.tmpdir, "out.zip")
        zip_main(["-C", self.tmpdir, "--files-from", manifest, out])

        self.assertEqual(self._zip_contents(out), ["a.txt", "c.txt"])

    def test_files_from_ignores_blank_and_comment_lines(self):
        self._make_file("a.txt", b"a")
        self._make_file("b.txt", b"b")

        manifest = os.path.join(self.tmpdir, "manifest.list")
        with open(manifest, "w", encoding="utf-8") as fh:
            fh.write("# comment\n")
            fh.write("\n")
            fh.write("a.txt\n")
            fh.write("   \n")
            fh.write("# trailing\n")
            fh.write("b.txt\n")

        out = os.path.join(self.tmpdir, "out.zip")
        zip_main(["-C", self.tmpdir, "--files-from", manifest, out])

        self.assertEqual(self._zip_contents(out), ["a.txt", "b.txt"])

    def test_files_from_combined_with_positional(self):
        self._make_file("a.txt", b"a")
        self._make_file("b.txt", b"b")
        self._make_file("c.txt", b"c")

        manifest = os.path.join(self.tmpdir, "manifest.list")
        with open(manifest, "w", encoding="utf-8") as fh:
            fh.write("b.txt\n")

        out = os.path.join(self.tmpdir, "out.zip")
        zip_main(["-C", self.tmpdir, "--files-from", manifest, out, "a.txt"])

        self.assertEqual(self._zip_contents(out), ["a.txt", "b.txt"])

    def test_no_inputs_errors(self):
        out = os.path.join(self.tmpdir, "out.zip")
        with self.assertRaises(SystemExit):
            zip_main(["-C", self.tmpdir, out])


if __name__ == "__main__":
    mozunit.main()
