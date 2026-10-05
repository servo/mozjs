# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1969953 - Move Homepage settings strings to .label attributes, part {index}."""

    source = "browser/browser/preferences/preferences.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
home-homepage-title =
    .label = { COPY_PATTERN(from_path, "home-homepage-header") }

home-homepage-new-windows =
    .label = { COPY_PATTERN(from_path, "home-homepage-mode-label2") }

home-homepage-new-tabs =
    .label = { COPY_PATTERN(from_path, "home-newtabs-mode-label") }

home-homepage-custom-homepage-button =
    .label = { COPY_PATTERN(from_path, "home-homepage-custom-homepage-url") }
""",
            from_path=source,
        ),
    )
