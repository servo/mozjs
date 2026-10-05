# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import re

# ruff linter deprecates List, Tuple required for Python 3.8 compatibility
from typing import Callable, Dict, List, Optional, Tuple  # noqa UP035

from tomlkit.items import Array, Table
from tomlkit.toml_document import TOMLDocument

from .ini import combine_fields

__all__ = ["read_toml", "alphabetize_toml_str", "add_skip_if", "sort_paths"]

CreateBug = Optional[Callable[[], object]]
ListStr = List[str]  # noqa UP006
OptArray = Optional[Array]
OptRegex = Optional[re.Pattern]
OptStr = Optional[str]
OptConditions = List[Tuple[str, OptStr]]  # noqa UP006
TupleStrBoolStr = Tuple[str, bool, str]  # noqa UP006
TupleOptStrListStr = Tuple[OptStr, ListStr]  # noqa UP006

FILENAME_REGEX = r"^([A-Za-z0-9_./-]*)([Bb][Uu][Gg])([-_]*)([0-9]+)([A-Za-z0-9_./-]*)$"
DEFAULT_SECTION = "DEFAULT"


def sort_paths_keyfn(k):
    sort_paths_keyfn.rx = getattr(sort_paths_keyfn, "rx", None)  # static
    if sort_paths_keyfn.rx is None:
        sort_paths_keyfn.rx = re.compile(FILENAME_REGEX)
    name = str(k)
    if name == DEFAULT_SECTION:
        return ""
    m = sort_paths_keyfn.rx.findall(name)
    if len(m) == 1 and len(m[0]) == 5:
        prefix = m[0][0]  # text before "Bug"
        bug = m[0][1]  # the word "Bug"
        underbar = m[0][2]  # underbar or dash (optional)
        num = m[0][3]  # the bug id
        suffix = m[0][4]  # text after the bug id
        name = f"{prefix}{bug.lower()}{underbar}{int(num):09d}{suffix}"
        return name
    return name


def sort_paths(paths):
    """
    Returns a list of paths (tests) in a manifest in alphabetical order.
    Ensures DEFAULT is first and filenames with a bug number are
    in the proper order.
    """
    return sorted(paths, key=sort_paths_keyfn)


def parse_toml_str(contents):
    """
    Parse TOML contents using rtoml (fastest), tomllib, or toml
    """
    try:
        import rtoml

        TOMLDecodeError = ValueError  # rtoml raises ValueError on parse errors
        TOMLloads = rtoml.loads
    except ImportError:
        try:
            from tomllib import TOMLDecodeError
            from tomllib import loads as TOMLloads
        except ImportError:
            from toml import TomlDecodeError as TOMLDecodeError
            from toml import loads as TOMLloads

    error = None
    manifest = None
    try:
        manifest = TOMLloads(contents)
    except TOMLDecodeError as pe:
        error = str(pe)
    return error, manifest


def parse_tomlkit_str(contents):
    """
    Parse TOML contents using tomlkit
    """
    import tomlkit
    from tomlkit.exceptions import TOMLKitError

    error = None
    manifest = None
    try:
        manifest = tomlkit.parse(contents)
    except TOMLKitError as pe:
        error = str(pe)
    return error, manifest


def read_toml(
    fp,
    defaults=None,
    default=DEFAULT_SECTION,
    _comments=None,
    _separators=None,
    strict=True,
    handle_defaults=True,
    document=False,
    add_line_no=False,
):
    """
    read a .toml file and return a list of [(section, values)]
    - fp : file pointer or path to read
    - defaults : default set of variables
    - default : name of the section for the default section
    - comments : characters that if they start a line denote a comment
    - separators : strings that denote key, value separation in order
    - strict : whether to be strict about parsing
    - handle_defaults : whether to incorporate defaults into each section
    - document: read TOML with tomlkit and return source in test["document"]
    - add_line_no: add the line number where the test name appears in the file to the source. Also, the document variable must be set to True for this flag to work. (This is used only to generate the documentation)
    """

    # variables
    defaults = defaults or {}
    default_section = {}
    sections = []
    if isinstance(fp, str):
        filename = fp
        fp = open(fp, encoding="utf-8")
    elif hasattr(fp, "name"):
        filename = fp.name
    else:
        filename = "unknown"
    contents = fp.read()
    inline_comment_rx = re.compile(r"\s#.*$")

    if document:  # Use tomlkit to parse the file contents
        error, manifest = parse_tomlkit_str(contents)
    else:
        error, manifest = parse_toml_str(contents)
    if error:
        raise OSError(f"Error parsing TOML manifest file {filename}: {error}")

    # handle each section of the manifest
    for section in manifest.keys():
        current_section = {}
        for key in manifest[section].keys():
            val = manifest[section][key]
            if isinstance(val, bool):  # must coerce to lowercase string
                if val:
                    val = "true"
                else:
                    val = "false"
            elif isinstance(val, list):
                new_vals = ""
                for v in val:
                    if len(new_vals) > 0:
                        new_vals += os.linesep
                    new_val = str(v).strip()  # coerce to str
                    comment_found = inline_comment_rx.search(new_val)
                    if comment_found:
                        new_val = new_val[0 : comment_found.span()[0]]
                    if " = " in new_val:
                        raise Exception(
                            f"Should not assign in {key} list condition for {section}"
                        )
                    new_vals += new_val
                val = new_vals
            else:
                val = str(val).strip()  # coerce to str
                comment_found = inline_comment_rx.search(val)
                if comment_found:
                    val = val[0 : comment_found.span()[0]]
                if " = " in val:
                    raise Exception(
                        f"Should not assign in {key} condition for {section}"
                    )
            current_section[key] = val
        if section.lower() == default.lower():
            default_section = current_section
            # DEFAULT does NOT appear in the output
        else:
            sections.append((section, current_section))

    # merge global defaults with the DEFAULT section
    defaults = combine_fields(defaults, default_section)
    if handle_defaults:
        # merge combined defaults into each section
        sections = [(i, combine_fields(defaults, j)) for i, j in sections]

    if document and add_line_no:
        # Take the line where the test name appears in the file.
        for i, _ in enumerate(sections):
            line = contents.split(sections[i][0])[0].count(os.linesep) + 1
            manifest.setdefault(sections[i][0], {})["lineno"] = str(line)
    elif not document:
        manifest = None

    return sections, defaults, manifest


def unused_condition(condition: str) -> bool:
    """true if condition does not exist in the CI infrastructure"""

    if condition.find("os == 'linux' && os_version == '22.04'") >= 0 and (
        condition.find(" asan") >= 0 or condition.find(" tsan") >= 0
    ):
        return True
    if condition.find("os == 'win'") >= 0 and condition.find(" tsan") >= 0:
        return True
    return False


def idiomatic_condition(cond: str, condition: str) -> TupleOptStrListStr:
    """
    Given the cond (skip-if, run-if)...
    Converts a manifest condtiion into an idiomatic form
    cond is None if the condition should be removed
    Returns (condition, new_conds) where
    - condition is the revised condition
    - new_conds are NEW conditions to add
    """

    AND: str = "&&"
    EQUAL: str = "=="
    NOT_EQUAL: str = "!="
    GREATER_EQUAL: str = ">="
    new_conds: ListStr = []
    ops: ListStr = condition.replace("processor", "arch").split(AND)
    single_ops: ListStr = []  # one word ops, no equality/comparison
    new_ops: ListStr = []
    new_op: OptStr = None
    permutations: ListStr = []  # like !debug -> asan,opt,tsan
    android_version: OptStr = None
    os: OptStr = None
    os_version: OptStr = None
    arch: OptStr = None
    display: OptStr = None
    k_seen: Dict = {}  # noqa UP006
    k: str = ""
    v: str = ""
    i: int = 0

    if "verify" in condition:
        return (condition, new_conds)  # do not edit with verify*
    # Get values
    for i in range(len(ops)):
        op: str = ops[i]
        op = op.strip()
        if EQUAL in op:
            kv = op.split(EQUAL)
            if len(kv) != 2:
                single_ops.append(op)
                continue
            k, v = kv
            k = k.strip(" '\"")
            vv = v.strip()
            v = vv.strip("'\"")
            if k in k_seen:  # duplicate key => ignore
                continue
            k_seen[k] = True
            if k == "android_version":
                android_version = v
                op = f"android_version == '{android_version}'"
            elif k == "arch":
                arch = v
                op = f"arch == '{arch}'"
            elif k == "display":
                display = v
                op = f"display == '{display}'"
            elif k == "os":
                os = v
                op = f"os == '{os}'"
            elif k == "os_version":
                os_version = v
                op = f"os_version == '{os_version}'"
            elif v == vv:  # not quoted
                if v == "false":
                    if k == "debug":
                        permutations.append(["asan", "opt", "tsan"])
                    else:
                        single_ops.append("!" + k)
                    op = None
                elif v == "true":
                    single_ops.append(k)
                    op = None
                else:
                    op = f"{k} == {v}"
            else:
                op = f"{k} == '{v}'"
            if op is not None:
                new_ops.append(op)
        elif NOT_EQUAL in op or GREATER_EQUAL in op:
            new_ops.append(op)
        elif op == "apple_catalina":
            if os is None:
                os = "mac"
                op = f"os == '{os}'"
                new_ops.insert(0, op)
            if os_version is None:
                os_version = "10.15"
                op = f"os_version == '{os_version}'"
                new_ops.insert(1, op)
            if arch is None:
                arch = "x86_64"
                op = f"arch == '{arch}'"
                new_ops.insert(2, op)
        elif op == "apple_silicon":
            if os is None:
                os = "mac"
                op = f"os == '{os}'"
                new_ops.insert(0, op)
            if os_version is None:
                os_version = "15.30"
                op = f"os_version == '{os_version}'"
                new_ops.insert(1, op)
            if arch is None:
                arch = "aarch64"
                op = f"arch == '{arch}'"
                new_ops.insert(2, op)
        elif op == "win10_2009":
            if os is None:
                os = "win"
                op = f"os == '{os}'"
                new_ops.insert(0, op)
            if os_version is None:
                os_version = "10.2009"
                op = f"os_version == '{os_version}'"
                new_ops.insert(1, op)
            if arch is None:
                arch = "x86_64"
                op = f"arch == '{arch}'"
                new_ops.insert(2, op)
        elif op == "win11_2009":  # no longer used
            return (None, new_conds)
        elif op == "!debug":
            permutations.append(["asan", "opt", "tsan"])
        else:
            single_ops.append(op)

    # place single operands after equality/comparison operands
    ops = []
    for op in new_ops:
        ops.append(op)
    single_ops.sort()
    for op in single_ops:
        ops.append(op)

    # Handle special cases
    if android_version is not None:
        from mozinfo.platforminfo import android_api_to_os_version

        v = android_api_to_os_version(android_version)
        if os is None:
            os = "android"
            op = f"os == '{os}'"
            i = 0
            ops.insert(i, op)
        else:
            i = ops.index(f"os == '{os}'")
        if os_version is None:
            op = f"os_version == '{v}'"
            i += 1
            ops.insert(i, op)
        elif v != os_version:
            raise Exception(
                f"android_version == '{android_version}' conflicts with os_version == '{os_version}'"
            )
    if os is not None:
        i = ops.index(f"os == '{os}'")
        if os == "linux":
            if os_version is None:
                if cond == "skip-if":
                    new_cond = "os == 'linux' && os_version == '22.04' && arch == 'x86_64' && display == 'wayland'"
                    for single_op in single_ops:
                        new_cond += " && " + single_op
                    new_conds.append(new_cond)
                    os_version = "24.04"
                    op = f"os_version == '{os_version}'"
                    i += 1
                    ops.insert(i, op)
            else:
                i = ops.index(f"os_version == '{os_version}'")
            if arch is None:
                if cond == "skip-if":
                    arch = "x86_64"
                    op = f"arch == '{arch}'"
                    i += 1
                    ops.insert(i, op)
            else:
                i = ops.index(f"arch == '{arch}'")
            if display is None:
                if cond == "skip-if":
                    if os_version == "22.04":
                        display = "wayland"
                    else:
                        display = "x11"
                    op = f"display == '{display}'"
                    i += 1
                    ops.insert(i, op)
            elif os_version == "24.04" and display == "wayland":
                i = ops.index(f"display == '{display}'")
                display = "x11"
                op = f"display == '{display}'"
                ops[i] = op
        elif os == "mac":
            if os_version is None:  # do NOT guess os_version on mac
                if cond == "skip-if":
                    new_cond = (
                        "os == 'mac' && os_version == '10.15' && arch == 'x86_64'"
                    )
                    for single_op in single_ops:
                        new_cond += " && " + single_op
                    new_conds.append(new_cond)
                    new_cond = (
                        "os == 'mac' && os_version == '14.70' && arch == 'x86_64'"
                    )
                    for single_op in single_ops:
                        new_cond += " && " + single_op
                    new_conds.append(new_cond)
                    os_version = "15.30"
                    op = f"os_version == '{os_version}'"
                    i += 1
                    ops.insert(i, op)
            elif os_version == "11.20":  # no longer used
                return (None, new_conds)
            else:
                i = ops.index(f"os_version == '{os_version}'")
            if arch is None and cond == "skip-if":
                if os_version in ["10.15", "14.70"]:
                    arch = "x86_64"
                else:
                    arch = "aarch64"
                op = f"arch == '{arch}'"
                i += 1
                ops.insert(i, op)
        elif os == "win":
            if os_version is None:
                pass  # do NOT guess os_version on win
            elif os_version == "11.2009":  # no longer used
                return (None, new_conds)

    # Fix values
    new_ops = []
    for op in ops:
        new_op = op
        if EQUAL in op:
            kv = op.split(EQUAL)
            if len(kv) == 2:
                k, v = kv
                k = k.strip(" '\"")
                v = v.strip(" '\"")
                if k == "android_version":
                    new_op = None
                elif k == "bits":
                    if arch is None:
                        arch = "x86_64"
                        if v == "32":
                            arch = "x86"
                        elif (
                            os is not None
                            and os == "mac"
                            and os_version is not None
                            and os_version not in ["10.15", "14.70"]
                        ):
                            arch = "aarch64"
                        new_op = f"arch == '{arch}'"
                    else:
                        new_op = None
        if new_op is not None:
            new_ops.append(new_op)
    condition = " && ".join(new_ops)
    new_conds.insert(0, condition)
    if len(permutations) > 0:
        perm_conds: ListStr = []
        for perm in permutations:  # for each set of permutations
            for condition in new_conds:  # for all the conditions
                for p in perm:
                    c = condition
                    if c != "":
                        c += " && "
                    c += p
                    perm_conds.append(c)
            new_conds = perm_conds
            perm_conds = []
    i = 0
    while i < len(new_conds):
        condition = new_conds[i]
        if unused_condition(condition):
            del new_conds[i]
        else:
            i += 1
    if len(new_conds) > 0:
        condition = new_conds.pop(0)
        if not condition:
            condition = None
    else:
        condition = None
    return (condition, new_conds)


def add_unique_condition(
    conds: OptConditions, condition: str, comment: str
) -> OptConditions:
    """only add a condition if it is unique"""

    ignore: bool = False
    for c in conds:
        if _should_ignore_new_condition(c[0], condition):
            ignore = True
    if not ignore:
        conds.append([condition, comment])
    return conds


def alphabetize_toml_str(manifest, fix: bool = False):
    """
    Will take a TOMLkit manifest document (i.e. from a previous invocation
    of read_toml(..., document=True) and accessing the document
    from mp.source_documents[filename]) and return it as a string
    in sorted order by section (i.e. test file name, taking bug ids into consideration).
    If fix then fix non-idiomatic conditions
    """

    from tomlkit import array, document, dumps, table
    from tomlkit.items import Comment, String

    preamble: str = ""
    new_manifest = document()
    first_section: bool = False
    sections: Dict = {}  # noqa UP006

    for k, v in manifest.body:
        if k is None:
            preamble += v.as_string()
            continue
        if not isinstance(v, Table):
            raise Exception(f"MP TOML illegal keyval in preamble: {k} = {v}")
        section = None
        if not first_section:
            if k == DEFAULT_SECTION:
                new_manifest.add(k, v)
            else:
                new_manifest.add(DEFAULT_SECTION, table())
            first_section = True
        else:
            values = v.items()
            if len(values) == 1:
                for kk, vv in values:
                    if isinstance(vv, Table):  # unquoted, dotted key
                        section = f"{k}.{kk}"
                        sections[section] = vv
        if section is None:
            section = str(k).strip("'\"")
            sections[section] = v
        if fix:
            new_conds: ListStr = []  # conditions to add
            del_conds: ListStr = []  # conditions to remove
            keyvals: Dict = sections[section]  # noqa UP006
            for cond, skip_if in keyvals.items():
                conds: OptConditions = []
                first: OptStr = None  # first skip-if condition
                first_comment: str = ""  # first skip-if comment
                e_cond: OptStr = None  # existing skip-if condition
                e_comment: str = ""  # existing skip-if comment
                comment1: str = ""

                if not cond.endswith("-if"):
                    continue
                # handle the first condition uniquely to perserve whitespace
                if len(skip_if) == 1:
                    for e in skip_if._iter_items():
                        if isinstance(e, String):
                            if first is None:
                                if first_comment:  # comment before condition
                                    first_comment = _simplify_comment(first_comment)
                                    conds = add_unique_condition(
                                        conds, " comment", first_comment
                                    )
                                    comment1 = first_comment
                                    first_comment = ""
                                first = e.as_string().strip('"')
                        else:
                            c: str = e.as_string().strip()
                            if c and c != ",":
                                first_comment += c
                    if skip_if.trivia is not None:
                        first_comment += skip_if.trivia.comment
                        skip_if.trivia.indent = ""
                        skip_if.trivia.comment_ws = ""
                        skip_if.trivia.comment = ""  # remove comment after array
                    first_comment = _simplify_comment(first_comment)
                if first is not None:
                    e_cond, new_conds = idiomatic_condition(cond, first)
                e_comment = first_comment

                # loop over all skip-if conditions
                for e in skip_if._iter_items():
                    if isinstance(e, String):
                        if e_cond is not None:
                            conds = add_unique_condition(conds, e_cond, e_comment)
                            for new_cond in new_conds:
                                conds = add_unique_condition(conds, new_cond, e_comment)

                            e_cond = None
                            e_comment = ""
                            new_conds = []
                        if len(e) > 0:
                            e_cond = e.as_string().strip('"')

                            if e_cond == first:
                                e_cond = None  # don't repeat first
                                e_comment = ""
                            else:
                                e_cond, new_conds = idiomatic_condition(cond, e_cond)
                    elif isinstance(e, Comment):
                        simple = _simplify_comment(e.as_string())
                        if e_cond == "" or (e_cond is None and simple != first_comment):
                            e_cond = " comment"
                            e_comment = simple
                        elif e_comment:
                            e_comment += "\n  # " + simple
                        elif simple != comment1:
                            e_comment = simple
                if e_cond is not None:
                    conds = add_unique_condition(conds, e_cond, e_comment)
                    for new_cond in new_conds:
                        conds = add_unique_condition(conds, new_cond, e_comment)
                if len(conds) > 0:
                    # Save updated conditions in order
                    conds.sort()
                    mp_array: Array = array()
                    for c in conds:
                        if c[0] == " comment":
                            mp_array.add_line(indent="  ", comment=c[1])
                        else:
                            mp_array.add_line(c[0], indent="  ", comment=c[1])
                    mp_array.add_line("", indent="")  # fixed in write_toml_str
                    sections[section][cond] = mp_array
                else:
                    del_conds.append(cond)

            if len(del_conds) > 0:
                for cond in del_conds:
                    del sections[section][cond]

    if not first_section:
        new_manifest.add(DEFAULT_SECTION, table())

    for section in sort_paths([k for k in sections.keys() if k != DEFAULT_SECTION]):
        new_manifest.add(section, sections[section])

    manifest_str = dumps(new_manifest)

    # tomlkit fixups
    manifest_str = preamble + manifest_str.replace('"",]', "]")
    while manifest_str.endswith("\n\n"):
        manifest_str = manifest_str[:-1]
    return manifest_str


def _simplify_comment(comment):
    """Remove any leading #, but preserve leading whitespace in comment"""
    if comment is None:
        return None

    length = len(comment)
    i = 0
    j = -1  # remove exactly one space
    while i < length and comment[i] in " \n#":
        i += 1
        if i < length and comment[i] == " ":
            j += 1
    comment = comment[i:]
    if j > 0:
        comment = " " * j + comment
    return comment.rstrip()


def _should_keep_existing_condition(
    existing_condition: str, new_condition: str
) -> bool:
    """
    Checks the new condition is equal or not simpler than the existing one
    """
    return (
        existing_condition == new_condition
        or not existing_condition.startswith(new_condition)
        or not existing_condition.endswith(new_condition)
    )


def _should_ignore_new_condition(existing_condition: str, new_condition: str) -> bool:
    """
    Checks if the new condition is equal or more complex than an existing one
    Avoid ignoring conditions like "os == 'win' && !debug" if the
    existing condition is "debug" (etc.)
    """
    return (
        existing_condition == new_condition
        or new_condition.startswith(existing_condition)
        or new_condition.endswith(existing_condition)
    )


class Mode:
    "Skipfails mode of operation"

    NORMAL: int = 0
    CARRYOVER: int = 1
    KNOWN_INTERMITTENT: int = 2
    NEW_FAILURE: int = 3
    REPLACE_TBD: int = 4
    CARRYOVER_FILED: int = 5
    KNOWN_INTERMITTENT_FILED: int = 6
    NEW_FAILURE_FILED: int = 7


class Carry:
    "Helper class for add_skip_if to call is_carryover()"

    def __init__(self):
        self.os_rx: OptRegex = None
        self.opt_rx: OptRegex = None
        self.not_opt_rx: OptRegex = None
        self.debug_rx: OptRegex = None
        self.not_debug_rx: OptRegex = None
        self.asan_rx: OptRegex = None
        self.tsan_rx: OptRegex = None
        self.ccov_rx: OptRegex = None
        self.text_index: int = 0  # to facilitate testing only

    def test_index(self) -> int:
        "Returns expected relative index number of next test"
        self.text_index += 1
        return self.text_index

    def initialize(self):
        if self.opt_rx is None:
            self.os_rx = re.compile(r"os == ['\"]([a-z]+)['\"]")
            # pre = Either start of string ^,
            #   Or a preceding character that is not a letter, digit, !, or _.
            pre = r"(?:(?<![A-Za-z0-9!_])|^)"
            # post = Either whitespace,
            #   Or a non-alphanumeric, non-underscore character,
            #   Or end of string.
            post = r"(?:(?=\s)|(?=[^A-Za-z0-9_])|$)"
            self.opt_rx = re.compile(pre + "opt" + post)
            self.not_opt_rx = re.compile(pre + "!opt" + post)
            self.debug_rx = re.compile(pre + "debug" + post)
            self.not_debug_rx = re.compile(pre + "!debug" + post)
            self.asan_rx = re.compile(pre + "asan" + post)
            self.tsan_rx = re.compile(pre + "tsan" + post)
            self.ccov_rx = re.compile(pre + "ccov" + post)

    def os(self, s: str) -> str:
        self.initialize()
        m = self.os_rx.search(s)
        if m is not None:
            return m[0]
        return ""

    def opt(self, s: str) -> bool:
        self.initialize()
        return bool(self.opt_rx.search(s))

    def not_opt(self, s: str) -> bool:
        self.initialize()
        return bool(self.not_opt_rx.search(s))

    def debug(self, s: str) -> bool:
        self.initialize()
        return bool(self.debug_rx.search(s))

    def not_debug(self, s: str) -> bool:
        self.initialize()
        return bool(self.not_debug_rx.search(s))

    def asan(self, s: str) -> bool:
        self.initialize()
        return bool(self.asan_rx.search(s))

    def tsan(self, s: str) -> bool:
        self.initialize()
        return bool(self.tsan_rx.search(s))

    def ccov(self, s: str) -> bool:
        self.initialize()
        return bool(self.ccov_rx.search(s))

    def is_carryover(self, existing_condition: str, new_condition: str) -> bool:
        """
        Checks if the new condition is is a carryover from the existing
        condition. See ./test/SKIP-FAILS.txt and Bug 197161
        """
        self.initialize()
        # Does the OS match?
        e_os: str = self.os(existing_condition)
        os: str = self.os(new_condition)
        if e_os:
            if not os or e_os != os:
                return False  # os mismatch
        e_opt: bool = self.opt(existing_condition)
        e_not_opt: bool = self.not_opt(existing_condition)
        e_debug: bool = self.debug(existing_condition)
        e_not_debug: bool = self.not_debug(existing_condition)
        e_asan: bool = self.asan(existing_condition)
        e_tsan: bool = self.tsan(existing_condition)
        e_ccov: bool = self.ccov(existing_condition)
        opt: bool = self.opt(new_condition)
        not_opt: bool = self.not_opt(new_condition)
        debug: bool = self.debug(new_condition)
        not_debug: bool = self.not_debug(new_condition)
        asan: bool = self.asan(new_condition)
        tsan: bool = self.tsan(new_condition)
        ccov: bool = self.ccov(new_condition)
        # NOTE: the sum of bool in Python is the most efficient way to
        # determine how many vars are true (as bool is represented at 0 or 1)
        if sum(  # existing_condition has NO build type
            [e_opt, e_not_opt, e_debug, e_not_debug, e_asan, e_tsan, e_ccov]
        ) == 0 and (e_os and os and e_os == os):
            return True  # build type not present and os explicit match
        if (e_opt or e_not_debug) and (opt or not_debug):
            return True  # both optimized
        if (e_debug and sum([asan, tsan, ccov]) == 1) or (
            sum([e_asan, e_tsan, e_ccov]) == 1 and debug
        ):
            return False  # debug != [asan, tsan, ccov]
        if (  # both conditions have a compatible build type
            sum([e_not_opt, e_debug, e_asan, e_tsan, e_ccov]) == 1
            and sum([not_opt, debug, asan, tsan, ccov]) == 1
        ):
            return True
        return False


def add_skip_if(
    manifest: TOMLDocument,
    filename: str,
    condition: str,
    bug_reference: OptStr = None,
    create_bug_lambda: CreateBug = None,
    mode: Mode = Mode.NORMAL,
) -> TupleStrBoolStr:
    """
    Will take a TOMLkit manifest document (i.e. from a previous invocation
    of read_toml(..., document=True) and accessing the document
    from mp.source_documents[filename]) and mutate it
    in sorted order by section (i.e. test file name, taking bug ids into consideration).
    Determine if this condition is a carryover (see ./test/SKIP-FAILS.txt and Bug 1971610)
    In carryover mode only consider carryover edits (do not create bugs)
    Else when not in carryover mode and create_bug_lambda is not None
      then invoke create_bug_lambda to create new bug

    Returns (additional_comment, carryover) where
      additional_comment is the empty string (used in other manifest add_skip_if functions)
      carryover is True if this condition is carried over from an existing condition
    bug_reference is returned as None if the skip-if was ignored
    """

    from tomlkit import array
    from tomlkit.items import Comment, String, Whitespace

    carry = Carry()
    additional_comment: str = ""
    carryover: bool = False  # not carried over from a previous skip-if
    if filename not in manifest:
        raise Exception(f"TOML manifest does not contain section: {filename}")
    keyvals: Dict = manifest[filename]  # noqa UP006
    first: OptStr = None
    first_comment: str = ""
    skip_if: OptArray = None
    ignore_cond: bool = False  # this condition should not be added
    if "skip-if" in keyvals:
        skip_if = keyvals["skip-if"]
        if len(skip_if) == 1:
            for e in skip_if._iter_items():
                if not first:
                    if not isinstance(e, Whitespace):
                        first = e.as_string().strip('"')
                else:
                    c = e.as_string()
                    if c != ",":
                        first_comment += c
            if skip_if.trivia is not None:
                first_comment += skip_if.trivia.comment
    mp_array: Array = array()
    if skip_if is None:  # add the first one line entry to the table
        if mode != Mode.CARRYOVER:
            if create_bug_lambda is not None:
                bug = create_bug_lambda()
                if bug is not None:
                    bug_reference = f"Bug {bug.id}"
            mp_array.add_line(condition, indent="  ", comment=bug_reference)
            mp_array.add_line("", indent="")  # fixed in write_toml_str
            skip_if = {"skip-if": mp_array}
            keyvals.update(skip_if)
    else:
        # We store the conditions in a regular python array so we can sort them before
        # dumping them in the TOML
        conds: OptConditions = []
        if first is not None:
            if _should_ignore_new_condition(first, condition):
                ignore_cond = True
            if first_comment:
                first_comment = _simplify_comment(first_comment)
            if _should_keep_existing_condition(first, condition):
                conds.append([first, first_comment])
                if (
                    not ignore_cond
                    and mode == Mode.CARRYOVER
                    and carry.is_carryover(first, condition)
                ):
                    carryover = True
                    bug_reference = first_comment
            elif bug_reference is None and create_bug_lambda is None:
                bug_reference = first_comment
        if len(skip_if) > 1:
            e_cond = None
            e_comment = None
            for e in skip_if._iter_items():
                if isinstance(e, String):
                    if e_cond is not None:
                        if _should_keep_existing_condition(e_cond, condition):
                            conds.append([e_cond, e_comment])
                            if (
                                not ignore_cond
                                and mode == Mode.CARRYOVER
                                and carry.is_carryover(e_cond, condition)
                            ):
                                carryover = True
                                bug_reference = e_comment
                        elif bug_reference is None and create_bug_lambda is None:
                            bug_reference = e_comment
                        e_comment = None
                        e_cond = None
                    if len(e) > 0:
                        e_cond = e.as_string().strip('"')
                        if _should_ignore_new_condition(e_cond, condition):
                            ignore_cond = True
                elif isinstance(e, Comment):
                    e_comment = _simplify_comment(e.as_string())
            if e_cond is not None:
                if _should_keep_existing_condition(e_cond, condition):
                    conds.append([e_cond, e_comment])
                    if (
                        not ignore_cond
                        and mode == Mode.CARRYOVER
                        and carry.is_carryover(e_cond, condition)
                    ):
                        carryover = True
                        bug_reference = e_comment
                elif bug_reference is None and create_bug_lambda is None:
                    bug_reference = e_comment
        if ignore_cond:
            carryover = False
            bug_reference = None
        else:
            if mode == Mode.NORMAL and create_bug_lambda is not None:
                bug = create_bug_lambda()
                if bug is not None:
                    bug_reference = f"Bug {bug.id}"
            conds.append([condition, bug_reference])
        conds.sort()
        for c in conds:
            mp_array.add_line(c[0], indent="  ", comment=c[1])
        mp_array.add_line("", indent="")  # fixed in write_toml_str
        skip_if = {"skip-if": mp_array}
        del keyvals["skip-if"]
        keyvals.update(skip_if)
    return (additional_comment, carryover, bug_reference)


def _should_remove_cond(
    condition: str,
    os_name: OptStr = None,
    os_version: OptStr = None,
    processor: OptStr = None,
):
    to_ignore = [os_name, os_version, processor]
    for part in to_ignore:
        if part is not None and part not in condition:
            return False
    return True


def remove_skip_if(
    manifest: TOMLDocument,
    os_name: OptStr = None,
    os_version: OptStr = None,
    processor: OptStr = None,
):
    from tomlkit import array
    from tomlkit.items import Comment, String

    if os_name is None and os_version is None and processor is None:
        raise ValueError("Needs at least os name, version or processor to be set.")

    has_removed_items = False

    for filename in manifest:
        key_values = manifest[filename]
        if isinstance(key_values, Table) and "skip-if" in key_values:
            condition_array = key_values["skip-if"]
            if isinstance(condition_array, Array):
                new_conditions: Array = array()
                condition = None
                comment = None
                conditions_to_add: OptConditions = []
                for item in condition_array._iter_items():
                    if isinstance(item, String):
                        if condition is not None:
                            if not _should_remove_cond(
                                condition, os_name, os_version, processor
                            ):
                                conditions_to_add.append((condition, comment))
                            else:
                                has_removed_items = True
                            condition = None
                            comment = None
                        if len(item) > 0:
                            condition = item.as_string().strip('"')
                    elif isinstance(item, Comment):
                        comment = _simplify_comment(item.as_string())
                if condition is not None:
                    if not _should_remove_cond(
                        condition, os_name, os_version, processor
                    ):
                        conditions_to_add.append((condition, comment))
                    else:
                        has_removed_items = True

                if len(conditions_to_add) > 0:
                    for condition, comment in conditions_to_add:
                        new_conditions.add_line(condition, comment=comment, indent="  ")
                if len(new_conditions) > 0:
                    new_conditions.add_line("", indent="")
                    key_values.update({"skip-if": new_conditions})
                else:
                    del key_values["skip-if"]

    return has_removed_items


def replace_tbd_skip_if(
    manifest: TOMLDocument, filename: str, condition: str, bugid: str
) -> bool:
    """
    Edits the test ["filename"] in manifest with the given condition
    that has a bug reference `# Bug TBD`
    with the actual bugid
    returns True if properly updated
    """
    from tomlkit import array
    from tomlkit.items import Comment, String, Whitespace

    updated: bool = False  # was the TBD found and updated with bugid?
    BUG_TBD: str = "Bug TBD"  # string we are looking for
    if filename not in manifest:
        raise Exception(f"TOML manifest does not contain section: {filename}")
    keyvals: Dict = manifest[filename]  # noqa UP006
    if not "skip-if" in keyvals:
        raise Exception(
            f"TOML manifest for section: {filename} does not contain a skip-if condition"
        )
    skip_if: Array = keyvals["skip-if"]
    mp_array: Array = array()
    conds: OptConditions = []
    first: OptStr = None  # first skip-if condition
    first_comment: str = ""  # first skip-if comment
    e_cond: OptStr = None  # existing skip-if condition
    e_comment: str = ""  # existing skip-if comment

    # handle the first condition uniquely to properly perserve whitespace
    if len(skip_if) == 1:
        for e in skip_if._iter_items():
            if first is None:
                if not isinstance(e, Whitespace):
                    first = e.as_string().strip('"')
            else:
                c = e.as_string()
                if c != ",":
                    first_comment += c
        if skip_if.trivia is not None:
            first_comment += skip_if.trivia.comment
    if first is not None:
        if first_comment:
            first_comment = _simplify_comment(first_comment)
        if first == condition and first_comment.endswith(BUG_TBD):
            i: int = max(first_comment.find(BUG_TBD), 0)
            first_comment = f"{' ' * i}Bug {bugid}"
            updated = True
        e_cond = first
        e_comment = first_comment

    # loop over all skip-if conditions to find BUG_TBD
    for e in skip_if._iter_items():
        if isinstance(e, String):
            if e_cond is not None:
                conds.append([e_cond, e_comment])
                e_cond = None
                e_comment = ""
            if len(e) > 0:
                e_cond = e.as_string().strip('"')
                if e_cond == first:
                    e_cond = None  # don't repeat first
        elif isinstance(e, Comment):
            e_comment = _simplify_comment(e.as_string())
        if e_cond == condition and e_comment.endswith(BUG_TBD):
            i: int = max(e_comment.find(BUG_TBD), 0)
            e_comment = f"{' ' * i}Bug {bugid}"
            updated = True
    if e_cond is not None:
        conds.append([e_cond, e_comment])

    # Update TOML document for the test with the updated skip-if comment
    conds.sort()
    for c in conds:
        mp_array.add_line(c[0], indent="  ", comment=c[1])
    mp_array.add_line("", indent="")  # fixed in write_toml_str
    skip_if = {"skip-if": mp_array}
    del keyvals["skip-if"]
    keyvals.update(skip_if)

    return updated
