# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import platform
import subprocess
import sys

from mozboot.util import get_tools_dir
from mozfile import which
from mozfile.mozfile import remove as mozfileremove
from packaging.version import Version

NODE_MIN_VERSION = Version("12.22.12")
NPM_MIN_VERSION = Version("6.14.16")

NODE_MACHING_VERSION_NOT_FOUND_MESSAGE = """
Could not find Node.js executable later than %s.

Executing `mach bootstrap --no-system-changes` should
install a compatible version in ~/.mozbuild on most platforms.
""".strip()

NPM_MACHING_VERSION_NOT_FOUND_MESSAGE = """
Could not find npm executable later than %s.

Executing `mach bootstrap --no-system-changes` should
install a compatible version in ~/.mozbuild on most platforms.
""".strip()

NODE_NOT_FOUND_MESSAGE = """
nodejs is either not installed or is installed to a non-standard path.

Executing `mach bootstrap --no-system-changes` should
install a compatible version in ~/.mozbuild on most platforms.
""".strip()

NPM_NOT_FOUND_MESSAGE = """
Node Package Manager (npm) is either not installed or installed to a
non-standard path.

Executing `mach bootstrap --no-system-changes` should
install a compatible version in ~/.mozbuild on most platforms.
""".strip()


def find_node_paths():
    """Determines the possible paths for node executables.

    Returns a list of paths, which includes the build state directory.
    """
    mozbuild_tools_dir = get_tools_dir()

    if platform.system() == "Windows":
        mozbuild_node_path = os.path.join(mozbuild_tools_dir, "node")
    else:
        mozbuild_node_path = os.path.join(mozbuild_tools_dir, "node", "bin")

    # We still fallback to the PATH, since on OSes that don't have toolchain
    # artifacts available to download, Node may be coming from $PATH.
    paths = [mozbuild_node_path] + os.environ.get("PATH").split(os.pathsep)

    if platform.system() == "Windows":
        paths += [
            "%s\\nodejs" % os.environ.get("SystemDrive"),
            os.path.join(os.environ.get("ProgramFiles"), "nodejs"),
            os.path.join(os.environ.get("PROGRAMW6432"), "nodejs"),
            os.path.join(os.environ.get("PROGRAMFILES"), "nodejs"),
        ]

    return paths


def check_executable_version(exe, wrap_call_with_node=False):
    """Determine the version of a Node executable by invoking it.

    May raise ``subprocess.CalledProcessError`` or ``ValueError`` on failure.
    """
    out = None
    # npm may be a script (Except on Windows), so we must call it with node.
    if wrap_call_with_node and platform.system() != "Windows":
        binary, _ = find_node_executable()
        if binary:
            out = (
                subprocess
                .check_output([binary, exe, "--version"], universal_newlines=True)
                .lstrip("v")
                .rstrip()
            )

    # If we can't find node, or we don't need to wrap it, fallback to calling
    # direct.
    if not out:
        out = (
            subprocess
            .check_output([exe, "--version"], universal_newlines=True)
            .lstrip("v")
            .rstrip()
        )
    return Version(out)


def find_node_executable(
    nodejs_exe=os.environ.get("NODEJS"), min_version=NODE_MIN_VERSION
):
    """Find a Node executable from the mozbuild directory.

    Returns a tuple containing the the path to an executable binary and a
    version tuple. Both tuple entries will be None if a Node executable
    could not be resolved.
    """
    if nodejs_exe:
        try:
            version = check_executable_version(nodejs_exe)
        except (subprocess.CalledProcessError, ValueError):
            return None, None

        if version >= min_version:
            return nodejs_exe, version.release

        return None, None

    # "nodejs" is first in the tuple on the assumption that it's only likely to
    # exist on systems (probably linux distros) where there is a program in the path
    # called "node" that does something else.
    return find_executable("node", min_version)


def find_npm_executable(min_version=NPM_MIN_VERSION):
    """Find a Node executable from the mozbuild directory.

    Returns a tuple containing the the path to an executable binary and a
    version tuple. Both tuple entries will be None if a Node executable
    could not be resolved.
    """
    return find_executable("npm", min_version, True)


def find_npx_executable(min_version=NPM_MIN_VERSION):
    """Find the npx executable from the mozbuild directory.

    Returns a tuple containing the path to an executable binary and a
    version tuple. Both tuple entries will be None if an npx executable
    could not be resolved.
    """
    return find_executable("npx", min_version, True)


def find_executable(name, min_version, use_node_for_version_check=False):
    paths = find_node_paths()
    exe = which(name, path=paths)

    if not exe:
        return None, None

    # Verify we can invoke the executable and its version is acceptable.
    try:
        version = check_executable_version(exe, use_node_for_version_check)
    except (subprocess.CalledProcessError, ValueError):
        return None, None

    if version < min_version:
        return None, None

    return exe, version.release


def check_node_executables_valid():
    node_path, version = find_node_executable()
    if not node_path:
        print(NODE_NOT_FOUND_MESSAGE)
        return False
    if not version:
        print(NODE_MACHING_VERSION_NOT_FOUND_MESSAGE % NODE_MIN_VERSION)
        return False

    npm_path, version = find_npm_executable()
    if not npm_path:
        print(NPM_NOT_FOUND_MESSAGE)
        return False
    if not version:
        print(NPM_MACHING_VERSION_NOT_FOUND_MESSAGE % NPM_MIN_VERSION)
        return False

    return True


def package_setup(
    package_root,
    package_name,
    should_update=False,
    should_clobber=False,
    no_optional=False,
    skip_logging=False,
):
    """Ensure `package_name` at `package_root` is installed.

    When `should_update` is true, clobber, install, and produce a new
    "package-lock.json" file.

    This populates `package_root/node_modules`.

    """
    orig_cwd = os.getcwd()

    if should_update:
        should_clobber = True

    try:
        # npm sometimes fails to respect cwd when it is run using check_call so
        # we manually switch folders here instead.
        os.chdir(package_root)

        if should_clobber:
            remove_directory(os.path.join(package_root, "node_modules"), skip_logging)

        npm_path, _ = find_npm_executable()
        if not npm_path:
            return 1

        node_path, _ = find_node_executable()
        if not node_path:
            return 1

        extra_parameters = ["--loglevel=error"]

        if no_optional:
            extra_parameters.append("--no-optional")

        package_lock_json_path = os.path.join(package_root, "package-lock.json")

        if should_update:
            cmd = [npm_path, "install"]
            mozfileremove(package_lock_json_path)
        else:
            cmd = [npm_path, "ci"]

        # On non-Windows, ensure npm is called via node, as node may not be in the
        # path.
        if platform.system() != "Windows":
            cmd.insert(0, node_path)

        cmd.extend(extra_parameters)

        # Ensure that bare `node` and `npm` in scripts, including post-install scripts, finds the
        # binary we're invoking with.  Without this, it's easy for compiled extensions to get
        # mismatched versions of the Node.js extension API.
        path = os.environ.get("PATH", "").split(os.pathsep)
        node_dir = os.path.dirname(node_path)
        if node_dir not in path:
            path = [node_dir] + path

        if not skip_logging:
            print(
                'Installing %s for mach using "%s"...' % (package_name, " ".join(cmd))
            )
        result = call_process(
            package_name, cmd, append_env={"PATH": os.pathsep.join(path)}
        )

        if not result:
            return 1

        bin_path = os.path.join(package_root, "node_modules", ".bin", package_name)

        if not skip_logging:
            print("\n%s installed successfully!" % package_name)
            print("\nNOTE: Your local %s binary is at %s\n" % (package_name, bin_path))

    finally:
        os.chdir(orig_cwd)


def remove_directory(path, skip_logging=False):
    if not skip_logging:
        print("Clobbering %s..." % path)
    if sys.platform.startswith("win") and have_winrm():
        process = subprocess.Popen(["winrm", "-rf", path])
        process.wait()
    else:
        mozfileremove(path)


def call_process(name, cmd, cwd=None, append_env={}):
    env = dict(os.environ)
    env.update(append_env)

    try:
        with open(os.devnull, "w") as fnull:
            subprocess.check_call(cmd, cwd=cwd, stdout=fnull, env=env)
    except subprocess.CalledProcessError:
        if cwd:
            print("\nError installing %s in the %s folder, aborting." % (name, cwd))
        else:
            print("\nError installing %s, aborting." % name)

        return False

    return True


def have_winrm():
    # `winrm -h` should print 'winrm version ...' and exit 1
    try:
        p = subprocess.Popen(
            ["winrm.exe", "-h"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT
        )
        return p.wait() == 1 and p.stdout.read().startswith("winrm")
    except Exception:
        return False
