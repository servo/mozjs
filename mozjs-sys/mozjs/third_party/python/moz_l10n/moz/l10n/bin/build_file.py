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

import json
import logging
from argparse import ArgumentParser, RawDescriptionHelpFormatter
from os import makedirs
from os.path import dirname, exists, join, relpath
from shutil import copyfile
from textwrap import dedent

from moz.l10n.bin.build import write_target_file
from moz.l10n.formats import UnsupportedFormat
from moz.l10n.model import Entry
from moz.l10n.resource import parse_resource, serialize_resource

log = logging.getLogger(__name__)


def cli() -> None:
    parser = ArgumentParser(
        description=dedent(
            """
            Build one localization file for release.

            Uses the --source file as a baseline, applying --l10n localizations (if set) to build --target.

            Trims out all comments and messages not in the source file.
            """
        ),
        formatter_class=RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "-v", "--verbose", action="count", default=0, help="increase logging verbosity"
    )
    parser.add_argument("--source", metavar="PATH", required=True, help="source file")
    parser.add_argument("--l10n", metavar="PATH", help="localization file")
    parser.add_argument("--target", metavar="PATH", required=True, help="output target")
    parser.add_argument(
        "--coverage-base",
        metavar="DIR",
        help="base dir for coverage reporting: update <DIR>/coverage.json with "
        "this file's translation ratio, keyed by --target's path relative to DIR "
        "(e.g. browser/browser.ftl), matching l10n-build's keys",
    )
    args = parser.parse_args()

    log_level = (
        logging.WARNING
        if args.verbose == 0
        else logging.INFO
        if args.verbose == 1
        else logging.DEBUG
    )
    logging.basicConfig(format="%(message)s", level=log_level)

    try:
        try:
            source_res = parse_resource(args.source)
        except UnsupportedFormat:
            source_res = None
        makedirs(dirname(args.target), exist_ok=True)
        total_count = 0
        missing_ids: list[str | list[str]] = []
        if source_res is None:
            from_path = (
                args.l10n
                if args.l10n is not None and exists(args.l10n)
                else args.source
            )
            copyfile(from_path, args.target)
        elif args.l10n is None:
            with open(args.target, "w", encoding="utf-8") as file:
                for line in serialize_resource(source_res, trim_comments=True):
                    file.write(line)
            total_count = sum(
                isinstance(entry, Entry)
                for section in source_res.sections
                for entry in section.entries
            )
        else:
            _, total_count, missing_ids = write_target_file(
                args.source, source_res, args.l10n, args.target
            )

        if args.coverage_base:
            # Match l10n-build: one coverage.json per locale dir, keyed by the
            # file path relative to that dir (e.g. browser/browser.ftl). The
            # --coverage-base dir holds the coverage.json and is the root the
            # key is computed against. Read any pre-existing file and
            # add/overwrite this resource's entry.
            coverage_path = join(args.coverage_base, "coverage.json")
            coverage_name = relpath(args.target, args.coverage_base)
            coverage_data: dict[str, dict[str, object]] = {}
            if exists(coverage_path):
                with open(coverage_path, encoding="utf-8") as file:
                    coverage_data = json.load(file)
            coverage_data[coverage_name] = {
                "total": total_count,
                "missing": missing_ids,
            }
            makedirs(args.coverage_base, exist_ok=True)
            with open(coverage_path, "w", encoding="utf-8") as file:
                json.dump(coverage_data, file, indent=2, sort_keys=True)
    except (OSError, UnsupportedFormat) as error:
        raise SystemExit(error)


if __name__ == "__main__":
    cli()
