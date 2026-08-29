# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate import COPY_PATTERN
from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1971438 - Migrate strings for updated history preferences section, part {index}."""
    source = "browser/browser/preferences/preferences.ftl"
    target = source

    ctx.add_transforms(
        target,
        source,
        transforms_from(
            """
history-remember-description2 =
    .description = {COPY_PATTERN(from_path, "history-remember-description")}

history-dontremember-description2 =
    .description = {COPY_PATTERN(from_path, "history-dontremember-description")}
""",
            from_path=source,
        ),
    )
