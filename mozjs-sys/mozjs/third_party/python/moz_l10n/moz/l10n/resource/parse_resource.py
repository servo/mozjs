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

from collections.abc import Sequence
from typing import Callable, cast

from ..formats import Format, UnsupportedFormat, detect_format
from ..formats.dtd.parse import dtd_parse
from ..formats.fluent.parse import fluent_parse
from ..formats.gettext.parse import gettext_parse
from ..formats.inc.parse import inc_parse
from ..formats.ini.parse import ini_parse
from ..formats.plain_json.parse import plain_json_parse
from ..formats.properties.parse import properties_parse
from ..formats.webext.parse import webext_parse
from ..model import Message, Resource

android_parse: Callable[..., Resource[Message]] | None
xliff_parse: Callable[..., Resource[Message]] | None
try:
    from ..formats.android.parse import android_parse
    from ..formats.xliff.parse import xliff_parse
except ImportError:
    android_parse = None
    xliff_parse = None


def parse_resource(
    input: Format | str | None,
    source: str | bytes | None = None,
    *,
    android_ascii_spaces: bool = False,
    android_literal_quotes: bool = False,
    gettext_plurals: Sequence[str] | None = None,
    gettext_skip_obsolete: bool = False,
    properties_printf_placeholders: bool = False,
    xliff_source_entries: bool = False,
) -> Resource[Message]:
    """
    Parse a Resource from its string representation.

    The first argument may be an explicit Format,
    the file path as a string, or None.
    For the latter two types,
    an attempt is made to detect the appropriate format.

    If the first argument is a string path,
    the `source` argument is optional,
    as the file will be opened and read if `source` is not set.
    """
    input_is_file = False
    if source is None:
        if not isinstance(input, str):
            raise TypeError("Source is required if type is not a string path")
        with open(input, mode="rb") as file:
            source = file.read()
            input_is_file = True
    # TODO post-py38: should be a match
    format = input if isinstance(input, Format) else detect_format(input, source)
    if format == Format.dtd:
        return dtd_parse(source)
    elif format == Format.fluent:
        return fluent_parse(source)
    elif format == Format.gettext:
        # Workaround for https://github.com/izimobil/polib/issues/170
        source_ = cast(str, input) if input_is_file else source
        return gettext_parse(
            source_, plurals=gettext_plurals, skip_obsolete=gettext_skip_obsolete
        )
    elif format == Format.inc:
        return inc_parse(source)
    elif format == Format.ini:
        return ini_parse(source)
    elif format == Format.plain_json:
        return plain_json_parse(source)
    elif format == Format.properties:
        return properties_parse(
            source, printf_placeholders=properties_printf_placeholders
        )
    elif format == Format.webext:
        return webext_parse(source)
    elif format == Format.android and android_parse is not None:
        return android_parse(
            source,
            ascii_spaces=android_ascii_spaces,
            literal_quotes=android_literal_quotes,
        )
    elif format == Format.xliff and xliff_parse is not None:
        return xliff_parse(source, source_entries=xliff_source_entries)
    else:
        msg = (
            "Resource format detection failed"
            if format is None
            else f"Unsupported resource format: {format.name}"
        )
        if xliff_parse is None and (
            (input_is_file and cast(str, input).endswith((".xlf", ".xliff", ".xml")))
            or (isinstance(source, str) and source.startswith("<"))
            or (isinstance(source, bytes) and source.startswith(b"<"))
        ):
            msg += ". For XML support `lxml` is required."
        raise UnsupportedFormat(msg)
