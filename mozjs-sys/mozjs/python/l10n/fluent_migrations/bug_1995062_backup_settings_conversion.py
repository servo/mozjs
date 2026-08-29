# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.transforms import COPY_PATTERN
import fluent.syntax.ast as FTL


def migrate(ctx):
    """Bug 1995062 - Convert Backup settings to config-based prefs, part {index}"""

    path = "browser/browser/backupSettings.ftl"

    ctx.add_transforms(
        path,
        path,
        [
            FTL.Message(
                id=FTL.Identifier("settings-data-backup-toggle-on2"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("label"),
                        value=COPY_PATTERN(path, "settings-data-backup-toggle-on"),
                    ),
                ],
            ),
            FTL.Message(
                id=FTL.Identifier("settings-data-backup-toggle-off2"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("label"),
                        value=COPY_PATTERN(path, "settings-data-backup-toggle-off"),
                    ),
                ],
            ),
        ],
    )
