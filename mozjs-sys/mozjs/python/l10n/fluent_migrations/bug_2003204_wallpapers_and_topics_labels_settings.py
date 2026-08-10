# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2003204 - Open customization panel from about:preferences#home, part {index}"""

    source = "browser/browser/preferences/preferences.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
home-prefs-manage-topics-link2 =
    .label = { COPY_PATTERN(from_path, "home-prefs-manage-topics-link")}

home-prefs-choose-wallpaper-link2 =
    .label = { COPY_PATTERN(from_path, "home-prefs-choose-wallpaper-link")}

""",
            from_path=source,
        ),
    )
