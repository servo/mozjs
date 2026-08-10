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

from dataclasses import dataclass, field
from logging import getLogger
from typing import Any, TypeVar, Union

from moz.l10n.model import (
    CatchallKey,
    Entry,
    Expression,
    Id,
    Message,
    Pattern,
    PatternMessage,
    Resource,
    Section,
    SelectMessage,
    VariableRef,
)
from moz.l10n.paths.config import L10nConfigPaths
from moz.l10n.paths.discover import L10nDiscoverPaths
from moz.l10n.resource import parse_resource

log = getLogger(__name__)
M = TypeVar("M", bound=Union[Message, str])


@dataclass
class MigrationContext:
    paths: L10nConfigPaths | L10nDiscoverPaths
    ref_path: str
    locale: str
    parse_options: dict[str, Any]
    target_id: Id = ()

    _prev_ids: list[Id] = field(default_factory=list)
    _resources: dict[str, Resource[Message] | None] = field(default_factory=dict)

    def get_resource(self, ref_path: str) -> Resource[Message] | None:
        if ref_path in self._resources:
            return self._resources[ref_path]
        tgt_path, _ = self.paths.target(ref_path, locale=self.locale)
        try:
            res = parse_resource(tgt_path, **self.parse_options)
        except OSError:
            log.debug(f"Resource not available: {tgt_path}")
            res = None
        except Exception:
            log.debug(f"Parse error: {tgt_path}", exc_info=True)
            res = None
        self._resources[ref_path] = res
        return res

    def pretty_id(self, id: tuple[str, ...]) -> str:
        return f"{'.'.join(id)} in {self.ref_path} for locale {self.locale}"

    def _update(self, id: Id | str) -> None:
        if self.target_id:
            self._prev_ids.append(self.target_id)
        self.target_id = (id,) if isinstance(id, str) else id

    def __str__(self) -> str:
        return self.pretty_id(self.target_id)


def get_entry(res: Resource[M], *id: str) -> Entry[M] | None:
    """
    Get an entry matching `id` from `res`.

    If not found return None
    """
    for section in res.sections:
        if section.id:
            sid_len = len(section.id)
            if section.id != id[:sid_len]:
                continue
            eid = id[sid_len:]
        else:
            eid = id
        entry = next(
            (e for e in section.entries if isinstance(e, Entry) and e.id == eid),
            None,
        )
        if entry is not None:
            return entry
    return None


def get_pattern(
    res: Resource[Message],
    *id: str,
    property: str | None = None,
    default: Pattern | None = None,
    variant: tuple[str | CatchallKey, ...] | str | None = None,
) -> Pattern:
    """
    Get a pattern matching `id`, `property`, and `variant` from `res`.

    If `property` is not set, a pattern from the `id` entry's `.value` is returned.
    Otherwise, a pattern from its `.properties[property]` is returned.

    If the message for `id` and `property` is a SelectMessage,
    either the pattern matching `variant` is returned,
    or (if not found) the fallback pattern is returned.

    If `default` is a Pattern, it is returned if no matching pattern is found.
    Otherwise, a StopIteration exception is raised.
    """
    entry = get_entry(res, *id)
    if entry is None:
        if default is not None:
            return default
        raise StopIteration

    if property is None:
        msg = entry.value
    elif property in entry.properties:
        msg = entry.properties[property]
    else:
        raise StopIteration

    if isinstance(msg, PatternMessage):
        return msg.pattern
    elif isinstance(msg, SelectMessage):
        if isinstance(variant, str):
            variant = (variant,)
        if variant in msg.variants:
            return msg.variants[variant]
        return next(
            pattern
            for keys, pattern in msg.variants.items()
            if all(isinstance(key, CatchallKey) for key in keys)
        )

    raise ValueError(f"Value of {id} entry is not a Message")


def insert_entry_after(
    res: Resource[M],
    entry: Entry[M],
    *src_ids: tuple[str, ...] | str,
) -> None:
    """
    Insert `entry` in `res` after the last entry
    with an `.id` matching one of `src_ids`,
    or if none such are found,
    at the end of the last section matching any of `ids`.

    Note that `entry.id` is expected to include its section id,
    which will be dropped before insertion.

    If no suitable insertion position is found, raises StopIteration.
    """
    id_set = {id if isinstance(id, tuple) else (id,) for id in src_ids}
    last_section: Section[M] | None = None
    for section in reversed(res.sections):
        if section.id:
            sid_len = len(section.id)
            eid_set = {id[sid_len:] for id in id_set if section.id == id[:sid_len]}
            if not eid_set:
                continue
        else:
            sid_len = 0
            eid_set = id_set
        for e_rev_idx, e in enumerate(reversed(section.entries)):
            if isinstance(e, Entry) and e.id in eid_set:
                if sid_len:
                    entry.id = entry.id[sid_len:]
                section.entries.insert(len(section.entries) - e_rev_idx, entry)
                return
        last_section = section
    if last_section is None:
        raise StopIteration
    else:
        if last_section.id:
            entry.id = entry.id[len(last_section.id) :]
        last_section.entries.append(entry)


def plural_message(
    var_name: str,
    *,
    zero: Pattern | None = None,
    one: Pattern | None = None,
    two: Pattern | None = None,
    few: Pattern | None = None,
    many: Pattern | None = None,
    other: Pattern,
) -> SelectMessage:
    """
    Construct a SelectMessage using `var_name` to determine the plural category,
    with the given variants.

    By convention, use the following as `var_name`:

    - `"n"` for Gettext plurals
    - `"quantity"` for Android strings.
    """
    raw_variants: list[tuple[str | CatchallKey, Pattern | None]] = [
        ("zero", zero),
        ("one", one),
        ("two", two),
        ("few", few),
        ("many", many),
        (CatchallKey("other"), other),
    ]
    return SelectMessage(
        declarations={var_name: Expression(VariableRef(var_name), "number")},
        selectors=(VariableRef(var_name),),
        variants={
            (key,): pattern for key, pattern in raw_variants if pattern is not None
        },
    )
