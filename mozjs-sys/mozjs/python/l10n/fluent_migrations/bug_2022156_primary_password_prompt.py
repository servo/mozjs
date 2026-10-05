# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2022156 - Migrate primary password prompt string to passwordmgr Fluent, part {index}."""

    source = "security/manager/chrome/pipnss/pipnss.properties"
    target = "toolkit/toolkit/passwordmgr/passwordmgr.ftl"

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
primary-password-prompt-message = { COPY(from_path, "CertPasswordPromptDefault") }
""",
            from_path=source,
        ),
    )
