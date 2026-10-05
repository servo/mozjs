# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Package the application into the requested archive format.

import argparse
import os
import subprocess
import sys

import mozpack.path as mozpath

_TAR_FLAGS = (
    "--owner=0",
    "--group=0",
    "--numeric-owner",
    "--mode=go-w",
    "--exclude=.mkdir.done",
)

FORMAT_SUFFIX = {
    "TAR": ".tar",
    "TGZ": ".tar.gz",
    "XZ": ".tar.xz",
    "BZ2": ".tar.bz2",
    "ZIP": ".zip",
    "DMG": ".dmg",
    "APK": "",
}


def _run_tar_pipeline(tar_bin, cwd, tar_args, output_path, compress_argv):
    tar_argv = [tar_bin, "-c", *_TAR_FLAGS, "-f", "-", *tar_args]
    if compress_argv is None:
        with open(output_path, "wb") as out:
            return subprocess.call(tar_argv, cwd=cwd, stdout=out)
    tar = subprocess.Popen(tar_argv, cwd=cwd, stdout=subprocess.PIPE)
    with open(output_path, "wb") as out:
        comp = subprocess.Popen(compress_argv, stdin=tar.stdout, stdout=out)
        tar.stdout.close()
        comp_rc = comp.wait()
        tar_rc = tar.wait()
    return tar_rc or comp_rc


def _zip_package(cwd, pkg_dir, output_path):
    from mozbuild.action import zip as zip_action

    args = ["-C", cwd, "-x", "**/.mkdir.done", output_path, pkg_dir]
    return zip_action.main(args) or 0


def _make_dmg(cwd, pkg_dir, output_path, dsstore, background, icon, volume_name):
    from mozbuild.action import make_dmg as dmg_action

    args = []
    if dsstore:
        args += ["--dsstore", dsstore]
    if background:
        args += ["--background", background]
    if icon:
        args += ["--icon", icon]
    if volume_name:
        args += ["--volume-name", volume_name]
    args += [pkg_dir, output_path]
    prev = os.getcwd()
    try:
        os.chdir(cwd)
        return dmg_action.main(args) or 0
    finally:
        os.chdir(prev)


def main(argv):
    parser = argparse.ArgumentParser(
        description="Package the application into the requested archive format."
    )
    parser.add_argument(
        "--format",
        required=True,
        choices=("TAR", "TGZ", "XZ", "BZ2", "ZIP", "DMG"),
    )
    parser.add_argument("--cwd", required=True)
    parser.add_argument("--pkg-dir", required=True)
    parser.add_argument("--output-dir", default="")
    parser.add_argument("--basename", required=True)
    parser.add_argument("--tar", default="tar")
    parser.add_argument("--app-name", default=None)
    parser.add_argument("--strong-compression", action="store_true")
    parser.add_argument("--dsstore", default=None)
    parser.add_argument("--background", default=None)
    parser.add_argument("--icon", default=None)
    parser.add_argument("--volume-name", default=None)
    args = parser.parse_args(argv)

    cwd = mozpath.normsep(os.path.abspath(args.cwd))
    filename = args.basename + FORMAT_SUFFIX[args.format]
    if os.path.isabs(args.output_dir):
        output = mozpath.normsep(os.path.join(args.output_dir, filename))
    else:
        output = mozpath.normsep(os.path.join(cwd, args.output_dir, filename))

    os.makedirs(os.path.dirname(output), exist_ok=True)

    if args.format == "TAR":
        return _run_tar_pipeline(args.tar, cwd, [args.pkg_dir], output, None)
    if args.format == "TGZ":
        return _run_tar_pipeline(
            args.tar, cwd, [args.pkg_dir], output, ["gzip", "-vf9"]
        )
    if args.format == "XZ":
        xz_argv = ["xz", "--compress", "--stdout"]
        if args.strong_compression:
            xz_argv += ["-9", "--extreme"]
        return _run_tar_pipeline(args.tar, cwd, [args.pkg_dir], output, xz_argv)
    if args.format == "BZ2":
        if args.app_name:
            tar_args = ["-C", args.pkg_dir, args.app_name]
        else:
            tar_args = [args.pkg_dir]
        return _run_tar_pipeline(args.tar, cwd, tar_args, output, ["bzip2", "-vf"])
    if args.format == "ZIP":
        return _zip_package(cwd, args.pkg_dir, output)
    return _make_dmg(
        cwd,
        args.pkg_dir,
        output,
        args.dsstore,
        args.background,
        args.icon,
        args.volume_name,
    )


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
