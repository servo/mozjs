# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1998985 - Use HTML elements for search mode switcher, part {index}."""

    source = "browser/browser/browser.ftl"

    ctx.add_transforms(
        source,
        source,
        transforms_from(
            """
urlbar-searchmode-button3 =
    .title = {COPY_PATTERN(from_path, "urlbar-searchmode-button2.tooltiptext")}

urlbar-searchmode-button-no-engine2 =
    .title = {COPY_PATTERN(from_path, "urlbar-searchmode-button-no-engine.tooltiptext")}

urlbar-searchmode-no-keyword2 =
    .title = {COPY_PATTERN(from_path, "urlbar-searchmode-no-keyword.tooltiptext")}

urlbar-searchmode-dropmarker2 =
    .title = {COPY_PATTERN(from_path, "urlbar-searchmode-dropmarker.tooltiptext")}

urlbar-searchmode-bookmarks2 = {COPY_PATTERN(from_path, "urlbar-searchmode-bookmarks.label")}

urlbar-searchmode-tabs2 = {COPY_PATTERN(from_path, "urlbar-searchmode-tabs.label")}

urlbar-searchmode-history2 = {COPY_PATTERN(from_path, "urlbar-searchmode-history.label")}

urlbar-searchmode-actions2 = {COPY_PATTERN(from_path, "urlbar-searchmode-actions.label")}

urlbar-searchmode-exit-button2 =
    .title = {COPY_PATTERN(from_path, "urlbar-searchmode-exit-button.tooltiptext")}

urlbar-searchmode-default2 =
    .title = {COPY_PATTERN(from_path, "urlbar-searchmode-default.tooltiptext")}

urlbar-go-button2 =
    .title = {COPY_PATTERN(from_path, "urlbar-go-button.tooltiptext")}

urlbar-searchmode-popup-one-off-header = {COPY_PATTERN(from_path, "urlbar-searchmode-popup-one-off-description-menucaption.label")}

urlbar-searchmode-popup-header = {COPY_PATTERN(from_path, "urlbar-searchmode-popup-header-menucaption.label")}

urlbar-searchmode-popup-search-settings-panelitem = {COPY_PATTERN(from_path, "urlbar-searchmode-popup-search-settings-menuitem.label")}

urlbar-searchmode-popup-add-engine = {COPY_PATTERN(from_path, "search-one-offs-add-engine.label")}
    .title = {COPY_PATTERN(from_path, "search-one-offs-add-engine.tooltiptext")}
""",
            from_path=source,
        ),
    )
