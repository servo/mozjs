# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2005798 - Remove label property from PDF Annotations strings, part {index}."""

    source = "browser/browser/newtab/asrouter.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
annotations-make-default-pdf-primary-cta-label = {COPY_PATTERN(from_path, "annotations-make-default-pdf-primary-cta.label")}
annotations-make-default-pdf-next-label = {COPY_PATTERN(from_path, "annotations-make-default-pdf-next.label")}
""",
            from_path=source,
        ),
    )
