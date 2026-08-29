# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1972072 - Convert Address Bar - Firefox Suggest to config-based prefs, part {index}."""

    target = "browser/browser/preferences/preferences.ftl"

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
addressbar-header-1 =
    .label = {COPY_PATTERN(from_path, "addressbar-header")}
    .description = {COPY_PATTERN(from_path, "addressbar-suggest-1")}
addressbar-header-firefox-suggest-2 =
    .label = {COPY_PATTERN(from_path, "addressbar-header-firefox-suggest-1")}
    .description = {COPY_PATTERN(from_path, "addressbar-suggest-firefox-suggest-1")}
addressbar-locbar-suggest-all-option-2 =
    .label = {COPY_PATTERN(from_path, "addressbar-locbar-suggest-all-option.label")}
    .description = {COPY_PATTERN(from_path, "addressbar-locbar-suggest-all-option-desc")}
addressbar-locbar-suggest-sponsored-option-2 =
    .label = {COPY_PATTERN(from_path, "addressbar-locbar-suggest-sponsored-option.label")}
    .description = {COPY_PATTERN(from_path, "addressbar-locbar-suggest-sponsored-desc")}
addressbar-dismissed-suggestions-label-2 =
    .label = {COPY_PATTERN(from_path, "addressbar-dismissed-suggestions-label")}
    .description = {COPY_PATTERN(from_path, "addressbar-restore-dismissed-suggestions-description")}
""",
            from_path=target,
        ),
    )
