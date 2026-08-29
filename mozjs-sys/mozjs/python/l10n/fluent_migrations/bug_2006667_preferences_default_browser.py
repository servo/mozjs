# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import re
from fluent.migrate.transforms import TransformPattern, COPY_PATTERN
import fluent.syntax.ast as FTL


class STRIP_ELLIPSIS(TransformPattern):
    def visit_TextElement(self, node):
        node.value = re.sub(r"(?:…|\.\.\.)$", "", node.value)
        return node


def migrate(ctx):
    """Bug 2006667 - Add Default browser section to Sync settings page, part {index}"""
    path = "browser/browser/preferences/preferences.ftl"

    ctx.add_transforms(
        path,
        path,
        [
            FTL.Message(
                id=FTL.Identifier("set-as-my-default-browser-2"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("label"),
                        value=STRIP_ELLIPSIS(path, "set-as-my-default-browser.label"),
                    ),
                    FTL.Attribute(
                        id=FTL.Identifier("accesskey"),
                        value=COPY_PATTERN(path, "set-as-my-default-browser.accesskey"),
                    ),
                ],
            ),
        ],
    )
