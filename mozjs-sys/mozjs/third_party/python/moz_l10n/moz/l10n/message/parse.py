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

from typing import Callable

from ..formats import Format, UnsupportedFormat
from ..formats.fluent.parse import fluent_parse_message
from ..formats.mf2.message_parser import mf2_parse_message
from ..formats.properties.parse import properties_parse_message
from ..formats.webext.parse import webext_parse_message
from ..model import Message, PatternMessage
from ..util.printf import parse_printf_pattern

android_parse_message: Callable[..., PatternMessage] | None = None
xliff_parse_message: Callable[..., PatternMessage] | None = None
try:
    from ..formats.android.parse import android_parse_message
    from ..formats.xliff.parse import xliff_parse_message
except ImportError:
    pass


def parse_message(
    format: Format,
    source: str,
    *,
    android_ascii_spaces: bool = False,
    printf_placeholders: bool = False,
    webext_placeholders: dict[str, dict[str, str]] | None = None,
    xliff_is_xcode: bool = False,
) -> Message:
    """
    Parse a `Message` from its string representation.

    Custom parsers are used for `android`, `fluent`, `mf2`, `properties`, `webext`, and `xliff` formats.
    `properties` and other formats not listed may include printf specifiers if `printf_placeholders` is enabled.

    Parsing a `webext` message that contains named placeholders requires
    providing the message's `webext_placeholders` dict.

    To parse an `xliff` message with XCode customizations, enable `xliff_is_xcode`.
    """
    # TODO post-py38: should be a match
    if format == Format.properties:
        return properties_parse_message(source, printf_placeholders=printf_placeholders)
    elif format == Format.webext:
        return webext_parse_message(source, webext_placeholders)
    elif format == Format.android:
        if android_parse_message is None:
            raise UnsupportedFormat("Parsing Android messages requires [xml] extra")
        return android_parse_message(source, ascii_spaces=android_ascii_spaces)
    elif format == Format.xliff:
        if xliff_parse_message is None:
            raise UnsupportedFormat("Parsing XLIFF messages requires [xml] extra")
        return xliff_parse_message(source, is_xcode=xliff_is_xcode)
    elif format == Format.mf2:
        return mf2_parse_message(source)
    elif format == Format.fluent:
        return fluent_parse_message(source)
    elif printf_placeholders:
        return PatternMessage(list(parse_printf_pattern(source)))
    else:
        return PatternMessage([source] if source else [])
