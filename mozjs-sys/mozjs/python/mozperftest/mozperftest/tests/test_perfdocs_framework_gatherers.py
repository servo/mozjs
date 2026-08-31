# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import json
import pathlib
import tempfile
from unittest import mock
from unittest.mock import MagicMock

import pytest

SAMPLE_YAML = "name: raptor\nmanifest: test.ini\nstatic-only: False\n"
AWSY_YAML = (
    "name: awsy\n"
    "suites:\n"
    "  Awsy tests:\n"
    "    owner: 'Awsy Team'\n"
    "    description: 'Memory tests'\n"
    "    tests:\n"
    "      tp6: ''\n"
)
SAMPLE_INI = "[Example]\ntest_url = https://example.com\nalert_on = fcp\n"
TALOS_JSON = json.dumps({"suites": {"ts": {"tests": ["ts_paint", "ts_paint_cached"]}}})


def _setup_fg_logger(mock_logger, structured_logger, top_dir):
    from mozperftest.perfdocs.logger import PerfDocLogger

    PerfDocLogger.LOGGER = structured_logger
    PerfDocLogger.PATHS = ["perfdocs"]
    PerfDocLogger.TOP_DIR = top_dir

    import mozperftest.perfdocs.framework_gatherers as fg

    fg.logger = mock_logger


def _make_gatherer(cls, tmp_path, yaml_content=SAMPLE_YAML):
    yaml_file = tmp_path / "config.yml"
    yaml_file.write_text(yaml_content)
    return cls(yaml_file, tmp_path)


def _concrete_gatherer(tmp_path, yaml_content=SAMPLE_YAML):
    from mozperftest.perfdocs.framework_gatherers import FrameworkGatherer

    class Concrete(FrameworkGatherer):
        def get_suite_list(self):
            return {}

        def build_metrics_documentation(self, y):
            return []

    return _make_gatherer(Concrete, tmp_path, yaml_content)


def test_get_metric_heading_exact_match(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        mock_logger = MagicMock()
        _setup_fg_logger(mock_logger, structured_logger, top_dir)

        g = _concrete_gatherer(top_dir)
        metrics_info = {"FirstPaint": {"aliases": ["fcp"], "description": "..."}}
        assert g._get_metric_heading("FirstPaint", metrics_info) == "FirstPaint"


def test_get_metric_heading_alias(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)
        g = _concrete_gatherer(top_dir)
        metrics_info = {"FirstPaint": {"aliases": ["fcp"], "description": "..."}}
        assert g._get_metric_heading("fcp", metrics_info) == "FirstPaint"


def test_get_metric_heading_matcher(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)
        g = _concrete_gatherer(top_dir)
        metrics_info = {
            "SpeedIndex": {"aliases": [], "description": "...", "matcher": "Speed.*"}
        }
        assert g._get_metric_heading("SpeedIndex-v2", metrics_info) == "SpeedIndex"


def test_get_metric_heading_not_found(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)
        g = _concrete_gatherer(top_dir)
        with pytest.raises(Exception, match="Could not find a metric heading"):
            g._get_metric_heading(
                "unknown", {"known": {"aliases": [], "description": "..."}}
            )


def test_get_task_match(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)
        g = _concrete_gatherer(top_dir)
        match = g.get_task_match("test-linux64/opt-some-task")
        assert match is not None
        assert match.group(1) == "test-linux64/opt"
        assert match.group(2) == "some-task"
        assert g.get_task_match("noslash") is None


def test_get_manifest_path(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)
        g = _concrete_gatherer(top_dir)
        path = g.get_manifest_path()
        assert path == top_dir / "test.ini"
        # second call returns cached value without re-reading yaml
        with mock.patch("mozperftest.perfdocs.framework_gatherers.read_yaml") as m:
            path2 = g.get_manifest_path()
        assert path2 == path
        m.assert_not_called()


def test_get_suite_list_not_implemented(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        from mozperftest.perfdocs.framework_gatherers import FrameworkGatherer

        class Bare(FrameworkGatherer):
            pass

        g = _make_gatherer(Bare, top_dir)
        with pytest.raises(NotImplementedError):
            g.get_suite_list()


def test_build_metrics_documentation_not_implemented(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        from mozperftest.perfdocs.framework_gatherers import FrameworkGatherer

        class Bare(FrameworkGatherer):
            pass

        g = _make_gatherer(Bare, top_dir)
        with pytest.raises(NotImplementedError):
            g.build_metrics_documentation({})


def test_raptor_get_suite_list_cached(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        from mozperftest.perfdocs.framework_gatherers import RaptorGatherer

        g = _make_gatherer(RaptorGatherer, top_dir)
        g._suite_list = {"suite": ["test.ini"]}

        with mock.patch(
            "mozperftest.perfdocs.framework_gatherers.TestManifest"
        ) as MockTM:
            result = g.get_suite_list()

        assert result == {"suite": ["test.ini"]}
        MockTM.assert_not_called()


def test_raptor_get_ci_tasks_dict_type(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        from mozperftest.perfdocs.framework_gatherers import RaptorGatherer

        g = _make_gatherer(RaptorGatherer, top_dir)
        g._taskgraph = {
            "test-linux64/opt-raptor-tp6": {
                "task": {"payload": {"command": [" '--test tp6 '"]}},
                "attributes": {"run_on_projects": ["mozilla-central"]},
            }
        }
        g._get_ci_tasks()
        assert "tp6" in g._task_list
        assert "test-linux64/opt" in g._task_list["tp6"]
        assert g._task_list["tp6"]["test-linux64/opt"][0]["test_name"] == "raptor-tp6"


def test_raptor_get_ci_tasks_object_type(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        from mozperftest.perfdocs.framework_gatherers import RaptorGatherer

        task_obj = MagicMock()
        task_obj.task = {"payload": {"command": [" '--test tp6 '"]}}
        task_obj.attributes = {"run_on_projects": ["autoland"]}

        g = _make_gatherer(RaptorGatherer, top_dir)
        g._taskgraph = {"test-linux64/opt-raptor-tp6": task_obj}
        g._get_ci_tasks()
        assert "tp6" in g._task_list


@mock.patch("mozperftest.perfdocs.framework_gatherers.TestManifest")
def test_raptor_get_subtests_searchfox_link(MockTM, structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        manifest_path = top_dir / "test.ini"
        manifest_path.write_text(SAMPLE_INI)

        mock_tm = MagicMock()
        mock_tm.active_tests.return_value = [
            {
                "name": "Example",
                "manifest": str(manifest_path),
                "here": str(top_dir),
                "manifest_relpath": "test.ini",
                "path": str(top_dir / "test.js"),
                "relpath": "test.js",
                "test_url": "https://example.com",
                "alert_on": "fcp",
            }
        ]
        mock_tm.source_documents = {str(manifest_path): {"Example": {"lineno": "2"}}}
        MockTM.return_value = mock_tm

        from mozperftest.perfdocs.framework_gatherers import RaptorGatherer

        g = _make_gatherer(RaptorGatherer, top_dir)
        subtests = g._get_subtests_from_ini(manifest_path, "suite")

        assert "Example" in subtests
        assert "link searchfox" in subtests["Example"]
        assert "test.ini" in subtests["Example"]["link searchfox"]


@mock.patch("mozperftest.perfdocs.framework_gatherers.TestManifest")
def test_raptor_get_subtests_cputime_for_desktop(MockTM, structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        manifest_path = top_dir / "test.ini"

        mock_tm = MagicMock()
        mock_tm.active_tests.return_value = [
            {
                "name": "Example",
                "manifest": str(manifest_path),
                "here": str(top_dir),
                "manifest_relpath": "test.ini",
                "path": str(top_dir / "test.js"),
                "relpath": "test.js",
                "alert_on": "",
            }
        ]
        mock_tm.source_documents = {str(manifest_path): {}}
        MockTM.return_value = mock_tm

        from mozperftest.perfdocs.framework_gatherers import RaptorGatherer

        g = _make_gatherer(RaptorGatherer, top_dir)
        subtests = g._get_subtests_from_ini(manifest_path, "desktop")

        assert "cpuTime" in subtests["Example"]["metrics"]


def test_raptor_get_test_list_initializes_suite(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        from mozperftest.perfdocs.framework_gatherers import RaptorGatherer

        g = _make_gatherer(RaptorGatherer, top_dir)

        with mock.patch.object(
            g, "get_suite_list", return_value={"my_suite": ["test.ini"]}
        ), mock.patch.object(
            g, "_get_subtests_from_ini", return_value={"test1": {"name": "test1"}}
        ), mock.patch.object(g, "_get_ci_tasks"):
            result = g.get_test_list()

        assert "my_suite" in result
        assert "test1" in result["my_suite"]


def test_raptor_build_test_description_no_tests_found(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        mock_logger = MagicMock()
        _setup_fg_logger(mock_logger, structured_logger, top_dir)

        from mozperftest.perfdocs.framework_gatherers import RaptorGatherer

        g = _make_gatherer(RaptorGatherer, top_dir)
        with pytest.raises(Exception, match="No tests exist"):
            g.build_test_description("my_test", suite_name="suite")

        mock_logger.critical.assert_called_once()


def test_raptor_build_test_description_fields(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        from mozperftest.perfdocs.framework_gatherers import RaptorGatherer

        g = _make_gatherer(RaptorGatherer, top_dir)
        g._descriptions["my_suite"] = [
            {
                "name": "my_test-firefox",
                "owner": "Test Team",
                "test_url": "https://example.com/<page>",
                "secondary_url": "https://secondary.com",
                "link searchfox": "https://searchfox.org/path#42",
                "playback_pageset_manifest": "{subtest}_manifest.zip",
                "extra_field": "value with\nnewline",
                "manifest": "testing/raptor/test.ini",
                "metrics": [],
            }
        ]

        result = g.build_test_description(
            "my_test", suite_name="my_suite", metrics_info={}
        )
        text = result[0]

        assert "my_test-firefox" in text
        assert "Test Team" in text
        assert r"\<" in text
        assert "secondary.com" in text
        assert "searchfox" in text
        assert "_manifest.zip" in text
        assert "value with newline" in text


def test_raptor_build_test_description_with_task_list(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        from mozperftest.perfdocs.framework_gatherers import RaptorGatherer

        g = _make_gatherer(RaptorGatherer, top_dir)
        g._descriptions["my_suite"] = [
            {"name": "my_test", "manifest": "", "metrics": []}
        ]
        g._task_list["my_test"] = {
            "test-linux64/opt": [
                {
                    "test_name": "my_test-e10s",
                    "run_on_projects": ["mozilla-central"],
                }
            ]
        }

        with mock.patch(
            "mozperftest.perfdocs.framework_gatherers.match_run_on_projects",
            return_value=True,
        ):
            result = g.build_test_description(
                "my_test", suite_name="my_suite", metrics_info={}
            )

        text = result[0]
        assert "Test Task" in text
        assert "my_test-e10s" in text


@mock.patch("mozperftest.perfdocs.framework_gatherers.TestManifest")
@mock.patch("mozperftest.perfdocs.framework_gatherers.ScriptInfo")
def test_mozperftest_get_test_list_skips_objdir(MockSI, MockTM, structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        suite_dir = top_dir / "suite"
        suite_dir.mkdir()
        (suite_dir / "perftest.toml").write_text('["test.js"]')

        obj_dir = top_dir / "obj-debug" / "suite"
        obj_dir.mkdir(parents=True)
        (obj_dir / "perftest.toml").write_text('["test.js"]')

        mock_si = MagicMock()
        mock_si.__getitem__ = MagicMock(return_value="test.js")
        MockSI.return_value = mock_si

        mock_tm = MagicMock()
        mock_tm.active_tests.return_value = [{"path": str(suite_dir / "test.js")}]
        MockTM.return_value = mock_tm

        from mozperftest.perfdocs.framework_gatherers import MozperftestGatherer

        g = MozperftestGatherer(top_dir / "config.yml", top_dir)
        result = g.get_test_list()

        processed_paths = [str(c[0][0]) for c in MockTM.call_args_list]
        assert all("obj-debug" not in p for p in processed_paths)
        assert len(result) > 0


@mock.patch("mozperftest.perfdocs.framework_gatherers.TestManifest")
@mock.patch("mozperftest.perfdocs.framework_gatherers.ScriptInfo")
def test_mozperftest_get_test_list_name_cleaning(MockSI, MockTM, structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        suite_dir = top_dir / "suite"
        suite_dir.mkdir()
        (suite_dir / "perftest.toml").write_text('["test.js"]')

        mock_tm = MagicMock()
        mock_tm.active_tests.return_value = [
            {"path": str(suite_dir / "test.module.js")}
        ]
        MockTM.return_value = mock_tm

        mock_si_js = MagicMock()
        mock_si_js.__getitem__ = MagicMock(return_value="test.js")
        mock_si_dotted = MagicMock()
        mock_si_dotted.__getitem__ = MagicMock(return_value="test.module")

        MockSI.side_effect = [mock_si_js, mock_si_dotted]

        from mozperftest.perfdocs.framework_gatherers import MozperftestGatherer

        g1 = MozperftestGatherer(top_dir / "config.yml", top_dir)
        result1 = g1.get_test_list()
        assert any("test.js" in k for suite in result1.values() for k in suite)

        mock_tm2 = MagicMock()
        mock_tm2.active_tests.return_value = [
            {"path": str(suite_dir / "test.module.js")}
        ]
        MockTM.return_value = mock_tm2
        MockSI.side_effect = None
        MockSI.return_value = mock_si_dotted

        suite_dir2 = top_dir / "suite2"
        suite_dir2.mkdir()
        (suite_dir2 / "perftest.toml").write_text('["test.module.js"]')

        g2 = MozperftestGatherer(top_dir / "config2.yml", top_dir)
        result2 = g2.get_test_list()
        assert any("testmodule" in k for suite in result2.values() for k in suite)


def _make_talos_workspace(tmp_path):
    talos_dir = tmp_path / "testing" / "talos"
    talos_dir.mkdir(parents=True)
    (talos_dir / "talos.json").write_text(TALOS_JSON)
    return tmp_path


def test_talos_get_ci_tasks_dict_type(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = _make_talos_workspace(pathlib.Path(tmpdir))
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        from mozperftest.perfdocs.framework_gatherers import TalosGatherer

        g = _make_gatherer(TalosGatherer, top_dir)
        g._taskgraph = {
            "test-linux64/opt-talos-ts": {
                "task": {
                    "extra": {"suite": "talos"},
                    "payload": {"command": [" '--suite ts '"]},
                },
                "attributes": {"run_on_projects": ["mozilla-central"]},
            }
        }
        g._get_ci_tasks()
        assert "ts_paint" in g._task_list
        assert "test-linux64/opt" in g._task_list["ts_paint"]


def test_talos_get_ci_tasks_object_type(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = _make_talos_workspace(pathlib.Path(tmpdir))
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        task_obj = MagicMock()
        task_obj.task = {
            "extra": {"suite": "talos"},
            "payload": {"command": [" '--suite ts '"]},
        }
        task_obj.attributes = {"run_on_projects": ["autoland"]}

        from mozperftest.perfdocs.framework_gatherers import TalosGatherer

        g = _make_gatherer(TalosGatherer, top_dir)
        g._taskgraph = {"test-linux64/opt-talos-ts": task_obj}
        g._get_ci_tasks()
        assert "ts_paint" in g._task_list


def test_talos_build_test_description_general_list(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        from mozperftest.perfdocs.framework_gatherers import TalosGatherer

        g = _make_gatherer(TalosGatherer, top_dir)
        result = g.build_test_description(
            "ts_paint",
            test_description="- First item\n- Second item",
            suite_name="Talos Tests",
        )
        text = result[0]
        assert "ts_paint" in text
        assert "First item" in text
        assert "Second item" in text


def test_talos_build_test_description_example_data(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        from mozperftest.perfdocs.framework_gatherers import TalosGatherer

        g = _make_gatherer(TalosGatherer, top_dir)
        result = g.build_test_description(
            "ts_paint",
            test_description="- Example Data\n   * sample: 42",
            suite_name="Talos Tests",
        )
        text = result[0]
        assert "code-block" in text


def test_talos_build_test_description_sub_list(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        from mozperftest.perfdocs.framework_gatherers import TalosGatherer

        g = _make_gatherer(TalosGatherer, top_dir)
        result = g.build_test_description(
            "ts_paint",
            test_description="- Main item    * sub item one    * sub item two",
            suite_name="Talos Tests",
        )
        text = result[0]
        assert "Main item" in text


def test_talos_build_test_description_with_descriptions(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        from mozperftest.perfdocs.framework_gatherers import TalosGatherer

        g = _make_gatherer(TalosGatherer, top_dir)
        g._descriptions["ts_paint"] = {
            "__doc__": "ignored dunder",
            "filters": "ignored filters key",
            "lower_is_better": True,
            "win_path": {"key": "val\\ue"},
        }
        result = g.build_test_description(
            "ts_paint", test_description="- a description", suite_name="Talos Tests"
        )
        text = result[0]
        assert "lower_is_better" in text
        assert "__doc__" not in text
        assert "filters" not in text
        assert r"val/ue" in text


def test_talos_build_test_description_with_task_list(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        from mozperftest.perfdocs.framework_gatherers import TalosGatherer

        g = _make_gatherer(TalosGatherer, top_dir)
        g._task_list["ts_paint"] = {
            "test-linux64/opt": [
                {"test_name": "ts_paint-e10s", "run_on_projects": ["mozilla-central"]}
            ]
        }

        with mock.patch(
            "mozperftest.perfdocs.framework_gatherers.match_run_on_projects",
            return_value=True,
        ):
            result = g.build_test_description(
                "ts_paint", test_description="- item", suite_name="Talos Tests"
            )

        assert "Test Task" in result[0]
        assert "ts_paint-e10s" in result[0]


def test_talos_build_suite_section(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        from mozperftest.perfdocs.framework_gatherers import TalosGatherer

        g = _make_gatherer(TalosGatherer, top_dir)
        section = g.build_suite_section("talos tests", "Some content")
        assert section[0] == "talos tests"
        assert section[1] == "*" * len("talos tests")


def _awsy_gatherer(tmp_path):
    yaml_file = tmp_path / "config.yml"
    yaml_file.write_text(AWSY_YAML)
    from mozperftest.perfdocs.framework_gatherers import AwsyGatherer

    return AwsyGatherer(yaml_file, tmp_path)


def test_awsy_generate_ci_tasks_dict_type(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        g = _awsy_gatherer(top_dir)
        g._taskgraph = {
            "test-linux64/opt-awsy-tp6": {
                "task": {"extra": {"suite": "awsy-tp6"}},
                "attributes": {"run_on_projects": ["mozilla-central"]},
            }
        }
        g._generate_ci_tasks()
        assert "test-linux64/opt" in g._task_list
        tasks = g._task_list["test-linux64/opt"]
        assert tasks[0]["test_name"] == "awsy-tp6"


def test_awsy_generate_ci_tasks_object_type(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        task_obj = MagicMock()
        task_obj.task = {"extra": {"suite": "awsy-base"}}
        task_obj.attributes = {"run_on_projects": ["autoland"]}

        g = _awsy_gatherer(top_dir)
        g._taskgraph = {"test-linux64/opt-awsy-base": task_obj}
        g._generate_ci_tasks()
        assert "test-linux64/opt" in g._task_list


def test_awsy_get_suite_list(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        g = _awsy_gatherer(top_dir)
        result = g.get_suite_list()
        assert "Awsy tests" in result
        assert "tp6" in result["Awsy tests"]


def test_awsy_build_suite_section(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        g = _awsy_gatherer(top_dir)
        section = g.build_suite_section("awsy tests", "Some content")
        assert section[0] == "Awsy tests"
        assert section[1] == "-" * len("Awsy tests")


def test_awsy_build_test_description_tp6(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        g = _awsy_gatherer(top_dir)
        g._task_list["test-linux64/opt"] = [
            {"test_name": "awsy-tp6", "run_on_projects": ["mozilla-central"]}
        ]

        result = g.build_test_description("tp6", "Base memory", suite_name="Awsy tests")
        text = result[0]

        assert "tp6" in text
        assert "Base memory" in text
        assert "Awsy Team" in text
        assert "awsy-tp6" in text
        assert "mozilla-central" in text


def test_awsy_build_test_description_tp5_uses_awsy_e10s_tag(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        g = _awsy_gatherer(top_dir)
        g._task_list["test-linux64/opt"] = [
            {"test_name": "awsy-e10s-tp5", "run_on_projects": []},
        ]

        result = g.build_test_description("tp5", "TP5 test", suite_name="Awsy tests")
        text = result[0]

        assert "awsy-e10s-tp5" in text
        assert "None" in text


def test_awsy_build_test_description_empty_run_on_projects(structured_logger):
    with tempfile.TemporaryDirectory() as tmpdir:
        top_dir = pathlib.Path(tmpdir)
        _setup_fg_logger(MagicMock(), structured_logger, top_dir)

        g = _awsy_gatherer(top_dir)
        g._task_list["test-linux64/opt"] = [{"test_name": "tp6", "run_on_projects": []}]

        result = g.build_test_description("tp6", "desc", suite_name="Awsy tests")
        assert "None" in result[0]
