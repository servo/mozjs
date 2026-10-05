# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate import COPY_PATTERN
from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1972371 - Migrate permissions settings content for the settings redesign part {index}."""
    path = "browser/browser/preferences/preferences.ftl"
    ctx.add_transforms(
        path,
        path,
        transforms_from(
            """
permissions-location2 =
    .label = {COPY_PATTERN(from_path, "permissions-location")}
permissions-localhost2 =
    .label = {COPY_PATTERN(from_path, "permissions-localhost")}
permissions-local-network2 =
    .label = {COPY_PATTERN(from_path, "permissions-local-network")}
permissions-xr2 =
    .label = {COPY_PATTERN(from_path, "permissions-xr")}
permissions-camera2 =
    .label = {COPY_PATTERN(from_path, "permissions-camera")}
permissions-microphone2 =
    .label = {COPY_PATTERN(from_path, "permissions-microphone")}
permissions-notification2 =
    .label = {COPY_PATTERN(from_path, "permissions-notification")}
permissions-autoplay2 =
    .label = {COPY_PATTERN(from_path, "permissions-autoplay")}
""",
            from_path=path,
        ),
    )
