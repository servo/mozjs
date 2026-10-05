# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate import COPY_PATTERN
from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2031669 - Customization button Nova update, part {index}."""
    source = "browser/browser/newtab/newtab.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
newtab-customize-panel-label =
    .label = {COPY_PATTERN(from_path, "newtab-customize-panel-icon-button-label")}
""",
            from_path=source,
        ),
    )
