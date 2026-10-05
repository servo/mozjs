# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate import COPY_PATTERN
from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2039925 - Move New Tab preference strings into newtab.ftl, with -srd suffix for variants shared with the legacy renderer, part {index}."""

    source = "browser/browser/preferences/preferences.ftl"
    newtab_target = "browser/browser/newtab/newtab.ftl"

    ctx.add_transforms(
        newtab_target,
        newtab_target,
        transforms_from(
            """
home-prefs-homepage-extension-option =
    .label = {COPY_PATTERN(from_path, "home-prefs-homepage-extension-option.label")}

home-homepage-title =
    .label = {COPY_PATTERN(from_path, "home-homepage-title.label")}

home-homepage-new-windows =
    .label = {COPY_PATTERN(from_path, "home-homepage-new-windows.label")}

home-homepage-new-tabs =
    .label = {COPY_PATTERN(from_path, "home-homepage-new-tabs.label")}

home-homepage-custom-homepage-button =
    .label = {COPY_PATTERN(from_path, "home-homepage-custom-homepage-button.label")}

home-custom-homepage-card-header =
    .label = {COPY_PATTERN(from_path, "home-custom-homepage-card-header.label")}

home-custom-homepage-address =
    .placeholder = {COPY_PATTERN(from_path, "home-custom-homepage-address.placeholder")}

home-custom-homepage-address-button =
    .label = {COPY_PATTERN(from_path, "home-custom-homepage-address-button.label")}

home-custom-homepage-no-results =
    .label = {COPY_PATTERN(from_path, "home-custom-homepage-no-results.label")}

home-custom-homepage-delete-address-button =
    .aria-label = {COPY_PATTERN(from_path, "home-custom-homepage-delete-address-button.aria-label")}
    .title = {COPY_PATTERN(from_path, "home-custom-homepage-delete-address-button.title")}

home-custom-homepage-replace-with-prompt =
    .label = {COPY_PATTERN(from_path, "home-custom-homepage-replace-with-prompt.label")}

home-custom-homepage-current-pages-button =
    .label = {COPY_PATTERN(from_path, "home-custom-homepage-current-pages-button.label")}

home-custom-homepage-bookmarks-button =
    .label = {COPY_PATTERN(from_path, "home-custom-homepage-bookmarks-button.label")}

home-prefs-content-header =
    .label = {COPY_PATTERN(from_path, "home-prefs-content-header.label")}

home-prefs-search-header2 =
    .label = {COPY_PATTERN(from_path, "home-prefs-search-header2.label")}

home-prefs-stories-header2 =
    .label = {COPY_PATTERN(from_path, "home-prefs-stories-header2.label")}
    .description = {COPY_PATTERN(from_path, "home-prefs-stories-header2.description")}

home-prefs-widgets-header =
    .label = {COPY_PATTERN(from_path, "home-prefs-widgets-header.label")}

home-prefs-lists-header =
    .label = {COPY_PATTERN(from_path, "home-prefs-lists-header.label")}

home-prefs-timer-header =
    .label = {COPY_PATTERN(from_path, "home-prefs-timer-header.label")}

home-prefs-sports-widget-header =
    .label = {COPY_PATTERN(from_path, "home-prefs-sports-widget-header.label")}

home-prefs-clocks-header =
    .label = {COPY_PATTERN(from_path, "home-prefs-clocks-header.label")}

home-prefs-mission-message2 =
    .message = {COPY_PATTERN(from_path, "home-prefs-mission-message2.message")}

home-prefs-manage-topics-link2 =
    .label = {COPY_PATTERN(from_path, "home-prefs-manage-topics-link2.label")}

home-prefs-choose-wallpaper-link2 =
    .label = {COPY_PATTERN(from_path, "home-prefs-choose-wallpaper-link2.label")}

home-prefs-firefox-home-disabled-notice =
    .message = {COPY_PATTERN(from_path, "home-prefs-firefox-home-disabled-notice.message")}

home-prefs-sections-rows-option-srd =
    .label = {COPY_PATTERN(from_path, "home-prefs-sections-rows-option.label")}

home-restore-defaults-srd =
    .label = {COPY_PATTERN(from_path, "home-restore-defaults.label")}
    .accesskey = {COPY_PATTERN(from_path, "home-restore-defaults.accesskey")}

home-mode-choice-default-fx-srd =
    .label = {COPY_PATTERN(from_path, "home-mode-choice-default-fx.label")}

home-mode-choice-custom-srd =
    .label = {COPY_PATTERN(from_path, "home-mode-choice-custom.label")}

home-mode-choice-blank-srd =
    .label = {COPY_PATTERN(from_path, "home-mode-choice-blank.label")}

home-prefs-shortcuts-header-srd =
    .label = {COPY_PATTERN(from_path, "home-prefs-shortcuts-header.label")}

home-prefs-shortcuts-select =
    .aria-label = {COPY_PATTERN(from_path, "home-prefs-shortcuts-header.label")}

home-prefs-shortcuts-by-option-sponsored-srd =
    .label = {COPY_PATTERN(from_path, "home-prefs-shortcuts-by-option-sponsored.label")}

home-prefs-recommended-by-option-sponsored-stories-srd =
    .label = {COPY_PATTERN(from_path, "home-prefs-recommended-by-option-sponsored-stories.label")}

home-prefs-highlights-option-visited-pages-srd =
    .label = {COPY_PATTERN(from_path, "home-prefs-highlights-option-visited-pages.label")}

home-prefs-highlights-options-bookmarks-srd =
    .label = {COPY_PATTERN(from_path, "home-prefs-highlights-options-bookmarks.label")}

home-prefs-highlights-option-most-recent-download-srd =
    .label = {COPY_PATTERN(from_path, "home-prefs-highlights-option-most-recent-download.label")}

home-prefs-recent-activity-header-srd =
    .label = {COPY_PATTERN(from_path, "home-prefs-recent-activity-header.label")}

home-prefs-recent-activity-select =
    .aria-label = {COPY_PATTERN(from_path, "home-prefs-recent-activity-header.label")}

home-prefs-weather-header-srd =
    .label = {COPY_PATTERN(from_path, "home-prefs-weather-header.label")}

home-prefs-support-firefox-header-srd =
    .label = {COPY_PATTERN(from_path, "home-prefs-support-firefox-header.label")}

home-prefs-mission-message-learn-more-link-srd = {COPY_PATTERN(from_path, "home-prefs-mission-message-learn-more-link")}
""",
            from_path=source,
        ),
    )

    # home-prefs-firefox-logo-header reuses the translation of the profile
    # window's "{ -brand-short-name } logo" alt text, which lives in a
    # different source file.
    profiles_source = "browser/browser/profiles.ftl"
    ctx.add_transforms(
        newtab_target,
        newtab_target,
        transforms_from(
            """
home-prefs-firefox-logo-header =
    .label = {COPY_PATTERN(from_path, "profile-window-logo.alt")}
""",
            from_path=profiles_source,
        ),
    )
