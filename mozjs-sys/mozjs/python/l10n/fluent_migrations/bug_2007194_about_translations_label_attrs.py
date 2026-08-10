# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2007194 - Migrate about:translations to moz-select, part {index}."""

    source = "toolkit/toolkit/about/aboutTranslations.ftl"
    target = source

    ctx.add_transforms(
        target,
        source,
        transforms_from(
            """
about-translations-detect-default-label =
  .label = { COPY_PATTERN(from_path, "about-translations-detect-default") }

about-translations-detect-language-label =
  .label = { COPY_PATTERN(from_path, "about-translations-detect-language") }

about-translations-select-label =
  .label = { COPY_PATTERN(from_path, "about-translations-select") }
            """,
            from_path=source,
        ),
    )
