# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os.path
import re
import sys
import yaml

with open(os.path.join(os.path.dirname(__file__), "diskarzhan.yaml")) as fd:
    _database = yaml.safe_load(fd)
    cxx_api = _database["stdcxx"]
    c_api = _database["stdc"]


def fix_includes(path, raw_content, changes):
    lines_to_delete = {lineno for lineno, _ in changes}
    prev_content = raw_content.split("\n")
    new_content = [
        raw_line
        for lineno, raw_line in enumerate(prev_content, start=1)
        if lineno not in lines_to_delete
    ]
    with open(path, "w") as outfd:
        outfd.write("\n".join(new_content))


def lint_std_headers(path, raw_content):
    # If there a:
    #   namespace std {...}
    # or a:
    #   using namespace std
    # or even:
    #   namespace std = ...
    # then we just look for symbol tokens.
    # Otherwise we look for the very common std::{token} pattern,
    # which avoids same false negative.
    if re.search(r"\bnamespace\s+std\b", raw_content):
        symbol_pattern = r"\b{}\b"
    else:
        symbol_pattern = r"\bstd::{}\b"

    changes = []
    for header, symbols in cxx_api.items():
        headerline = rf"#\s*include <{header}>"
        if not (match := re.search(headerline, raw_content)):
            continue
        if re.search(
            "|".join(symbol_pattern.format(symbol) for symbol in symbols), raw_content
        ):
            continue

        msg = f"{path} includes <{header}> but does not reference any of its API"
        lineno = 1 + raw_content.count("\n", 0, match.start())
        changes.append((lineno, msg))

    return changes


def lint_cstd_headers(path, raw_content):
    symbol_pattern = r"\b((std)?::)?{}\b"

    changes = []
    for header, symbols in c_api.items():
        headerline = rf"#\s*include <({header}|c{header[:-2]})>"
        if not (match := re.search(headerline, raw_content)):
            continue
        if re.search(
            "|".join(symbol_pattern.format(symbol) for symbol in symbols), raw_content
        ):
            continue

        msg = (
            f"{path} includes <{match.group(1)}> but does not reference any of its API"
        )
        lineno = 1 + raw_content.count("\n", 0, match.start())
        changes.append((lineno, msg))

    return changes


def lint(paths, *, fix=False):

    change_count = 0
    for path in paths:
        try:
            with open(path) as fd:
                raw_content = fd.read()
        except UnicodeDecodeError:
            continue

        changes = lint_std_headers(path, raw_content)
        changes += lint_cstd_headers(path, raw_content)

        if fix:
            fix_includes(path, raw_content, changes)
        else:
            for lineno, msg in changes:
                print(f"{path}:{lineno}: {msg}")

        change_count += len(changes)

    return change_count


def run():
    import argparse

    parser = argparse.ArgumentParser(description="cleanup standard includes")
    parser.add_argument("sources", nargs="+", type=str, help="files to cleanup")
    parser.add_argument(
        "--fix", action="store_true", help="apply modification in-place"
    )
    args = parser.parse_args()
    changed = lint(args.sources, fix=args.fix)
    print(
        f"{'Applied' if args.fix else 'Found'} {changed} change{'s' if changed else ''}"
    )
    if not args.fix and changed:
        sys.exit(1)
