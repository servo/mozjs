# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate import COPY_PATTERN
from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2026066 - Move shortcuts description from moz-toggle to moz-select, part {index}."""
    source = "browser/browser/newtab/newtab.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
newtab-custom-shortcuts-nova =
    .label = {COPY_PATTERN(from_path, "newtab-custom-shortcuts-toggle.label")}
""",
            from_path=source,
        ),
    )
