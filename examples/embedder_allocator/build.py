#!/usr/bin/env python3

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# This file

import os
import pathlib
import subprocess
import sys
from typing import Mapping

def create_env() -> Mapping[str, str]:
    env = os.environ.copy()
    mimalloc_include_dir = pathlib.Path(__file__).parent.joinpath('mimalloc/include')
    assert mimalloc_include_dir.is_dir(), "Could not find mimalloc include directory"
    env['SERVO_CUSTOM_ALLOC_INCLUDE_DIR'] = mimalloc_include_dir.as_posix()
    return env


def main():
    completed_process = subprocess.run(sys.argv[1:], env=create_env())
    sys.exit(completed_process.returncode)


if __name__ == '__main__':
    main()
