# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this,
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import re
import subprocess
from functools import cache
from pathlib import Path
from typing import Literal, Optional, Union, overload

from packaging.version import Version

from mozversioncontrol.errors import (
    InvalidRepoPath,
    MissingConfigureInfo,
    MissingVCSInfo,
    MissingVCSTool,
)
from mozversioncontrol.repo.git import GitRepository
from mozversioncontrol.repo.jj import (
    MINIMUM_SUPPORTED_JJ_VERSION,
    JjVersionError,
    JujutsuRepository,
)
from mozversioncontrol.repo.mercurial import HgRepository
from mozversioncontrol.repo.source import SrcRepository

VCS_CLASSES: dict[str, type] = {
    "hg": HgRepository,
    "git": GitRepository,
    "jj": JujutsuRepository,
    "src": SrcRepository,
}


@overload
def get_specific_repository_object(
    data: str, output_format: Literal["git"]
) -> GitRepository: ...


@overload
def get_specific_repository_object(
    data: str, output_format: Literal["hg"]
) -> HgRepository: ...


@overload
def get_specific_repository_object(
    data: str, output_format: Literal["jj"]
) -> JujutsuRepository: ...


@overload
def get_specific_repository_object(
    data: str, output_format: Literal["src"]
) -> SrcRepository: ...


def get_specific_repository_object(path: Optional[Union[str, Path]], vcs: str):
    """Return a repository object for the given VCS and path."""
    resolved_path = Path(path).resolve()

    try:
        vcs_cls = VCS_CLASSES[vcs]
    except KeyError:
        raise ValueError(
            f"Unsupported VCS: '{vcs}'; expected one of {tuple(VCS_CLASSES)}"
        )
    return vcs_cls(resolved_path)


@cache
def _check_jj_version():
    """Check that jj meets the minimum version requirement.

    Cached so that the subprocess call only happens once per mach invocation.
    If it doesn't raise the first time, we assume the version remains valid.
    """
    try:
        result = subprocess.run(
            ["jj", "--version"],
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError:
        raise MissingVCSTool(".jj/ directory exists but jj binary not usable")
    raw_jj_version = result.stdout.strip()
    match = re.search(r"\b(\d+\.\d+\.\d+)\b", raw_jj_version)

    if not match:
        raise ValueError(f"Could not parse jj version from output: {raw_jj_version}")

    current_jj_version = Version(match.group(1))

    if current_jj_version < MINIMUM_SUPPORTED_JJ_VERSION:
        raise JjVersionError(
            f"Detected jj version {current_jj_version}, "
            f"but version {MINIMUM_SUPPORTED_JJ_VERSION} or newer is required.\n"
            f'Full "jj --version" output was: "{raw_jj_version}"'
        )


def get_repository_object(path: Optional[Union[str, Path]]):
    """Get a repository object for the repository at `path`.
    If `path` is not a known VCS repository, raise an exception.
    """
    # If we provide a path to hg that does not match the on-disk casing (e.g.,
    # because `path` was normcased), then the hg fsmonitor extension will call
    # watchman with that path and watchman will spew errors.
    path = Path(path).resolve()
    if (path / ".hg").is_dir():
        return HgRepository(path)
    if (path / ".jj").is_dir():
        try_using_jj = os.getenv("MOZ_AVOID_JJ_VCS") in (None, "0", "")
        if try_using_jj:
            _check_jj_version()
            return JujutsuRepository(path)

    if (path / ".git").exists():
        return GitRepository(path)
    if (path / "config" / "milestone.txt").exists():
        return SrcRepository(path)
    raise InvalidRepoPath(f"Unknown VCS, or not a source checkout: {path}")


def get_repository_from_build_config(config):
    """Obtain a repository from the build configuration.

    Accepts an object that has a ``topsrcdir`` and ``subst`` attribute.
    """
    flavor = config.substs.get("VCS_CHECKOUT_TYPE")

    # If in build mode, only use what configure found. That way we ensure
    # that everything in the build system can be controlled via configure.
    if not flavor:
        raise MissingConfigureInfo(
            "could not find VCS_CHECKOUT_TYPE "
            "in build config; check configure "
            "output and verify it could find a "
            "VCS binary"
        )

    if flavor == "hg":
        return HgRepository(Path(config.topsrcdir), hg=config.substs["HG"])
    elif flavor == "jj":
        return JujutsuRepository(
            Path(config.topsrcdir), jj=config.substs["JJ"], git=config.substs["GIT"]
        )
    elif flavor == "git":
        return GitRepository(Path(config.topsrcdir), git=config.substs["GIT"])
    elif flavor == "src":
        return SrcRepository(Path(config.topsrcdir), src=config.substs["SRC"])
    else:
        raise MissingVCSInfo(f"unknown VCS_CHECKOUT_TYPE value: {flavor}")


def get_repository_from_env():
    """Obtain a repository object by looking at the environment.

    If inside a build environment (denoted by presence of a ``buildconfig``
    module), VCS info is obtained from it, as found via configure. This allows
    us to respect what was passed into configure. Otherwise, we fall back to
    scanning the filesystem.
    """
    try:
        import buildconfig

        return get_repository_from_build_config(buildconfig)
    except (ImportError, MissingVCSTool):
        pass

    paths_to_check = [Path.cwd(), *Path.cwd().parents]

    for path in paths_to_check:
        try:
            return get_repository_object(path)
        except InvalidRepoPath:
            continue

    raise MissingVCSInfo(
        f"Could not find Mercurial / Git / JJ checkout for {Path.cwd()}"
    )
