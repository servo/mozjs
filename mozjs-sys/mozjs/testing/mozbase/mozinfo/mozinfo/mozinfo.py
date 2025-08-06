#!/usr/bin/env python

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

# TODO: it might be a good idea of adding a system name (e.g. 'Ubuntu' for
# linux) to the information; I certainly wouldn't want anyone parsing this
# information and having behaviour depend on it

import os
import platform
import re
import sys
from ctypes.util import find_library

from .string_version import StringVersion

# keep a copy of the os module since updating globals overrides this
_os = os


class unknown(object):
    """marker class for unknown information"""

    # pylint: disable=W1629
    def __nonzero__(self):
        return False

    def __bool__(self):
        return False

    def __str__(self):
        return "UNKNOWN"


unknown = unknown()  # singleton


# get system information
info = {
    "os": unknown,
    "processor": unknown,
    "version": unknown,
    "os_version": unknown,
    "bits": unknown,
    "has_sandbox": unknown,
    "display": None,
    "automation": bool(os.environ.get("MOZ_AUTOMATION", False)),
}
(system, node, release, version, machine, processor) = platform.uname()
(bits, linkage) = platform.architecture()

# get os information and related data
if system in ["Microsoft", "Windows"]:
    info["os"] = "win"
    # uname().processor on Windows gives the full CPU name but
    # mozinfo.processor is only about CPU architecture
    processor = machine
    system = os.environ.get("OS", system).replace("_", " ")
    (major, minor, build_number, _, _) = os.sys.getwindowsversion()
    version = "%d.%d.%d" % (major, minor, build_number)
    if major == 10 and minor == 0 and build_number >= 22000:
        major = 11

    # 2009 == 22H2 software update.  These are the build numbers
    # we use 2009 as the "build" which maps to what taskcluster tasks see
    if build_number == 22621 or build_number == 19045:
        build_number = 2009

    os_version = "%d.%d" % (major, build_number)
elif system.startswith(("MINGW", "MSYS_NT")):
    # windows/mingw python build (msys)
    info["os"] = "win"
    os_version = version = unknown
elif system == "Linux":
    # Attempt to use distro package to determine Linux distribution first.
    # Failing that, fall back to use the platform method.
    # Note that platform.linux_distribution() will be deprecated as of 3.8
    # and this block will be removed once support for 2.7/3.5 is dropped.
    try:
        from distro import linux_distribution
    except ImportError:
        from platform import linux_distribution

    output = linux_distribution()
    (distribution, os_version, codename) = tuple(str(item.title()) for item in output)

    if not processor:
        processor = machine
    if not distribution:
        distribution = "lfs"
    if not os_version:
        os_version = release
    if not codename:
        codename = "unknown"
    version = "%s %s" % (distribution, os_version)

    if os.environ.get("WAYLAND_DISPLAY"):
        info["display"] = "wayland"
    elif os.environ.get("DISPLAY"):
        info["display"] = "x11"

    info["os"] = "linux"
    info["linux_distro"] = distribution
elif system in ["DragonFly", "FreeBSD", "NetBSD", "OpenBSD"]:
    info["os"] = "bsd"  # community builds
    version = os_version = sys.platform
elif system == "Darwin":
    (release, versioninfo, machine) = platform.mac_ver()
    version = "OS X %s" % release
    versionNums = release.split(".")[:2]
    os_version = "%s.%s" % (versionNums[0], versionNums[1].ljust(2, "0"))
    info["os"] = "mac"
elif sys.platform in ("solaris", "sunos5"):
    info["os"] = "unix"  # community builds
    os_version = version = sys.platform
else:
    os_version = version = unknown

info["apple_silicon"] = False
if (
    info["os"] == "mac"
    and float(os_version) > 10.15
    and processor == "arm"
    and bits == "64bit"
):
    info["apple_silicon"] = True

info["apple_catalina"] = False
if info["os"] == "mac" and float(os_version) == 10.15:
    info["apple_catalina"] = True

info["win10_2009"] = False
if info["os"] == "win" and version == "10.0.19045":
    info["win10_2009"] = True

info["win11_2009"] = False
if info["os"] == "win" and version == "10.0.22621":
    info["win11_2009"] = True

info["version"] = version
info["os_version"] = StringVersion(os_version)
info["is_ubuntu"] = "Ubuntu" in version

# processor type and bits
if processor in ["i386", "i686"]:
    if bits == "32bit":
        processor = "x86"
    elif bits == "64bit":
        processor = "x86_64"
elif processor.upper() == "AMD64":
    bits = "64bit"
    processor = "x86_64"
elif processor.upper() == "ARM64":
    bits = "64bit"
    processor = "aarch64"
elif processor == "arm" and bits == "64bit":
    processor = "aarch64"

bits = re.search(r"(\d+)bit", bits).group(1)
info.update(
    {
        "processor": processor,
        "bits": int(bits),
    }
)

# we want to transition to this instead of using `!debug`, etc.
info["arch"] = info["processor"]


if info["os"] == "linux":
    import ctypes
    import errno

    PR_SET_SECCOMP = 22
    SECCOMP_MODE_FILTER = 2
    ctypes.CDLL(find_library("c"), use_errno=True).prctl(
        PR_SET_SECCOMP, SECCOMP_MODE_FILTER, 0
    )
    info["has_sandbox"] = ctypes.get_errno() == errno.EFAULT
else:
    info["has_sandbox"] = True

# standard value of choices, for easy inspection
choices = {
    "os": ["linux", "win", "mac"],
    "bits": [32, 64],
    "processor": ["x86", "x86_64", "aarch64"],
}


def sanitize(info):
    """Do some sanitization of input values, primarily
    to handle universal Mac builds."""
    if "processor" in info and info["processor"] == "universal-x86-x86_64":
        # If we're running on OS X 10.6 or newer, assume 64-bit
        if release[:4] >= "10.6":  # Note this is a string comparison
            info["processor"] = "x86_64"
            info["bits"] = 64
        else:
            info["processor"] = "x86"
            info["bits"] = 32


# method for updating information


def update(new_info):
    """
    Update the info.

    :param new_info: Either a dict containing the new info or a path/url
                     to a json file containing the new info.
    """
    from six import string_types

    if isinstance(new_info, string_types):
        # lazy import
        import json

        import mozfile

        f = mozfile.load(new_info)
        new_info = json.loads(f.read())
        f.close()

    info.update(new_info)
    sanitize(info)
    globals().update(info)

    # convenience data for os access
    for os_name in choices["os"]:
        globals()["is" + os_name.title()] = info["os"] == os_name


def find_and_update_from_json(*dirs, **kwargs):
    """Find a mozinfo.json file, load it, and update global symbol table.

    This method will first check the relevant objdir directory for the
    necessary mozinfo.json file, if the current script is being run from a
    Mozilla objdir.

    If the objdir directory did not supply the necessary data, this method
    will then look for the required mozinfo.json file from the provided
    tuple of directories.

    If file is found, the global symbols table is updated via a helper method.

    If no valid files are found, this method no-ops unless the raise_exception
    kwargs is provided with explicit boolean value of True.

    :param tuple dirs: Directories in which to look for the file.
    :param dict kwargs: optional values:
                        raise_exception: if True, exceptions are raised.
                        False by default.
    :returns: None: default behavior if mozinfo.json cannot be found.
              json_path: string representation of mozinfo.json path.
    :raises: IOError: if raise_exception is True and file is not found.
    """
    # First, see if we're in an objdir
    try:
        from mozboot.mozconfig import MozconfigFindException
        from mozbuild.base import BuildEnvironmentNotFoundException, MozbuildObject

        build = MozbuildObject.from_environment()
        json_path = _os.path.join(build.topobjdir, "mozinfo.json")
        if _os.path.isfile(json_path):
            update(json_path)
            return json_path
    except ImportError:
        pass
    except (BuildEnvironmentNotFoundException, MozconfigFindException):
        pass

    for dir in dirs:
        d = _os.path.abspath(dir)
        json_path = _os.path.join(d, "mozinfo.json")
        if _os.path.isfile(json_path):
            update(json_path)
            return json_path

    # by default, exceptions are suppressed. Set this to True if otherwise
    # desired.
    if kwargs.get("raise_exception", False):
        raise IOError("mozinfo.json could not be found.")
    return None


def output_to_file(path):
    import json

    with open(path, "w") as f:
        f.write(json.dumps(info))


update({})

# exports
__all__ = list(info.keys())
__all__ += ["is" + os_name.title() for os_name in choices["os"]]
__all__ += [
    "info",
    "unknown",
    "main",
    "choices",
    "update",
    "find_and_update_from_json",
    "output_to_file",
    "StringVersion",
]


def main(args=None):
    # parse the command line
    from optparse import OptionParser

    parser = OptionParser(description=__doc__)
    for key in choices:
        parser.add_option(
            "--%s" % key,
            dest=key,
            action="store_true",
            default=False,
            help="display choices for %s" % key,
        )
    options, args = parser.parse_args()

    # args are JSON blobs to override info
    if args:
        # lazy import
        import json

        for arg in args:
            if _os.path.exists(arg):
                string = open(arg).read()
            else:
                string = arg
            update(json.loads(string))

    # print out choices if requested
    flag = False
    for key, value in options.__dict__.items():
        if value is True:
            print(
                "%s choices: %s"
                % (key, " ".join([str(choice) for choice in choices[key]]))
            )
            flag = True
    if flag:
        return

    # otherwise, print out all info
    for key, value in info.items():
        print("%s: %s" % (key, value))


if __name__ == "__main__":
    main()
