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

from collections.abc import Iterator
from dataclasses import dataclass, field
from typing import Generic, Literal, TypeVar, Union

from .formats import Format
from .util.normalize import _normalize_declarations, _normalize_pattern

__all__ = [
    "CatchallKey",
    "Comment",
    "Entry",
    "Expression",
    "Id",
    "Markup",
    "Message",
    "Metadata",
    "Pattern",
    "PatternMessage",
    "Resource",
    "Section",
    "SelectMessage",
    "VariableRef",
]


@dataclass
class VariableRef:
    name: str

    def __repr__(self) -> str:
        return f"VariableRef({self.name!r})"


@dataclass
class Expression:
    """
    A valid Expression must contain a non-None `arg`, `function`, or both.

    An Expression with no `function` and non-empty `options` is not valid.
    """

    arg: str | VariableRef | None
    function: str | None = None
    options: dict[str, str | VariableRef] = field(default_factory=dict)
    attributes: dict[str, str | Literal[True]] = field(default_factory=dict)

    def variable_refs(self) -> Iterator[VariableRef]:
        """
        VariableRef values in this Expression
        """
        if isinstance(self.arg, VariableRef):
            yield self.arg
        for opt in self.options.values():
            if isinstance(opt, VariableRef):
                yield opt

    def __repr__(self) -> str:
        body = [repr(self.arg)]
        if self.function:
            body.append(f"function={self.function!r}")
        if self.options:
            body.append(f"options={self.options!r}")
        if self.attributes:
            body.append(f"attributes={self.attributes!r}")
        return f"Expression({','.join(body)})"


@dataclass
class Markup:
    kind: Literal["open", "standalone", "close"]
    name: str
    options: dict[str, str | VariableRef] = field(default_factory=dict)
    attributes: dict[str, str | Literal[True]] = field(default_factory=dict)

    def variable_refs(self) -> Iterator[VariableRef]:
        """
        VariableRef values in this Markup
        """
        for opt in self.options.values():
            if isinstance(opt, VariableRef):
                yield opt

    def __repr__(self) -> str:
        body = [repr(self.kind), repr(self.name)]
        if self.options:
            body.append(f"options={self.options!r}")
        if self.attributes:
            body.append(f"attributes={self.attributes!r}")
        return f"Expression({','.join(body)})"


Pattern = list[Union[str, Expression, Markup]]
"""
A linear sequence of text and placeholders corresponding to potential output of a message.

String values represent literal text.
String values include all processing of the underlying text values, including escape sequence processing.
"""


@dataclass
class PatternMessage:
    """
    A message without selectors and with a single pattern.
    """

    pattern: Pattern
    declarations: dict[str, Expression] = field(default_factory=dict)

    def is_empty(self) -> bool:
        """
        Is the message's pattern empty, or consists only of empty strings?
        """
        return all(el == "" for el in self.pattern)

    def normalize(self) -> PatternMessage:
        """
        Drop unused declarations,
        join adjacent literal elements,
        and drop empty literal elements.

        Mutates this PatternMessage.
        """
        var_refs: set[str] = set()
        _normalize_pattern(self.pattern, var_refs)
        _normalize_declarations(self, var_refs)
        return self

    def __repr__(self) -> str:
        body = (
            f"declarations={self.declarations!r},pattern={self.pattern!r}"
            if self.declarations
            else repr(self.pattern)
        )
        return f"PatternMessage({body})"


@dataclass
class CatchallKey:
    value: str | None = field(default=None, compare=False)
    """
    An optional string identifier for the default/catch-all variant.
    """

    def __hash__(self) -> int:
        """
        Consider all catchall-keys as equivalent to each other
        """
        return 1

    def __str__(self) -> str:
        return self.value or ""

    def __repr__(self) -> str:
        return f"CatchallKey({self.value!r})"


@dataclass
class SelectMessage:
    """
    A message with one or more selectors and a corresponding number of variants.
    """

    declarations: dict[str, Expression]
    selectors: tuple[VariableRef, ...]
    variants: dict[tuple[str | CatchallKey, ...], Pattern]

    def is_empty(self) -> bool:
        """
        Are all the message's patterns empty, or consist only of empty strings?
        """
        return all(
            all(el == "" for el in pattern) for pattern in self.variants.values()
        )

    def normalize(self) -> SelectMessage:
        """
        Drop unused declarations,
        join adjacent literal elements,
        and drop empty literal elements in all patterns.

        Mutates this SelectMessage.
        """
        var_refs = set(sel.name for sel in self.selectors)
        for pattern in self.variants.values():
            _normalize_pattern(pattern, var_refs)
        _normalize_declarations(self, var_refs)
        return self

    def selector_expressions(self) -> tuple[Expression, ...]:
        return tuple(self.declarations[var.name] for var in self.selectors)


Message = Union[PatternMessage, SelectMessage]


@dataclass
class LinePos:
    """
    The source line position of an entry or section header.
    """

    start: int
    """
    The starting line of the entry or section.
    May be less than `value` if preceded by a comment.
    """

    key: int
    """
    The start line of the entry or section header key or name.
    """

    value: int
    """
    The start line of the entry pattern or section header.
    """

    end: int
    """
    The line one past the end of the entry or section header.
    """

    def __repr__(self) -> str:
        body = [repr(self.start), repr(self.key), repr(self.value), repr(self.end)]
        return f"LinePos({','.join(body)})"


@dataclass
class Metadata:
    """
    Metadata is attached to a resource, section, or a single entry.

    The type parameter defines the metadata value type.
    """

    key: str
    """
    A non-empty string keyword.

    Most likely a sequence of `a-z` characters,
    but may technically contain any characters
    which might require escaping in the syntax.
    """

    value: str
    """
    The metadata contents.

    Values have all their character \\escapes processed.
    """

    def __repr__(self) -> str:
        return f"Metadata({self.key!r},{self.value!r})"


@dataclass
class Comment:
    comment: str
    """
    A standalone comment.

    May contain multiple lines separated by newline characters.
    Lines should have any comment-start sigil and up to one space trimmed from the start,
    along with any trailing whitespace.

    An empty or whitespace-only comment will be represented by an empty string.
    """

    linepos: LinePos | None = field(default=None, compare=False)
    """
    The parsed position of the comment,
    available for some formats.
    """

    def __repr__(self) -> str:
        body = [repr(self.comment)]
        if self.linepos:
            body.append(f"linepos={self.linepos!r}")
        return f"Comment({','.join(body)})"


V_co = TypeVar("V_co", bound=Union[Message, str], covariant=True)
"""
The Message value type.
"""

Id = tuple[str, ...]
"""
An entry or section identifier.
"""


class _WithMeta:
    meta: list[Metadata]

    def get_meta(self, key: str) -> str | None:
        """
        Get the value of the first metadata entry with a matching `key`, if any.
        """
        return next((m.value for m in self.meta if m.key == key), None)

    def has_meta(self, key: str, value: str | None = None) -> bool:
        """
        Returns True if any metadata entry has a matching `key`
        and, if not `None`, a matching `value`.
        """
        return any(
            m.key == key and (value is None or m.value == value) for m in self.meta
        )

    def set_meta(self, key: str, value: str) -> None:
        """
        Set the value of the first metadata entry with a matching `key`,
        or add a new metadata entry if no matching entry exists.
        """
        prev = next((m for m in self.meta if m.key == key), None)
        if prev is None:
            self.meta.append(Metadata(key, value))
        else:
            prev.value = value

    def del_meta(self, key: str) -> int:
        """
        Remove metadata entries with a matching `key`.
        Returns the number of removed entries.
        """
        n = len(self.meta)
        self.meta = [m for m in self.meta if m.key != key]
        return n - len(self.meta)


@dataclass
class Entry(Generic[V_co], _WithMeta):
    """
    A message entry.

    The first type parameter defines the Message value type,
    and the second one defines the metadata value type.
    """

    id: Id
    """
    The entry identifier.

    This MUST be a non-empty tuple of non-empty `string` values.

    The entry identifiers are not normalized,
    i.e. they do not include this identifier.

    In a valid resource, each entry has a distinct normalized identifier,
    i.e. the concatenation of its section header identifier (if any) and its own.
    """

    value: V_co
    """
    The value of an entry, i.e. the message.

    String values have all their character escapes processed.
    """

    properties: dict[str, V_co] = field(default_factory=dict)
    """
    Additional values for an entry,
    such as the attributes of a Fluent term or message.

    String values have all their character escapes processed.
    """

    comment: str = ""
    """
    A comment on this entry.

    May contain multiple lines separated by newline characters.
    Lines should have any comment-start sigil and up to one space trimmed from the start,
    along with any trailing whitespace.

    An empty or whitespace-only comment will be represented by an empty string.
    """

    meta: list[Metadata] = field(default_factory=list)
    """
    Metadata attached to this entry.
    """

    linepos: LinePos | None = field(default=None, compare=False)
    """
    The parsed position of the entry,
    available for some formats.
    """

    def __repr__(self) -> str:
        body = [repr(self.id)]
        if self.comment or self.meta:
            if self.comment:
                body.append(f"comment={self.comment!r}")
            if self.meta:
                body.append(f"meta={self.meta!r}")
            body.append(f"value={self.value!r}")
        else:
            body.append(repr(self.value))
        if self.properties:
            body.append(f"properties={self.properties!r}")
        if self.linepos:
            body.append(f"linepos={self.linepos!r}")
        return f"Entry({','.join(body)})"


@dataclass
class Section(Generic[V_co], _WithMeta):
    """
    A section of a resource.

    The first type parameter defines the Message value type,
    and the second one defines the metadata value type.
    """

    id: Id
    """
    The section identifier.

    Each `string` part of the identifier MUST be a non-empty string.

    The top-level or anonymous section has an empty `id` array.
    The resource syntax requires this array to be non-empty
    for all sections after the first one,
    but empty identifier arrays MAY be used
    when this data model is used to represent other message resource formats,
    such as Fluent FTL files.

    The entry identifiers are not normalized,
    i.e. they do not include this identifier.
    """

    entries: list[Entry[V_co] | Comment]
    """
    Section entries consist of message entries and comments.

    Empty lines are not included in the data model.
    """

    comment: str = ""
    """
    A comment on the whole section, which applies to all of its entries.

    May contain multiple lines separated by newline characters.
    Lines should have any comment-start sigil and up to one space trimmed from the start,
    along with any trailing whitespace.

    An empty or whitespace-only comment will be represented by an empty string.
    """

    meta: list[Metadata] = field(default_factory=list)
    """
    Metadata attached to this section.
    """

    linepos: LinePos | None = field(default=None, compare=False)
    """
    The parsed position of the section,
    available for some formats.
    """

    def __repr__(self) -> str:
        body = [repr(self.id)]
        if self.comment or self.meta:
            if self.comment:
                body.append(f"comment={self.comment!r}")
            if self.meta:
                body.append(f"meta={self.meta!r}")
            body.append(f"entries={self.entries!r}")
        else:
            body.append(repr(self.entries))
        if self.linepos:
            body.append(f"linepos={self.linepos!r}")
        return f"Section({','.join(body)})"


@dataclass
class Resource(Generic[V_co], _WithMeta):
    """
    A message resource.

    The first type parameter defines the Message value type,
    and the second one defines the metadata value type.
    """

    format: Format | None
    """
    The serialization format for the resource, if any.
    """

    sections: list[Section[V_co]]
    """
    The body of a resource, consisting of an array of sections.

    A valid resource may have an empty sections array.
    """

    comment: str = ""
    """
    A comment on the whole resource, which applies to all of its sections and entries.

    May contain multiple lines separated by newline characters.
    Lines should have any comment-start sigil and up to one space trimmed from the start,
    along with any trailing whitespace.

    An empty or whitespace-only comment will be represented by an empty string.
    """

    meta: list[Metadata] = field(default_factory=list)
    """
    Metadata attached to the whole resource.
    """

    def all_entries(self) -> Iterator[Entry[V_co]]:
        """
        All entries in all resource sections.
        """
        return (
            entry
            for section in self.sections
            for entry in section.entries
            if isinstance(entry, Entry)
        )

    def __repr__(self) -> str:
        body = [f"Format.{self.format.name}" if self.format else "None"]
        if self.comment or self.meta:
            if self.comment:
                body.append(f"comment={self.comment!r}")
            if self.meta:
                body.append(f"meta={self.meta!r}")
            body.append(f"sections={self.sections!r}")
        else:
            body.append(repr(self.sections))
        return f"Resource({','.join(body)})"
