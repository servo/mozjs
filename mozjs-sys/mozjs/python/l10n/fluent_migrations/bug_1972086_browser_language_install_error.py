# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1972086 - Migrate browser language install error to SRD, part {index}."""

    source = "browser/browser/preferences/languages.ftl"
    target = "browser/browser/preferences/preferences.ftl"

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
browser-language-install-error =
    .message = {COPY_PATTERN(from_path, "browser-languages-error")}
""",
            from_path=source,
        ),
    )
