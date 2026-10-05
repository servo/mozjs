# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate import COPY_PATTERN
from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2028317 - Migrate recommended stories toggle label for Nova, part {index}."""
    source = "browser/browser/newtab/newtab.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
newtab-recommended-stories-toggle =
    .label = {COPY_PATTERN(from_path, "newtab-custom-stories-toggle.label")}
""",
            from_path=source,
        ),
    )
