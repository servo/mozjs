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
from ..formats.fluent.serialize import fluent_serialize_message
from ..formats.mf2.serialize import mf2_serialize_message
from ..formats.properties.serialize import properties_serialize_message
from ..formats.webext.serialize import webext_serialize_message
from ..model import Message, Pattern, PatternMessage

android_serialize_message: Callable[[Message], str] | None = None
xliff_serialize_message: Callable[[Message], str] | None = None
try:
    from ..formats.android.serialize import android_serialize_message
    from ..formats.xliff.serialize import xliff_serialize_message
except ImportError:
    pass


def serialize_message(
    format: Format | None,
    msg: Message | Pattern,
    *,
    fluent_escape_empty: bool = False,
    fluent_escape_syntax: bool = True,
) -> str:
    """
    Serialize a `Message` or `Pattern` to its string representation.

    Custom serialisers are used for `android`, `fluent`, `mf2`, `properties`, `webext`, and `xliff` formats.
    Many formats rely on non-string message parts including an appropriate `source` attribute.

    SelectMessage serialization is only supported for `fluent` and `mf2`.
    """
    if isinstance(msg, list):
        msg = PatternMessage(msg)
    # TODO post-py38: should be a match
    if format == Format.properties:
        return properties_serialize_message(msg)
    elif format == Format.webext:
        # Placeholders are discarded
        return webext_serialize_message(msg)[0]
    elif format == Format.android:
        if android_serialize_message is None:
            raise UnsupportedFormat("Serializing Android messages requires [xml] extra")
        return android_serialize_message(msg)
    elif format == Format.xliff:
        if xliff_serialize_message is None:
            raise UnsupportedFormat("Serializing XLIFF messages requires [xml] extra")
        return xliff_serialize_message(msg)
    elif format == Format.mf2:
        return mf2_serialize_message(msg)
    elif format == Format.fluent:
        return fluent_serialize_message(
            msg, escape_empty=fluent_escape_empty, escape_syntax=fluent_escape_syntax
        )
    elif not isinstance(msg, PatternMessage) or msg.declarations:
        raise ValueError(f"Unsupported message: {msg}")
    else:
        res = ""
        for part in msg.pattern:
            if isinstance(part, str):
                res += part
            else:
                part_source = part.attributes.get("source", None)
                if isinstance(part_source, str):
                    res += part_source
                else:
                    raise ValueError(f"Unsupported placeholder: {part}")
        return res
