# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, # You can obtain one at http://mozilla.org/MPL/2.0/.

import logging
import sys

from mach.decorators import Command, CommandArgument, SubCommand

from mozbuild.vendor.moz_yaml import MozYamlVerifyError, load_moz_yaml


# Fun quirk of ./mach - you can specify a default argument as well as subcommands.
# If the default argument matches a subcommand, the subcommand gets called. If it
# doesn't, we wind up in the default command.
@Command(
    "vendor",
    category="misc",
    description="Vendor third-party dependencies into the source repository.",
)
@CommandArgument(
    "--check-for-update",
    action="store_true",
    help="For scripted use, prints the new commit to update to, or nothing if up to date.",
    default=False,
)
@CommandArgument(
    "--add-to-exports",
    action="store_true",
    help="Will attempt to add new header files into any relevant EXPORTS block.",
    default=False,
)
@CommandArgument(
    "--ignore-modified",
    action="store_true",
    help="Ignore modified files in current checkout.",
    default=False,
)
@CommandArgument("-r", "--revision", help="Repository tag or commit to update to.")
@CommandArgument(
    "-f",
    "--force",
    action="store_true",
    help="Force a re-vendor even if we're up to date",
)
@CommandArgument(
    "--verify", "-v", action="store_true", help="(Only) verify the manifest."
)
@CommandArgument(
    "--patch-mode",
    help="Select how vendored patches will be imported. 'none' skips patch import, and"
    "'only' imports patches and skips library vendoring.",
    default="",
)
@CommandArgument(
    "--new-files-only",
    action="store_true",
    help="Vendor files for the current revision that don't exist in the vendor directory.",
    default=False,
)
@CommandArgument("library", nargs=1, help="The moz.yaml file of the library to vendor.")
def vendor(
    command_context,
    library,
    revision,
    ignore_modified=False,
    check_for_update=False,
    add_to_exports=False,
    force=False,
    verify=False,
    patch_mode=None,
    new_files_only=False,
):
    """
    Vendor third-party dependencies into the source repository.

    Vendoring rust and python can be done with ./mach vendor [rust/python].
    Vendoring other libraries can be done with ./mach vendor [arguments] path/to/file.yaml
    """
    library = library[0]
    assert library not in ["rust", "python"]

    command_context.populate_logger()
    command_context.log_manager.enable_unstructured()
    if check_for_update:
        logging.disable(level=logging.CRITICAL)

    try:
        manifest = load_moz_yaml(library)
        if verify:
            print("%s: OK" % library)
            sys.exit(0)
    except MozYamlVerifyError as e:
        print(e)
        sys.exit(1)

    if "vendoring" not in manifest:
        raise Exception(
            "Cannot perform update actions if we don't have a 'vendoring' section in the moz.yaml"
        )

    if new_files_only:
        if manifest["vendoring"].get("flavor") != "individual-files":
            print("--new-files-only can only be used with individual-files flavor")
            sys.exit(1)

        # New files only implies current revision, ignore modified and patch mode of none.
        revision = manifest["origin"]["revision"]
        ignore_modified = True
        patch_mode = "none"

    patch_modes = "none", "only", "check"
    if patch_mode and patch_mode not in patch_modes:
        print(
            "Unknown patch mode given '%s'. Please use one of: 'none' or 'only'."
            % patch_mode
        )
        sys.exit(1)

    patches = manifest["vendoring"].get("patches")
    if patches and not patch_mode and not check_for_update:
        print(
            "Patch mode was not given when required. Please use one of: 'none' or 'only'"
        )
        sys.exit(1)
    if patch_mode == "only" and not patches:
        print(
            "Patch import was specified for %s but there are no vendored patches defined."
            % library
        )
        sys.exit(1)

    if not ignore_modified and not check_for_update:
        check_modified_files(command_context)
    elif ignore_modified and not check_for_update and not new_files_only:
        print(
            "Because you passed --ignore-modified we will not be "
            + "able to detect spurious upstream updates."
        )

    if not revision:
        revision = "HEAD"

    from mozbuild.vendor.vendor_manifest import VendorManifest

    vendor_command = command_context._spawn(VendorManifest)
    vendor_command.vendor(
        command_context,
        library,
        manifest,
        revision,
        ignore_modified,
        check_for_update,
        force,
        add_to_exports,
        patch_mode,
        new_files_only,
    )

    sys.exit(0)


def check_modified_files(command_context):
    """
    Ensure that there aren't any uncommitted changes to files
    in the working copy, since we're going to change some state
    on the user.
    """
    modified = command_context.repository.get_changed_files("M")
    if modified:
        command_context.log(
            logging.ERROR,
            "modified_files",
            {},
            """You have uncommitted changes to the following files:

{files}

Please commit or stash these changes before vendoring, or re-run with `--ignore-modified`.
""".format(
                files="\n".join(sorted(modified))
            ),
        )
        sys.exit(1)


# =====================================================================


@SubCommand(
    "vendor",
    "rust",
    description="Vendor rust crates from crates.io into third_party/rust",
)
@CommandArgument(
    "--ignore-modified",
    action="store_true",
    help="Ignore modified files in current checkout",
    default=False,
)
@CommandArgument(
    "--force",
    action="store_true",
    help=("Ignore any kind of error that happens during vendoring"),
    default=False,
)
@CommandArgument(
    "--issues-json",
    help="Path to a code-review issues.json file to write out",
)
def vendor_rust(command_context, **kwargs):
    from mozbuild.vendor.vendor_rust import VendorRust

    vendor_command = command_context._spawn(VendorRust)
    issues_json = kwargs.pop("issues_json", None)
    ok = vendor_command.vendor(**kwargs)
    if issues_json:
        with open(issues_json, "w") as fh:
            fh.write(vendor_command.serialize_issues_json())
    if ok:
        sys.exit(0)
    else:
        print("Errors occurred; new rust crates were not vendored.")
        sys.exit(1)


# =====================================================================


@SubCommand(
    "vendor",
    "python",
    description="Vendor Python packages from pypi.org into third_party/python. "
    "Some extra files like docs and tests will automatically be excluded."
    "Downloads the packages listed in third_party/python/pyproject.toml, along "
    "with their transitive dependencies, and adds them to version control.",
    virtualenv_name="vendor",
)
@CommandArgument(
    "--keep-extra-files",
    action="store_true",
    default=False,
    help="Keep all files, including tests and documentation.",
)
@CommandArgument(
    "--add",
    action="append",
    default=[],
    help="Specify one or more dependencies to vendor.\nUse the format: '<dependency>==<version>' (e.g. '--add pip==24.1.1')",
)
@CommandArgument(
    "--remove",
    action="append",
    default=[],
    help="Remove one or more vendored dependencies.\nUse the format: '<dependency>' (e.g. '--remove pip')",
)
@CommandArgument(
    "-P",
    "--upgrade-package",
    action="append",
    default=[],
    help="Specify one or more dependencies to upgrade.\nFormat: '<dependency>' (e.g. '--upgrade-package pip)\n\nNote: This will not do anything is the package is pinned with '==' in the pyproject.toml",
)
@CommandArgument(
    "-U",
    "--upgrade",
    action="store_true",
    default=False,
    help="Upgrade all unpinned dependencies to their latest compatible versions before vendoring.",
)
@CommandArgument(
    "-f",
    "--force",
    action="store_true",
    help="Force a re-vendor even if we're up to date.",
)
def vendor_python(
    command_context, keep_extra_files, add, remove, upgrade, upgrade_package, force
):
    from mozbuild.vendor.vendor_python import VendorPython

    if upgrade or upgrade_package and add or remove:
        command_context.log(
            logging.ERROR,
            "vendor-python-upgrade-and-add-or-remove",
            {},
            "Upgrading packages and adding or removing others at the same time is forbidden. Please complete them as separate commits.",
        )
        return 1

    vendor_command = command_context._spawn(VendorPython)
    vendor_command.vendor(
        keep_extra_files, add, remove, upgrade, upgrade_package, force
    )
