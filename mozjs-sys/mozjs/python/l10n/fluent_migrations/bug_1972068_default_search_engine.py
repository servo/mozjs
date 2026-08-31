# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1972068 - Convert default search engine section of search settings to config-based prefs, part {index}."""

    target = "browser/browser/preferences/preferences.ftl"

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
search-engine-group =
    .label = {COPY_PATTERN(from_path, "search-engine-default-header")}
search-default-engine =
    .aria-label = {COPY_PATTERN(from_path, "search-engine-default-header")}
""",
            from_path=target,
        ),
    )
