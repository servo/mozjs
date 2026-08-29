# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2022574 - Migrate applications column labels to headings, part {index}."""

    target = "browser/browser/preferences/preferences.ftl"

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
applications-type-heading = {COPY_PATTERN(from_path, "applications-type-column.label")}

applications-action-heading = {COPY_PATTERN(from_path, "applications-action-column.label")}
""",
            from_path=target,
        ),
    )
