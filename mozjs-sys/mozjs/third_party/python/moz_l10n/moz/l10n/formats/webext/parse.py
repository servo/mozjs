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

from re import compile
from typing import Any

from ...model import (
    Comment,
    Entry,
    Expression,
    Message,
    Pattern,
    PatternMessage,
    Resource,
    Section,
    VariableRef,
)
from ...util.loads import json_linecomment_loads
from .. import Format

placeholder = compile(r"\$([a-zA-Z0-9_@]+)\$|(\$[1-9])|\$(\$+)")
pos_arg = compile(r"\$([1-9])")


def webext_parse(source: str | bytes) -> Resource[Message]:
    """
    Parse a messages.json file into a message resource.

    Named placeholders are represented as declarations,
    with an attribute used for an example, if it's available.

    The parsed resource will not include any metadata.
    """
    json: dict[str, dict[str, Any]] = json_linecomment_loads(source)[0]
    entries: list[Entry[Message] | Comment] = [
        Entry(
            (key,),
            webext_parse_message(msg["message"], msg.get("placeholders", None)),
            comment=msg.get("description", ""),
        )
        for key, msg in json.items()
    ]
    return Resource(Format.webext, [Section((), entries)])


def webext_parse_message(
    source: str, placeholders: dict[str, dict[str, str]] | None
) -> PatternMessage:
    """
    Parse a single messages.json message.

    Named placeholders are represented as declarations,
    with an attribute used for an example, if it's available.
    """
    ph_data = (
        {k.lower(): v for k, v in placeholders.items()}
        if placeholders is not None
        else {}
    )
    pattern: Pattern = []
    declarations: dict[str, Expression] = {}
    pos = 0
    for m in placeholder.finditer(source):
        text = source[pos : m.start()]
        if text:
            if pattern and isinstance(pattern[-1], str):
                pattern[-1] += text
            else:
                pattern.append(text)
        if m[1]:
            # Named placeholder, with content & optional example in placeholders object
            ph_key = m[1].lower()
            ph = ph_data.get(ph_key, None)
            if ph is None:
                raise ValueError(f"Missing placeholders entry for {ph_key}")
            elif "_name" in ph:
                ph_name = ph["_name"]
            else:
                decl_src = ph["content"]
                decl_arg_match = pos_arg.fullmatch(decl_src)
                decl_value = (
                    Expression(
                        VariableRef(f"arg{decl_arg_match[1]}"),
                        attributes={"source": decl_src},
                    )
                    if decl_arg_match
                    else Expression(decl_src)
                )
                if "example" in ph:
                    decl_value.attributes["example"] = ph["example"]
                ph_name = m[1].replace("@", "_")
                if ph_name[0].isdigit():
                    ph_name = f"_{ph_name}"
                declarations[ph_name] = decl_value
                ph["_name"] = ph_name
            exp = Expression(VariableRef(ph_name), attributes={"source": m[0]})
            pattern.append(exp)
        elif m[2]:
            # Indexed placeholder
            ph_src = m[2]
            pattern.append(
                Expression(
                    VariableRef(f"arg{ph_src[1]}"), attributes={"source": ph_src}
                )
            )
        else:
            # Escaped literal dollar sign
            if pattern and isinstance(pattern[-1], str):
                pattern[-1] += m[3]
            else:
                pattern.append(m[3])
        pos = m.end()
    if pos < len(source):
        rest = source[pos:]
        if pattern and isinstance(pattern[-1], str):
            pattern[-1] += rest
        else:
            pattern.append(rest)
    return PatternMessage(pattern, declarations)
