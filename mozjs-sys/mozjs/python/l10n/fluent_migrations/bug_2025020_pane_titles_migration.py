# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2025020 - Add .title attributes to all pane titles consistently, part {index}."""

    source = "browser/browser/preferences/preferences.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
pane-search-title2 = {COPY_PATTERN(from_path, "pane-search-title")}
    .title = {COPY_PATTERN(from_path, "pane-search-title")}

pane-privacy-title3 = {COPY_PATTERN(from_path, "pane-privacy-title2")}
    .title = {COPY_PATTERN(from_path, "pane-privacy-title2")}

pane-ai-controls-title2 = {COPY_PATTERN(from_path, "pane-ai-controls-title")}
    .title = {COPY_PATTERN(from_path, "pane-ai-controls-title")}

pane-downloads-title2 = {COPY_PATTERN(from_path, "pane-downloads-title")}
    .title = {COPY_PATTERN(from_path, "pane-downloads-title")}

pane-languages-title2 = {COPY_PATTERN(from_path, "pane-languages-title")}
    .title = {COPY_PATTERN(from_path, "pane-languages-title")}

settings-pane-labs-title2 = {COPY_PATTERN(from_path, "settings-pane-labs-title")}
    .title = {COPY_PATTERN(from_path, "settings-pane-labs-title")}

pane-tabs-browsing-title2 = {COPY_PATTERN(from_path, "pane-tabs-browsing-title")}
    .title = {COPY_PATTERN(from_path, "pane-tabs-browsing-title")}

pane-account-sync-title2 = {COPY_PATTERN(from_path, "pane-account-sync-title")}
    .title = {COPY_PATTERN(from_path, "pane-account-sync-title")}

pane-passwords-autofill-title2 = {COPY_PATTERN(from_path, "pane-passwords-autofill-title")}
    .title = {COPY_PATTERN(from_path, "pane-passwords-autofill-title")}

pane-permissions-data-title2 = {COPY_PATTERN(from_path, "pane-permissions-data-title")}
    .title = {COPY_PATTERN(from_path, "pane-permissions-data-title")}

help-button-label2 = {COPY_PATTERN(from_path, "help-button-label")}
    .title = {COPY_PATTERN(from_path, "help-button-label")}

addons-button-label2 = {COPY_PATTERN(from_path, "addons-button-label")}
    .title = {COPY_PATTERN(from_path, "addons-button-label")}

preferences-ai-controls-header2 =
    .heading = { pane-ai-controls-title2 }

pane-downloads2 =
    .heading = { pane-downloads-title2 }

preferences-languages-header2 =
    .heading = { pane-languages-title2 }
""",
            from_path=source,
        ),
    )

    # Migration for more-from-moz-title in moreFromMozilla.ftl
    moreFromMozilla_source = "browser/browser/preferences/moreFromMozilla.ftl"
    moreFromMozilla_target = moreFromMozilla_source

    ctx.add_transforms(
        moreFromMozilla_target,
        moreFromMozilla_target,
        transforms_from(
            """
more-from-moz-title2 = {COPY_PATTERN(from_path, "more-from-moz-title")}
    .title = {COPY_PATTERN(from_path, "more-from-moz-title")}
""",
            from_path=moreFromMozilla_source,
        ),
    )
