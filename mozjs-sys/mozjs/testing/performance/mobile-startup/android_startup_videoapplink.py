# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import os
import pathlib
import re
import subprocess
import sys
import time

from mozperftest.utils import ON_TRY

# Add the python packages installed by mozperftest
sys.path.insert(0, os.environ["PYTHON_PACKAGES"])

import cv2
import numpy as np
from mozdevice import ADBDevice
from mozperftest.profiler import ProfilingMediator

PROD_FENIX = "fenix"
PROD_CHRM = "chrome-m"
BACKGROUND_TABS = [
    "https://www.google.com/search?q=toronto+weather",
    "https://en.m.wikipedia.org/wiki/Anemone_hepatica",
    "https://www.amazon.ca/gp/aw/gb?ref_=navm_cs_gb&discounts-widget",
    "https://www.espn.com/nfl/game/_/gameId/401671793/chiefs-falcons",
]
ITERATIONS = 5


class ImageAnalzer:
    def __init__(self, browser, test, test_url):
        self.video = None
        self.browser = browser
        self.test = test
        self.test_url = test_url
        self.width = 0
        self.height = 0
        self.video_name = ""
        self.package_name = os.environ["BROWSER_BINARY"]
        self.device = ADBDevice()
        self.profiler = ProfilingMediator()
        self.cpu_data = {"total": {"time": []}}
        if self.browser == PROD_FENIX:
            self.intent = "org.mozilla.fenix/org.mozilla.fenix.IntentReceiverActivity"
        elif self.browser == PROD_CHRM:
            self.intent = (
                "com.android.chrome/com.google.android.apps.chrome.IntentDispatcher"
            )
        else:
            raise Exception("Bad browser name")
        self.nav_start_command = (
            f"am start-activity -W -n {self.intent} -a "
            f"android.intent.action.VIEW -d "
        )
        self.view_intent_command = (
            f"am start-activity -W -n {self.intent} -a " f"android.intent.action.VIEW"
        )

        self.device.shell("mkdir -p /sdcard/Download")
        self.device.shell("settings put global window_animation_scale 1")
        self.device.shell("settings put global transition_animation_scale 1")
        self.device.shell("settings put global animator_duration_scale 1")

    def app_setup(self):
        if ON_TRY:
            self.device.shell(f"pm clear {self.package_name}")
        time.sleep(3)
        self.skip_onboarding()
        self.device.shell(
            f"pm grant {self.package_name} android.permission.POST_NOTIFICATIONS"
        )  # enabling notifications
        if self.test != "homeview_startup":
            self.create_background_tabs()
        self.device.shell(f"am force-stop {self.package_name}")

    def skip_onboarding(self):
        # Skip onboarding for chrome and fenix
        if self.browser == PROD_CHRM:
            self.device.shell_output(
                'echo "chrome --no-default-browser-check --no-first-run '
                '--disable-fre" > /data/local/tmp/chrome-command-line '
            )
            self.device.shell("am set-debug-app --persistent com.android.chrome")
        elif self.browser == PROD_FENIX:
            self.device.shell(
                "am start-activity -W -a android.intent.action.MAIN --ez "
                "performancetest true -n org.mozilla.fenix/org.mozilla.fenix.App"
            )

    def create_background_tabs(self):
        # Add background tabs that allow us to see the impact of having background tabs open
        # when we do the cold applink startup test. This makes the test workload more realistic
        # and will also help catch regressions that affect per-open-tab startup work.
        for website in BACKGROUND_TABS:
            self.device.shell(self.nav_start_command + website)
            time.sleep(3)
        if self.test == "mobile_restore":
            self.load_page_to_test_startup()

    def get_video(self, run):
        self.video_name = f"vid{run}_{self.browser}.mp4"
        video_location = f"/sdcard/Download/{self.video_name}"

        # Bug 1927548 - Recording command doesn't use mozdevice shell because the mozdevice shell
        # outputs an adbprocess obj whose adbprocess.proc.kill() does not work when called
        recording = subprocess.Popen(
            [
                "adb",
                "shell",
                "screenrecord",
                "--bugreport",
                video_location,
            ]
        )

        # Start Profilers if enabled.
        self.profiler.start()

        if self.test == "cold_view_nav_end":
            self.load_page_to_test_startup()
        elif self.test in ["mobile_restore", "homeview_startup"]:
            self.open_browser_with_view_intent()

        # Stop Profilers if enabled.
        self.profiler.stop(os.environ["TESTING_DIR"], run)

        self.process_cpu_info(run)
        recording.kill()
        time.sleep(5)
        self.device.command_output(
            ["pull", "-a", video_location, os.environ["TESTING_DIR"]]
        )

        time.sleep(4)
        video_location = pathlib.Path(os.environ["TESTING_DIR"], self.video_name)

        self.video = cv2.VideoCapture(video_location)
        self.width = self.video.get(cv2.CAP_PROP_FRAME_WIDTH)
        self.height = self.video.get(cv2.CAP_PROP_FRAME_HEIGHT)
        self.device.shell(f"am force-stop {self.package_name}")

    def get_image(self, frame_position):
        self.video.set(cv2.CAP_PROP_POS_FRAMES, frame_position)
        ret, frame = self.video.read()
        if not ret:
            raise Exception("Frame not read")
        # We crop out the top 100 pixels in each image as when we have --bug-report in the
        # screen-recording command it displays a timestamp which interferes with the image comparisons
        return frame[100 : int(self.height), 0 : int(self.width)]

    def error(self, img1, img2):
        h = img1.shape[0]
        w = img1.shape[1]
        diff = cv2.subtract(img1, img2)
        err = np.sum(diff**2)
        mse = err / (float(h * w))
        return mse

    def get_page_loaded_time(self):
        """
        Returns the index of the frame where the main image on the shopify demo page is displayed
        for the first time.
        Specifically, we find the index of the first frame whose image is within an error of 20
        compared to the final frame, via binary search. The binary search assumes that the error
        compared to the final frame decreases monotonically in the captured frames.
        """
        final_frame_index = self.video.get(cv2.CAP_PROP_FRAME_COUNT) - 1
        final_frame = self.get_image(final_frame_index)

        lo = 0
        hi = final_frame_index

        while lo < hi:
            mid = (lo + hi) // 2
            diff = self.error(self.get_image(mid), final_frame)
            if diff <= 20:
                hi = mid
            else:
                lo = mid + 1
        return lo

    def get_time_from_frame_num(self, frame_num):
        self.video.set(cv2.CAP_PROP_POS_FRAMES, frame_num)
        self.video.read()
        return self.video.get(cv2.CAP_PROP_POS_MSEC)

    def load_page_to_test_startup(self):
        # Navigate to the page we want to use for testing startup
        self.device.shell(self.nav_start_command + self.test_url)
        time.sleep(5)

    def open_browser_with_view_intent(self):
        self.device.shell(self.view_intent_command)
        time.sleep(5)

    def process_cpu_info(self, run):
        cpu_info = self.device.shell_output(
            f"ps -A -o name=,cpu=,time+=,%cpu= | grep {self.package_name}"
        ).split("\n")
        total_time_seconds = tab_processes_time = 0
        for process in cpu_info:
            process_name = re.search(r"([\w\d_.:]+)\s", process).group(1)
            time = re.search(r"\s(\d+):(\d+).(\d+)\s", process)
            time_seconds = (
                10 * int(time.group(3))
                + 1000 * int(time.group(2))
                + 60 * 1000 * int(time.group(1))
            )
            total_time_seconds += time_seconds
            if "org.mozilla.fenix:tab" in process_name:
                process_name = "org.mozilla.fenix:tab"

            if process_name not in self.cpu_data.keys():
                self.cpu_data[process_name] = {}
                self.cpu_data[process_name]["time"] = []

            if "org.mozilla.fenix:tab" == process_name:
                tab_processes_time += time_seconds
                continue
            self.cpu_data[process_name]["time"] += [time_seconds]

        if tab_processes_time:
            self.cpu_data["org.mozilla.fenix:tab"]["time"] += [tab_processes_time]
        self.cpu_data["total"]["time"] += [total_time_seconds]

    def perfmetrics_cpu_data_ingesting(self):
        for process in self.cpu_data.keys():
            print(
                'perfMetrics: {"values": '
                + str(self.cpu_data[process]["time"])
                + ', "name": "'
                + process
                + '-cpu-time", "shouldAlert": true }'
            )


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise Exception("Didn't pass the args properly :(")
    start_video_timestamp = []
    browser = sys.argv[1]
    test = sys.argv[2]
    test_url = sys.argv[3]

    perfherder_names = {
        "cold_view_nav_end": "applink_startup",
        "mobile_restore": "tab_restore",
        "homeview_startup": "homeview_startup",
    }

    ImageObject = ImageAnalzer(browser, test, test_url)
    for iteration in range(ITERATIONS):
        ImageObject.app_setup()
        ImageObject.get_video(iteration)
        nav_done_frame = ImageObject.get_page_loaded_time()
        start_video_timestamp += [ImageObject.get_time_from_frame_num(nav_done_frame)]
    print(
        'perfMetrics: {"values": '
        + str(start_video_timestamp)
        + ', "name": "'
        + perfherder_names[test]
        + '", "shouldAlert": true}'
    )
    ImageObject.perfmetrics_cpu_data_ingesting()
