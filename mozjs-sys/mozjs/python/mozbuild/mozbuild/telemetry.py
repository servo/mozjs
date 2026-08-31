# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
This file contains functions used for telemetry.
"""

import functools
import os
import platform
import subprocess
import sys

import distro
import mozpack.path as mozpath


def cpu_brand_linux():
    """
    Read the CPU brand string out of /proc/cpuinfo on Linux.
    """
    with open("/proc/cpuinfo") as f:
        for line in f:
            if line.startswith("model name"):
                _, brand = line.split(": ", 1)
                return brand.rstrip()
    # not found?
    return None


def cpu_brand_windows():
    """
    Read the CPU brand string from the registry on Windows.
    """
    try:
        import _winreg
    except ImportError:
        import winreg as _winreg

    try:
        h = _winreg.OpenKey(
            _winreg.HKEY_LOCAL_MACHINE,
            r"HARDWARE\DESCRIPTION\System\CentralProcessor\0",
        )
        (brand, ty) = _winreg.QueryValueEx(h, "ProcessorNameString")
        if ty == _winreg.REG_SZ:
            return brand
    except OSError:
        pass
    return None


def cpu_brand_mac():
    """
    Get the CPU brand string via sysctl on macos.
    """
    import ctypes
    import ctypes.util

    libc = ctypes.cdll.LoadLibrary(ctypes.util.find_library("c"))
    # First, find the required buffer size.
    bufsize = ctypes.c_size_t(0)
    result = libc.sysctlbyname(
        b"machdep.cpu.brand_string", None, ctypes.byref(bufsize), None, 0
    )
    if result != 0:
        return None
    bufsize.value += 1
    buf = ctypes.create_string_buffer(bufsize.value)
    # Now actually get the value.
    result = libc.sysctlbyname(
        b"machdep.cpu.brand_string", buf, ctypes.byref(bufsize), None, 0
    )
    if result != 0:
        return None

    return buf.value.decode()


def get_cpu_brand():
    """
    Get the CPU brand string as returned by CPUID.
    """
    return {
        "Linux": cpu_brand_linux,
        "Windows": cpu_brand_windows,
        "Darwin": cpu_brand_mac,
    }.get(platform.system(), lambda: None)()


def get_psutil_stats():
    """Return whether psutil exists and its associated stats.

    @returns (bool, int, int, int) whether psutil exists, the logical CPU count,
        physical CPU count, and total number of bytes of memory.
    """
    try:
        import psutil

        return (
            True,
            psutil.cpu_count(),
            psutil.cpu_count(logical=False),
            psutil.virtual_memory().total,
        )
    except ImportError:
        return False, None, None, None


def filter_args(command, argv, topsrcdir, topobjdir, cwd=None):
    """
    Given the full list of command-line arguments, remove anything up to and including `command`,
    and attempt to filter absolute pathnames out of any arguments after that.
    """
    if cwd is None:
        cwd = os.getcwd()

    # Each key is a pathname and the values are replacement sigils
    paths = {
        topsrcdir: "$topsrcdir/",
        topobjdir: "$topobjdir/",
        mozpath.normpath(os.path.expanduser("~")): "$HOME/",
        # This might override one of the existing entries, that's OK.
        # We don't use a sigil here because we treat all arguments as potentially relative
        # paths, so we'd like to get them back as they were specified.
        mozpath.normpath(cwd): "",
    }

    args = list(argv)
    while args:
        a = args.pop(0)
        if a == command:
            break

    def filter_path(p):
        p = mozpath.abspath(p)
        base = mozpath.basedir(p, paths.keys())
        if base:
            return paths[base] + mozpath.relpath(p, base)
        # Best-effort.
        return "<path omitted>"

    return [filter_path(arg) for arg in args]


def get_distro_and_version():
    if sys.platform.startswith("linux"):
        return distro.id(), distro.version()
    elif sys.platform.startswith("darwin"):
        return "macos", platform.mac_ver()[0]
    elif sys.platform.startswith("win32") or sys.platform.startswith("msys"):
        ver = sys.getwindowsversion()
        return "windows", f"{ver.major}.{ver.minor}.{ver.build}"
    else:
        return sys.platform, ""


def get_shell_info():
    """Returns if the current shell was opened by vscode and if it's a SSH connection"""

    return (
        True if "vscode" in os.getenv("TERM_PROGRAM", "") else False,
        bool(os.getenv("SSH_CLIENT", "")),
    )


def get_vscode_running():
    """Return if the vscode is currently running."""
    return any(
        _is_process_running(pname)
        for pname in ("Code.exe", "Code Helper (Renderer)", "code")
    )


def _is_process_running(process_name: str) -> bool:
    """Check if a process is running using psutil."""

    @functools.cache
    def list_running_processes():
        running_processes = set()
        try:
            import psutil

            for proc in psutil.process_iter():
                try:
                    running_processes.add(proc.name())
                except Exception:
                    continue
        except Exception:
            ...
        return running_processes

    return process_name in list_running_processes()


def get_fleet_running() -> bool:
    """Try to determine if FleetDM endpoint management tool is running on the machine.

    If we can't make a determination, return False.
    """
    system = platform.system()
    if system == "Darwin":
        return _is_process_running("osqueryd")
    elif system == "Windows":
        try:
            out = subprocess.check_output(
                ["sc", "query", "Fleet osquery"], stderr=subprocess.DEVNULL, text=True
            )
            for line in out.splitlines():
                if "STATE" in line and "RUNNING" in line:
                    return True
        except (subprocess.CalledProcessError, FileNotFoundError):
            pass
    elif system == "Linux":
        try:
            result = subprocess.run(
                ["systemctl", "is-active", "osqueryd"],
                capture_output=True,
                check=False,
            )
            if result.returncode == 0:
                return True
        except (subprocess.CalledProcessError, FileNotFoundError):
            pass
        return _is_process_running("osqueryd")
    return False


def get_crowdstrike_running() -> bool:
    """Try to determine if CrowdStrike security software is running on the machine.

    If we can't make a determination, return False.
    """
    system = platform.system()
    if system == "Darwin":
        try:
            out = subprocess.check_output(
                ["systemextensionsctl", "list"], stderr=subprocess.DEVNULL, text=True
            )
            for line in out.splitlines():
                if (
                    "com.crowdstrike.falcon.Agent" in line
                    and "activated enabled" in line.lower()
                ):
                    return True
        except (subprocess.CalledProcessError, FileNotFoundError):
            pass
    elif system == "Windows":
        try:
            out = subprocess.check_output(
                ["sc", "query", "CSFalconService"], stderr=subprocess.DEVNULL, text=True
            )
            for line in out.splitlines():
                if "STATE" in line and "RUNNING" in line:
                    return True
        except (subprocess.CalledProcessError, FileNotFoundError):
            pass
    elif system == "Linux":
        try:
            result = subprocess.run(
                ["systemctl", "is-active", "falcon-sensor"],
                capture_output=True,
                check=False,
            )
            if result.returncode == 0:
                return True
        except (subprocess.CalledProcessError, FileNotFoundError):
            pass
        return _is_process_running("falcon-sensor")

    return False
