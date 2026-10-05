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

from collections import defaultdict
from collections.abc import Iterator

from lxml import etree

from ...model import (
    Entry,
    Expression,
    Markup,
    Message,
    Metadata,
    PatternMessage,
    VariableRef,
)
from .common import attrib_as_metadata, element_as_metadata, pretty_name, xliff_ns
from .parse_xcode import parse_xcode_pattern


def parse_trans_unit(
    unit: etree._Element, is_xcode: bool, from_source: bool
) -> Entry[Message]:
    id = unit.attrib.get("id", None)
    if id is None:
        raise ValueError(f'Missing "id" attribute for <trans-unit>: {unit}')
    meta = attrib_as_metadata(unit, None, ("id",))
    if unit.text and not unit.text.isspace():
        raise ValueError(f"Unexpected text in <trans-unit>: {unit.text}")

    msg_name = "target" if not from_source else "source"
    msg_el = None
    notes: list[str] = []
    seen: dict[str, int] = defaultdict(int)
    for el in unit:
        if isinstance(el, etree._Comment):
            meta.append(Metadata("comment()", el.text))
        else:
            name = pretty_name(el, str(el.tag))
            if name == msg_name:
                if msg_el:
                    raise ValueError(
                        f"Duplicate <{msg_name}> in <trans-unit> {id}: {unit}"
                    )
                msg_el = el
                meta += attrib_as_metadata(el, msg_name)
            else:
                idx = seen[name] + 1
                base = f"{name}[{idx}]" if idx > 1 else name
                meta += element_as_metadata(el, base, True)
                seen[name] = idx
                if name == "note" and el.text:
                    author = el.attrib.get("from", "")
                    note = el.text.strip()
                    notes.append(f"{author}: {note}" if author else note)
        if el.tail and not el.tail.isspace():
            raise ValueError(f"Unexpected text in <trans-unit>: {el.tail}")

    msg = PatternMessage(
        [] if msg_el is None else list(parse_pattern(msg_el, is_xcode))
    )
    return Entry((id,), msg, comment="\n\n".join(notes), meta=meta)


def parse_pattern(
    el: etree._Element, is_xcode: bool
) -> Iterator[str | Expression | Markup]:
    if el.text:
        if is_xcode:
            yield from parse_xcode_pattern(el.text)
        else:
            yield el.text
    for child in el:
        q = etree.QName(child.tag)
        ns = q.namespace
        name = q.localname if not ns or ns in xliff_ns else q.text
        options: dict[str, str | VariableRef] = dict(child.attrib)
        if name in ("x", "bx", "ex"):
            yield Markup("standalone", name, options)
        elif isinstance(child.tag, str):
            yield Markup("open", name, options)
            yield from parse_pattern(child, is_xcode)
            yield Markup("close", name)
        if child.tail:
            yield child.tail
