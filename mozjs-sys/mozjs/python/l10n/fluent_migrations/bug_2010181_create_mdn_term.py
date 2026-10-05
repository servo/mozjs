# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2010181 - Migrate value for MDN brand name, part {index}."""

    source = "browser/browser/preferences/moreFromMozilla.ftl"
    target = "toolkit/toolkit/branding/brandings.ftl"

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
-mdn-brand-name = { COPY_PATTERN(from_path, "more-from-moz-mdn-title")}
-yelp-brand-name = Yelp
""",
            from_path=source,
        ),
    )
