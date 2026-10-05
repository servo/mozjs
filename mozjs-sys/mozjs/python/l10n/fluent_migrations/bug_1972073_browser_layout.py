# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate import COPY_PATTERN
from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1972073 - Update browser layout chooser strings, part {index}."""

    source = "browser/browser/preferences/preferences.ftl"

    ctx.add_transforms(
        source,
        source,
        transforms_from(
            """
browser-layout-header2 =
    .label = {COPY_PATTERN(from_path, "browser-layout-header")}

browser-layout-show-sidebar2 =
    .label = {COPY_PATTERN(from_path, "browser-layout-show-sidebar.label")}
    .description = {COPY_PATTERN(from_path, "browser-layout-show-sidebar-desc")}
""",
            from_path=source,
        ),
    )
