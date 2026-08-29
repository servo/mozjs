# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1994888 - Always use setting-group for group headings, part {index}."""

    target = "browser/browser/preferences/preferences.ftl"

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
startup-group =
    .label = {COPY_PATTERN(from_path, "startup-header")}

appearance-group =
    .label = {COPY_PATTERN(from_path, "preferences-web-appearance-header")}
    .description = {COPY_PATTERN(from_path, "preferences-web-appearance-description")}

drm-group =
    .label = {COPY_PATTERN(from_path, "drm-content-header")}

performance-group =
    .label = {COPY_PATTERN(from_path, "performance-title")}

browsing-group =
    .label = {COPY_PATTERN(from_path, "browsing-title")}

network-proxy-group =
    .label = {COPY_PATTERN(from_path, "network-settings-title")}
    .description = {COPY_PATTERN(from_path, "network-proxy-connection-description")}

non-technical-privacy-group =
    .label = {COPY_PATTERN(from_path, "non-technical-privacy-header")}

cookies-site-data-group =
    .label = {COPY_PATTERN(from_path, "sitedata-header")}

history-group =
    .label = {COPY_PATTERN(from_path, "history-header")}

browsing-protection-group =
    .label = {COPY_PATTERN(from_path, "security-browsing-protection")}

httpsonly-group =
    .label = {COPY_PATTERN(from_path, "httpsonly-header")}
    .description = {COPY_PATTERN(from_path, "httpsonly-label.description")}

payments-group =
    .label = {COPY_PATTERN(from_path, "autofill-payment-methods-title")}

addresses-group =
    .label = {COPY_PATTERN(from_path, "autofill-addresses-title")}

dns-over-https-group =
    .label = {COPY_PATTERN(from_path, "preferences-doh-header")}

history-remember-description4 =
    .aria-label = { history-group.label }
    .description = {COPY_PATTERN(from_path, "history-remember-description3.description")}

history-dontremember-description4 =
    .aria-label = { history-group.label }
    .description = {COPY_PATTERN(from_path, "history-dontremember-description3.description")}

history-custom-description4 =
    .aria-label = { history-group.label }
    .description = {COPY_PATTERN(from_path, "history-custom-description3.description")}
""",
            from_path=target,
        ),
    )
