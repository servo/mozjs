# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1710459 - Migrate the description to a menucaption label, part {index}."""

    source = "browser/browser/browser.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
urlbar-searchmode-popup-description-menucaption =
    .label = {COPY_PATTERN(from_path, "urlbar-searchmode-popup-description")}
urlbar-searchmode-popup-sticky-description-menucaption =
    .label = {COPY_PATTERN(from_path, "urlbar-searchmode-popup-sticky-description")}
""",
            from_path=source,
        ),
    )
