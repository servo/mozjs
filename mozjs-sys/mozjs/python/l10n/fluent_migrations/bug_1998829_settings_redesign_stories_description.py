# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1998829 - Add 'Stories' section to Firefox Home Settings Redesign, part {index}"""

    source = "browser/browser/preferences/preferences.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
home-prefs-stories-header2 =
    .label = { COPY_PATTERN(from_path, "home-prefs-stories-header.label")}
    .description = { COPY_PATTERN(from_path, "home-prefs-recommended-by-description-generic")}
""",
            from_path=source,
        ),
    )
