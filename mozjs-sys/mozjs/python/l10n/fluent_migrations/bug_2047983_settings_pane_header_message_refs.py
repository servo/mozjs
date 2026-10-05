# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2047983 - Update fluent strings for Settings pane headers that use message references, part {index}."""

    source = "browser/browser/preferences/preferences.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
pane-downloads3 =
    .heading = {COPY_PATTERN(from_path, "pane-downloads-title2")}

preferences-languages-header3 =
    .heading = {COPY_PATTERN(from_path, "pane-languages-title2")}

preferences-ai-controls-header3 =
    .heading = {COPY_PATTERN(from_path, "pane-ai-controls-title2")}
""",
            from_path=source,
        ),
    )
