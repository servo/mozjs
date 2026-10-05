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
from collections import defaultdict
from os import makedirs
from os.path import dirname, exists, join, relpath
from shutil import copyfile
from textwrap import dedent

from moz.l10n.formats import Format, UnsupportedFormat
from moz.l10n.model import Comment, Entry, Message, Resource, Section
from moz.l10n.paths.config import L10nConfigPaths
from moz.l10n.resource import parse_resource, serialize_resource

log = logging.getLogger(__name__)


def cli() -> None:
    parser = ArgumentParser(
        description=dedent(
            """
            Build localization files for release.

            Iterates source files as defined by --config, reads localization sources from --base, and writes to --target.

            Trims out all comments and messages not in the source files for each of the --locales.

            For Fluent, adds empty files for any missing from the target locale.
            For other formats, copies file from the source locale if they are missing from the target.
            """
        ),
        formatter_class=RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "-v", "--verbose", action="count", default=0, help="increase logging verbosity"
    )
    parser.add_argument(
        "--config", metavar="PATH", required=True, help="l10n.toml config file"
    )
    parser.add_argument(
        "--base", metavar="PATH", required=True, help="base dir for localizations"
    )
    parser.add_argument(
        "--target", metavar="PATH", required=True, help="target dir for localizations"
    )
    parser.add_argument(
        "--locales", metavar="LOCALE", nargs="+", required=True, help="target locales"
    )
    parser.add_argument(
        "--coverage",
        action="store_true",
        help="write a coverage.json file per locale with the translation ratio",
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

    cfg_path: str = args.config
    l10n_base: str = args.base
    l10n_target: str = args.target
    locales: set[str] = set(args.locales)
    write_coverage: bool = args.coverage

    # locale -> [ftl_missing, src_fallback]
    msg_data: dict[str, list[int]] = defaultdict(lambda: [0, 0])

    # locale -> {file_path -> {"total": int, "missing": [id, ...]}}.
    # Pre-initialized so every requested locale gets a coverage.json,
    # even if no source files were parseable.
    coverage_data: dict[str, dict[str, dict[str, int | list[str | list[str]]]]] = {
        locale: {} for locale in locales
    }

    paths = L10nConfigPaths(cfg_path)
    paths.base = l10n_base
    paths.locales = None
    for (source_path, l10n_path_template), path_locales in paths.all().items():
        log.debug(f"source {source_path}")
        try:
            source = parse_resource(source_path)
        except UnsupportedFormat:
            source = None
        for locale in locales.intersection(path_locales) if path_locales else locales:
            l10n_path = l10n_path_template.format(locale=locale)
            rel_path = relpath(l10n_path, l10n_base)
            tgt_path = join(l10n_target, rel_path)
            makedirs(dirname(tgt_path), exist_ok=True)
            if source:
                msg_delta, total_count, missing_ids = write_target_file(
                    rel_path, source, l10n_path, tgt_path
                )
                if msg_delta < 0:
                    msg_data[locale][0] -= msg_delta
                elif msg_delta > 0:
                    msg_data[locale][1] += msg_delta
                else:
                    msg_data[locale]
                if write_coverage:
                    file_key = relpath(l10n_path, join(l10n_base, locale))
                    coverage_data[locale][file_key] = {
                        "total": total_count,
                        "missing": missing_ids,
                    }
            else:
                from_path = l10n_path if exists(l10n_path) else source_path
                if from_path != tgt_path:
                    copy = "copy" if from_path == l10n_path else "copy-src"
                    log.info(f"{copy} {rel_path}")
                    copyfile(from_path, tgt_path)
                else:
                    log.info(f"skip {rel_path}")

    if write_coverage:
        for locale, file_coverage in coverage_data.items():
            coverage_path = join(l10n_target, locale, "coverage.json")
            makedirs(dirname(coverage_path), exist_ok=True)
            with open(coverage_path, "w", encoding="utf-8") as f:
                json.dump(file_coverage, f, indent=2, sort_keys=True)

    log.info("----")
    for locale, (ftl_missing, src_fallback) in sorted(
        msg_data.items(), key=lambda d: d[0]
    ):
        log.info(f"{locale}:")
        log.info(f"  ftl_missing  {ftl_missing:>6}")
        log.info(f"  src_fallback {src_fallback:>6}")


def write_target_file(
    name: str,
    source_res: Resource[Message],
    l10n_path: str,
    tgt_path: str,
) -> tuple[int, int, list[str | list[str]]]:
    if exists(l10n_path):
        l10n_res = parse_resource(l10n_path)
        l10n_map = {
            section.id + entry.id: entry
            for section in l10n_res.sections
            for entry in section.entries
            if isinstance(entry, Entry)
        }
        l10n_res.sections = []
    else:
        l10n_res = Resource(source_res.format, [])
        l10n_map = {}
    # Fluent uses per-message fallback at runtime, allowing resources to be incomplete.
    is_fluent = source_res.format == Format.fluent
    msg_delta = 0
    total_count = 0
    missing_ids: list[str | list[str]] = []

    def missing_id(id: tuple[str, ...]) -> str | list[str]:
        # Keep the id structural; a single-part id is simplified to a string.
        return id[0] if len(id) == 1 else list(id)

    def get_entry(
        section_id: tuple[str, ...], source_entry: Entry[Message] | Comment
    ) -> Entry[Message] | Comment | None:
        nonlocal msg_delta, total_count
        if isinstance(source_entry, Comment):
            return None
        total_count += 1
        id = section_id + source_entry.id
        if id in l10n_map:
            l10n_entry = l10n_map[id]
            if is_fluent and not l10n_entry.id[0].startswith("-"):
                # If source_res includes Fluent message attributes not in l10n_res, exclude the entry.
                # If l10n_res includes additional message attributes,
                # include the entry but leave out the attributes not in source_res.
                sk = source_entry.properties.keys()
                lk = l10n_entry.properties.keys()
                if sk != lk:
                    if set(sk).issubset(lk):
                        l10n_entry.properties = {
                            name: l10n_entry.properties[name] for name in sk
                        }
                        return l10n_entry
                    msg_delta -= 1
                    missing_ids.append(missing_id(id))
                    return None
            return l10n_entry
        elif is_fluent:
            msg_delta -= 1
            missing_ids.append(missing_id(id))
            return None
        else:
            msg_delta += 1
            missing_ids.append(missing_id(id))
            return source_entry

    for section in source_res.sections:
        tgt_entries = [
            entry
            for entry_ in section.entries
            if (entry := get_entry(section.id, entry_)) is not None
        ]
        l10n_res.sections.append(Section(section.id, tgt_entries))

    msg = f"merge {name}"
    log.info(f"{msg} ({msg_delta:+d})" if msg_delta != 0 else msg)
    with open(tgt_path, "w", encoding="utf-8") as file:
        for line in serialize_resource(l10n_res, trim_comments=True):
            file.write(line)
    return msg_delta, total_count, missing_ids


if __name__ == "__main__":
    cli()
