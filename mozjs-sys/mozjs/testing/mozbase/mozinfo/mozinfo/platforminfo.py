# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
from pathlib import Path

# ruff linter deprecates Dict required for Python 3.8 compatibility
from typing import Any, Dict, Optional  # noqa UP035

import yaml

DictAny = Dict[str, Any]  # noqa UP006
DictStr = Dict[str, str]  # noqa UP006
OptTestSettings = Optional[DictAny]

# Loaded from the YAML file that is the single source of truth shared with
# tools/lint/mozcheck (Rust, via include_str!). See the YAML file's header
# for the documentation links.
with (Path(__file__).parent / "android_os_to_api_map.yaml").open(
    encoding="utf-8"
) as _f:
    android_os_to_api_map: DictStr = yaml.safe_load(_f)["android_os_to_api_map"]


def android_os_to_api_version(os_version: str):
    api_version = android_os_to_api_map.get(os_version)
    if api_version is None:
        raise Exception(
            f"Unknown Android OS version '{os_version}'. Supported versions are {(android_os_to_api_map.keys())}."
        )
    return api_version


def android_api_to_os_version(api_version: str):
    if not isinstance(api_version, str):
        api_version = str(api_version)
    os_version = None
    for k in android_os_to_api_map.keys():
        if android_os_to_api_map[k] == api_version:
            os_version = k
            break
    if os_version is None:
        raise Exception(
            f"Unknown Android API version '{api_version}'. Supported versions are {android_os_to_api_map.values()}."
        )
    return os_version


class PlatformInfo:
    variant_data = {}

    buildmap = {
        "debug-isolated-process": "isolated_process",
    }

    def __init__(self, test_settings: OptTestSettings = None) -> None:
        if not test_settings:
            return

        self._platform: DictAny = test_settings["platform"]
        self._platform_os: DictStr = self._platform["os"]
        self._build: DictStr = test_settings["build"]
        self._runtime: DictStr = test_settings.get("runtime", {})

        self.build = self._platform_os.get("build")
        self.display = self._platform.get("display")
        self.os = self._clean_os()
        self.os_version = self._clean_os_version()
        self.arch = self._clean_arch()
        self.bits = self._get_bits()
        self.build_type = self._clean_build_type()
        self.opt = self._build["type"] == "opt"
        self.debug = self._build["type"] == "debug"
        self.test_variant = self._clean_test_variant()

    def _clean_os(self) -> str:
        name = self._platform_os["name"]
        if name is None:
            raise Exception("Could not find platform name")

        pretty = name
        if pretty == "windows":
            pretty = "win"
        elif pretty == "macosx":
            pretty = "mac"

        supported_os = ("win", "mac", "linux", "android")
        if pretty not in supported_os:
            raise ValueError(
                f"Unknown os name {pretty}. Supported os are {supported_os}"
            )

        return pretty

    def _clean_os_version(self) -> str:
        version = self._platform_os["version"]
        if version is None:
            raise Exception("Could not find platform version")

        if self.os in ["mac", "linux"]:
            # Hack for macosx 11.20 reported as 11.00
            if self.os == "mac":
                if version == "1100":
                    return "11.20"
                elif version == "1500":
                    return "15.30"
            if len(version) == 5 and version[2] == ".":
                return version  # already has a dot
            return version[0:2] + "." + version[2:4]
        if self.os == "android":
            if version not in android_os_to_api_map and version.endswith(".0"):
                version = version[:-2]
            android_version = android_os_to_api_map.get(version)
            if android_version is None:
                raise Exception(
                    f"Unknown android OS version {version}. Supported versions are {list(android_os_to_api_map.keys())}."
                )

        build = self.build
        if build is not None and self.os == "win":
            if build == "24h2":
                version += ".26100"
            elif build == "25h2":
                version += ".26200"
            else:
                version += "." + build
        return version

    def _clean_arch(self) -> str:
        arch = self._platform["arch"]
        if arch is None:
            raise Exception("Could not find platform architecture")

        if arch == "x86" or arch.find("32") >= 0:
            return "x86"
        elif arch not in ("aarch64", "ppc", "arm7"):
            return "x86_64"
        return arch

    def _get_bits(self) -> str:
        cleaned_arch = self.arch
        if cleaned_arch == "x86":
            return "32"
        return "64"

    def _clean_build_type(self):
        build_type = self._build["type"]
        keys = self._build.keys()
        if len(keys) > 1:
            filtered_types = [x for x in keys if x not in ["type", "shippable"]]
            if len(filtered_types) > 0:
                build_type = filtered_types[0]

        build_type = self.buildmap.get(build_type, build_type)

        # TODO: this is a hack, but these don't apply:
        if build_type in ["devedition", "mingwclang"]:  # only on beta, no mozinfo
            build_type = "opt"
        if self.os == "mac" and build_type == "ccov":  # not scheduled
            build_type = "opt"
        if (
            self.os == "android" and build_type == "lite"
        ):  # no specific way to skip this, treat as normal android
            build_type = "opt"
        return build_type

    def get_variant_data(self):
        import yaml

        if PlatformInfo.variant_data:
            return PlatformInfo.variant_data

        # if running locally via `./mach ...`, assuming running from root of repo
        filename = (
            os.environ.get("GECKO_PATH", ".") + "/taskcluster/test_configs/variants.yml"
        )

        with open(filename) as f:
            PlatformInfo.variant_data = yaml.safe_load(f.read())

        return PlatformInfo.variant_data

    def get_variant_condition(self, test_variant: str) -> str:
        variant_data = self.get_variant_data()
        if test_variant not in variant_data.keys():
            return ""

        mozinfo = variant_data[test_variant].get("mozinfo", "")

        # This is a hack as we have no-fission and fission variants
        # sharing a common mozinfo variable.
        # TODO: what other hacks like this exist?
        if test_variant in ["no-fission", "1proc"]:
            mozinfo = "!" + mozinfo
        return mozinfo

    def _clean_test_variant(self) -> str:
        # TODO: consider adding display here
        runtimes = list(self._runtime.keys())
        variants = [v for v in [self.get_variant_condition(x) for x in runtimes] if v]
        variants.sort()  # keep variants in consistent order
        test_variant = "+".join(variants)
        if not runtimes or not test_variant:
            test_variant = "no_variant"
        return test_variant

    # Used for test data
    def from_dict(self, data: DictAny):
        self.os = data["os"]
        self.os_version = data["os_version"]
        self.arch = data["arch"]
        self.bits = data["bits"]
        self.build = data.get("build")
        self.display = data.get("display")
        self.build_type = data["build_type"]
        self.opt = data["opt"]
        self.debug = data["debug"]
        self.test_variant = data["runtime"]

    def to_dict(self):
        return {
            "arch": self.arch,
            "bits": self.bits,
            "build": self.build,
            "build_type": self.build_type,
            "debug": self.debug,
            "display": self.display,
            "opt": self.opt,
            "os": self.os,
            "os_version": self.os_version,
            "runtime": self.test_variant,
        }
