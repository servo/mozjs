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
    """Bug 1971835 - Update account preferences strings, part {index}."""

    source = "browser/browser/preferences/preferences.ftl"

    ctx.add_transforms(
        source,
        source,
        [
            FTL.Message(
                id=FTL.Identifier("sync-sign-out2"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("label"),
                        value=STRIP_ELLIPSIS(source, "sync-sign-out.label"),
                    ),
                    FTL.Attribute(
                        id=FTL.Identifier("accesskey"),
                        value=COPY_PATTERN(source, "sync-sign-out.accesskey"),
                    ),
                ],
            ),
            FTL.Message(
                id=FTL.Identifier("sync-manage-account2"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("label"),
                        value=COPY_PATTERN(source, "sync-manage-account"),
                    ),
                    FTL.Attribute(
                        id=FTL.Identifier("accesskey"),
                        value=COPY_PATTERN(source, "sync-manage-account.accesskey"),
                    ),
                ],
            ),
        ],
    )
