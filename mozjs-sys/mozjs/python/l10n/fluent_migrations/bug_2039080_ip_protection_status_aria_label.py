# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2039080 - Add aria-label to VPN panel header for macOS VO, part {index}."""

    source = "browser/browser/ipProtection.ftl"
    target = source

    ctx.add_transforms(
        target,
        source,
        transforms_from(
            """
ipprotection-connection-status-connected-1 = { COPY_PATTERN(from_path, "ipprotection-connection-status-connected") }
    .aria-label = { COPY_PATTERN(from_path, "ipprotection-connection-status-connected") }
ipprotection-connection-status-disconnected-1 = { COPY_PATTERN(from_path, "ipprotection-connection-status-disconnected") }
    .aria-label = { COPY_PATTERN(from_path, "ipprotection-connection-status-disconnected") }
ipprotection-connection-status-excluded-1 = { COPY_PATTERN(from_path, "ipprotection-connection-status-excluded") }
    .aria-label = { COPY_PATTERN(from_path, "ipprotection-connection-status-excluded") }
ipprotection-connection-status-connecting-1 = { COPY_PATTERN(from_path, "ipprotection-connection-status-connecting") }
    .aria-label = { COPY_PATTERN(from_path, "ipprotection-connection-status-connecting") }
ipprotection-connection-status-paused-title-2 = { COPY_PATTERN(from_path, "ipprotection-connection-status-paused-title-1") }
    .aria-label = { COPY_PATTERN(from_path, "ipprotection-connection-status-paused-title-1") }
ipprotection-connection-status-generic-error-title-1 = { COPY_PATTERN(from_path, "ipprotection-connection-status-generic-error-title") }
    .aria-label = { COPY_PATTERN(from_path, "ipprotection-connection-status-generic-error-title") }
ipprotection-connection-status-network-error-title-1 = { COPY_PATTERN(from_path, "ipprotection-connection-status-network-error-title") }
    .aria-label = { COPY_PATTERN(from_path, "ipprotection-connection-status-network-error-title") }
ipprotection-connection-status-blocked-error-title-1 = { COPY_PATTERN(from_path, "ipprotection-connection-status-blocked-error-title") }
    .aria-label = { COPY_PATTERN(from_path, "ipprotection-connection-status-blocked-error-title") }
""",
            from_path=source,
        ),
    )
