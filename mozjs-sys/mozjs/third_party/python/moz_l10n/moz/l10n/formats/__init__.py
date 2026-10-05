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

from enum import Enum
from io import BytesIO
from json import JSONDecodeError
from os.path import splitext
from typing import Any

from ..util.loads import json_linecomment_loads

# from moz.l10n.formats.xliff.common import xliff_ns
xliff_ns = {
    "urn:oasis:names:tc:xliff:document:1.0",
    "urn:oasis:names:tc:xliff:document:1.1",
    "urn:oasis:names:tc:xliff:document:1.2",
}

Format = Enum(
    "Format",
    (
        "android",
        "dtd",
        "fluent",
        "gettext",
        "inc",
        "ini",
        "mf2",
        "plain_json",
        "properties",
        "webext",
        "xliff",
    ),
)
"""
Identifier for a supported resource format.
"""

l10n_extensions = {
    ".dtd",
    ".ftl",
    ".inc",
    ".ini",
    ".json",
    ".properties",
    ".po",
    ".pot",
    ".xlf",
    ".xliff",
    ".xml",
}
"""Extensions used by localization file formats."""

bilingual_extensions = {
    ".po",
    ".pot",
    ".xlf",
    ".xliff",
}
"""
Extensions used by file formats (XLIFF & gettext) that contain
both the reference and target languages in the same file.
"""


class UnsupportedFormat(Exception):
    pass


def detect_format(name: str | None, source: bytes | str | None = None) -> Format | None:
    """
    Detect the format of the input based on its file extension
    and/or contents.

    Returns a `Format` enum value, or `None` if the input is not recognized.
    Without `source`, JSON and XML based formats are
    only recognized if they have a distinctive file extension.
    """
    if not name:
        ext = None
    else:
        _, ext = splitext(name)
        if ext == ".dtd":
            return Format.dtd
        elif ext == ".ftl":
            return Format.fluent
        elif ext in {".po", ".pot"}:
            return Format.gettext
        elif ext == ".inc":
            return Format.inc
        elif ext == ".ini":
            return Format.ini
        elif ext == ".properties":
            return Format.properties
        elif ext in {".xlf", ".xliff"}:
            return Format.xliff

    if source is None:
        return None

    # Try parsing as JSON first, unless we're pretty sure it's XML
    if ext != ".xml":
        try:
            json, valid_json = json_linecomment_loads(source)
        except JSONDecodeError:
            json, valid_json = None, False
        if is_object_of_strings(json):
            assert isinstance(json, dict)
            if all(is_webext_message(m) for m in json.values()):
                return Format.webext
            else:
                return Format.plain_json if valid_json else None
        elif json is not None or valid_json:
            return None  # JSON, but not an object with string scalar values

    # Let's presume the input is XML and look at its root node.
    try:
        from lxml.etree import LxmlError, iterparse

        bs = source.encode() if isinstance(source, str) else source
        _, xml_root = next(iterparse(BytesIO(bs), events=("start",)))
        ns = xml_root.nsmap.get(None, None)
        if ns:
            return Format.xliff if ns in xliff_ns else None
        return Format.android if xml_root.tag == "resources" else None
    except ImportError:
        pass
    except LxmlError:
        # Must be separate and after ImportError
        pass

    return None


def is_object_of_strings(obj: Any) -> bool:
    if not isinstance(obj, dict):
        return False
    for value in obj.values():
        if isinstance(value, dict):
            if not is_object_of_strings(value):
                return False
        elif not isinstance(value, str):
            return False
    return True


def is_webext_message(obj: Any) -> bool:
    if not isinstance(obj, dict):
        return False
    has_message = False
    for key, value in obj.items():
        if key == "message":
            has_message = True
        elif key == "description":
            pass
        elif key == "placeholders":
            if not isinstance(value, dict):
                return False
        else:
            return False
    return has_message
