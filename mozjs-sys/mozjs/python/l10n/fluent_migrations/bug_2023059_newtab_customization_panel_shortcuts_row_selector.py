# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate import COPY_PATTERN
from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2023059 - Update shortcuts section in customization panel for Nova, part {index}."""
    source = "browser/browser/newtab/newtab.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
newtab-custom-row-selector2 =
    .label = {COPY_PATTERN(from_path, "newtab-custom-row-selector")}
""",
            from_path=source,
        ),
    )
