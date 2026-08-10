# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate import COPY_PATTERN
from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2037282 - Add manage widgets option to widget context menu, part {index}."""
    source = "browser/browser/newtab/newtab.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
newtab-widget-section-menu-manage = {COPY_PATTERN(from_path, "newtab-widget-manage-widget-button.label")}
""",
            from_path=source,
        ),
    )
