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
    """Bug 1990961 - Convert Firefox Updates section to config-based prefs, part {index}."""

    path = "browser/browser/preferences/preferences.ftl"

    ctx.add_transforms(
        path,
        path,
        [
            FTL.Message(
                id=FTL.Identifier("update-application-warning-cross-user-setting-2"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("message"),
                        value=COPY_PATTERN(
                            path, "update-application-warning-cross-user-setting"
                        ),
                    ),
                ],
            ),
            FTL.Message(
                id=FTL.Identifier("update-history-2"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("label"),
                        value=STRIP_ELLIPSIS(path, "update-history.label"),
                    ),
                    FTL.Attribute(
                        id=FTL.Identifier("accesskey"),
                        value=COPY_PATTERN(path, "update-history.accesskey"),
                    ),
                ],
            ),
        ],
    )
