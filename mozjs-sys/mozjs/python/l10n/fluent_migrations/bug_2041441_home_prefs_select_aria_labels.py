# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2041441 - [a11y] Multiple inputs are unlabeled, part {index}."""

    source = "browser/browser/preferences/preferences.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
home-prefs-shortcuts-select =
    .aria-label = {COPY_PATTERN(from_path, "home-prefs-shortcuts-header.label")}

home-prefs-recent-activity-select =
    .aria-label = {COPY_PATTERN(from_path, "home-prefs-recent-activity-header.label")}
""",
            from_path=source,
        ),
    )
