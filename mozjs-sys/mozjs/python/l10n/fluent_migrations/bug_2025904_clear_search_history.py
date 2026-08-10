# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2025904 - Migrate searchbar clear history strings, part {index}."""

    source = "browser/chrome/browser/search.properties"
    target = "browser/browser/browser.ftl"

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
clear-search-history =
    .label = { COPY(from_path, "cmd_clearHistory") }
    .accesskey = { COPY(from_path, "cmd_clearHistory_accesskey") }
""",
            from_path=source,
        ),
    )
