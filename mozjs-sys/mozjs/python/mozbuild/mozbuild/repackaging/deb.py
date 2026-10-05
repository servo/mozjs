# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import shutil
import subprocess
import tarfile
import tempfile
import zipfile
from email.utils import format_datetime

import mozfile
import mozpack.path as mozpath
from mozilla_version.gecko import GeckoVersion

from mozbuild.repackaging.utils import (
    application_ini_data_from_tar,
    copy_plain_config,
    get_build_variables,
    inject_desktop_entry_file,
    inject_distribution_folder,
    inject_prefs_file,
    mv_manpage_files,
    render_templates,
)


class NoDebPackageFound(Exception):
    """Raised when no .deb is found after calling dpkg-buildpackage"""

    def __init__(self, deb_file_path) -> None:
        super().__init__(
            f"No {deb_file_path} package found after calling dpkg-buildpackage"
        )


class HgServerError(Exception):
    """Raised when Hg responds with an error code that is not 404 (i.e. when there is an outage)"""

    def __init__(self, msg) -> None:
        super().__init__(msg)


# Maps our CI/release pipeline's architecture names (e.g., "x86_64")
# into architectures ("amd64") compatible with Debian's dpkg-buildpackage tool.
# This is the target architecture we are building the .deb package for.
_DEB_ARCH = {
    "all": "all",
    "x86": "i386",
    "x86_64": "amd64",
    "aarch64": "arm64",
}

# Defines the sysroot (build host's) architecture for each target architecture in the pipeline.
# It defines the architecture dpkg-buildpackage runs on.
_DEB_SYSROOT_ARCH = {
    "all": "amd64",
    "x86": "i386",
    "x86_64": "amd64",
    "aarch64": "amd64",
}

# Assigns the Debian distribution version for the sysroot based on the target architecture.
# It defines the Debian distribution dpkg-buildpackage runs on.
_DEB_SYSROOT_DIST = {
    "all": "jessie",
    "x86": "jessie",
    "x86_64": "jessie",
    "aarch64": "buster",
}


def repackage_deb(
    log,
    infile,
    output,
    template_dir,
    arch,
    version,
    build_number,
    product,
    release_type,
    fluent_localization,
    fluent_resource_loader,
):
    if not tarfile.is_tarfile(infile):
        raise Exception("Input file %s is not a valid tarfile." % infile)

    tmpdir = _create_temporary_directory(arch)
    source_dir = os.path.join(tmpdir, "source")
    try:
        mozfile.extract_tarball(infile, source_dir)
        extracted = os.listdir(source_dir)
        if len(extracted) != 1:
            raise Exception(
                f"Input file {infile} didn't extract to a single directory."
            )
        app_name = extracted[0]
        application_ini_data = application_ini_data_from_tar(infile)
        pkg_version = _get_deb_pkg_version(
            version, application_ini_data["build_id"], build_number
        )
        build_variables = get_build_variables(
            application_ini_data,
            _DEB_ARCH[arch],
            pkg_version,
            product=product,
        )
        build_variables["DEPENDS"] = "${shlibs:Depends},"
        build_variables["CHANGELOG_DATE"] = format_datetime(
            build_variables["TIMESTAMP"]
        )

        deb_dir = mozpath.join(source_dir, "debian")

        copy_plain_config(template_dir, deb_dir)
        render_templates(
            template_dir,
            deb_dir,
            build_variables,
            exclude_file_names=["package-prefs.js"],
        )

        with open(mozpath.join(source_dir, app_name, "is-packaged-app"), "w") as f:
            f.write("This is a packaged app.\n")

        inject_distribution_folder(source_dir, "debian", app_name)
        inject_desktop_entry_file(
            log,
            deb_dir,
            build_variables,
            product,
            release_type,
            fluent_localization,
            fluent_resource_loader,
        )
        mv_manpage_files(deb_dir, build_variables)
        inject_prefs_file(source_dir, app_name, template_dir)
        _generate_deb_archive(
            source_dir,
            target_dir=tmpdir,
            output_file_path=output,
            build_variables=build_variables,
            arch=arch,
        )

    finally:
        shutil.rmtree(tmpdir)


def repackage_deb_l10n(
    input_xpi_file,
    input_tar_file,
    output,
    template_dir,
    version,
    build_number,
    product,
    extensions_dir,
):
    arch = "all"

    tmpdir = _create_temporary_directory(arch)
    source_dir = os.path.join(tmpdir, "source")
    try:
        langpack_metadata = _extract_langpack_metadata(input_xpi_file)
        langpack_dir = mozpath.join(source_dir, extensions_dir)
        application_ini_data = application_ini_data_from_tar(input_tar_file)
        pkg_version = _get_deb_pkg_version(
            version, application_ini_data["build_id"], build_number
        )
        langpack_id = langpack_metadata["langpack_id"]
        build_variables = get_build_variables(
            application_ini_data,
            _DEB_ARCH[arch],
            pkg_version,
            # Debian package names are only lowercase
            package_name_suffix=f"-l10n-{langpack_id.lower()}",
            description_suffix=f" - {langpack_metadata['description']}",
            product=product,
        )

        if product == "devedition":
            depends_package = "firefox-devedition"
        else:
            depends_package = application_ini_data["remoting_name"]

        depends_version = build_variables["PKG_VERSION"]

        # Thunderbird prepends epoch 1 to the version number to prevent
        # accidental downgrading to Thunderbird .debs in non-Mozilla APT
        # repositories.
        #
        # See bug 2005200
        if depends_package == "thunderbird":
            depends_version = f"1:{depends_version}"

        depends = f"{depends_package} (= {depends_version})"

        build_variables["DEPENDS"] = depends
        build_variables["CHANGELOG_DATE"] = format_datetime(
            build_variables["TIMESTAMP"]
        )

        deb_dir = mozpath.join(source_dir, "debian")

        copy_plain_config(template_dir, deb_dir)
        render_templates(template_dir, deb_dir, build_variables)

        os.makedirs(langpack_dir, exist_ok=True)
        shutil.copy(
            input_xpi_file,
            mozpath.join(
                langpack_dir,
                f"{langpack_metadata['browser_specific_settings']['gecko']['id']}.xpi",
            ),
        )
        _generate_deb_archive(
            source_dir,
            target_dir=tmpdir,
            output_file_path=output,
            build_variables=build_variables,
            arch=arch,
        )
    finally:
        shutil.rmtree(tmpdir)


def _get_deb_pkg_version(version, build_id, build_number):
    gecko_version = GeckoVersion.parse(version)
    deb_pkg_version = (
        f"{gecko_version}~{build_id}"
        if gecko_version.is_nightly
        else f"{gecko_version}~build{build_number}"
    )
    return deb_pkg_version


def _generate_deb_archive(
    source_dir, target_dir, output_file_path, build_variables, arch
):
    command = _get_command(arch)
    subprocess.check_call(command, cwd=source_dir)
    deb_arch = _DEB_ARCH[arch]
    deb_file_name = (
        f"{build_variables['PKG_NAME']}_{build_variables['PKG_VERSION']}_{deb_arch}.deb"
    )
    deb_file_path = mozpath.join(target_dir, deb_file_name)

    if not os.path.exists(deb_file_path):
        raise NoDebPackageFound(deb_file_path)

    subprocess.check_call(["dpkg-deb", "--info", deb_file_path])
    shutil.move(deb_file_path, output_file_path)


def _get_command(arch):
    deb_arch = _DEB_ARCH[arch]
    command = [
        "dpkg-buildpackage",
        # TODO: Use long options once we stop supporting Debian Jessie. They're more
        # explicit.
        #
        # Long options were added in dpkg 1.18.8 which is part of Debian Stretch.
        #
        # https://git.dpkg.org/cgit/dpkg/dpkg.git/commit/?h=1.18.x&id=293bd243a19149165fc4fd8830b16a51d471a5e9
        # https://packages.debian.org/stretch/dpkg-dev
        "-us",  # --unsigned-source
        "-uc",  # --unsigned-changes
        "-b",  # --build=binary
    ]

    if deb_arch != "all":
        command.append(f"--host-arch={deb_arch}")

    if _is_chroot_available(arch):
        flattened_command = " ".join(command)
        command = [
            "chroot",
            _get_chroot_path(arch),
            "bash",
            "-c",
            f"cd /tmp/*/source; {flattened_command}",
        ]

    return command


def _create_temporary_directory(arch):
    if _is_chroot_available(arch):
        return tempfile.mkdtemp(dir=f"{_get_chroot_path(arch)}/tmp")
    else:
        return tempfile.mkdtemp()


def _is_chroot_available(arch):
    return os.path.isdir(_get_chroot_path(arch))


def _get_chroot_path(arch):
    # At the moment the Firefox build baseline for i386 and amd64 is jessie and the baseline for arm64 is buster.
    # These baselines are defined in taskcluster/scripts/misc/build-sysroot.sh
    # The debian-repackage image defined in taskcluster/docker/debian-repackage/Dockerfile
    # bootstraps /srv/jessie-i386, /srv/jessie-amd64, and /srv/buster-amd64 roots.
    # We use these roots to run the repackage step and generate shared
    # library dependencies that match the Firefox build baseline.
    deb_sysroot_dist = _DEB_SYSROOT_DIST[arch]
    deb_sysroot_arch = _DEB_SYSROOT_ARCH[arch]
    return f"/srv/{deb_sysroot_dist}-{deb_sysroot_arch}"


_MANIFEST_FILE_NAME = "manifest.json"


def _extract_langpack_metadata(input_xpi_file):
    with tempfile.TemporaryDirectory() as d:
        with zipfile.ZipFile(input_xpi_file) as zip:
            zip.extract(_MANIFEST_FILE_NAME, path=d)

        with open(mozpath.join(d, _MANIFEST_FILE_NAME)) as f:
            return json.load(f)
