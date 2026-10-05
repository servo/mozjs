# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1968118 - Migrate pane-privacy-title to pane-privacy-title2 and pane-privacy-section, part {index}."""

    source = "browser/browser/preferences/preferences.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
pane-privacy-title2 = {COPY_PATTERN(from_path, "pane-privacy-title")}
pane-privacy-section =
    .heading = {COPY_PATTERN(from_path, "pane-privacy-title")}
""",
            from_path=source,
        ),
    )
