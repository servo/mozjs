# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1972365 - Move fonts preferences strings to .label attributes, part {index}."""

    source = "browser/browser/preferences/preferences.ftl"

    ctx.add_transforms(
        source,
        source,
        transforms_from(
            """
preferences-fonts-header2 =
    .label = { COPY_PATTERN(from_path, "preferences-fonts-header") }

default-font-2 =
    .label = { COPY_PATTERN(from_path, "default-font") }
    .accesskey = { COPY_PATTERN(from_path, "default-font.accesskey") }

default-font-size-2 =
    .label = { COPY_PATTERN(from_path, "default-font-size") }
    .accesskey = { COPY_PATTERN(from_path, "default-font-size.accesskey") }
""",
            from_path=source,
        ),
    )
