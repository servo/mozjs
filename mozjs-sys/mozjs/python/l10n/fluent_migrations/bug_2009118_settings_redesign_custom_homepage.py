# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2009118 - Custom Homepage Settings: show a reorderable, deletable list of user-added custom URLs, part {index}."""

    source = "browser/browser/preferences/preferences.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
home-custom-homepage-card-header =
    .label = { COPY_PATTERN(from_path, "home-custom-homepage-subheader")}

home-custom-homepage-no-results =
    .label = { COPY_PATTERN(from_path, "home-custom-homepage-no-websites-yet")}
""",
            from_path=source,
        ),
    )
