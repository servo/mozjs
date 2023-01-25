# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import sys

from mozboot.base import BaseBootstrapper
from mozboot.linux_common import LinuxBootstrapper


class ArchlinuxBootstrapper(LinuxBootstrapper, BaseBootstrapper):
    """Archlinux experimental bootstrapper."""

    def __init__(self, version, dist_id, **kwargs):
        print("Using an experimental bootstrapper for Archlinux.", file=sys.stderr)
        BaseBootstrapper.__init__(self, **kwargs)

    def install_packages(self, packages):
        # watchman is not available via pacman
        packages = [p for p in packages if p != "watchman"]
        self.pacman_install(*packages)

    def upgrade_mercurial(self, current):
        self.pacman_install("mercurial")

    def pacman_install(self, *packages):
        command = ["pacman", "-S", "--needed"]
        if self.no_interactive:
            command.append("--noconfirm")

        command.extend(packages)

        self.run_as_root(command)
