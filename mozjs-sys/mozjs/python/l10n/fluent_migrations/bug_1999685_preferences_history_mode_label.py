# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate import COPY_PATTERN
from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1999685 - Migrate history descriptions to new IDs with aria-labels, part {index}."""
    source = "browser/browser/preferences/preferences.ftl"
    target = source

    ctx.add_transforms(
        target,
        source,
        transforms_from(
            """
history-remember-label2 = {COPY_PATTERN(from_path, "history-remember-label")}

history-remember-description3 =
    .aria-label = { history-remember-label2 }
    .description = {COPY_PATTERN(from_path, "history-remember-description2.description")}

history-dontremember-description3 =
    .aria-label = { history-remember-label2 }
    .description = {COPY_PATTERN(from_path, "history-dontremember-description2.description")}

history-custom-description3 =
    .aria-label = { history-remember-label2 }
    .description = {COPY_PATTERN(from_path, "history-custom-description.description")}
""",
            from_path=source,
        ),
    )
