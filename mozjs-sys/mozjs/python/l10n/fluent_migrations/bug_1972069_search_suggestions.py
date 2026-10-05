# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1972069 - Convert search engine suggestions section of search settings to config-based prefs, part {index}."""

    target = "browser/browser/preferences/preferences.ftl"

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
search-suggestions-cant-show-2 =
    .message = {COPY_PATTERN(from_path, "search-suggestions-cant-show.message")}
""",
            from_path=target,
        ),
    )
