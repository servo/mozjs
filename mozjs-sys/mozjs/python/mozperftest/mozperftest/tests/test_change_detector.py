#!/usr/bin/env python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import pathlib
from unittest import mock

import mozunit
import pytest

from mozperftest.tests.support import temp_file
from mozperftest.tools import PerformanceChangeDetected, run_change_detector


@pytest.mark.asyncio
async def test_change_detector_basic(kwargs=None, return_value=({}, {})):
    mocked_detector = mock.MagicMock()
    mocked_detector_module = mock.MagicMock()
    mocked_detector_module.ChangeDetector = mocked_detector

    with mock.patch.dict(
        "sys.modules",
        {
            "mozperftest_tools.regression_detector": mocked_detector_module,
        },
    ):
        mocked_detector.return_value.detect_changes.return_value = return_value

        with temp_file() as f:
            parent_dir = pathlib.Path(f).parent

            if kwargs is None:
                kwargs = {
                    "test_name": "browsertime-test",
                    "new_test_name": None,
                    "platform": "test-platform/opt",
                    "new_platform": None,
                    "base_branch": "try",
                    "new_branch": "try",
                    "base_revision": "99",
                    "new_revision": "99",
                }

            run_change_detector(parent_dir, kwargs)

        mocked_detector.return_value.detect_changes.assert_called()

    return mocked_detector_module


@pytest.mark.asyncio
async def test_change_detector_with_task_name():
    await test_change_detector_basic({
        "task_names": ["test-platform/opt-browsertime-test"],
        "new_test_name": None,
        "platform": None,
        "new_platform": None,
        "base_branch": "try",
        "new_branch": "try",
        "base_revision": "99",
        "new_revision": "99",
    })


@pytest.mark.asyncio
async def test_change_detector_option_failure():
    with pytest.raises(Exception):
        await test_change_detector_basic({
            "test_name": None,
            "new_test_name": None,
            "platform": "test-platform/opt",
            "new_platform": None,
            "base_branch": "try",
            "new_branch": "try",
            "base_revision": "99",
            "new_revision": "99",
        })

    with pytest.raises(Exception):
        await test_change_detector_basic({
            "test_name": "browsertime-test",
            "new_test_name": None,
            "platform": None,
            "new_platform": None,
            "base_branch": "try",
            "new_branch": "try",
            "base_revision": "99",
            "new_revision": "99",
        })


@pytest.mark.asyncio
async def test_change_detector_with_detection():
    with pytest.raises(PerformanceChangeDetected):
        await test_change_detector_basic(
            {
                "task_names": ["test-platform/opt-browsertime-test"],
                "new_test_name": None,
                "platform": None,
                "new_platform": None,
                "base_branch": "try",
                "new_branch": "try",
                "base_revision": "99",
                "new_revision": "99",
            },
            (["detection"], {"warm": {"metric": {"detection": [99]}}, "cold": {}}),
        )


if __name__ == "__main__":
    mozunit.main()
