# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import fluent.syntax.ast as FTL
from fluent.migrate import COPY_PATTERN


def migrate(ctx):
    """Bug 2001154 - Show redesigned Downloads settings on new pane, part {index}."""

    source = "browser/browser/preferences/preferences.ftl"

    ctx.add_transforms(
        source,
        source,
        [
            FTL.Message(
                id=FTL.Identifier("download-save-files-header"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("label"),
                        value=COPY_PATTERN(source, "download-save-where-2.label"),
                    ),
                ],
            ),
            FTL.Message(
                id=FTL.Identifier("download-save-where-3"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("aria-label"),
                        value=COPY_PATTERN(source, "download-save-where-2.label"),
                    ),
                ],
            ),
        ],
    )
