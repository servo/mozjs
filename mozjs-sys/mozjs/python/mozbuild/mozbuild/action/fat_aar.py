# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Fetch and unpack architecture-specific Maven zips, verify cross-architecture
compatibility, and ready inputs to an Android multi-architecture fat AAR build.
"""

import argparse
import subprocess
import sys
from collections import OrderedDict, defaultdict
from hashlib import sha1  # We don't need a strong hash to compare inputs.
from io import BytesIO
from zipfile import ZipFile

import buildconfig
import mozpack.path as mozpath
from mozpack.copier import FileCopier
from mozpack.files import JarFinder
from mozpack.mozjar import JarReader
from mozpack.packager.unpack import UnpackFinder


def _download_zip(distdir, arch):
    # The mapping from Android CPU architecture to TC job is defined here, and the TC index
    # lookup is mediated by python/mozbuild/mozbuild/artifacts.py and
    # python/mozbuild/mozbuild/artifact_builds.py.
    jobs = {
        "arm64-v8a": "android-aarch64-opt",
        "armeabi-v7a": "android-arm-opt",
        "x86_64": "android-x86_64-opt",
    }

    dest = mozpath.join(distdir, "input", arch)
    subprocess.check_call([
        sys.executable,
        mozpath.join(buildconfig.topsrcdir, "mach"),
        "artifact",
        "install",
        "--job",
        jobs[arch],
        "--distdir",
        dest,
        "--no-tests",
        "--no-process",
        "--artifact-filter",
        "public/build/target.maven.zip",
    ])
    return mozpath.join(dest, "target.maven.zip")


def fat_aar(distdir, zip_paths, no_process=False, no_compatibility_check=False):
    if no_process:
        print("Not processing architecture-specific artifact Maven AARs.")
        return 0

    # Map {filename: {fingerprint: [arch1, arch2, ...]}}.
    diffs = defaultdict(lambda: defaultdict(list))
    missing_arch_prefs = set()
    # Collect multi-architecture inputs to the fat AAR.
    copier = FileCopier()

    for arch, zip_path in zip_paths.items():
        if not zip_path:
            zip_path = _download_zip(distdir, arch)
        # Map old non-architecture-specific path to new architecture-specific path.
        old_rewrite_map = {
            "greprefs.js": f"{arch}/greprefs.js",
            "defaults/pref/geckoview-prefs.js": f"defaults/pref/{arch}/geckoview-prefs.js",
        }

        # Architecture-specific preferences files.
        arch_prefs = set(old_rewrite_map.values())
        missing_arch_prefs |= set(arch_prefs)

        aars = [
            (path, file)
            for (path, file) in JarFinder(zip_path, JarReader(zip_path)).find(
                "**/geckoview-*.aar"
            )
        ]
        if len(aars) != 1:
            raise ValueError(
                f'Maven zip "{zip_path}" with more than one candidate AAR found: {sorted(p for p, _ in aars)}'
            )
        aar_path, aar_file = aars[0]

        jar_finder = JarFinder(
            aar_file.file.filename, JarReader(fileobj=aar_file.open())
        )
        for path, fileobj in UnpackFinder(jar_finder):
            # Native libraries go straight through.
            if mozpath.match(path, "jni/**"):
                copier.add(path, fileobj)

            elif path in arch_prefs:
                copier.add(path, fileobj)

            elif path in ("classes.jar", "annotations.zip"):
                # annotations.zip differs due to timestamps, but the contents should not.

                # `JarReader` fails on the non-standard `classes.jar` produced by Gradle/aapt,
                # and it's not worth working around, so we use Python's zip functionality
                # instead.
                z = ZipFile(BytesIO(fileobj.open().read()))
                for r in z.namelist():
                    fingerprint = sha1(z.open(r).read()).hexdigest()
                    diffs[f"{path}!/{r}"][fingerprint].append(arch)

            else:
                fingerprint = sha1(fileobj.open().read()).hexdigest()
                # There's no need to distinguish `target.maven.zip` from `assets/omni.ja` here,
                # since in practice they will never overlap.
                diffs[path][fingerprint].append(arch)

            missing_arch_prefs.discard(path)

    # Some differences are allowed across the architecture-specific AARs.  We could allow-list
    # the actual content, but it's not necessary right now.
    allow_pattern_list = {
        "AndroidManifest.xml",  # Min SDK version is different for 32- and 64-bit builds.
        "classes.jar!/org/mozilla/gecko/util/HardwareUtils.class",  # Min SDK as well.
        "classes.jar!/org/mozilla/geckoview/BuildConfig.class",
        # Each input captures its CPU architecture.
        "chrome/toolkit/content/global/buildconfig.html",
        # Bug 1556162: localized resources are not deterministic across
        # per-architecture builds triggered from the same push.
        "**/*.ftl",
        "**/*.dtd",
        "**/*.properties",
    }

    not_allowed = OrderedDict()

    def format_diffs(ds):
        # Like '  armeabi-v7a, arm64-v8a -> XXX\n  x86_64 -> YYY'.
        return "\n".join(
            sorted(
                "  {archs} -> {fingerprint}".format(
                    archs=", ".join(sorted(archs)), fingerprint=fingerprint
                )
                for fingerprint, archs in ds.items()
            )
        )

    for p, ds in sorted(diffs.items()):
        if len(ds) <= 1:
            # Only one hash across all inputs: roll on.
            continue

        if any(mozpath.match(p, pat) for pat in allow_pattern_list):
            print(
                f'Allowed: Path "{p}" has architecture-specific versions:\n{format_diffs(ds)}'
            )
            continue

        not_allowed[p] = ds

    for p, ds in not_allowed.items():
        print(
            f'Disallowed: Path "{p}" has architecture-specific versions:\n{format_diffs(ds)}'
        )

    for missing in sorted(missing_arch_prefs):
        print(
            f"Disallowed: Inputs missing expected architecture-specific input: {missing}"
        )

    if not no_compatibility_check and (missing_arch_prefs or not_allowed):
        return 1

    output_dir = mozpath.join(distdir, "output")
    copier.copy(output_dir)

    return 0


_ALL_ARCHS = ("armeabi-v7a", "arm64-v8a", "x86_64")


def main(argv):
    description = """Fetch and unpack architecture-specific Maven zips, verify cross-architecture
compatibility, and ready inputs to an Android multi-architecture fat AAR build."""

    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("architectures", metavar="arch", nargs="+", choices=_ALL_ARCHS)
    parser.add_argument(
        "--no-process", action="store_true", help="Do not process Maven AARs."
    )
    parser.add_argument(
        "--no-compatibility-check",
        action="store_true",
        help="Do not fail if Maven AARs are not compatible.",
    )
    parser.add_argument("--distdir", required=True)

    for arch in _ALL_ARCHS:
        command_line_flag = arch.replace("_", "-")
        parser.add_argument(f"--{command_line_flag}", dest=arch)

    args = parser.parse_args(argv)
    args_dict = vars(args)

    zip_paths = {arch: args_dict.get(arch) for arch in args.architectures}

    if not zip_paths:
        raise ValueError("You must provide at least one Maven zip!")

    return fat_aar(
        args.distdir,
        zip_paths,
        no_process=args.no_process,
        no_compatibility_check=args.no_compatibility_check,
    )


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
