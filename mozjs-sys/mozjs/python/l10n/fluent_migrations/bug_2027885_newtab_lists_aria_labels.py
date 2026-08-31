# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate import COPY_PATTERN
from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2027885 - Preserve translations for newtab Lists aria labels, part {index}."""
    source = "browser/browser/newtab/newtab.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
newtab-widget-lists-name-placeholder-checklist2 =
    .placeholder = {COPY_PATTERN(from_path, "newtab-widget-lists-name-placeholder-checklist.placeholder")}
    .aria-label = {COPY_PATTERN(from_path, "newtab-widget-lists-menu-edit")}

newtab-widget-lists-name-placeholder-new2 =
    .placeholder = {COPY_PATTERN(from_path, "newtab-widget-lists-name-placeholder-new.placeholder")}
    .aria-label = {COPY_PATTERN(from_path, "newtab-widget-lists-menu-edit")}

newtab-widget-lists-menu-edit2 =
    .aria-label = {COPY_PATTERN(from_path, "newtab-widget-lists-menu-edit")}

newtab-widget-lists-button-add-item =
    {COPY_PATTERN(from_path, "newtab-widget-lists-input-add-an-item.placeholder")}

newtab-widget-lists-input-add-an-item2 =
    .placeholder = {COPY_PATTERN(from_path, "newtab-widget-lists-input-add-an-item.placeholder")}
    .aria-label = {COPY_PATTERN(from_path, "newtab-widget-lists-input-add-an-item.placeholder")}
""",
            from_path=source,
        ),
    )
