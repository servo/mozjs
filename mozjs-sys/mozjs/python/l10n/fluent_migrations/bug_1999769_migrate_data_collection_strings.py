# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1999769 - Convert Data Collection section to config-based structure, part {index}."""

    source = "browser/browser/preferences/preferences.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
data-collection =
    .label = {COPY_PATTERN(from_path, "collection-header2")}
    .description = {COPY_PATTERN(from_path, "preferences-collection-description")}
    .searchkeywords = {COPY_PATTERN(from_path, "collection-header2.searchkeywords")}

data-collection-link = {COPY_PATTERN(from_path, "preferences-collection-privacy-notice")}

data-collection-preferences-across-profiles =
    .message = {COPY_PATTERN(from_path, "preferences-across-profiles")}

data-collection-profiles-link = {COPY_PATTERN(from_path, "preferences-view-profiles")}

data-collection-health-report-telemetry-disabled =
    .message = {COPY_PATTERN(from_path, "collection-health-report-telemetry-disabled")}

data-collection-health-report =
    .label = {COPY_PATTERN(from_path, "collection-health-report2.label")}
    .accesskey = {COPY_PATTERN(from_path, "collection-health-report2.accesskey")}
    .description = {COPY_PATTERN(from_path, "collection-health-report-description")}

data-collection-studies-link =
    .label = {COPY_PATTERN(from_path, "collection-studies-link")}

data-collection-usage-ping =
    .label = {COPY_PATTERN(from_path, "collection-usage-ping.label")}
    .description = {COPY_PATTERN(from_path, "collection-usage-ping-description")}
    .accesskey = {COPY_PATTERN(from_path, "collection-usage-ping.accesskey")}

addon-recommendations3 =
    .label = {COPY_PATTERN(from_path, "addon-recommendations2.label")}
    .description = {COPY_PATTERN(from_path, "addon-recommendations-description")}

""",
            from_path=source,
        ),
    )
