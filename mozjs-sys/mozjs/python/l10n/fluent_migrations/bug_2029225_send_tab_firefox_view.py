# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2029225 - Convert Send Tab Firefox String Attributes, part {index}."""

    target = "browser/browser/fxviewTabList.ftl"

    ctx.add_transforms(
        target,
        target,
        transforms_from(
"""
fxviewtabrow-send-to-mobile-enable-sync2  = {COPY_PATTERN(from_path, "fxviewtabrow-send-to-mobile-enable-sync.label")}
fxviewtabrow-send-to-mobile-connect-phone2  = {COPY_PATTERN(from_path, "fxviewtabrow-send-to-mobile-connect-phone.label")}
fxviewtabrow-send-to-mobile-device-missing2  = {COPY_PATTERN(from_path, "fxviewtabrow-send-to-mobile-device-missing.label")}
""",
            from_path=target,
        ),
    )
