# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import sys
import tempfile
import time
from pathlib import Path

import mozlog
from mozdevice import ADBDevice, ADBError

from mozperftest.layers import Layer
from mozperftest.system.android_perf_tuner import tune_performance
from mozperftest.utils import download_file

HERE = Path(__file__).parent

_ROOT_URL = "https://firefox-ci-tc.services.mozilla.com/api/index/v1/task/"
_FENIX_NIGHTLY_BUILDS = (
    "mobile.v3.firefox-android.apks.fenix-nightly.latest.{architecture}"
    "/artifacts/public/build/fenix/{architecture}/target.apk"
)
_GV_BUILDS = "gecko.v2.mozilla-central.shippable.latest.mobile.android-"
_REFBROW_BUILDS = (
    "mobile.v2.reference-browser.nightly.latest.{architecture}"
    "/artifacts/public/target.{architecture}.apk"
)

_PERMALINKS = {
    "fenix_nightly_armeabi_v7a": _ROOT_URL
    + _FENIX_NIGHTLY_BUILDS.format(architecture="armeabi-v7a"),
    "fenix_nightly_arm64_v8a": _ROOT_URL
    + _FENIX_NIGHTLY_BUILDS.format(architecture="arm64-v8a"),
    # The two following aliases are used for Fenix multi-commit testing in CI
    "fenix_nightlysim_multicommit_arm64_v8a": None,
    "fenix_nightlysim_multicommit_armeabi_v7a": None,
    "gve_nightly_aarch64": _ROOT_URL
    + _GV_BUILDS
    + "aarch64-opt/artifacts/public/build/geckoview_example.apk",
    "gve_nightly_api16": _ROOT_URL
    + _GV_BUILDS
    + "arm-opt/artifacts/public/build/geckoview_example.apk",
    "refbrow_nightly_aarch64": _ROOT_URL
    + _REFBROW_BUILDS.format(architecture="arm64-v8a"),
    "refbrow_nightly_api16": _ROOT_URL
    + _REFBROW_BUILDS.format(architecture="armeabi-v7a"),
}


class DeviceError(Exception):
    pass


class ADBLoggedDevice(ADBDevice):
    def __init__(self, *args, **kw):
        self._provided_logger = kw.pop("logger")
        super(ADBLoggedDevice, self).__init__(*args, **kw)

    def _get_logger(self, logger_name, verbose):
        return self._provided_logger


class AndroidDevice(Layer):
    """Use an android device via ADB"""

    name = "android"
    activated = False

    arguments = {
        "app-name": {
            "type": str,
            "default": "org.mozilla.firefox",
            "help": "Android app name",
        },
        "timeout": {
            "type": int,
            "default": 60,
            "help": "Timeout in seconds for adb operations",
        },
        "clear-logcat": {
            "action": "store_true",
            "default": False,
            "help": "Clear the logcat when starting",
        },
        "capture-adb": {
            "type": str,
            "default": "stdout",
            "help": (
                "Captures adb calls to the provided path. "
                "To capture to stdout, use 'stdout'."
            ),
        },
        "capture-logcat": {
            "type": str,
            "default": None,
            "help": "Captures the logcat to the provided path.",
        },
        "perf-tuning": {
            "action": "store_true",
            "default": False,
            "help": (
                "If set, device will be tuned for performance. "
                "This helps with decreasing the noise."
            ),
        },
        "intent": {"type": str, "default": None, "help": "Intent to use"},
        "activity": {"type": str, "default": None, "help": "Activity to use"},
        "install-apk": {
            "nargs": "*",
            "default": [],
            "help": (
                "APK to install to the device "
                "Can be a file, an url or an alias url from "
                " %s" % ", ".join(_PERMALINKS.keys())
            ),
        },
    }

    def __init__(self, env, mach_cmd):
        super(AndroidDevice, self).__init__(env, mach_cmd)
        self.android_activity = self.app_name = self.device = None
        self.capture_logcat = self.capture_file = None
        self._custom_apk_path = None

    @property
    def custom_apk_path(self):
        if self._custom_apk_path is None:
            custom_apk_path = Path(HERE, "..", "user_upload.apk")
            if custom_apk_path.exists():
                self._custom_apk_path = custom_apk_path
        return self._custom_apk_path

    def custom_apk_exists(self):
        return self.custom_apk_path is not None

    def enable_notifications(self, package_id):
        """
        The code block with pm grant enables notifications for the app,
        otherwise during testing a request to enable/disable notifications will persist
        through app shutdowns.
        """
        self.device.shell(
            f"pm grant {package_id} android.permission.POST_NOTIFICATIONS"
        )

    def disable_notifications(self, package_id):
        self.device.shell(
            f"pm revoke {package_id} android.permission.POST_NOTIFICATIONS"
        )

    def skip_app_onboarding(self, package_id):
        """
        We skip onboarding for focus in measure_start_up.py because it's stateful
        and needs to be called for every cold start intent.
        Onboarding only visibly gets in the way of our MAIN test results.
        """
        # This sets mutable state we only need to pass this flag once, before we start the test
        self.device.shell(
            f"am start-activity -W -a android.intent.action.MAIN --ez "
            f"performancetest true -n {package_id}/org.mozilla.fenix.App"
        )
        time.sleep(4)  # ensure skip onboarding call has time to propagate.

    def setup(self):
        if self.custom_apk_exists():
            self.info(
                f"Replacing --android-install-apk with custom APK found at "
                f"{self.custom_apk_path}"
            )
            self.set_arg("android-install-apk", [self.custom_apk_path])

    def teardown(self):
        if self.capture_file is not None:
            self.capture_file.close()
        if self.capture_logcat is not None and self.device is not None:
            self.info("Dumping logcat into %r" % str(self.capture_logcat))
            with self.capture_logcat.open("wb") as f:
                for line in self.device.get_logcat():
                    f.write(line.encode("utf8", errors="replace") + b"\n")

    def _set_output_path(self, path):
        if path in (None, "stdout"):
            return path
        # check if the path is absolute or relative to output
        path = Path(path)
        if not path.is_absolute():
            return Path(self.get_arg("output"), path)
        return path

    def install_application(self, applications, package_id=None):
        if not self.app_name:
            self.app_name = package_id

        # Install APKs
        for apks in applications:
            apk = apks
            self.info("Uninstalling old version")
            self.device.uninstall_app(self.get_arg("android-app-name"))
            self.info("Installing %s" % apk)
            if str(apk) in _PERMALINKS:
                apk = _PERMALINKS[apk]
            if str(apk).startswith("http"):
                with tempfile.TemporaryDirectory() as tmpdirname:
                    target = Path(tmpdirname, "target.apk")
                    self.info("Downloading %s" % apk)
                    download_file(apk, target)
                    self.info("Installing downloaded APK")
                    self.device.install_app(str(target))
            elif "fenix" in self.app_name:
                self.info("Installing Fenix APK with baseline profile")
                self.device.install_app_baseline_profile(apk, replace=True)
                output = self.device.shell_output(
                    f"dumpsys package dexopt | grep -A 1 {self.app_name}"
                )
                self.info(output)
            else:
                self.device.install_app(apk, replace=True)
            self.info("Done.")

        # checking that the app is installed
        if not self.device.is_app_installed(self.app_name):
            raise Exception("%s is not installed" % self.app_name)

    def run(self, metadata):
        self.app_name = self.get_arg("android-app-name")
        self.android_activity = self.get_arg("android-activity")
        self.clear_logcat = self.get_arg("clear-logcat")
        self.metadata = metadata
        self.verbose = self.get_arg("verbose")
        self.capture_adb = self._set_output_path(self.get_arg("capture-adb"))
        self.capture_logcat = self._set_output_path(self.get_arg("capture-logcat"))

        if metadata.binary:
            self.app_name = metadata.binary

        # capture the logs produced by ADBDevice
        logger_name = "mozperftest-adb"
        logger = mozlog.structuredlog.StructuredLogger(logger_name)
        if self.capture_adb == "stdout":
            stream = sys.stdout
            disable_colors = False
        else:
            stream = self.capture_file = self.capture_adb.open("w")
            disable_colors = True

        handler = mozlog.handlers.StreamHandler(
            stream=stream,
            formatter=mozlog.formatters.MachFormatter(
                verbose=self.verbose, disable_colors=disable_colors
            ),
        )
        logger.add_handler(handler)
        try:
            self.device = ADBLoggedDevice(
                verbose=self.verbose, timeout=self.get_arg("timeout"), logger=logger
            )
        except (ADBError, AttributeError) as e:
            self.error("Could not connect to the phone. Is it connected?")
            raise DeviceError(str(e))

        if self.clear_logcat:
            self.device.clear_logcat()

        self.install_application(self.get_arg("android-install-apk"))

        if self.get_arg("android-perf-tuning", False):
            tune_performance(self.device)

        # set up default activity with the app name if none given
        if self.android_activity is None:
            # guess the activity, given the app
            if "fenix" in self.app_name:
                self.android_activity = "org.mozilla.fenix.IntentReceiverActivity"
            elif "geckoview_example" in self.app_name:
                self.android_activity = (
                    "org.mozilla.geckoview_example.GeckoViewActivity"
                )
            self.set_arg("android_activity", self.android_activity)

        self.info("Android environment:")
        self.info("- Application name: %s" % self.app_name)
        self.info("- Activity: %s" % self.android_activity)
        self.info("- Intent: %s" % self.get_arg("android_intent"))
        return metadata
