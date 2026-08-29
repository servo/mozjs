# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate import COPY_PATTERN
from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1996822 - Migrate string from sponsored newtab weather string to new aria-description, maintaining title, part {index}."""
    source = "browser/browser/newtab/newtab.ftl"
    target = source

    ctx.add_transforms(
        "browser/browser/newtab/newtab.ftl",
        "browser/browser/newtab/newtab.ftl",
        transforms_from(
            """
newtab-weather-see-forecast-description =
    .title = {COPY_PATTERN(from_path, "newtab-weather-see-forecast.title")}
    .aria-description = {COPY_PATTERN(from_path, "newtab-weather-sponsored")}
""",
            from_path="browser/browser/newtab/newtab.ftl",
        ),
    )
