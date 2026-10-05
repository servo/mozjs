# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2040360 - Settings-redesign: Custom tracking protection cookies drop down missing "block" context, part {index}."""

    source = "browser/browser/preferences/preferences.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
preferences-etp-custom-cookie-behavior-accept-all =
    .label = {COPY_PATTERN(from_path, "preferences-etpc-custom-cookie-behavior-accept-all.label")}

preferences-etp-custom-cookie-behavior-isolate-cross-site-cookies =
    .label = {COPY_PATTERN(from_path, "sitedata-option-block-cross-site-cookies2.label")}
""",
            from_path=source,
        ),
    )
