# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import buildconfig
import mozpack.path as mozpath
import mozshellutil


# Rewrite a newline-separated input file of mozbuild-style paths (i.e.
# "/"-prefixed denoting topsrcdir-relative and "!/"-prefixed denoting
# topobjdir-relative) into a shell-quoted list of absolute paths.
def write_paths(fh, path_list):
    def resolve_path(path):
        if path.startswith("/"):
            return mozpath.join(buildconfig.topsrcdir, path[1:])
        if path.startswith("!/"):
            return mozpath.join(buildconfig.topobjdir, path[2:])
        raise ValueError(f'Unsupported path "{path}"')

    with open(path_list) as path_list_file:
        paths = [
            mozpath.normpath(resolve_path(p))
            for p in path_list_file.read().splitlines()
        ]
    fh.write(mozshellutil.quote(*paths))
