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

from collections.abc import Mapping
from re import split
from typing import Any, Literal

from ..model import (
    CatchallKey,
    Entry,
    Expression,
    Markup,
    Message,
    Metadata,
    Pattern,
    PatternMessage,
    SelectMessage,
    VariableRef,
)


def entry_from_json(key: str, json: dict[str, Any]) -> Entry[Message]:
    """
    Marshal the JSON output of `moz.l10n.message.entry_to_json()`
    back into a parsed `moz.l10n.model.Entry`.

    May raise `MF2ValidationError`.
    """
    if r"\." in key:
        id = tuple(str(part).replace(r"\.", ".") for part in split(r"(?<!\\)\.", key))
    else:
        id = tuple(key.split("."))
    meta = [Metadata(key, value) for key, value in json["@"]] if "@" in json else []

    return Entry(
        id=id,
        meta=meta,
        comment=json.get("#", ""),
        value=message_from_json(json["="]) if "=" in json else PatternMessage([]),
        properties={key: message_from_json(value) for key, value in json["+"].items()}
        if "+" in json
        else {},
    )


def message_from_json(json: list[Any] | dict[str, Any]) -> Message:
    """
    Marshal the JSON output of `moz.l10n.message.message_to_json()`
    back into a parsed `moz.l10n.model.Message`.

    May raise `MF2ValidationError`.
    """
    if isinstance(json, Mapping) and "sel" in json:
        return SelectMessage(
            declarations={
                name: _expression(value) for name, value in json["decl"].items()
            },
            selectors=tuple(VariableRef(sel) for sel in json["sel"]),
            variants={
                tuple(
                    key if isinstance(key, str) else CatchallKey(key["*"] or None)
                    for key in variant["keys"]
                ): _pattern(variant["pat"])
                for variant in json["alt"]
            },
        )
    else:
        declarations = {}
        if isinstance(json, Mapping):
            if "decl" in json:
                declarations = {
                    name: _expression(value) for name, value in json["decl"].items()
                }
            pattern = _pattern(json["msg"])
        else:
            pattern = _pattern(json)
        return PatternMessage(pattern, declarations)


def _pattern(json: list[str | dict[str, Any]]) -> Pattern:
    return [
        part
        if isinstance(part, str)
        else _expression(part)
        if "_" in part or "$" in part or "fn" in part
        else _markup(part)
        for part in json
    ]


def _expression(json: dict[str, Any]) -> Expression:
    if "_" in json:
        arg = json["_"]
    elif "$" in json:
        arg = VariableRef(json["$"])
    else:
        arg = None
    function = json.get("fn", None)
    options = _options(json["opt"]) if function is not None and "opt" in json else {}
    return Expression(arg, function, options, json.get("attr", {}))


def _markup(json: dict[str, Any]) -> Markup:
    kind: Literal["open", "standalone", "close"]
    if "open" in json:
        kind = "open"
        name = json["open"]
    elif "close" in json:
        kind = "close"
        name = json["close"]
    else:
        kind = "standalone"
        name = json["elem"]
    return Markup(
        kind,
        name,
        _options(json.get("opt", {})),
        json.get("attr", {}),
    )


def _options(json: dict[str, Any]) -> dict[str, str | VariableRef]:
    return {
        name: value if isinstance(value, str) else VariableRef(value["$"])
        for name, value in json.items()
    }
