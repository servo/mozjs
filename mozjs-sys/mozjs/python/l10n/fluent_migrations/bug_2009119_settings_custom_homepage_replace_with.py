# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2009119 - Custom Homepage Settings: wire up "Replace with" buttons, part {index}."""

    source = "browser/browser/preferences/preferences.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
home-custom-homepage-replace-with-prompt =
    .label = { COPY_PATTERN(from_path, "home-custom-homepage-replace-with")}
""",
            from_path=source,
        ),
    )
