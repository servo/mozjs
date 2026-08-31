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

from typing import TYPE_CHECKING, Iterable

if TYPE_CHECKING:
    from moz.l10n.model import Message, Pattern

__all__ = [
    "_normalize_declarations",
    "_normalize_pattern",
]


def _normalize_pattern(pattern: Pattern, var_refs: set[str]) -> None:
    from moz.l10n.model import Expression

    empty_literal = Expression("")
    i = 0
    at_str = False
    while i < len(pattern):
        el = pattern[i]
        if isinstance(el, str):
            if el == "":
                pattern.pop(i)
            elif at_str:
                pattern[i - 1] += el  # type: ignore
                pattern.pop(i)
            else:
                at_str = True
                i += 1
        elif el == empty_literal:
            pattern.pop(i)
        else:
            at_str = False
            i += 1
            for var in el.variable_refs():
                var_refs.add(var.name)


def _normalize_declarations(msg: Message, var_refs: set[str]) -> None:
    decl_refs = {
        name: set(var.name for var in decl.variable_refs() if var.name != name)
        for name, decl in msg.declarations.items()
    }
    for name in list(var_refs):
        var_refs.update(_var_dependencies(decl_refs, name))
    for name in list(msg.declarations):
        if name not in var_refs:
            del msg.declarations[name]


def _var_dependencies(decl_refs: dict[str, set[str]], name: str) -> Iterable[str]:
    drs = decl_refs.get(name, None)
    if drs:
        del decl_refs[name]
        for dr in drs:
            yield dr
            yield from _var_dependencies(decl_refs, dr)
