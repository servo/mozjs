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
    """Bug 1971841 - Convert Sync section to config-based prefs, part {index}"""

    path = "browser/browser/preferences/preferences.ftl"

    ctx.add_transforms(
        path,
        path,
        [
            FTL.Message(
                id=FTL.Identifier("sync-group-label"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("label"),
                        value=COPY_PATTERN(path, "pane-sync-title3"),
                    ),
                ],
            ),
            FTL.Message(
                id=FTL.Identifier("prefs-sync-turn-on-syncing-2"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("label"),
                        value=STRIP_ELLIPSIS(path, "prefs-sync-turn-on-syncing.label"),
                    ),
                    FTL.Attribute(
                        id=FTL.Identifier("accesskey"),
                        value=COPY_PATTERN(
                            path, "prefs-sync-turn-on-syncing.accesskey"
                        ),
                    ),
                ],
            ),
            FTL.Message(
                id=FTL.Identifier("prefs-sync-now-button-2"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("label"),
                        value=COPY_PATTERN(path, "prefs-sync-now-button.label"),
                    ),
                    FTL.Attribute(
                        id=FTL.Identifier("accesskey"),
                        value=COPY_PATTERN(path, "prefs-sync-now-button.accesskey"),
                    ),
                ],
            ),
            FTL.Message(
                id=FTL.Identifier("prefs-syncing-button-2"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("label"),
                        value=COPY_PATTERN(path, "prefs-syncing-button.label"),
                    ),
                    FTL.Attribute(
                        id=FTL.Identifier("title"),
                        value=COPY_PATTERN(path, "prefs-sync-now-button.label"),
                    ),
                ],
            ),
            FTL.Message(
                id=FTL.Identifier("sync-device-name-header-2"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("label"),
                        value=COPY_PATTERN(path, "sync-device-name-header"),
                    ),
                ],
            ),
            FTL.Message(
                id=FTL.Identifier("sync-connect-another-device-2"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("label"),
                        value=COPY_PATTERN(path, "sync-connect-another-device"),
                    ),
                ],
            ),
        ],
    )
