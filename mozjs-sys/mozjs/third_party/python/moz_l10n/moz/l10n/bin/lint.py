#!/usr/bin/env python

# Copyright Mozilla Foundation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from __future__ import annotations

import logging
import sys
from argparse import ArgumentParser, RawDescriptionHelpFormatter
from collections.abc import Iterable
from enum import Enum
from glob import glob
from os import getcwd
from os.path import abspath, isdir, relpath, splitext
from textwrap import dedent

from moz.l10n.formats import UnsupportedFormat, l10n_extensions
from moz.l10n.paths.config import L10nConfigPaths
from moz.l10n.paths.discover import L10nDiscoverPaths
from moz.l10n.resource import parse_resource

log = logging.getLogger(__name__)

Result = Enum("Result", ("OK", "SKIP", "UNSUPPORTED", "FAIL"))


def cli() -> None:
    parser = ArgumentParser(
        description=dedent(
            """
            Lint/validate localization resources.

            If `paths` is a single directory, it is iterated with L10nConfigPaths if --config is set, or L10nDiscoverPaths otherwise.

            If `paths` is not a single directory, its values are treated as glob expressions, with ** support.

            FIXME: Currently only checks that files can be parsed, and does not check their contents more deeply.
            """
        ),
        formatter_class=RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "-q", "--quiet", action="store_true", help="only log input argument errors"
    )
    parser.add_argument(
        "-v", "--verbose", action="count", default=0, help="increase logging verbosity"
    )
    parser.add_argument(
        "--config", metavar="PATH", type=str, help="path to l10n.toml config file"
    )
    parser.add_argument(
        "-u",
        "--skip-unknown",
        action="store_true",
        help="skip files without a known L10n extension",
    )
    parser.add_argument("paths", nargs="*", type=str, help="directory or files to fix")
    args = parser.parse_args()

    log_level = (
        logging.ERROR
        if args.quiet
        else (
            logging.WARNING
            if args.verbose == 0
            else logging.INFO
            if args.verbose == 1
            else logging.DEBUG
        )
    )
    logging.basicConfig(format="%(message)s", level=log_level)

    res = lint(
        args.paths,
        config_path=args.config,
        skip_unknown=args.skip_unknown,
    )
    sys.exit(res)


def lint(
    file_paths: list[str],
    *,
    config_path: str | None = None,
    skip_unknown: bool = False,
) -> int:
    """
    Lint/validate `file_paths` localization resources.

    If a single directory is given,
    it is iterated with `L10nConfigPaths` if `config_path` is set,
    or `L10nDiscoverPaths` otherwise.

    If `file_paths` is not a single directory,
    the paths are treated as glob expressions.

    Returns 0 on success, 1 on parse error, or 2 on argument error.
    """
    if config_path:
        if file_paths:
            log.error("With --config, paths must not be set.")
            return 2
        cfg_paths = L10nConfigPaths(config_path)
        root_dir = abspath(cfg_paths.base)
        path_iter: Iterable[str] = cfg_paths.ref_paths
    elif len(file_paths) == 1 and isdir(file_paths[0]):
        root_dir = abspath(file_paths[0])
        path_iter = L10nDiscoverPaths(root_dir, ref_root=".").ref_paths
    elif file_paths:
        root_dir = getcwd()
        path_iter = (path for fp in file_paths for path in glob(fp, recursive=True))
    else:
        log.error("Either paths of --config is required")
        return 2

    ok = 0
    unsupported = 0
    failed = 0
    for path in path_iter:
        res = lint_file(root_dir, path, skip_unknown)
        if res == Result.SKIP:
            pass
        elif res == Result.UNSUPPORTED:
            unsupported += 1
        elif res == Result.FAIL:
            failed += 1
        else:
            ok += 1
    if not ok and not unsupported and not failed:
        log.warning("Found no localization resources")
    return 1 if failed or unsupported else 0


def lint_file(root: str, path: str, skip_unknown: bool) -> Result:
    try:
        log_path = relpath(path, root)
        if log_path.startswith(".."):
            log_path = path
    except ValueError:
        log_path = path
    if skip_unknown and splitext(path)[1] not in l10n_extensions:
        log.info(f"skip {log_path}")
        return Result.SKIP
    try:
        parse_resource(path)
        log.info(f"ok {log_path}")
        return Result.OK
    except (UnsupportedFormat, UnicodeDecodeError):
        log.warning(f"unsupported {log_path}")
        return Result.UNSUPPORTED
    except Exception as error:
        log.warning(f"FAIL {log_path} - {error}")
        return Result.FAIL


if __name__ == "__main__":
    cli()
