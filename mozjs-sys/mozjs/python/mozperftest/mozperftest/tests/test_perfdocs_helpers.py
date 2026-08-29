# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import pathlib
import tempfile
from unittest import mock
from unittest.mock import MagicMock

import pytest

testdata = [
    {
        "table_specifications": {
            "title": ["not a string"],
            "widths": [10, 10, 10, 10],
            "header_rows": 1,
            "headers": [["Coconut 1", "Coconut 2", "Coconut 3"]],
            "indent": 2,
        },
        "error_msg": "TableBuilder attribute title must be a string.",
    },
    {
        "table_specifications": {
            "title": "I've got a lovely bunch of coconuts",
            "widths": ("not", "a", "list"),
            "header_rows": 1,
            "headers": [["Coconut 1", "Coconut 2", "Coconut 3"]],
            "indent": 2,
        },
        "error_msg": "TableBuilder attribute widths must be a list of integers.",
    },
    {
        "table_specifications": {
            "title": "There they are all standing in a row",
            "widths": ["not an integer"],
            "header_rows": 1,
            "headers": [["Coconut 1", "Coconut 2", "Coconut 3"]],
            "indent": 2,
        },
        "error_msg": "TableBuilder attribute widths must be a list of integers.",
    },
    {
        "table_specifications": {
            "title": "Big ones, small ones",
            "widths": [10, 10, 10, 10],
            "header_rows": "not an integer",
            "headers": [["Coconut 1", "Coconut 2", "Coconut 3"]],
            "indent": 2,
        },
        "error_msg": "TableBuilder attribute header_rows must be an integer.",
    },
    {
        "table_specifications": {
            "title": "Some as big as your head!",
            "widths": [10, 10, 10, 10],
            "header_rows": 1,
            "headers": ("not", "a", "list"),
            "indent": 2,
        },
        "error_msg": "TableBuilder attribute headers must be a two-dimensional list of strings.",
    },
    {
        "table_specifications": {
            "title": "(And bigger)",
            "widths": [10, 10, 10, 10],
            "header_rows": 1,
            "headers": ["not", "two", "dimensional"],
            "indent": 2,
        },
        "error_msg": "TableBuilder attribute headers must be a two-dimensional list of strings.",
    },
    {
        "table_specifications": {
            "title": "Give 'em a twist, a flick of the wrist'",
            "widths": [10, 10, 10, 10],
            "header_rows": 1,
            "headers": [[1, 2, 3]],
            "indent": 2,
        },
        "error_msg": "TableBuilder attribute headers must be a two-dimensional list of strings.",
    },
    {
        "table_specifications": {
            "title": "That's what the showman said!",
            "widths": [10, 10, 10, 10],
            "header_rows": 1,
            "headers": [["Coconut 1", "Coconut 2", "Coconut 3"]],
            "indent": "not an integer",
        },
        "error_msg": "TableBuilder attribute indent must be an integer.",
    },
]

table_specifications = {
    "title": "I've got a lovely bunch of coconuts",
    "widths": [10, 10, 10],
    "header_rows": 1,
    "headers": [["Coconut 1", "Coconut 2", "Coconut 3"]],
    "indent": 2,
}


@pytest.mark.parametrize("testdata", testdata)
def test_table_builder_invalid_attributes(testdata):
    from mozperftest.perfdocs.doc_helpers import TableBuilder

    table_specifications = testdata["table_specifications"]
    error_msg = testdata["error_msg"]

    with pytest.raises(TypeError) as error:
        TableBuilder(
            table_specifications["title"],
            table_specifications["widths"],
            table_specifications["header_rows"],
            table_specifications["headers"],
            table_specifications["indent"],
        )

    assert str(error.value) == error_msg


def test_table_builder_mismatched_columns():
    from mozperftest.perfdocs.doc_helpers import (
        MismatchedRowLengthsException,
        TableBuilder,
    )

    table_specifications = {
        "title": "I've got a lovely bunch of coconuts",
        "widths": [10, 10, 10, 42],
        "header_rows": 1,
        "headers": [["Coconut 1", "Coconut 2", "Coconut 3"]],
        "indent": 2,
    }

    with pytest.raises(MismatchedRowLengthsException) as error:
        TableBuilder(
            table_specifications["title"],
            table_specifications["widths"],
            table_specifications["header_rows"],
            table_specifications["headers"],
            table_specifications["indent"],
        )
    assert (
        str(error.value)
        == "Number of table headers must match number of column widths."
    )


def test_table_builder_add_row_too_long():
    from mozperftest.perfdocs.doc_helpers import (
        MismatchedRowLengthsException,
        TableBuilder,
    )

    table = TableBuilder(
        table_specifications["title"],
        table_specifications["widths"],
        table_specifications["header_rows"],
        table_specifications["headers"],
        table_specifications["indent"],
    )
    with pytest.raises(MismatchedRowLengthsException) as error:
        table.add_row([
            "big ones",
            "small ones",
            "some as big as your head!",
            "(and bigger)",
        ])
    assert (
        str(error.value)
        == "Number of items in a row must must number of columns defined."
    )


def test_table_builder_add_rows_type_error():
    from mozperftest.perfdocs.doc_helpers import TableBuilder

    table = TableBuilder(
        table_specifications["title"],
        table_specifications["widths"],
        table_specifications["header_rows"],
        table_specifications["headers"],
        table_specifications["indent"],
    )
    with pytest.raises(TypeError) as error:
        table.add_rows([
            "big ones",
            "small ones",
            "some as big as your head!",
            "(and bigger)",
        ])
    assert str(error.value) == "add_rows() requires a two-dimensional list of strings."


def test_table_builder_validate():
    from mozperftest.perfdocs.doc_helpers import TableBuilder

    table = TableBuilder(
        table_specifications["title"],
        table_specifications["widths"],
        table_specifications["header_rows"],
        table_specifications["headers"],
        table_specifications["indent"],
    )
    table.add_row(["big ones", "small ones", "some as big as your head!"])
    table.add_row([
        "Give 'em a twist",
        "A flick of the wrist",
        "That's what the showman said!",
    ])
    table = table.finish_table()
    print(table)
    assert (
        table == "  .. list-table:: **I've got a lovely bunch of coconuts**\n"
        "     :widths: 10 10 10\n     :header-rows: 1\n\n"
        "     * - **Coconut 1**\n       - Coconut 2\n       - Coconut 3\n"
        "     * - **big ones**\n       - small ones\n       - some as big as your head!\n"
        "     * - **Give 'em a twist**\n       - A flick of the wrist\n"
        "       - That's what the showman said!\n\n"
    )


def _setup_utils_logger(mock_logger, structured_logger, top_dir):
    from mozperftest.perfdocs.logger import PerfDocLogger

    PerfDocLogger.LOGGER = structured_logger
    PerfDocLogger.PATHS = [str(top_dir)]
    PerfDocLogger.TOP_DIR = top_dir

    import mozperftest.perfdocs.utils as utls

    utls.logger = mock_logger


def test_read_yaml_exception(structured_logger):
    mock_logger = MagicMock()
    with tempfile.TemporaryDirectory() as tmpdir:
        _setup_utils_logger(mock_logger, structured_logger, pathlib.Path(tmpdir))
        from mozperftest.perfdocs.utils import read_yaml

        result = read_yaml(pathlib.Path(tmpdir, "nonexistent.yml"))

    assert result == {}
    assert mock_logger.warning.call_count == 1
    args, _ = mock_logger.warning.call_args
    assert "Error opening file" in args[0]


def test_are_dirs_equal_right_only(structured_logger):
    mock_logger = MagicMock()
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_utils_logger(mock_logger, structured_logger, top_dir)

        dir1 = top_dir / "dir1"
        dir2 = top_dir / "dir2"
        dir1.mkdir()
        dir2.mkdir()
        (dir2 / "extra.txt").write_text("extra")

        from mozperftest.perfdocs.utils import are_dirs_equal

        result = are_dirs_equal(dir1, dir2)

    assert result is False
    log_msgs = [c[0][0] for c in mock_logger.log.call_args_list]
    assert any("Missing in new docs" in m for m in log_msgs)


def test_are_dirs_equal_funny_files(structured_logger):
    mock_logger = MagicMock()
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_utils_logger(mock_logger, structured_logger, top_dir)

        dir1 = top_dir / "dir1"
        dir2 = top_dir / "dir2"
        dir1.mkdir()
        dir2.mkdir()

        fake_dcmp = MagicMock()
        fake_dcmp.left_only = []
        fake_dcmp.right_only = []
        fake_dcmp.funny_files = ["funny.txt"]
        fake_dcmp.common_files = []
        fake_dcmp.common_dirs = []

        with mock.patch("filecmp.dircmp", return_value=fake_dcmp):
            from mozperftest.perfdocs.utils import are_dirs_equal

            result = are_dirs_equal(dir1, dir2)

    assert result is False
    log_msgs = [c[0][0] for c in mock_logger.log.call_args_list]
    assert any("funny" in m for m in log_msgs)


@mock.patch("mozperftest.perfdocs.utils.ON_TRY", False)
def test_are_dirs_equal_mismatch(structured_logger):
    mock_logger = MagicMock()
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_utils_logger(mock_logger, structured_logger, top_dir)

        dir1 = top_dir / "dir1"
        dir2 = top_dir / "dir2"
        dir1.mkdir()
        dir2.mkdir()
        (dir1 / "file.txt").write_text("old content\n")
        (dir2 / "file.txt").write_text("new content\n")

        from mozperftest.perfdocs.utils import are_dirs_equal

        result = are_dirs_equal(dir1, dir2)

    assert result is False
    log_msgs = [c[0][0] for c in mock_logger.log.call_args_list]
    assert any("Mismatch" in m for m in log_msgs)
    assert any("diff" in m.lower() for m in log_msgs)


@mock.patch("mozperftest.perfdocs.utils.ON_TRY", False)
def test_are_dirs_equal_common_dirs(structured_logger):
    mock_logger = MagicMock()
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_utils_logger(mock_logger, structured_logger, top_dir)

        dir1 = top_dir / "dir1"
        dir2 = top_dir / "dir2"
        (dir1 / "sub").mkdir(parents=True)
        (dir2 / "sub").mkdir(parents=True)
        (dir1 / "sub" / "file.txt").write_text("same content")
        (dir2 / "sub" / "file.txt").write_text("same content")

        from mozperftest.perfdocs.utils import are_dirs_equal

        result = are_dirs_equal(dir1, dir2)

    assert result is True


def test_get_changed_files():
    mock_repo = MagicMock()
    mock_repo.get_changed_files.return_value = ["file1.py", "file2.py"]
    mock_repo.get_outgoing_files.return_value = ["file2.py", "file3.py"]

    with mock.patch(
        "mozperftest.perfdocs.utils.get_repository_object", return_value=mock_repo
    ):
        from mozperftest.perfdocs.utils import get_changed_files

        result = get_changed_files(pathlib.Path("/some/path"))

    assert set(result) == {"file1.py", "file2.py", "file3.py"}
