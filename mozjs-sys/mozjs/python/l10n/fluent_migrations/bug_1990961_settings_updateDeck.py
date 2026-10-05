# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import re
import fluent.syntax.ast as FTL
from fluent.migrate.helpers import transforms_from
from fluent.migrate.transforms import TransformPattern


class STRIP_HTML(TransformPattern):
    def visit_TextElement(self, node):
        node.value = re.sub(r"<[^>]+>", "", node.value)
        return node


class REPLACE_LABEL_WITH_A(TransformPattern):
    """Replace <label> tags with <a> tags."""

    def visit_TextElement(self, node):
        node.value = re.sub(r"<label\b", "<a", node.value)
        node.value = re.sub(r"</label>", "</a>", node.value)
        return node


def migrate(ctx):
    """Bug 1990961 - Convert Firefox Updates section to config-based prefs, part {index}."""

    source = "browser/browser/aboutDialog.ftl"

    ctx.add_transforms(
        source,
        source,
        transforms_from(
            """
settings-update-checking-for-updates =
    .label = { COPY_PATTERN(from_path, "update-checkingForUpdates") }

settings-update-applying =
    .label = { COPY_PATTERN(from_path, "update-applying") }

settings-update-policy-disabled =
    .label = { COPY_PATTERN(from_path, "update-policy-disabled") }

settings-update-no-updates-found =
    .label = { COPY_PATTERN(from_path, "update-noUpdatesFound") }

settings-update-checking-failed =
    .label = { COPY_PATTERN(from_path, "aboutdialog-update-checking-failed") }

settings-update-other-instance-handling-updates =
    .label = { COPY_PATTERN(from_path, "update-otherInstanceHandlingUpdates") }

settings-update-restarting =
    .label = { COPY_PATTERN(from_path, "update-restarting") }
""",
            from_path=source,
        ),
    )

    ctx.add_transforms(
        source,
        source,
        [
            FTL.Message(
                id=FTL.Identifier("settings-update-downloading-2"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("label"),
                        value=STRIP_HTML(source, "settings-update-downloading"),
                    ),
                ],
            ),
            FTL.Message(
                id=FTL.Identifier("settings-update-unsupported"),
                value=REPLACE_LABEL_WITH_A(source, "update-unsupported"),
            ),
            FTL.Message(
                id=FTL.Identifier("settings-update-internal-error"),
                value=REPLACE_LABEL_WITH_A(source, "update-internal-error2"),
            ),
        ],
    )
