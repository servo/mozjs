#!/usr/bin/env python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
from unittest import mock

import mozunit
import pytest
from mozbuild.base import MachCommandBase  # noqa

from mozperftest.runner import main
from mozperftest.utils import silence


def test_main():
    with pytest.raises(SystemExit), silence():
        main(["--help"])


@mock.patch("mozperftest.PerftestArgumentParser.parse_args")
def test_main_perf_flags(mocked_argparser, set_perf_flags):
    mocked_parse_args = mock.MagicMock()
    mocked_argparser.return_value = mocked_parse_args
    with mock.patch(
        "mozperftest.runner._activate_virtualenvs", return_value="fake_path"
    ) as _, mock.patch(
        "mozperftest.runner.run_tests", return_value="fake_path"
    ) as _, silence():
        main([""])

    assert "--gecko-profile" in mocked_argparser.call_args[1]["args"]


def test_tools():
    with mock.patch(
        "mozperftest.runner._activate_virtualenvs", return_value="fake_path"
    ) as _:
        with pytest.raises(SystemExit), silence():
            main(["tools"])


@mock.patch("mozperftest.utils.install_package")
@mock.patch("mozperftest.PerftestToolsArgumentParser")
def test_side_by_side(arg, patched_mozperftest_tools):
    with mock.patch(
        "mozperftest.runner._activate_virtualenvs", return_value="fake_path"
    ) as _, mock.patch(
        "mozperftest.runner._create_artifacts_dir", return_value="fake_path"
    ) as _, mock.patch(
        "mozperftest.runner._save_params", return_value="fake_path"
    ) as _, mock.patch(
        "sys.modules", return_value=mock.MagicMock()
    ) as _:
        main(
            [
                "tools",
                "side-by-side",
                "-t",
                "fake-test-name",
            ]
        )


if __name__ == "__main__":
    mozunit.main()
