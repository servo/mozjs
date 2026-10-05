# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Generate locale-manifest.in for chrome multilocale packaging.

import argparse
import sys
from pathlib import Path


def main(argv):
    parser = argparse.ArgumentParser(
        description="Generate locale-manifest.in for chrome multilocale packaging."
    )
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--base-path", required=True)
    parser.add_argument("--locales", required=True, nargs="+")
    parser.add_argument("--locale-entries", nargs="*", default=[])
    args = parser.parse_args(argv)

    lines = ["", "[multilocale]", f"{args.base_path}/res/multilocale.txt"]
    for locale in args.locales:
        for entry in args.locale_entries:
            lines.append(f"{entry}{locale}@JAREXT@")
            lines.append(f"{entry}{locale}.manifest")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
