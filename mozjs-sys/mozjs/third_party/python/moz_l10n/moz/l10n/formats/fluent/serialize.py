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

from collections.abc import Callable, Iterator
from re import compile, fullmatch
from typing import Any

from fluent.syntax import FluentSerializer
from fluent.syntax import ast as ftl
from fluent.syntax.serializer import serialize_expression

from ...model import (
    CatchallKey,
    Comment,
    Entry,
    Expression,
    Message,
    Metadata,
    Pattern,
    PatternMessage,
    Resource,
    Section,
    SelectMessage,
    VariableRef,
)

ws_at_start = compile(r"\s+")
ws_at_end = compile(r"\s+$")


def fluent_serialize(
    resource: (Resource[str] | Resource[Message]),
    *,
    escape_syntax: bool = True,
    serialize_metadata: Callable[[Metadata], str | None] | None = None,
    trim_comments: bool = False,
) -> Iterator[str]:
    """
    Serialize a resource as the contents of a Fluent FTL file.

    Section identifiers and multi-part message identifiers are not supported.

    Function names are upper-cased, and expressions using the `message` function
    are mapped to message and term references.

    If `escape_syntax` is set to `False`,
    syntax characters such as { and } in text elements are not escaped.

    Yields each entry and comment separately.
    If the resource includes any metadata, a `serialize_metadata` callable must be provided
    to map each field into a comment value, or to discard it by returning an empty value.
    """
    ftl_ast = fluent_astify(
        resource,
        escape_syntax=escape_syntax,
        serialize_metadata=serialize_metadata,
        trim_comments=trim_comments,
    )
    serializer = FluentSerializer()
    nl_prefix = 0
    for entry in ftl_ast.body:
        yield serializer.serialize_entry(entry, nl_prefix)
        if not nl_prefix:
            nl_prefix = 1


def fluent_astify(
    resource: (Resource[str] | Resource[Message]),
    *,
    escape_syntax: bool = True,
    serialize_metadata: Callable[[Metadata], str | None] | None = None,
    trim_comments: bool = False,
) -> ftl.Resource:
    """
    Transform a resource into a corresponding Fluent AST structure.

    Section identifiers and multi-part message identifiers are not supported.

    Function names are upper-cased, and annotations with the `message` function
    are mapped to message and term references.

    If `escape_syntax` is set to `False`,
    syntax characters such as { and } in text elements are not escaped.

    If the resource includes any metadata other than a string resource `info` value,
    a `serialize_metadata` callable must be provided
    to map each field into a comment value, or to discard it by returning an empty value.
    """

    def comment_str(
        node: (Resource[Any] | Section[Any] | Entry[Any] | Comment),
    ) -> str:
        if trim_comments:
            return ""
        cs = node.comment.rstrip()
        if not isinstance(node, Comment) and node.meta:
            if not serialize_metadata:
                raise ValueError("Metadata requires serialize_metadata parameter")
            for field in node.meta:
                if (
                    isinstance(node, Resource)
                    and field.key == "info"
                    and field == node.meta[0]
                ):
                    continue
                meta_str = serialize_metadata(field)
                if meta_str:
                    ms = meta_str.strip("\n")
                    cs = f"{cs}\n{ms}" if cs else ms
        return cs

    body: list[ftl.EntryType] = []
    res_info = resource.meta[0] if resource.meta else None
    if (
        not trim_comments
        and res_info
        and res_info.key == "info"
        and isinstance(res_info.value, str)
        and res_info.value
    ):
        body.append(ftl.Comment(res_info.value))
        res_comment = resource.comment.rstrip()
    else:
        res_comment = comment_str(resource)
    if res_comment:
        body.append(ftl.ResourceComment(res_comment))
    for idx, section in enumerate(resource.sections):
        section_comment = comment_str(section)
        if (not trim_comments and idx != 0) or section_comment:
            body.append(ftl.GroupComment(section_comment))
        for entry in section.entries:
            if isinstance(entry, Comment):
                if not trim_comments:
                    body.append(ftl.Comment(entry.comment))
            else:
                body.append(
                    fluent_astify_entry(
                        entry, comment_str=comment_str, escape_syntax=escape_syntax
                    )
                )
    return ftl.Resource(body)


def fluent_serialize_entry(
    entry: Entry[str | Message],
    *,
    comment_str: Callable[[Entry[Any]], str] | None = None,
    escape_syntax: bool = True,
) -> str:
    """
    Serialize an Entry as a Fluent FTL message or term.

    Function names are upper-cased, and expressions using the `message` function
    are mapped to message and term references.

    If `escape_syntax` is set to `False`,
    syntax characters such as { and } in text elements are not escaped.
    """
    ftl_entry = fluent_astify_entry(
        entry, comment_str=comment_str, escape_syntax=escape_syntax
    )
    return FluentSerializer().serialize_entry(ftl_entry)


def fluent_astify_entry(
    entry: Entry[str | Message],
    *,
    comment_str: Callable[[Entry[Any]], str] | None = None,
    escape_syntax: bool = True,
) -> ftl.Message | ftl.Term:
    """
    Transform an Entry into a corresponding Fluent AST message or term.

    Function names are upper-cased, and expressions using the `message` function
    are mapped to message and term references.

    If `escape_syntax` is set to `False`,
    syntax characters such as { and } in text elements are not escaped.
    """

    if len(entry.id) != 1:
        raise ValueError(f"Unsupported message id: {entry.id}")
    id = entry.id[0]
    is_term = id.startswith("-")
    value = fluent_astify_message(
        entry.value, escape_empty=is_term, escape_syntax=escape_syntax
    )
    attributes = list(
        ftl.Attribute(
            ftl.Identifier(key),
            fluent_astify_message(val, escape_empty=True, escape_syntax=escape_syntax),
        )
        for key, val in entry.properties.items()
    )
    if comment_str is None:
        if entry.meta:
            raise ValueError("Metadata requires custom serializer")
        c_str = entry.comment.rstrip()
    else:
        c_str = comment_str(entry)
    comment = ftl.Comment(c_str) if c_str else None
    if is_term:
        return ftl.Term(ftl.Identifier(id[1:]), value, attributes, comment)
    else:
        if pattern_is_empty(value):
            value_ = None if attributes else valid_empty_pattern(value)
        else:
            value_ = value
        return ftl.Message(ftl.Identifier(id), value_, attributes, comment)


def fluent_serialize_message(
    message: str | Message | ftl.Pattern,
    *,
    escape_empty: bool = False,
    escape_syntax: bool = True,
) -> str:
    """
    Serialize a message as a Fluent pattern.

    Function names are upper-cased, and expressions using the `message` function
    are mapped to message and term references.

    If `escape_empty` is set to `True`, a message that would serialize as an empty string
    will instead be serialized as `{ "" }`.

    If `escape_syntax` is set to `False`,
    syntax characters such as { and } in text elements are not escaped.
    """
    pattern = (
        message
        if isinstance(message, ftl.Pattern)
        else fluent_astify_message(
            message, escape_empty=escape_empty, escape_syntax=escape_syntax
        )
    )
    return "".join(
        el.value if isinstance(el, ftl.TextElement) else serialize_expression(el)
        for el in pattern.elements
    )


def fluent_astify_message(
    message: str | Message,
    *,
    escape_empty: bool = False,
    escape_syntax: bool = True,
) -> ftl.Pattern:
    """
    Transform a message into a corresponding Fluent AST pattern.

    Function names are upper-cased, and expressions using the `message` function
    are mapped to message and term references.

    If `escape_empty` is set to `True`, a Pattern that would serialize as an empty string
    will instead be escaped as a StringLiteral.

    If `escape_syntax` is set to `False`,
    syntax characters such as { and } in text elements are not escaped.
    """

    if isinstance(message, (str, PatternMessage)):
        pattern = (
            ftl.Pattern(text(message, escape_syntax))
            if isinstance(message, str)
            else flat_pattern(message.declarations, message.pattern, escape_syntax)
        )
        if escape_empty and pattern_is_empty(pattern):
            return valid_empty_pattern(pattern)
        return pattern

    if not isinstance(message, SelectMessage):
        raise ValueError(f"Unsupported message: {message}")

    # It gets a bit complicated for SelectMessage. We'll be modifying this list,
    # building select expressions for each selector starting from the last one
    # until this list has only one entry `[[], pattern]`.
    #
    # We rely on the variants being in order, so that a variant with N keys
    # will be next to all other variants for which the first N-1 keys are equal.
    variants = [
        (list(keys), flat_pattern(message.declarations, value, escape_syntax))
        for keys, value in message.variants.items()
    ]

    other = fallback_name(message)
    keys0 = variants[0][0]
    while keys0:
        selector = value(message.declarations, message.selectors[len(keys0) - 1])
        if (
            isinstance(selector, ftl.FunctionReference)
            and selector.id.name == "NUMBER"
            and selector.arguments.positional
            and isinstance(selector.arguments.positional[0], ftl.VariableReference)
            and not selector.arguments.named
        ):
            selector = selector.arguments.positional[0]
        base_keys = []
        sel_exp = None
        i = 0
        while i < len(variants):
            keys, pattern = variants[i]
            key = keys.pop()  # Ultimately modifies keys0
            ftl_variant = ftl.Variant(
                variant_key(key, other), pattern, isinstance(key, CatchallKey)
            )
            if sel_exp and keys == base_keys:
                sel_exp.variants.append(ftl_variant)
                variants.pop(i)
            else:
                base_keys = keys
                sel_exp = ftl.SelectExpression(selector.clone(), [ftl_variant])
                variants[i] = (keys, ftl.Pattern([ftl.Placeable(sel_exp)]))
                i += 1
    if len(variants) != 1:
        raise ValueError(f"Error resolving select message variants (n={len(variants)})")
    return variants[0][1]


def fallback_name(message: SelectMessage) -> str:
    """
    Try `other`, `other1`, `other2`, ... until a free one is found.
    """
    i = 0
    key = root = "other"
    while any(key == str(k) for keys in message.variants for k in keys):
        i += 1
        key = f"{root}{i}"
    return key


def variant_key(
    key: str | CatchallKey, other: str
) -> ftl.NumberLiteral | ftl.Identifier:
    kv = str(key) or other
    try:
        float(kv)
        return ftl.NumberLiteral(kv)
    except Exception:
        if fullmatch(r"[a-zA-Z][\w-]*", kv):
            return ftl.Identifier(kv)
        raise ValueError(f"Unsupported variant key: {kv}")


def flat_pattern(
    decl: dict[str, Expression], pattern: Pattern, escape_syntax: bool
) -> ftl.Pattern:
    elements: list[ftl.TextElement | ftl.Placeable] = []
    last = len(pattern) - 1
    for idx, el in enumerate(pattern):
        if isinstance(el, str):
            if idx == 0:
                ws = ws_at_start.match(el)
                if ws is not None:
                    ws_end = ws.end()
                    elements.append(ftl.Placeable(value(decl, el[:ws_end])))
                    if ws_end == len(el):
                        continue
                    el = el[ws_end:]
            if idx == last:
                ws = ws_at_end.search(el)
                if ws is not None:
                    ws_start = ws.start()
                    if ws_start > 0:
                        elements += text(el[:ws_start], escape_syntax)
                    elements.append(ftl.Placeable(value(decl, el[ws_start:])))
                    continue
            elements += text(el, escape_syntax)
        elif isinstance(el, Expression):
            elements.append(ftl.Placeable(expression(decl, el)))
        else:
            raise ValueError(f"Conversion to Fluent not supported: {el}")
    return ftl.Pattern(elements)


def pattern_is_empty(pattern: ftl.Pattern) -> bool:
    return all(
        isinstance(el, ftl.TextElement) and (el.value == "" or el.value.isspace())
        for el in pattern.elements
    )


def valid_empty_pattern(pattern: ftl.Pattern) -> ftl.Pattern:
    """
    Fluent patterns are not valid if they serialize to an empty string.

    This is called only if `pattern_is_empty(pattern)` is `True`.
    """
    empty_text = "".join(
        el.value for el in pattern.elements if isinstance(el, ftl.TextElement)
    )
    return ftl.Pattern([ftl.Placeable(value({}, empty_text))])


not_text = compile(r"[{}]+|\n *[*.[]")


def text(source: str, escape_syntax: bool) -> list[ftl.TextElement | ftl.Placeable]:
    if not escape_syntax:
        return [ftl.TextElement(source)]
    elements: list[ftl.TextElement | ftl.Placeable] = []
    idx = 0
    for m in not_text.finditer(source):
        start = m.start()
        esc = m[0].lstrip()
        start += len(m[0]) - len(esc)
        if start > idx:
            elements.append(ftl.TextElement(source[idx:start]))
        elements.append(ftl.Placeable(ftl.StringLiteral(esc)))
        idx = m.end()
    if idx < len(source):
        elements.append(ftl.TextElement(source[idx:]))
    return elements


def expression(
    decl: dict[str, Expression], expr: Expression, decl_name: str = ""
) -> ftl.InlineExpression:
    arg = value(decl, expr.arg, decl_name) if expr.arg is not None else None
    if expr.function:
        return function_ref(decl, arg, expr.function, expr.options)
    elif expr.function:
        raise ValueError("Unsupported annotations are not supported")
    if arg:
        return arg
    raise ValueError("Invalid empty expression")


def function_ref(
    decl: dict[str, Expression],
    arg: ftl.InlineExpression | None,
    function: str,
    options: dict[str, str | VariableRef],
) -> ftl.InlineExpression:
    named: list[ftl.NamedArgument] = []
    for name, val in options.items():
        ftl_val = value(decl, val)
        if isinstance(ftl_val, ftl.Literal):
            named.append(ftl.NamedArgument(ftl.Identifier(name), ftl_val))
        else:
            raise ValueError(f"Fluent option value not literal for {name}: {ftl_val}")

    if function == "string":
        if not arg:
            raise ValueError("Argument required for :string")
        if named:
            raise ValueError("Options on :string are not supported")
        return arg
    if function == "number" and isinstance(arg, ftl.NumberLiteral) and not named:
        return arg
    if function == "message":
        if not isinstance(arg, ftl.Literal):
            raise ValueError(
                "Message and term references must have a literal message identifier"
            )
        match = fullmatch(r"(-?[a-zA-Z][\w-]*)(?:\.([a-zA-Z][\w-]*))?", arg.value)
        if not match:
            raise ValueError(f"Invalid message or term identifier: {arg.value}")
        msg_id = match[1]
        msg_attr = match[2]
        attr = ftl.Identifier(msg_attr) if msg_attr else None
        if msg_id[0] == "-":
            args = ftl.CallArguments(named=named) if named else None
            return ftl.TermReference(ftl.Identifier(msg_id[1:]), attr, args)
        elif named:
            raise ValueError("Options on message references are not supported")
        else:
            return ftl.MessageReference(ftl.Identifier(msg_id), attr)

    args = ftl.CallArguments([arg] if arg else None, named)
    return ftl.FunctionReference(ftl.Identifier(function.upper()), args)


# Non-printable ASCII C0 & C1 / Unicode Cc characters
esc_cc = {n: f"\\u{n:04X}" for r in (range(0, 32), range(127, 160)) for n in r}
esc_bs = compile(r'([\\"])')


def value(
    decl: dict[str, Expression], val: str | VariableRef, decl_name: str = ""
) -> ftl.InlineExpression:
    if isinstance(val, str):
        try:
            float(val)
            return ftl.NumberLiteral(val)
        except Exception:
            return ftl.StringLiteral(esc_bs.sub(r"\\\1", val).translate(esc_cc))
    elif val.name != decl_name and val.name in decl:
        return expression(decl, decl[val.name], val.name)
    else:
        return ftl.VariableReference(ftl.Identifier(val.name))
