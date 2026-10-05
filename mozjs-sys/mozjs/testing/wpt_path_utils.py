# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Utilities for working with Web Platform Test (WPT) paths.
"""

from typing import Dict, Optional  # noqa EP035

OptJs = Optional[Dict[str, bool]]  # noqa UP006

WP = "testing/web-platform/"
WPT0 = WP + "tests/infrastructure"
WPT_META0 = WP + "tests/infrastructure/metadata"
WPT_META0_CLASSIC = WP + "meta/infrastructure"
WPT1 = WP + "tests"
WPT_META1 = WPT1.replace("tests", "meta")
WPT2 = WP + "mozilla/tests"
WPT_META2 = WPT2.replace("tests", "meta")
WPT_MOZILLA = "/_mozilla"


def resolve_wpt_path(shortpath: str) -> str:
    """
    Resolves a short WPT path to its full path in the Firefox tree.

    Args:
        shortpath: A short WPT path like "/css/foo.html" or "/_mozilla/bar.html"

    Returns:
        The full path like "testing/web-platform/tests/css/foo.html"
    """
    if shortpath.startswith(WPT0):
        return shortpath
    elif shortpath.startswith(WPT1):
        return shortpath
    elif shortpath.startswith(WPT2):
        return shortpath
    elif shortpath.startswith(WPT_MOZILLA):
        shortpath = shortpath[len(WPT_MOZILLA) :]
        return WPT2 + shortpath
    else:
        return WPT1 + shortpath


def get_wpt_metadata_path(path: str) -> str:
    """
    Gets the metadata path for a given WPT path.

    Args:
        path: A WPT path (can be short or full)

    Returns:
        The corresponding metadata path
    """
    if path.startswith(WPT0):
        return path.replace(WPT0, WPT_META0, 1)
    elif path.startswith(WPT1):
        return path.replace(WPT1, WPT_META1, 1)
    elif path.startswith(WPT2):
        return path.replace(WPT2, WPT_META2, 1)
    elif path.startswith(WPT_MOZILLA):
        shortpath = path[len(WPT_MOZILLA) :]
        return WPT_META2 + shortpath
    else:
        return WPT_META1 + path


def get_wpt_path_and_metadata(shortpath: str) -> tuple[str, str]:
    """
    Gets both the resolved WPT path and its metadata path.

    Args:
        shortpath: A short or full WPT path

    Returns:
        A tuple of (path, metadata_path)
    """
    path = resolve_wpt_path(shortpath)
    meta = get_wpt_metadata_path(shortpath)
    return (path, meta)


def parse_wpt_path(
    shortpath: str, isdir_fn: Optional[callable] = None
) -> tuple[Optional[str], Optional[str], Optional[str], Optional[str]]:
    """
    Analyzes the WPT short path for a test and returns detailed information.

    Args:
        shortpath: A WPT path that may include query parameters
        isdir_fn: Optional function to check if a path is a directory

    Returns:
        A tuple of (path, manifest, query, anyjs) where:
        - path is the relative path to the test file
        - manifest is the relative path to the file metadata
        - query is the test file query parameters (or None)
        - anyjs is the html test file as reported by mozci (or None)
    """
    query: Optional[str] = None
    anyjs: OptJs = None

    i = shortpath.find("?")
    if i > 0:
        query = shortpath[i:]
        shortpath = shortpath[0:i]

    path, manifest = get_wpt_path_and_metadata(shortpath)

    failure_type = True
    if isdir_fn:
        failure_type = not isdir_fn(path)

    if failure_type:
        i = path.find(".any.")
        if i > 0:
            anyjs = {path: False}
            manifest = manifest.replace(path[i:], ".any.js")
            path = path[0:i] + ".any.js"
        else:
            i = path.find(".window.")
            if i > 0:
                anyjs = {path: False}
                manifest = manifest.replace(path[i:], ".window.js")
                path = path[0:i] + ".window.js"
            else:
                i = path.find(".worker.")
                if i > 0:
                    anyjs = {path: False}
                    manifest = manifest.replace(path[i:], ".worker.js")
                    path = path[0:i] + ".worker.js"
        manifest += ".ini"

    return (path, manifest, query, anyjs)
