# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import fluent.syntax.ast as FTL
from fluent.migrate import COPY_PATTERN


def migrate(ctx):
    """Bug 2034596 - Add Nova styling for address doorhanger, part {index}."""
    path = "browser/browser/preferences/formAutofill.ftl"
    ctx.add_transforms(
        path,
        path,
        [
            FTL.Message(
                id=FTL.Identifier("address-capture-edit-address-link"),
                value=COPY_PATTERN(path, "address-capture-edit-address-button.aria-label"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("aria-label"),
                        value=COPY_PATTERN(
                            path, "address-capture-edit-address-button.aria-label"
                        ),
                    ),
                ],
            ),
        ],
    )
