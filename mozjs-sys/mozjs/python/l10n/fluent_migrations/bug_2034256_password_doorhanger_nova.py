# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2034256 - Migrate password doorhanger username/password to label attributes, part {index}."""

    source = "browser/browser/browser.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
panel-save-update-username-2 =
    .label = {COPY_PATTERN(from_path, "panel-save-update-username")}
panel-save-update-password-2 =
    .label = {COPY_PATTERN(from_path, "panel-save-update-password")}
""",
            from_path=source,
        ),
    )
