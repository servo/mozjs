# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2009047 - Add strings for Manage Widgets panel in Customize panel, part {index}"""

    source = "browser/browser/newtab/newtab.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
newtab-custom-widget-section-toggle =
    .label = { COPY_PATTERN(from_path, "newtab-custom-widget-section-title") }

newtab-widget-manage-title = { COPY_PATTERN(from_path, "newtab-custom-widget-section-title") }
""",
            from_path=source,
        ),
    )
