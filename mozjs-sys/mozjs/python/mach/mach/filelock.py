# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import annotations

import sys
from pathlib import Path

_filelock_path = (
    Path(__file__).parents[3] / "third_party" / "python" / "filelock"
).as_posix()
sys.path.insert(0, _filelock_path)
try:
    from filelock import FileLock, SoftFileLock, Timeout  # noqa: E402
finally:
    sys.path.remove(_filelock_path)

__all__ = ["FileLock", "SoftFileLock", "Timeout"]
