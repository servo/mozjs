# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate import COPY_PATTERN
from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2044507 - Add Firefox logo toggle to about:settings#home, part {index}."""

    source = "browser/browser/profiles.ftl"
    target = "browser/browser/preferences/preferences.ftl"

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
home-prefs-firefox-logo-header =
    .label = {COPY_PATTERN(from_path, "profile-window-logo.alt")}
""",
            from_path=source,
        ),
    )
