# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import argparse

from mach.decorators import Command


class CustomParser(argparse.ArgumentParser):
    """A parser that stashes unrecognized args into extra_args and returns
    an empty remainder list, the same way MozlintParser does."""

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.add_argument("-l", "--linter", dest="linter", default=None)
        self.add_argument(
            "extra_args",
            nargs=argparse.REMAINDER,
            default=[],
        )

    def parse_known_args(self, *args, **kwargs):
        namespace, extra = super().parse_known_args(*args, **kwargs)
        namespace.extra_args = extra
        return namespace, []


def setup_custom_parser():
    return CustomParser()


@Command(
    "cmd_custom_parser",
    category="testing",
    parser=setup_custom_parser,
)
def run_custom_parser(command_context, extra_args=None, **kwargs):
    print(" ".join(extra_args or []))
