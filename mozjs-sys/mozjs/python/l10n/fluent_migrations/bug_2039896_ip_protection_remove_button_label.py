# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2039896 - Add .label attribute to IP Protection remove button string, part {index}."""

    source = "browser/browser/ipProtection.ftl"
    target = source

    ctx.add_transforms(
        target,
        source,
        transforms_from(
            """
ipprotection-feature-introduction-button-secondary-remove-1 =
    .label = { COPY_PATTERN(from_path, "ipprotection-feature-introduction-button-secondary-remove") }
""",
            from_path=source,
        ),
    )
