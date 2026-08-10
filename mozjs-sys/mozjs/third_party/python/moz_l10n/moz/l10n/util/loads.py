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

from json import JSONDecodeError, loads
from re import MULTILINE, compile
from typing import Any

str_comment = compile(r'^((?:[^"\n]|"(?:[^"\\\n]|\\.)*")*?)//.*', MULTILINE)
bytes_comment = compile(rb'^((?:[^"\n]|"(?:[^"\\\n]|\\.)*")*?)//.*', MULTILINE)


def json_linecomment_loads(source: str | bytes) -> tuple[Any, bool]:
    """
    `(json_data, is_valid_json)`

    Line comments // are supported in `webext` messages.json files.
    """
    try:
        return loads(source), True
    except JSONDecodeError:
        source_: str | bytes
        if isinstance(source, str):
            source_ = str_comment.sub(r"\1", source)
        else:
            source_ = bytes_comment.sub(rb"\1", source)
        return loads(source_), False
