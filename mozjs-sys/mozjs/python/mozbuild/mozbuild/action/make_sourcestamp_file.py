# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Write the package sourcestamp file from buildid.h and source-repo.h.

import argparse
import io
import sys
from pathlib import Path

from mozbuild.preprocessor import Preprocessor


def _extract(header_path, key):
    pp = Preprocessor()
    with open(header_path, encoding="utf-8") as fh:
        pp.processFile(fh, io.StringIO())
    return str(pp.context[key])


def main(argv):
    parser = argparse.ArgumentParser(
        description="Write the package sourcestamp file from buildid.h and source-repo.h."
    )
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--buildid-header", required=True, type=Path)
    parser.add_argument("--source-repo-header", type=Path)
    args = parser.parse_args(argv)

    lines = [_extract(args.buildid_header, "MOZ_BUILDID")]
    if args.source_repo_header:
        lines.append(_extract(args.source_repo_header, "MOZ_SOURCE_URL"))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
