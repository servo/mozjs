# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.transforms import COPY_PATTERN
from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2028605 - refresh the UX for the Report Broken Site feature, part {index}."""
    path = "browser/browser/reportBrokenSite.ftl"

    ctx.add_transforms(
        path,
        path,
        transforms_from(
            ""
report-broken-site-panel-reason-load-moz-box-button =
    .label = {COPY_PATTERN(from_path, "report-broken-site-panel-reason-load")}
report-broken-site-panel-reason-checkout-moz-box-button =
    .label = {COPY_PATTERN(from_path, "report-broken-site-panel-reason-checkout")}
report-broken-site-panel-reason-slow-moz-box-button =
    .label = {COPY_PATTERN(from_path, "report-broken-site-panel-reason-slow2")}
report-broken-site-panel-reason-media-moz-box-button =
    .label = {COPY_PATTERN(from_path, "report-broken-site-panel-reason-media2")}
report-broken-site-panel-reason-content-moz-box-button =
    .label = {COPY_PATTERN(from_path, "report-broken-site-panel-reason-content2")}
report-broken-site-panel-reason-account-moz-box-button =
    .label = {COPY_PATTERN(from_path, "report-broken-site-panel-reason-account2")}
report-broken-site-panel-reason-adblocker-moz-box-button =
    .label = {COPY_PATTERN(from_path, "report-broken-site-panel-reason-adblocker2")}
report-broken-site-panel-reason-notsupported-moz-box-button =
    .label = {COPY_PATTERN(from_path, "report-broken-site-panel-reason-notsupported")}
report-broken-site-panel-reason-other-moz-box-button =
    .label = {COPY_PATTERN(from_path, "report-broken-site-panel-reason-other")}
report-broken-site-panel-send-more-info-button =
    .label = {COPY_PATTERN(from_path, "report-broken-site-panel-send-more-info-link")}
report-broken-site-panel-preview-header2 =
    .title = {COPY_PATTERN(from_path, "report-broken-site-panel-preview-header.label")}
""",
            from_path=path,
        ),
    )
