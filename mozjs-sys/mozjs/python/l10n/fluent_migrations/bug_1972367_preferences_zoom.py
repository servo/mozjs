# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1972367 - Convert Zoom settings section to config-based prefs, part {index}."""

    target = "browser/browser/preferences/preferences.ftl"

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
preferences-zoom-header2 =
    .label = {COPY_PATTERN(from_path, "preferences-zoom-header")}
preferences-default-zoom-label =
    .label = {COPY_PATTERN(from_path, "preferences-default-zoom")}
    .accesskey = {COPY_PATTERN(from_path, "preferences-default-zoom.accesskey")}
""",
            from_path=target,
        ),
    )
