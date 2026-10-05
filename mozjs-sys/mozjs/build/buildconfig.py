# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from mozbuild.backend.configenvironment import PartialConfigEnvironment
from mozbuild.base import MozbuildObject

config = MozbuildObject.from_environment()
partial_config = PartialConfigEnvironment(config.topobjdir)

topsrcdir = config.topsrcdir
topobjdir = config.topobjdir
defines = partial_config.defines
substs = partial_config.substs
get_dependencies = partial_config.get_dependencies
