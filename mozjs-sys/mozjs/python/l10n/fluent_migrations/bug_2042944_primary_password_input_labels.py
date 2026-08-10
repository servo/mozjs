# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2042944 - Update styles for primary password settings dialogue, part {index}."""

    source = "toolkit/toolkit/preferences/preferences.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
set-password-old =
    .label = {COPY_PATTERN(from_path, "set-password-old-password")}
set-password-new =
    .label = {COPY_PATTERN(from_path, "set-password-new-password")}
set-password-reenter =
    .label = {COPY_PATTERN(from_path, "set-password-reenter-password")}
set-password-not-set =
    .label = {COPY_PATTERN(from_path, "set-password-old-password")}
    .placeholder = {COPY_PATTERN(from_path, "password-not-set.value")}
remove-password-old =
    .label = {COPY_PATTERN(from_path, "remove-password-old-password.value")}
""",
            from_path=source,
        ),
    )
