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
from re import compile

from ..model import Expression, VariableRef

printf = compile(
    #     1:argnum    2:python-name                  3:java-datetime                       4:type
    r"%(?:([1-9])\$|\(([\w.]+)\))?[-#+ 0,]?[0-9.]*(?:([Tt][A-Za-z]+)|(?:(?:hh?|ll?|[Lzjq])?([@%A-Za-z])))"
)


def parse_printf_pattern(src: str | None) -> Iterator[str | Expression]:
    if not src:
        return
    pos = 0
    for m in printf.finditer(src):
        start = m.start()
        if start > pos:
            yield src[pos:start]
        source = m[0]
        [argnum, argname, datetime, type] = m.groups()
        pos = m.end()

        if type == "%":
            yield Expression("%", attributes={"source": source})
            continue

        func: str | None
        # TODO post-py38: should be a match
        if datetime:
            func = "datetime"
        elif type in {"c", "C", "s", "S"}:
            func = "string"
        elif type in {"d", "D", "o", "O", "p", "u", "U", "x", "X"}:
            func = "integer"
        elif type in {"a", "A", "e", "E", "f", "F", "g", "G"}:
            func = "number"
        else:
            func = "printf"
        yield Expression(
            VariableRef(argname or ("arg" + (argnum or ""))),
            func,
            attributes={"source": source},
        )
    if pos < len(src):
        yield src[pos:]
