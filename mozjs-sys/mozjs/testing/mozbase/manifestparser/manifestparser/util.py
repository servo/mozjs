# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import ast
import os

# The normsep function is used everywhere, it's important enough in terms of
# optimization to justify the slight duplication that can be found below.
#
# TODO: Have a dedicated function for bytes and one for unicode.
if os.altsep and os.altsep != "/":
    altsep_b = os.altsep.encode("ascii")
if os.sep != "/":
    sep_b = os.sep.encode("ascii")

norm_needed = True

if os.sep != "/":
    if os.altsep and os.altsep != "/":

        def normsep(path):
            """
            Normalize path separators, by using forward slashes instead of whatever
            :py:const:`os.sep` is.
            """
            # Python 2 is happy to do things like byte_string.replace(u'foo',
            # u'bar'), but not Python 3.
            if isinstance(path, bytes):
                path = path.replace(sep_b, b"/")
                return path.replace(altsep_b, b"/")
            else:
                path = path.replace(os.sep, "/")
                return path.replace(os.altsep, "/")

    else:

        def normsep(path):
            if isinstance(path, bytes):
                return path.replace(sep_b, b"/")
            else:
                return path.replace(os.sep, "/")

elif os.altsep and os.altsep != "/":

    def normsep(path):
        if isinstance(path, bytes):
            return path.replace(altsep_b, b"/")
        else:
            return path.replace(os.altsep, "/")
        return path

else:

    def normsep(path):
        return path

        norm_needed = False  # noqa: F841


def evaluate_list_from_string(list_string):
    """
    This is a utility function for converting a string obtained from a manifest
    into a list. If the string is not a valid list when converted, an error will be
    raised from `ast.eval_literal`. For example, you can convert entries like this
    into a list:
    ```
        test_settings=
            ["hello", "world"],
            [1, 10, 100],
        values=
            5,
            6,
            7,
            8,
    ```
    """
    parts = [
        x.strip(",")
        for x in list_string.strip(",").replace("\r", "").split("\n")
        if x.strip()
    ]
    return ast.literal_eval("[" + ",".join(parts) + "]")
