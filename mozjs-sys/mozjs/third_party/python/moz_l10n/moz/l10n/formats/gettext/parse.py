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

from polib import pofile

from ...model import (
    CatchallKey,
    Comment,
    Entry,
    Expression,
    Message,
    Metadata,
    PatternMessage,
    Resource,
    Section,
    SelectMessage,
    VariableRef,
)
from .. import Format


def gettext_parse(
    source: str | bytes,
    *,
    plurals: Sequence[str] | None = None,
    skip_obsolete: bool = False,
) -> Resource[Message]:
    """
    Parse a .po or .pot file into a message resource

    Message identifiers may have one or two parts,
    with the second one holding the optional message context.

    If `plurals` is set,
    its strings are used instead of index values for plural keys.
    The last plural variant is always considered the catchall variant.

    If `skip_obsolete` is set,
    obsolete `~` commented entries will be left out of the output.

    Messages may include the following metadata:
    - `translator-comments`
    - `extracted-comments`
    - `reference`: `f"{file}:{line}"`, separately for each reference
    - `obsolete`: `""`
    - `flag`: separately for each flag
    - `plural`
    """
    pf = pofile(source if isinstance(source, str) else str(source, "utf8"))
    res_comment = pf.header.lstrip("\n").rstrip()
    res_meta: list[Metadata] = [
        Metadata(key, value.strip()) for key, value in pf.metadata.items()
    ]
    entries: list[Entry[Message] | Comment] = []
    for pe in pf:
        meta: list[Metadata] = []
        if pe.tcomment:
            meta.append(Metadata("translator-comments", pe.tcomment))
        for file, line in pe.occurrences:
            meta.append(Metadata("reference", f"{file}:{line}"))
        if pe.obsolete:
            if skip_obsolete:
                continue
            meta.append(Metadata("obsolete", "true"))
        for flag in pe.flags:
            meta.append(Metadata("flag", flag))
        if pe.msgid_plural:
            meta.append(Metadata("plural", pe.msgid_plural))
        if pe.msgstr_plural:
            keys = list(pe.msgstr_plural)
            keys.sort()
            sel = Expression(VariableRef("n"), "number")
            max_idx = keys[-1]
            value: Message = SelectMessage(
                declarations={"n": sel},
                selectors=(VariableRef("n"),),
                variants={
                    plural_key(idx, idx == max_idx, plurals): (
                        [pe.msgstr_plural[idx]] if idx in pe.msgstr_plural else []
                    )
                    for idx in range(max_idx + 1)
                },
            )
        else:
            value = PatternMessage([pe.msgstr])
        id = (pe.msgid, pe.msgctxt) if pe.msgctxt else (pe.msgid,)
        entries.append(Entry(id, value, comment=pe.comment, meta=meta))
    return Resource(Format.gettext, [Section((), entries)], res_comment, res_meta)


def plural_key(
    idx: int, is_catchall: bool, plurals: Sequence[str] | None
) -> tuple[str | CatchallKey]:
    ps = plurals[idx] if plurals else str(idx)
    key = CatchallKey(ps) if is_catchall else ps
    return (key,)
