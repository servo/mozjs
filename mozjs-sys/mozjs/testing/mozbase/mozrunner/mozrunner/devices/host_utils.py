# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import glob
import os
import platform
import re
import shutil
import subprocess
import sys
import urllib

MOZBUILD_PATH = os.environ.get(
    "MOZBUILD_STATE_PATH", os.path.expanduser(os.path.join("~", ".mozbuild"))
)

# We use the `android-device` directory for host utils for both Android and iOS.
EMULATOR_HOME_DIR = os.path.join(MOZBUILD_PATH, "android-device")

TOOLTOOL_PATH = "python/mozbuild/mozbuild/action/tooltool.py"

TRY_URL = "https://hg.mozilla.org/try/raw-file/default"

MANIFEST_PATH = "testing/config/tooltool-manifests"


def get_host_platform():
    plat = None
    if "darwin" in str(sys.platform).lower():
        plat = "macosx64"
    elif "win32" in str(sys.platform).lower():
        plat = "win32"
    elif "linux" in str(sys.platform).lower():
        if "64" in platform.architecture()[0]:
            plat = "linux64"
        else:
            plat = "linux32"
    return plat


def ensure_host_utils(build_obj, verbose):
    """Check whether MOZ_HOST_BIN has been set to a valid xre; if not,
    prompt to install one."""
    xre_path = os.environ.get("MOZ_HOST_BIN")
    err = None
    if not xre_path:
        err = (
            "environment variable MOZ_HOST_BIN is not set to a directory "
            "containing host xpcshell"
        )
    elif not os.path.isdir(xre_path):
        err = "$MOZ_HOST_BIN does not specify a directory"
    elif not os.path.isfile(os.path.join(xre_path, _get_xpcshell_name())):
        err = "$MOZ_HOST_BIN/xpcshell does not exist"
    if err:
        _maybe_update_host_utils(build_obj, verbose=verbose)
        xre_path = glob.glob(os.path.join(EMULATOR_HOME_DIR, "host-utils*"))
        for path in xre_path:
            if os.path.isdir(path) and os.path.isfile(
                os.path.join(path, _get_xpcshell_name())
            ):
                os.environ["MOZ_HOST_BIN"] = path
                err = None
                break
    if err:
        print("Host utilities not found: %s" % err)
        response = input("Download and setup your host utilities? (Y/n) ").strip()
        if response.lower().startswith("y") or response == "":
            _install_host_utils(build_obj, verbose=verbose)


def _log_debug(verbose, text):
    if verbose:
        print("DEBUG: %s" % text)


def _log_warning(text):
    print("WARNING: %s" % text)


def _log_info(text):
    print("%s" % text)


def _install_host_utils(build_obj, verbose):
    _log_info("Installing host utilities...")
    installed = False
    host_platform = get_host_platform()
    if host_platform:
        path = os.path.join(build_obj.topsrcdir, MANIFEST_PATH)
        path = os.path.join(path, host_platform, "hostutils.manifest")
        _get_tooltool_manifest(
            build_obj.substs,
            path,
            EMULATOR_HOME_DIR,
            "releng.manifest",
            verbose=verbose,
        )
        _tooltool_fetch(build_obj.substs)
        xre_path = glob.glob(os.path.join(EMULATOR_HOME_DIR, "host-utils*"))
        for path in xre_path:
            if os.path.isdir(path) and os.path.isfile(
                os.path.join(path, _get_xpcshell_name())
            ):
                os.environ["MOZ_HOST_BIN"] = path
                installed = True
            elif os.path.isfile(path):
                os.remove(path)
        if not installed:
            _log_warning("Unable to install host utilities.")
    else:
        _log_warning(
            "Unable to install host utilities -- your platform is not supported!"
        )


def _get_xpcshell_name():
    """
    Returns the xpcshell binary's name as a string (dependent on operating system).
    """
    xpcshell_binary = "xpcshell"
    if os.name == "nt":
        xpcshell_binary = "xpcshell.exe"
    return xpcshell_binary


def _maybe_update_host_utils(build_obj, verbose):
    """
    Compare the installed host-utils to the version name in the manifest;
    if the installed version is older, offer to update.
    """

    # Determine existing/installed version
    existing_path = None
    xre_paths = glob.glob(os.path.join(EMULATOR_HOME_DIR, "host-utils*"))
    for path in xre_paths:
        if os.path.isdir(path) and os.path.isfile(
            os.path.join(path, _get_xpcshell_name())
        ):
            existing_path = path
            break
    if existing_path is None:
        # if not installed, no need to upgrade (new version will be installed)
        return
    existing_version = os.path.basename(existing_path)

    # Determine manifest version
    manifest_version = None
    host_platform = get_host_platform()
    if host_platform:
        # Extract tooltool file name from manifest, something like:
        #     "filename": "host-utils-58.0a1.en-US-linux-x86_64.tar.gz",
        path = os.path.join(build_obj.topsrcdir, MANIFEST_PATH)
        manifest_path = os.path.join(path, host_platform, "hostutils.manifest")
        with open(manifest_path) as f:
            for line in f.readlines():
                m = re.search('.*"(host-utils-.*)"', line)
                if m:
                    manifest_version = m.group(1)
                    break

    # Compare, prompt, update
    if existing_version and manifest_version:
        hu_version_regex = r"host-utils-([\d\.]*)"
        manifest_version = float(re.search(hu_version_regex, manifest_version).group(1))
        existing_version = float(re.search(hu_version_regex, existing_version).group(1))
        if existing_version < manifest_version:
            _log_info("Your host utilities are out of date!")
            _log_info(
                "You have %s installed, but %s is available"
                % (existing_version, manifest_version)
            )
            response = input("Update host utilities? (Y/n) ").strip()
            if response.lower().startswith("y") or response == "":
                parts = os.path.split(existing_path)
                backup_dir = "_backup-" + parts[1]
                backup_path = os.path.join(parts[0], backup_dir)
                shutil.move(existing_path, backup_path)
                _install_host_utils(build_obj, verbose=verbose)


def _download_file(url, filename, path, verbose=False):
    _log_debug(verbose, "Download %s to %s/%s..." % (url, path, filename))
    f = urllib.request.urlopen(url)
    if not os.path.isdir(path):
        try:
            os.makedirs(path)
        except Exception as e:
            _log_warning(str(e))
            return False
    local_file = open(os.path.join(path, filename), "wb")
    local_file.write(f.read())
    local_file.close()
    _log_debug(verbose, "Downloaded %s to %s/%s" % (url, path, filename))
    return True


def _get_tooltool_manifest(substs, src_path, dst_path, filename, verbose=False):
    if not os.path.isdir(dst_path):
        try:
            os.makedirs(dst_path)
        except Exception as e:
            _log_warning(str(e))
    copied = False
    if substs and "top_srcdir" in substs:
        src = os.path.join(substs["top_srcdir"], src_path)
        if os.path.exists(src):
            dst = os.path.join(dst_path, filename)
            shutil.copy(src, dst)
            copied = True
            _log_debug(verbose, "Copied tooltool manifest %s to %s" % (src, dst))
    if not copied:
        url = os.path.join(TRY_URL, src_path)
        _download_file(url, filename, dst_path, verbose=verbose)


def _tooltool_fetch(substs):
    tooltool_full_path = os.path.join(substs["top_srcdir"], TOOLTOOL_PATH)
    command = [
        sys.executable,
        tooltool_full_path,
        "fetch",
        "-o",
        "-m",
        "releng.manifest",
    ]
    try:
        response = subprocess.check_output(command, cwd=EMULATOR_HOME_DIR)
        print("DEBUG: ", response)
    except Exception as e:
        print("WARNING: %s" % str(e))
