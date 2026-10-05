# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1710459 - Migrate the sidebar history sort by heading to a menucaption label, part {index}."""

    source = "browser/browser/sidebar.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
sidebar-history-sort-by-heading-menucaption =
    .label = {COPY_PATTERN(from_path, "sidebar-history-sort-by-heading")}
""",
            from_path=source,
        ),
    )
