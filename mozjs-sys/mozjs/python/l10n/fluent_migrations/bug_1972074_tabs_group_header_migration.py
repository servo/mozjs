# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import fluent.syntax.ast as FTL
from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1972074 - Migrate tabs-group-header to .label, part {index}"""
    source = "browser/browser/preferences/preferences.ftl"
    target = source

    ctx.add_transforms(
        target,
        source,
        transforms_from(
            """
tabs-group-header2 =
    .label = {COPY_PATTERN(from_path, "tabs-group-header.label")}
""",
            from_path=source,
        ),
    )
