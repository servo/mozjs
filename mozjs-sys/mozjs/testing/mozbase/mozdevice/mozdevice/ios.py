# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

"""mozdevice.ios is an experimental, early stages abstraction layer for running
tests with Gecko on iOS devices. Currently this package only provides support
for running tests on simulator, and is not ready for general-purpose use."""

import json
import os
import shutil
import subprocess
import sys
import tempfile

from mozprocess import processhandler


class IosDevice:
    def __init__(self, uuid, name, is_simulator):
        self.uuid = uuid
        self.name = name
        self.is_simulator = is_simulator

    @staticmethod
    def all_devices(is_simulator):
        if is_simulator:
            return IosDeviceSimulator.all_devices()
        else:
            return IosDeviceReal.all_devices()

    @staticmethod
    def select_device(is_simulator, device_uuid=None):
        if device_uuid is None:
            device_uuid = os.environ.get("DEVICE_UUID", None)

        all_devices = IosDevice.all_devices(is_simulator)
        for device in all_devices:
            if device_uuid is not None:
                if device.uuid == device_uuid:
                    return device
            elif not device.is_simulator or device.state == "Booted":
                return device
        raise Exception(
            "Couldn't find a booted and connected iOS device with the correct platform"
        )


# FIXME: Does not have support for the features of IosDeviceSimulator, nor does it
# have the same API yet...
class IosDeviceReal(IosDevice):
    def __init__(self, uuid, name):
        super().__init__(uuid, name, False)

    def install(self, app_bundle):
        subprocess.check_call([
            "xcrun",
            "devicectl",
            "device",
            "install",
            "app",
            "--device",
            self.uuid,
            app_bundle,
        ])

    @staticmethod
    def all_devices():
        with tempfile.NamedTemporaryFile() as tmpfile:
            subprocess.check_call(
                ["xcrun", "devicectl", "list", "devices", "-j", tmpfile.name],
                stdout=subprocess.DEVNULL,
            )
            output = json.load(tmpfile)

        if output["info"]["outcome"] != "success":
            sys.stderr.write("Failed to read device list")
            return []

        return [
            IosDeviceReal(device["identifier"], device["deviceProperties"]["name"])
            for device in output["result"]["devices"]
        ]


class IosDeviceSimulator(IosDevice):
    def __init__(self, uuid, name, runtime, datapath, logpath, state):
        super().__init__(uuid, name, True)
        self.runtime = runtime
        self.datapath = datapath
        self.logpath = logpath
        self.state = state

    def install(self, app_bundle):
        subprocess.check_call(["xcrun", "simctl", "install", self.uuid, app_bundle])

    def xcode_destination_specifier(self):
        return "platform=iOS simulator,id=" + self.uuid

    def launch_process(
        self,
        bundle_id,
        args=[],
        env=None,
        wait_for_debugger=False,
        terminate_running_process=True,
        **kwargs,
    ):
        # Put provided environment variables in `SIMCTL_CHILD_` so they
        # propagate into the simulator.
        kwargs["env"] = os.environ.copy()
        if env:
            for name, value in env.items():
                kwargs["env"]["SIMCTL_CHILD_" + name] = value

        # Specify provided flags
        extra_args = []
        if wait_for_debugger:
            extra_args += ["--wait-for-debugger"]
        if terminate_running_process:
            extra_args += ["--terminate-running-process"]

        # XXX: this should perhaps capture stdout/stderr with
        # `--stdout/--stderr` rather than `--console`?

        # FIXME: the ProcessHandlerMixin will have the pid for the xcrun
        # command, not the actual app, which isn't great for debugging.

        return processhandler.ProcessHandlerMixin(
            [
                "xcrun",
                "simctl",
                "launch",
                *extra_args,
                "--console",
                self.uuid,
                bundle_id,
                *args,
            ],
            **kwargs,
        )

    def test_root(self, bundle_id):
        container_path = subprocess.check_output(
            [
                "xcrun",
                "simctl",
                "get_app_container",
                self.uuid,
                bundle_id,
                "data",
            ],
            text=True,
        )
        return os.path.join(container_path.strip(), "test_root")

    def rm(self, path, force=False, recursive=False):
        try:
            if recursive:
                shutil.rmtree(path, ignore_errors=True)
            else:
                os.remove(path)
        except Exception:
            pass

    def mkdir(self, path, parents=False):
        if parents:
            os.makedirs(path, exist_ok=True)
        else:
            os.mkdir(path)

    def push(self, local, remote):
        shutil.copytree(local, remote, dirs_exist_ok=True)

    def pull(self, remote, local):
        shutil.copytree(remote, local, dirs_exist_ok=True)

    def chmod(self, path, recursive=False, mask="777"):
        if recursive:
            for root, dirs, files in os.walk(path):
                for d in dirs:
                    os.chmod(os.path.join(root, d), int(mask, 8))
                for f in files:
                    os.chmod(os.path.join(root, f), int(mask, 8))
        else:
            os.chmod(path, int(mask, 8))

    def is_file(self, path):
        return os.path.isfile(path)

    def is_dir(self, path):
        return os.path.isdir(path)

    def get_file(self, path, offset=None, length=None):
        with open(path, mode="rb") as f:
            if offset is not None and length is not None:
                f.seek(offset)
                return f.read(length)
            if offset is not None:
                f.seek(offset)
                return f.read()
            return f.read()

    def stop_application(self, bundle_id):
        try:
            subprocess.check_call([
                "xcrun",
                "simctl",
                "terminate",
                self.uuid,
                bundle_id,
            ])
        except subprocess.CalledProcessError:
            pass

    @staticmethod
    def all_devices():
        output = json.loads(
            subprocess.check_output(["xcrun", "simctl", "list", "devices", "-j"])
        )

        result = []
        for runtime, devices in output["devices"].items():
            for device in devices:
                if not device["isAvailable"]:
                    continue
                result.append(
                    IosDeviceSimulator(
                        device["udid"],
                        device["name"],
                        runtime,
                        device["dataPath"],
                        device["logPath"],
                        device["state"],
                    )
                )
        return result
