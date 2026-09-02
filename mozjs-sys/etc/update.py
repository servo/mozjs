#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import re

TARGET = "mozjs-sys/mozjs"


def extract_tarball(tarball, commit):
    print("Extracting tarball.")

    if not os.path.exists(tarball):
        raise Exception("Tarball not found at %s" % tarball)

    with tempfile.TemporaryDirectory() as directory:
        subprocess.check_call(["tar", "-xf", tarball, "-C", directory])

        contents = os.listdir(directory)
        if len(contents) != 1:
            raise Exception(
                "Found more than one directory in the tarball: %s" % ", ".join(contents)
            )
        subdirectory = contents[0]

        def get_cbindgen_version() -> str | None:
            # get cbindgen version from: build/moz.configure/bindgen.configure#10
            # cbindgen_min_version = Version("0.29.4")
            with open(
                os.path.join(
                    directory,
                    subdirectory,
                    "build",
                    "moz.configure",
                    "bindgen.configure",
                )
            ) as f:
                for line in f:
                    line = line.strip()
                    if "cbindgen_min_version =" in line:
                        return line.removeprefix(
                            'cbindgen_min_version = Version("'
                        ).removesuffix('")')
            return None

        cbindgen_version = get_cbindgen_version()
        assert cbindgen_version is not None, "Could not find cbindgen version!"

        def get_milestone_version() -> str | None:
            milestone_path = Path(directory, subdirectory, "config", "milestone.txt")
            if milestone_path.exists():
                with open(milestone_path) as f:
                    for line in f:
                        line = line.strip()
                        if line and not line.startswith("#"):
                            if line.count(".") != 2:
                                line += ".0"
                            return line
            return None

        milestone_version = get_milestone_version()
        assert milestone_version is not None, "Could not find milestone version!"

        PATCHED_CRATES = {
            "icu_collator": "mozjs_icu_collator",
            "icu_collator_data": "mozjs_icu_collator_data",
            "icu_normalizer": "mozjs_icu_normalizer",
            "icu_normalizer_data": "mozjs_icu_normalizer_data",
            "icu_collections": "mozjs_icu_collections",
            "utf8_iter": "mozjs_utf8_iter",
            "utf16_iter": "mozjs_utf16_iter",
        }

        def extract_rust_crate(subfolder: str, crate_name: str, cbindgen=True):
            extracted_crate_path = os.path.join(
                "mozjs-extracted-crates", crate_name.replace("-", "_")
            )
            shutil.copytree(
                os.path.join(directory, subdirectory, subfolder, crate_name),
                extracted_crate_path,
                ignore=shutil.ignore_patterns(
                    "Cargo.toml.orig", ".cargo-checksum.json", ".cargo_vcs_info.json"
                ),
            )
            if os.path.exists(os.path.join(extracted_crate_path, "moz.build")):
                os.remove(os.path.join(extracted_crate_path, "moz.build"))

            cargo_toml = Path(os.path.join(extracted_crate_path, "Cargo.toml"))
            cargo_toml_contents = cargo_toml.read_text()
            cargo_toml_contents = re.sub(
                r'^name = ".*"',
                f'name = "mozjs_{crate_name.replace("-", "_")}"',
                cargo_toml_contents,
                flags=re.MULTILINE,
                count=1,
            )
            cargo_toml_contents = re.sub(
                r'^version = ".*"',
                f'version = "{milestone_version}"',
                cargo_toml_contents,
                flags=re.MULTILINE,
                count=1,
            )
            if "repository = " in cargo_toml_contents:
                cargo_toml_contents = re.sub(
                    r'^repository = ".*"',
                    'repository = "https://github.com/servo/mozjs"',
                    cargo_toml_contents,
                    flags=re.MULTILINE,
                    count=1,
                )
            else:
                cargo_toml_contents = cargo_toml_contents.replace(
                    "[package]",
                    '[package]\nrepository = "https://github.com/servo/mozjs"',
                )
            cargo_toml_contents = cargo_toml_contents.replace(
                'readme = "README.md"\n', ""
            )

            for original_crate, patched_crate in PATCHED_CRATES.items():
                cargo_toml_contents = re.sub(
                    rf'^\[dependencies.{original_crate}\]\nversion = ".*"',
                    f'[dependencies.{original_crate}]\nversion = "={milestone_version}"\npackage = "{patched_crate}"\npath = "../{original_crate}"',
                    cargo_toml_contents,
                    flags=re.MULTILINE,
                )
                cargo_toml_contents = re.sub(
                    rf'^{original_crate} = ".*"',
                    f'{original_crate} = {{ package = "{patched_crate}", version = "={milestone_version}", path = "../{original_crate}" }}',
                    cargo_toml_contents,
                    flags=re.MULTILINE,
                )
                cargo_toml_contents = re.sub(
                    rf'^{original_crate} = {{ version = ".*?"',
                    f'{original_crate} = {{ package = "{patched_crate}", version = "={milestone_version}", path = "../{original_crate}"',
                    cargo_toml_contents,
                    flags=re.MULTILINE,
                )
                cargo_toml_contents = re.sub(
                    rf"^{original_crate} = {{ path",
                    f'{original_crate} = {{ package = "{patched_crate}", version = "={milestone_version}", path',
                    cargo_toml_contents,
                    flags=re.MULTILINE,
                )

            if cbindgen:
                if "links = " not in cargo_toml_contents:
                    cargo_toml_contents = cargo_toml_contents.replace(
                        "[package]", f'[package]\nlinks = "{crate_name}"'
                    )
                cargo_toml_contents += (
                    f'\n[build-dependencies.cbindgen]\nversion = "{cbindgen_version}"\n'
                )

            if "description" not in cargo_toml_contents:
                cargo_toml_contents = cargo_toml_contents.replace(
                    "[package]",
                    f'[package]\ndescription = "copy of {crate_name} for mozjs-sys"',
                )

            if "license = " not in cargo_toml_contents:
                # it's mozilla crate
                cargo_toml_contents = cargo_toml_contents.replace(
                    "[package]", '[package]\nlicense = "MPL-2.0"'
                )

            if crate_name == "icu_collator":
                # somebody used bad regex or AI in mozilla
                cargo_toml_contents = cargo_toml_contents.replace(
                    '[dev-dependencies.atoi]\nversion = "2.1.1"',
                    '[dev-dependencies.atoi]\nversion = "2.0.0"',
                )

            if crate_name == "properties_glue":
                cargo_toml_contents = cargo_toml_contents.replace(
                    "icu_collections = { ",
                    'icu_collections = { features = ["alloc"], ',
                )

            # because we roll our own crates we need to do some fixups
            if crate_name == "icu_collections":
                cargo_toml_contents += """
[dependencies.icu_casemap]
version = "2.1.1"
default-features = false
"""
                mod = Path(
                    os.path.join(
                        extracted_crate_path, "src", "codepointinvlist", "builder.rs"
                    )
                )
                mod_contents = mod.read_text()
                # append implementation of ClosureSink for our CodePointInversionListBuilder:
                # https://searchfox.org/firefox-main/rev/d83a08d81de20f3b72db70f531fad19090e1db4b/third_party/rust/icu_casemap/src/set.rs
                mod_contents += """
impl icu_casemap::ClosureSink for CodePointInversionListBuilder {
    fn add_char(&mut self, c: char) {
        self.add_char(c)
    }

    // The current version of CodePointInversionList doesn't include strings.
    // Trying to add a string is a no-op that will be optimized away.
    #[inline]
    fn add_string(&mut self, _string: &str) {}
}
"""
                mod.write_text(mod_contents)

            cargo_toml.write_text(cargo_toml_contents)

            if not cbindgen:
                return

            assert not os.path.exists(os.path.join(extracted_crate_path, "build.rs"))
            with open(os.path.join(extracted_crate_path, "build.rs"), "w") as f:
                f.write(
                    f"""fn main() {{
    let out_dir = std::path::PathBuf::from(std::env::var_os("OUT_DIR").unwrap());
    let include_dir = out_dir.join("include");
    std::fs::create_dir_all(&include_dir).unwrap();
    println!("cargo:include={{}}", include_dir.to_str().unwrap());
    cbindgen::generate(".")
        .expect("Unable to generate bindings")
        .write_to_file(include_dir.join("{crate_name.replace("-", "_") + ("_generated" if crate_name == "unicode-bidi-ffi" else "")}.h"));
}}
"""
                )

        # there are many crates defined in js/src/rust/shared/Cargo.toml
        # and we need to extract them to mozjs-extracted-crates
        # so that we can build them with our cargo invocation,
        # which will then link them into source
        if os.path.exists("mozjs-extracted-crates"):
            shutil.rmtree("mozjs-extracted-crates")
        extract_rust_crate("intl/bidi/rust", "unicode-bidi-ffi")
        extract_rust_crate("js/src/builtin", "normalizer_glue")
        extract_rust_crate("js/src/irregexp", "properties_glue")
        extract_rust_crate("js/src/builtin/intl", "collator_glue")
        extract_rust_crate("js/src/builtin/intl", "locale_glue")
        extract_rust_crate("third_party/rust", "icu_collator", cbindgen=False)
        extract_rust_crate("third_party/rust", "icu_collator_data", cbindgen=False)
        extract_rust_crate("third_party/rust", "icu_normalizer", cbindgen=False)
        extract_rust_crate("third_party/rust", "icu_normalizer_data", cbindgen=False)
        extract_rust_crate("third_party/rust", "icu_collections", cbindgen=False)
        extract_rust_crate("third_party/rust", "utf8_iter", cbindgen=False)
        extract_rust_crate("third_party/rust", "utf16_iter", cbindgen=False)

        subprocess.check_call(
            [
                "rsync",
                "--delete-excluded",
                "--filter=merge mozjs-sys/etc/filters.txt",
                "--prune-empty-dirs",
                "--quiet",
                "--recursive",
                os.path.join(directory, subdirectory, ""),
                os.path.join(TARGET, ""),
            ]
        )

    if commit:
        subprocess.check_call(
            ["git", "add", "--all", TARGET], stdout=subprocess.DEVNULL
        )
        subprocess.check_call(
            ["git", "commit", "-s", "-m", "Update SpiderMonkey"],
            stdout=subprocess.DEVNULL,
        )


def apply_patches():
    print("Applying patches.")
    patch_dir = os.path.abspath(os.path.join("mozjs-sys", "etc", "patches"))
    patches = sorted(
        os.path.join(patch_dir, p)
        for p in os.listdir(patch_dir)
        if p.endswith(".patch")
    )
    for p in patches:
        print("  Applying patch: %s." % p)
        subprocess.check_call(
            ["git", "apply", "--reject", "--directory=" + TARGET, p],
            stdout=subprocess.DEVNULL,
        )


def main(args):
    extract = None
    patch = True
    commit = True
    for arg in args:
        if arg == "--no-patch":
            patch = False
        elif arg == "--no-commit":
            commit = False
        else:
            extract = arg
    if extract:
        extract_tarball(os.path.abspath(extract), commit)
    if patch:
        apply_patches()


if __name__ == "__main__":
    import sys

    main(sys.argv[1:])
