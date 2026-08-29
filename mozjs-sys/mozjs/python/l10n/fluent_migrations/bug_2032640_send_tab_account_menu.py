# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2032640 - Convert Send Tab Account Menu String Attributes, part {index}."""

    target = "browser/browser/sync.ftl"

    ctx.add_transforms(
        target,
        target,
        transforms_from(
"""
fxa-menu-send-to-mobile-enable-sync2 = {COPY_PATTERN(from_path, "fxa-menu-send-to-mobile-enable-sync.label")}
fxa-menu-send-to-mobile-connect-phone2 = {COPY_PATTERN(from_path, "fxa-menu-send-to-mobile-connect-phone.label")}
fxa-menu-send-to-mobile-device-missing2 = {COPY_PATTERN(from_path, "fxa-menu-send-to-mobile-device-missing.label")}
""",
            from_path=target,
        ),
    )
