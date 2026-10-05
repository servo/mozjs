# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate import COPY_PATTERN
from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2026324 - Seed home-prefs-homepage-extension-option from browser-utils-url-extension, part {index}."""

    source = "toolkit/toolkit/global/browser-utils.ftl"
    target = "browser/browser/preferences/preferences.ftl"

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
home-prefs-homepage-extension-option =
    .label = {COPY_PATTERN(from_path, "browser-utils-url-extension")}
""",
            from_path=source,
        ),
    )
