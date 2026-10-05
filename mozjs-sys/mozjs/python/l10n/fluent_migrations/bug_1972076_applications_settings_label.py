# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1972076 - Migrate applications setting config labels, part {index}."""

    target = "browser/browser/preferences/preferences.ftl"

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
applications-setting-new-file-types =
    .label = {COPY_PATTERN(from_path, "applications-handle-new-file-types-description")}
applications-setting =
    .label = {COPY_PATTERN(from_path, "applications-header")}
    .description = {COPY_PATTERN(from_path, "applications-description")}
""",
            from_path=target,
        ),
    )
