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

import dataclasses
from copy import deepcopy
from logging import getLogger
from os.path import isdir, isfile, join
from typing import Any, Callable, Union

from moz.l10n.formats import Format, detect_format
from moz.l10n.model import (
    CatchallKey,
    Entry,
    Expression,
    Id,
    Markup,
    Message,
    Metadata,
    Pattern,
    PatternMessage,
    Resource,
    Section,
    SelectMessage,
)
from moz.l10n.paths import L10nConfigPaths, L10nDiscoverPaths
from moz.l10n.paths.android_locale import get_android_locale
from moz.l10n.resource import parse_resource, serialize_resource

from .utils import MigrationContext, get_entry, get_pattern, insert_entry_after

log = getLogger(__name__)

all_migrations: list[Migrate] = []

MigrationResult = Union[str, Message, Entry[Message]]
MigrationFunction = Callable[
    [Resource[Message], MigrationContext],
    Union[
        MigrationResult,
        tuple[MigrationResult, Union[set[str], set[Id]]],
        None,
    ],
]


class Migrate:
    parse_options: dict[str, Any]
    paths: L10nConfigPaths | L10nDiscoverPaths | None = None

    def __init__(
        self,
        map: dict[str, dict[tuple[str, ...] | str, MigrationFunction]],
        paths: str | L10nConfigPaths | L10nDiscoverPaths | None = None,
        **parse_options: Any,
    ) -> None:
        """
        Define a migration that adds entries according to `map` to resources in `paths`.

        This is primarily intended to be called from a migration script,
        which is then processed with the `l10n-migrate` CLI command.

        `map` is a mapping of resource reference paths to entry identifiers
        to functions that define their values;
        the function will be called with two positional arguments
        `(resource, context: MigrationContext)`.

        Functions defining new entries should return a Message, an Entry,
        or a tuple consisting of one of those along with a set of identifiers
        for entries after which the new entry should be inserted.

        If `paths` is a string, it needs to be either a path to a directory
        or a path to an l10n config file.
        This may also be set by an `l10n-migrate` CLI argument.
        """
        self.map = map
        if paths is not None:
            self.set_paths(paths)
        self.parse_options = parse_options
        all_migrations.append(self)

    def set_paths(self, paths: str | L10nConfigPaths | L10nDiscoverPaths) -> None:
        """
        If `paths` is a string, it needs to be either a path to a directory
        or a path to an l10n config file.
        """
        if isinstance(paths, (L10nConfigPaths, L10nDiscoverPaths)):
            self.paths = paths
        elif isdir(paths):
            self.paths = L10nDiscoverPaths(paths)
        elif isfile(paths):
            self.paths = L10nConfigPaths(
                paths, locale_map={"android_locale": get_android_locale}
            )
        else:
            raise ValueError(f"Not found: {paths}")

    def apply(self, dry_run: bool = False) -> dict[str, list[Entry[Message]]]:
        """
        Adds entries according to `map` to resources in `paths`.

        If an entry already exists with the target identifier, it is not modified.

        If no resource exists for a target locale, one is created.
        For .ini, JSON, and XML-based resources,
        the reference resource must exist to create a new resource.

        Returns a mapping of (localized) resource paths
        to a list of message entries added to it.
        """
        if self.paths is None:
            raise ValueError("Paths not set")
        all_changes: dict[str, list[Entry[Message]]] = {}
        for ref_path, res_add_entries in self.map.items():
            tgt_fmt, locales = self.paths.target(ref_path)
            if tgt_fmt is None:
                raise ValueError(f"Invalid reference path: {ref_path}")

            src_res: Resource[Message] | None = None
            for locale in locales:
                ctx = MigrationContext(self.paths, ref_path, locale, self.parse_options)
                res = ctx.get_resource(ref_path)
                if res is None:
                    if src_res is None:
                        src_res = _get_empty_resource(
                            join(self.paths.ref_root, ref_path)
                        )
                        if src_res is None:
                            log.warning(
                                f"Failed to parse source resource for {ref_path} (required for {locale})"
                            )
                            continue
                    res = deepcopy(src_res)

                changed: list[Entry[Message]] = []
                for id, create in res_add_entries.items():
                    ctx._update(id)
                    new_entry = _create_entry(res, ctx, create)
                    if new_entry is not None:
                        changed.append(new_entry)

                if changed:
                    nc = len(changed)
                    entries = "1 entry" if nc == 1 else f"{nc} entries"
                    log.info(f"Adding {entries} to {ref_path} for locale {locale}")
                    tgt_path = self.paths.format_target_path(tgt_fmt, locale)
                    if not dry_run:
                        with open(tgt_path, "w", encoding="utf-8") as file:
                            for line in serialize_resource(res):
                                file.write(line)
                    all_changes[tgt_path] = changed
        return all_changes


def copy(
    ref_path: None | str,
    id: tuple[str, ...] | str,
    *,
    property: str | None = None,
    replace: Callable[[Expression | Markup | str], Expression | Markup | str | None]
    | None = None,
    value_only: bool = False,
    variant: tuple[str | CatchallKey, ...] | str | None = None,
) -> MigrationFunction:
    """
    Create a copy migration function, from entry `id` in `ref_path`.

    If `ref_path` is None, the entry is copied from the current Resource.

    If `property` is set, the Message of the specified property is copied.
    Similarly, if `value_only` is set, only the `.value` Message is copied.

    If `variant` is set and the Message is a SelectMessage,
    the pattern of the specified variant is copied (or the default one).

    To change a message during the copy, define a `replace` function.
    It may mutate each placeholder directly,
    or return a non-None value to use as its replacement.
    To remove a placeholder, return an empty string.
    """
    if isinstance(id, str):
        id = (id,)
    if value_only and property:
        raise ValueError("value_only and property must not be set at the same time")

    def copy_(
        res: Resource[Message], ctx: MigrationContext
    ) -> tuple[Entry[Message] | Message, set[tuple[str, ...]]] | None:
        if ref_path:
            res_ = ctx.get_resource(ref_path)
            if res_ is None:
                log.debug(f"Copy-from resource not found: {ctx.pretty_id(id)}")
                return None
        else:
            res_ = res

        if property is None and variant is None:
            entry = get_entry(res_, *id)
            if entry:
                entry = deepcopy(entry)
                _replace_placeholders(entry, replace)
                return (entry.value if value_only else entry, {id})
            else:
                log.debug(f"Copy-from entry not found: {ctx.pretty_id(id)}")
                return None

        try:
            pattern = get_pattern(res_, *id, property=property, variant=variant)
            pattern = deepcopy(pattern)
            _replace_placeholders(pattern, replace)
            return PatternMessage(pattern), {id}
        except StopIteration:
            pp = f"property {property}" if property else ""
            pv = f"variant {variant}" if variant else ""
            pk = f"{pp}, {pv}" if property and variant else pp or pv
            log.debug(f"Copy-from pattern for {pk} not found: {ctx.pretty_id(id)}")
            return None

    return copy_


def _replace_placeholders(
    msg: Entry[Message] | Message | Pattern,
    replace: Callable[[Expression | Markup | str], Expression | Markup | str | None]
    | None,
) -> None:
    if not replace:
        pass
    elif isinstance(msg, Entry):
        _replace_placeholders(msg.value, replace)
        for prop in msg.properties.values():
            _replace_placeholders(prop, replace)
    elif isinstance(msg, SelectMessage):
        for variant in msg.variants.values():
            _replace_placeholders(variant, replace)
    else:
        pattern = msg.pattern if isinstance(msg, PatternMessage) else msg
        for idx, ph in enumerate(pattern):
            res = replace(ph)
            if res is not None:
                pattern[idx] = res


def entry(
    value: MigrationFunction | MigrationResult | None = None,
    properties: dict[str, MigrationFunction | MigrationResult] | None = None,
    *,
    allow_partial: bool = False,
    comment: str | None = None,
    meta: list[Metadata] | None = None,
) -> MigrationFunction:
    """
    Create a new Entry, from any number of source messages.

    With non-callable `value` and `properties`,
    the same message will be used for all locales.

    If `allow_partial` is False,
    None will be returned if any MigrationFunction return None.

    If `comment` and `meta` are None and `value` resolves to an Entry,
    its `comment` and `meta` (if any) are included in the result.
    """

    def entry_(
        res: Resource[Message],
        ctx: MigrationContext,
    ) -> tuple[Entry[Message], set[Id]] | None:
        insert_after: set[Id] = set()
        comment_ = comment
        meta_ = meta
        if callable(value):
            value_ = value(res, ctx)
            if isinstance(value_, tuple):
                insert_after.update(
                    k if isinstance(k, tuple) else (k,) for k in value_[1]
                )
                value_ = value_[0]
        else:
            value_ = value
        if isinstance(value_, Entry):
            if comment_ is None:
                comment_ = value_.comment
            if meta_ is None:
                meta_ = value_.meta
            value_ = value_.value
        if value_ is None:
            if not allow_partial and value is not None:
                log.debug(f"Entry value not found for {ctx}")
                return None
        if isinstance(value_, str):
            value_ = PatternMessage([value_])

        properties_: dict[str, Message] = {}
        if properties:
            for name, prop in properties.items():
                if callable(prop):
                    prop_res = prop(res, ctx)
                    if prop_res is None:
                        log.debug(f"Entry property {name} not found for {ctx}")
                        if allow_partial:
                            continue
                        else:
                            return None
                    if isinstance(prop_res, tuple):
                        insert_after.update(
                            k if isinstance(k, tuple) else (k,) for k in prop_res[1]
                        )
                        prop = prop_res[0]
                    else:
                        prop = prop_res
                if isinstance(prop, str):
                    prop = PatternMessage([prop])
                elif isinstance(prop, Entry):
                    prop = prop.value
                properties_[name] = prop

        if not value_ and not properties_:
            return None

        entry = Entry(
            ctx.target_id,
            value_ or PatternMessage([]),
            properties_,
            comment_ or "",
            meta_ or [],
        )
        return (entry, insert_after)

    return entry_


def _get_empty_resource(path: str) -> Resource[Message] | None:
    format = detect_format(path)

    res = None
    if format is None or format in {Format.ini, Format.xliff}:
        try:
            res = parse_resource(path)
            format = res.format
            assert format
        except Exception:
            return None

    if format in {Format.ini, Format.xliff}:
        assert res
        for section in res.sections:
            section.entries.clear()
        return res

    return Resource(format, [Section((), [])])


def _create_entry(
    res: Resource[Message],
    ctx: MigrationContext,
    create: Callable[
        [Resource[Message], MigrationContext],
        MigrationResult
        | tuple[MigrationResult, set[tuple[str, ...]] | set[str]]
        | None,
    ],
) -> Entry[Message] | None:
    """
    Adds an entry to `res`, created with the `create` function.

    The `create` function will be called with two arguments `(res: Resource, ctx: MigrationContext)`.
    It should return a Message, an Entry, or a tuple consisting of one of those,
    along with a set of identifiers for entries after which the new entry should be inserted.

    If an entry already exists, it is not modified.

    Returns `Entry[Message]` on success.
    """

    if get_entry(res, *ctx.target_id) is not None:
        log.info(f"Already defined: {ctx}")
        return None

    try:
        src_entry = create(res, ctx)
    except StopIteration:
        log.info(f"Source not found: {ctx}")
        return None

    if src_entry is None:
        return None

    if isinstance(src_entry, tuple):
        src_ids = src_entry[1]
        src_entry = src_entry[0]
    elif isinstance(src_entry, Entry):
        src_ids = {src_entry.id}
    else:
        src_ids = {ctx.target_id}

    # For .ini and .xliff, new_entry.id will initially contain the section id.
    # This is dropped later in insert_entry_after().
    if isinstance(src_entry, Entry):
        new_entry = dataclasses.replace(src_entry, id=ctx.target_id)
    elif isinstance(src_entry, (PatternMessage, SelectMessage)):
        new_entry = Entry(ctx.target_id, src_entry)
    else:
        raise ValueError(f"Unsupported entry type {type(src_entry)}: {ctx}")

    insert_entry_after(res, new_entry, *src_ids, *ctx._prev_ids)
    return new_entry
