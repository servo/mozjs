# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate import COPY_PATTERN
from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1992201 - Add settings sub-pane for profiles, part {index}."""
    source = "browser/browser/preferences/preferences.ftl"
    target = source

    ctx.add_transforms(
        target,
        source,
        transforms_from(
            """
preferences-profiles-group-header =
    .heading = {COPY_PATTERN(from_path, "preferences-profiles-header")}
preferences-profiles-subpane-description =
    .description = {COPY_PATTERN(from_path, "preferences-manage-profiles-description")}
preferences-profiles-section-header =
    .label = {COPY_PATTERN(from_path, "preferences-profiles-header")}
    .description = {COPY_PATTERN(from_path, "preferences-manage-profiles-description")}
""",
            from_path=source,
        ),
    )
