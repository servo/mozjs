# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate import COPY_PATTERN
from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2044354 - Containers: nova color migration, part {index}."""

    source = "browser/browser/profiles.ftl"
    target = "toolkit/toolkit/global/contextual-identity.ftl"

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
user-context-color-cyan =
    .label = {COPY_PATTERN(from_path, "profiles-cyan-theme")}
user-context-color-violet =
    .label = {COPY_PATTERN(from_path, "profiles-violet-theme")}
user-context-color-gray =
    .label = {COPY_PATTERN(from_path, "profiles-gray-theme")}
""",
            from_path=source,
        ),
    )
