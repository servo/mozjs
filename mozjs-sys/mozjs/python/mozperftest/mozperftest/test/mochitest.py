# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import json
import os
import sys
from contextlib import redirect_stdout
from pathlib import Path

from mozperftest.layers import Layer
from mozperftest.test.functionaltestrunner import (
    FunctionalTestRunner,
)
from mozperftest.utils import (
    EVAL_DATA_MATCHER,
    ON_TRY,
    PERF_METRICS_MATCHER,
    LogProcessor,
    NoEvalDataError,
    NoPerfMetricsError,
    install_requirements_file,
)


class MissingMochitestInformation(Exception):
    """Raised when information needed to run a mochitest is missing."""

    pass


class MochitestTestFailure(Exception):
    """Raised when a mochitest test returns a non-zero exit code."""

    pass


class MochitestData:
    def open_data(self, data):
        return {
            "name": "mochitest",
            "subtest": data["name"],
            "data": [
                {"file": "mochitest", "value": value, "xaxis": xaxis}
                for xaxis, value in enumerate(data["values"])
            ],
            "value": data.get("value", None),
            "unit": data.get("unit", None),
            "shouldAlert": data.get("shouldAlert", None),
            "lowerIsBetter": data.get("lowerIsBetter", None),
        }

    def transform(self, data):
        return data

    merge = transform


class _Mochitest(Layer):
    """Runs a mochitest test through `mach test` locally, and directly with mochitest in CI."""

    name = "mochitest"
    activated = True

    arguments = {
        "binary": {
            "type": str,
            "default": None,
            "help": ("Path to the browser."),
        },
        "cycles": {
            "type": int,
            "default": 1,
            "help": ("Number of cycles/iterations to do for the test."),
        },
        "manifest": {
            "type": str,
            "default": None,
            "help": (
                "Path to the manifest that contains the test (only required in CI)."
            ),
        },
        "manifest-flavor": {
            "type": str,
            "default": None,
            "help": "Mochitest flavor of the test to run (only required in CI).",
        },
        "extra-args": {
            "nargs": "*",
            "type": str,
            "default": [],
            "help": (
                "Additional arguments to pass to mochitest. Expected in a format such as: "
                "--mochitest-extra-args headless profile-path=/path/to/profile"
            ),
        },
        "name-change": {
            "action": "store_true",
            "default": False,
            "help": "Use the test name from the metadata instead of the test filename.",
        },
    }

    def __init__(self, env, mach_cmd):
        super().__init__(env, mach_cmd)
        self.topsrcdir = mach_cmd.topsrcdir
        self._mach_context = mach_cmd._mach_context
        self.python_path = mach_cmd.virtualenv_manager.python_path
        self.topobjdir = mach_cmd.topobjdir
        self.distdir = mach_cmd.distdir
        self.bindir = mach_cmd.bindir
        self.statedir = mach_cmd.statedir
        self.payloads_from_log = []
        self.topsrcdir = mach_cmd.topsrcdir

    def setup(self):
        if ON_TRY:
            # Install marionette requirements
            install_requirements_file(
                self.mach_cmd.virtualenv_manager,
                str(
                    Path(
                        os.getenv("MOZ_FETCHES_DIR"),
                        "config",
                        "marionette_requirements.txt",
                    )
                ),
            )

    def _enable_gecko_profiling(self):
        """Setup gecko profiling if requested."""
        gecko_profile_args = []

        gecko_profile_features = os.getenv(
            "MOZ_PROFILER_STARTUP_FEATURES", "js,stackwalk,cpu,screenshots,memory"
        )
        gecko_profile_threads = os.getenv(
            "MOZ_PROFILER_STARTUP_FILTERS", "GeckoMain,Compositor,Renderer"
        )
        gecko_profile_entries = os.getenv("MOZ_PROFILER_STARTUP_ENTRIES", "65536000")
        gecko_profile_interval = os.getenv("MOZ_PROFILER_STARTUP_INTERVAL", None)

        if self.get_arg("gecko-profile"):
            gecko_profile_args.append("--profiler")
            gecko_profile_args.extend([
                f"--setenv=MOZ_PROFILER_STARTUP_FEATURES={gecko_profile_features}",
                f"--setenv=MOZ_PROFILER_STARTUP_FILTERS={gecko_profile_threads}",
                f"--setenv=MOZ_PROFILER_STARTUP_ENTRIES={gecko_profile_entries}",
            ])
            if gecko_profile_interval:
                gecko_profile_args.append(
                    f"--setenv=MOZ_PROFILER_STARTUP_INTERVAL={gecko_profile_interval}"
                )
            if ON_TRY:
                gecko_profile_args.append("--profiler-save-only")

                output_dir_path = str(Path(self.get_arg("output")).resolve())
                gecko_profile_args.append(f"--setenv=MOZ_UPLOAD_DIR={output_dir_path}")
            else:
                # Setup where the profile gets saved to so it doesn't get deleted
                profile_path = os.getenv("MOZ_PROFILER_SHUTDOWN")
                if not profile_path:
                    output_dir = Path(self.get_arg("output"))
                    if not output_dir.is_absolute():
                        output_dir = Path(self.topsrcdir, output_dir)
                    output_dir.resolve().mkdir(parents=True, exist_ok=True)
                    profile_path = output_dir / "profile_mochitest.json"
                    os.environ["MOZ_PROFILER_SHUTDOWN"] = str(profile_path)
                self.info(f"Profile will be saved to: {profile_path}")

        return gecko_profile_args

    def _parse_extra_args(self):
        """Sets up the extra-args from the user for passing to mochitest."""
        parsed_extra_args = []
        for arg in self.get_arg("extra-args"):
            parsed_extra_args.append(f"--{arg}")
        return parsed_extra_args

    def _parse_browser_prefs(self, metadata):
        """Sets up browser prefs from metadata for passing to mochitest."""
        mochitest_prefs = []
        browser_prefs = metadata.get_options("browser_prefs")
        for key, value in browser_prefs.items():
            mochitest_prefs.append(f"--setpref={key}={value}")
        return mochitest_prefs

    def _setup_mochitest_android_args(self, metadata):
        """Sets up all the arguments needed to run mochitest android tests."""
        app = metadata.binary
        activity = self.get_arg("android-activity")
        if (app + ".") in activity:
            # Mochitest prefixes the activity with the app-name so we need to
            # remove it here if it exists.
            activity = activity.replace(app + ".", "")

        mochitest_android_args = [
            "--android",
            f"--app={app}",
            f"--activity={activity}",
        ]

        if not ON_TRY:
            os.environ["MOZ_HOST_BIN"] = self.mach_cmd.bindir
            mochitest_android_args.extend([
                f"--setenv=MOZ_HOST_BIN={os.environ['MOZ_HOST_BIN']}",
            ])
        else:
            os.environ["MOZ_HOST_BIN"] = str(
                Path(os.getenv("MOZ_FETCHES_DIR"), "hostutils")
            )
            mochitest_android_args.extend([
                f"--setenv=MOZ_HOST_BIN={os.environ['MOZ_HOST_BIN']}",
                f"--remote-webserver={os.environ['HOST_IP']}",
                "--http-port=8854",
                "--ssl-port=4454",
            ])

        return mochitest_android_args

    def _get_mochitest_args(self, metadata):
        """Handles setup for all mochitest-specific arguments."""
        mochitest_args = []

        mochitest_args.extend(self._enable_gecko_profiling())
        mochitest_args.extend(self._parse_extra_args())
        mochitest_args.extend(self._parse_browser_prefs(metadata))

        if self.get_arg("android"):
            mochitest_args.extend(self._setup_mochitest_android_args(metadata))

        return mochitest_args

    def remote_run(self, test, metadata):
        """Run tests in CI."""
        import runtests
        import runtestsremote
        from manifestparser import TestManifest
        from mochitest_options import MochitestArgumentParser

        manifest_flavor = self.get_arg("manifest-flavor")
        manifest_name = self.get_arg("manifest")
        if not manifest_name:
            raise MissingMochitestInformation(
                "Name of manifest that contains test needs to be"
                "specified (e.g. mochitest-common.ini)"
            )
        if not manifest_flavor:
            raise MissingMochitestInformation(
                "Mochitest flavor needs to be provided(e.g. plain, browser-chrome, ...)"
            )

        manifest_path = Path(test.parent, manifest_name)
        manifest = TestManifest([str(manifest_path)], strict=False)
        test_list = manifest.active_tests(paths=(str(test),))

        subsuite = None
        for parsed_test in test_list or []:
            if str(test) in str(Path(parsed_test.get("path", ""))):
                subsuite = parsed_test.get("subsuite", None)
                break

        # Use the mochitest argument parser to parse the extra argument
        # options, and produce an `args` object that has all the defaults
        parser = MochitestArgumentParser()
        args = parser.parse_args(self._get_mochitest_args(metadata))

        # Bug 1858155 - Attempting to only use one test_path triggers a failure
        # during test execution
        args.test_paths = [str(test.name), str(test.name)]
        args.keep_open = False
        args.runByManifest = True
        args.manifestFile = manifest
        args.topobjdir = self.topobjdir
        args.topsrcdir = self.topsrcdir
        args.flavor = manifest_flavor

        if subsuite:
            args.subsuite = subsuite

        fetch_dir = os.getenv("MOZ_FETCHES_DIR")
        if self.get_arg("android"):
            args.utilityPath = str(Path(fetch_dir, "hostutils"))
            args.xrePath = str(Path(fetch_dir, "hostutils"))
            args.extraProfileFiles.append(str(Path(fetch_dir, "bin", "plugins")))
            args.testingModulesDir = str(Path(fetch_dir, "modules"))
            args.symbolsPath = str(Path(fetch_dir, "crashreporter-symbols"))
            args.certPath = str(Path(fetch_dir, "certs"))
        else:
            args.app = self.get_arg("mochitest_binary")
            args.utilityPath = str(Path(fetch_dir, "bin"))
            args.extraProfileFiles.append(str(Path(fetch_dir, "bin", "plugins")))
            args.testingModulesDir = str(Path(fetch_dir, "modules"))
            args.symbolsPath = str(Path(fetch_dir, "crashreporter-symbols"))
            args.certPath = str(Path(fetch_dir, "certs"))

        log_processor = self._get_log_processor()

        with redirect_stdout(log_processor):
            # Perftest calls mochitest in-process, so there's no mozharness
            # layer to convert structured logs to TBPL. Request TBPL format
            # explicitly so the log processor sees human-readable lines.
            args.log_tbpl = [sys.stdout]
            if self.get_arg("android"):
                result = runtestsremote.run_test_harness(parser, args)
            else:
                result = runtests.run_test_harness(parser, args)

        return result, log_processor

    def run(self, metadata):
        test = Path(metadata.script["filename"])
        if self.get_arg("name-change", False):
            test_name = metadata.script["name"]
        else:
            test_name = test.name

        cycles = self.get_arg("cycles", 1)
        for cycle in range(1, cycles + 1):
            metadata.run_hook(
                "before_cycle", metadata, self.env, cycle, metadata.script
            )
            try:
                if ON_TRY:
                    status, log_processor = self.remote_run(test, metadata)
                else:
                    status, log_processor = FunctionalTestRunner.test(
                        self.mach_cmd,
                        [str(test)],
                        self._get_mochitest_args(metadata) + ["--keep-open=False"],
                    )
            finally:
                metadata.run_hook(
                    "after_cycle", metadata, self.env, cycle, metadata.script
                )

            if status is not None and status != 0:
                raise MochitestTestFailure("Test failed to run")

            self._extract_payload_from_log(log_processor, metadata)

        self._handle_payloads(metadata, test_name)

        return metadata

    @staticmethod
    def _get_log_processor():
        raise NotImplementedError

    def _extract_payload_from_log(self, log_processor, metadata):
        """The payload for perftests and evals are output to the log, and extracted
        into the mozperftest harness for processing."""
        raise NotImplementedError

    def _handle_payloads(self, metadata, test_name):
        """After the payloads are extracting from the log, handle the final processing."""
        raise NotImplementedError


class PerfMochitest(_Mochitest):
    """A mochitest that collects the `perfResults` from stdout"""

    @staticmethod
    def _get_log_processor():
        return LogProcessor(PERF_METRICS_MATCHER)

    def _extract_payload_from_log(self, log_processor, metadata):
        """Parse metrics found"""
        for metrics_line in log_processor.match:
            self.payloads_from_log.append(
                json.loads(metrics_line.split("|")[-1].strip())
            )

    def _handle_payloads(self, metadata, test_name):
        results = []
        for payload in self.payloads_from_log:
            # Expecting results like {"metric-name": value, "metric-name2": value, ...}
            if isinstance(payload, dict):
                for key, val in payload.items():
                    for r in results:
                        if r["name"] == key:
                            r["values"].append(val)
                            break
                    else:
                        results.append({"name": key, "values": [val]})
            # Expecting results like [
            #     {"name": "metric-name", "values": [value1, value2, ...], ...},
            #     {"name": "metric-name2", "values": [value1, value2, ...], ...},
            # ]
            else:
                for metric in payload:
                    for r in results:
                        if r["name"] == metric["name"]:
                            r["values"].extend(metric["values"])
                            break
                    else:
                        results.append(metric)

        if not results:
            raise NoPerfMetricsError("mochitest")

        metadata.add_result({
            "name": test_name,
            "framework": {"name": "mozperftest"},
            "transformer": "mozperftest.test.mochitest:MochitestData",
            "results": results,
        })


class EvalMochitest(_Mochitest):
    """A mochitest that collects the `evalDataPayload` from stdout"""

    @staticmethod
    def _get_log_processor():
        return LogProcessor(EVAL_DATA_MATCHER)

    def _extract_payload_from_log(self, log_processor, metadata):
        """Parse the eval data payload from the log."""
        for eval_line in log_processor.match:
            self.payloads_from_log.append(
                # Take:
                #   "evalDataPayload | { ... }"
                # Partition into:
                #   ('evalDataPayload ', '|', " { ... }")
                # Then extract the payload:
                #   "{ ... }"
                # And finally load it as JSON
                #   { ... }
                json.loads(eval_line.partition("|")[2].strip())
            )

    def _handle_payloads(self, metadata, test_name):
        if not self.payloads_from_log:
            raise NoEvalDataError("mochitest")

        output_dir = Path(self.get_arg("output")).resolve()
        output_dir.mkdir(parents=True, exist_ok=True)
        out_file = output_dir / f"{Path(test_name).stem}-eval-data.json"
        pretty_json = json.dumps(self.payloads_from_log, indent=2)
        out_file.write_text(pretty_json)

        try:
            display_path = out_file.relative_to(Path(self.topsrcdir))
        except ValueError:
            display_path = out_file

        print(f"Evaluation data written to {display_path}")

        metadata.add_eval_payload(test_name, self.payloads_from_log)
