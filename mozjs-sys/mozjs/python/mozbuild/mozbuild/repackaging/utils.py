# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import pathlib
import shutil
import tarfile
import zipfile
from datetime import datetime
from pathlib import Path
from string import Template
from tempfile import TemporaryDirectory

import mozpack.path as mozpath
from jinja2 import Environment, FileSystemLoader

from mozbuild.repackaging.application_ini import get_application_ini_values
from mozbuild.repackaging.desktop_file import generate_browser_desktop_entry_file_text


def application_ini_data_from_directory(application_directory):
    values = get_application_ini_values(
        application_directory,
        dict(section="App", value="Name"),
        dict(section="App", value="CodeName", fallback="Name"),
        dict(section="App", value="Vendor"),
        dict(section="App", value="RemotingName"),
        dict(section="App", value="BuildID"),
    )

    data = {
        "name": next(values),
        "display_name": next(values),
        "vendor": next(values),
        "remoting_name": next(values),
        "build_id": next(values),
    }

    return data


def application_ini_data_from_tar(input_tar_file):
    with TemporaryDirectory() as d:
        with tarfile.open(input_tar_file) as tar:
            application_ini_files = [
                tar_info
                for tar_info in tar.getmembers()
                if tar_info.name.endswith("/application.ini")
            ]
            if len(application_ini_files) == 0:
                raise ValueError(
                    f"Cannot find any application.ini file in archive {input_tar_file}"
                )
            if len(application_ini_files) > 1:
                raise ValueError(
                    f"Too many application.ini files found in archive {input_tar_file}. "
                    f"Found: {application_ini_files}"
                )

            tar.extract(application_ini_files[0], path=d)

        return application_ini_data_from_directory(d)


def copy_plain_config(input_template_dir, source_dir):
    filenames = [
        mozpath.basename(filename)
        for filename in os.listdir(input_template_dir)
        if Path(filename).suffix not in [".j2", ".js", ".in"]
    ]
    os.makedirs(source_dir, exist_ok=True)

    for filename in filenames:
        shutil.copy(
            mozpath.join(input_template_dir, filename),
            mozpath.join(source_dir, filename),
        )


def get_build_variables(
    application_ini_data,
    arch,
    version,
    package_name_suffix="",
    description_suffix="",
    product="",
    build_number="1",
):
    """
    Massage the application.ini info and other metadata into a dict suitable for passing into packaging templates
    """
    if product == "devedition":
        pkg_install_path = "usr/lib/firefox-devedition"
        pkg_name = f"firefox-devedition{package_name_suffix}"
    else:
        pkg_install_path = f"usr/lib/{application_ini_data['remoting_name'].lower()}"
        pkg_name = (
            f"{application_ini_data['remoting_name'].lower()}{package_name_suffix}"
        )
    timestamp = datetime.strptime(application_ini_data["build_id"], "%Y%m%d%H%M%S")

    return {
        "DESCRIPTION": f"{application_ini_data['vendor']} {application_ini_data['display_name']}{description_suffix}",
        "PKG_INSTALL_PATH": pkg_install_path,
        "PKG_NAME": pkg_name,
        "PKG_BUILD_NUMBER": build_number,
        "PKG_VERSION": version,
        "PRODUCT_NAME": application_ini_data["name"],
        "DISPLAY_NAME": application_ini_data["display_name"],
        "TIMESTAMP": timestamp,
        "MANPAGE_DATE": timestamp.strftime("%B %d, %Y"),
        "ARCH_NAME": arch,
        "Icon": pkg_name,
        "REMOTING_NAME": application_ini_data["remoting_name"],
    }


def get_manifest_from_langpack(xpi_file, output_path):
    try:
        zip_file = zipfile.ZipFile(xpi_file)
        manifest_file = zip_file.extract("manifest.json", output_path)
        return json.loads(pathlib.Path(manifest_file).read_text())
    except json.JSONDecodeError:
        return None


def inject_desktop_entry_file(
    log,
    source_dir,
    build_variables,
    product,
    release_type,
    fluent_localization,
    fluent_resource_loader,
):
    desktop_entry_template_path = mozpath.join(
        source_dir, f"{build_variables['PRODUCT_NAME'].lower()}.desktop"
    )
    desktop_entry_file_filename = f"{build_variables['PKG_NAME']}.desktop"

    # Check to see if a .desktop file is already supplied in the templates directory
    # If not, continue to generate default Firefox .desktop file
    if os.path.exists(desktop_entry_template_path):
        shutil.move(
            desktop_entry_template_path,
            mozpath.join(source_dir, desktop_entry_file_filename),
        )

        return

    desktop_entry_file_text = generate_browser_desktop_entry_file_text(
        log,
        build_variables,
        product,
        release_type,
        fluent_localization,
        fluent_resource_loader,
    )
    os.makedirs(source_dir, exist_ok=True)
    with open(mozpath.join(source_dir, desktop_entry_file_filename), "w") as f:
        f.write(desktop_entry_file_text)


def inject_distribution_folder(source_dir, source_type, app_name):
    distribution_ini_path = mozpath.join(source_dir, source_type, "distribution.ini")

    distribution_dir = mozpath.join(source_dir, app_name.lower(), "distribution")
    os.makedirs(distribution_dir, exist_ok=True)

    destination_path = mozpath.join(distribution_dir, "distribution.ini")
    if os.path.exists(destination_path):
        os.remove(destination_path)

    shutil.move(distribution_ini_path, destination_path)


def inject_prefs_file(source_dir, app_name, template_dir):
    src = mozpath.join(template_dir, "package-prefs.js")
    dst = mozpath.join(source_dir, app_name.lower(), "defaults/pref")
    shutil.copy(src, dst)


def mv_manpage_files(source_dir, build_variables):
    src = mozpath.join(source_dir, "manpage.1")
    if os.path.exists(src):
        dst = mozpath.join(source_dir, f"{build_variables['PKG_NAME']}.1")
        shutil.move(src, dst)
    src = mozpath.join(source_dir, "manpages")
    if os.path.exists(src):
        dst = mozpath.join(source_dir, f"{build_variables['PKG_NAME']}.manpages")
        shutil.move(src, dst)


def prepare_langpack_files(output_dir, xpi_directory):
    metadata = {}
    for xpi_file in Path(xpi_directory).rglob("*.langpack.xpi"):
        manifest = get_manifest_from_langpack(xpi_file, output_dir)
        if manifest is not None:
            language = manifest["langpack_id"]
            extension_id = manifest["browser_specific_settings"]["gecko"]["id"]
            metadata[language] = {
                "description": manifest["description"],
                "extension_id": extension_id,
            }
            output_file = mozpath.join(output_dir, f"{extension_id}.xpi")
            shutil.copy(xpi_file, output_file)

    return metadata


def render_templates(
    input_template_dir,
    source_dir,
    build_variables,
    exclude_file_names=None,
):
    exclude_file_names = [] if exclude_file_names is None else exclude_file_names

    filenames = [
        mozpath.basename(filename)
        for filename in os.listdir(input_template_dir)
        if Path(filename).suffix in [".j2", ".in"]
        and filename not in exclude_file_names
    ]
    os.makedirs(source_dir, exist_ok=True)

    for file_name in filenames:
        if file_name.endswith(".in"):
            with open(mozpath.join(input_template_dir, file_name)) as f:
                template = Template(f.read())
            with open(mozpath.join(source_dir, Path(file_name).stem), "w") as f:
                f.write(template.substitute(build_variables))
        elif file_name.endswith(".j2"):
            environment = Environment(loader=FileSystemLoader(input_template_dir))
            template = environment.get_template(file_name)
            with open(mozpath.join(source_dir, Path(file_name).stem), "w") as f:
                f.write(template.render(build_variables))
