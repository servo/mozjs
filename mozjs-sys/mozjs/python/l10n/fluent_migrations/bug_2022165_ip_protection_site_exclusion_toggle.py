# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2022165 - Use VPN for this site toggle not read correctly by screen reader, part {index}."""

    source = "browser/browser/ipProtection.ftl"
    target = source

    ctx.add_transforms(
        target,
        source,
        transforms_from(
            """
site-exclusion-toggle-enabled-1 =
    .label = { COPY_PATTERN(from_path, "site-exclusion-toggle-label") }
    .aria-label = { COPY_PATTERN(from_path, "site-exclusion-toggle-enabled.aria-label") }
site-exclusion-toggle-disabled-1 =
    .label = { COPY_PATTERN(from_path, "site-exclusion-toggle-label") }
    .aria-label = { COPY_PATTERN(from_path, "site-exclusion-toggle-disabled.aria-label") }
""",
            from_path=source,
        ),
    )
