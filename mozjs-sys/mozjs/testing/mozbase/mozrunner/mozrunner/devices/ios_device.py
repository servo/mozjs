import os
import subprocess

from mozbuild.base import MachCommandConditions as conditions
from mozdevice.ios import IosDevice

from .host_utils import ensure_host_utils


def verify_ios_device(
    build_obj,
    install=False,
    xre=False,
    verbose=False,
    app=None,
):
    is_simulator = conditions.is_ios_simulator(build_obj)

    device_verified = False

    devices = IosDevice.all_devices(is_simulator)
    for device in devices:
        if not device.is_simulator or device.state == "Booted":
            device_verified = True
            # FIXME: This roughly mimics how `verify_android_device` works but
            # seems kinda jank - should we be copying this?
            os.environ["DEVICE_UUID"] = device.uuid
            break

    if is_simulator and not device_verified:
        # FIXME: Offer to launch a simulator here.
        print("No iOS simulator started.")
        return

    if device_verified and install:
        if not app:
            app = "org.mozilla.ios.GeckoTestBrowser"

        device = IosDevice.select_device(is_simulator)
        if app == "org.mozilla.ios.GeckoTestBrowser":
            # FIXME: This should probably be happening as a build step, rather
            # than happening during verify_ios_device!
            print("Packaging GeckoTestBrowser...")
            subprocess.check_call([
                "xcodebuild",
                "-project",
                os.path.join(
                    build_obj.topsrcdir,
                    "mobile/ios/GeckoTestBrowser/GeckoTestBrowser.xcodeproj",
                ),
                "-scheme",
                "GeckoTestBrowser",
                "-destination",
                device.xcode_destination_specifier(),
                "install",
                "DSTROOT=" + build_obj.distdir,
                "TOPOBJDIR=" + build_obj.topobjdir,
            ])

            print("Installing GeckoTestBrowser...")
            device.install(
                os.path.join(build_obj.distdir, "Applications/GeckoTestBrowser.app")
            )
        else:
            # FIXME: If the app is already installed, don't prompt the user here
            # to align with verify_android_device.
            input(
                "Application %s cannot be automatically installed\n"
                "Install it now, then hit Enter " % app
            )

    if device_verified and xre:
        ensure_host_utils(build_obj, verbose)

    return device_verified
