# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2042353 - Label about:policies nav buttons when the sidebar is collapsed, part {index}."""

    source = "browser/browser/aboutPolicies.ftl"
    target = source

    ctx.add_transforms(
        target,
        source,
        transforms_from(
            """
active-policies-tab-title =
    .title = { COPY_PATTERN(from_path, "active-policies-tab") }
errors-tab-title =
    .title = { COPY_PATTERN(from_path, "errors-tab") }
documentation-tab-title =
    .title = { COPY_PATTERN(from_path, "documentation-tab") }
""",
            from_path=source,
        ),
    )
