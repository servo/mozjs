# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2009106 - Create a skeleton subpage for Custom URL, part {index}."""

    source = "browser/browser/preferences/preferences.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
home-custom-homepage-subpage =
    .heading = { COPY_PATTERN(from_path, "home-custom-homepage-header")}

home-custom-homepage-card =
    .heading = { COPY_PATTERN(from_path, "home-custom-homepage-subheader")}

""",
            from_path=source,
        ),
    )
