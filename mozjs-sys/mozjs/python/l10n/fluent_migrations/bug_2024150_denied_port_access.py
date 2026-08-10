# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import fluent.syntax.ast as FTL
from fluent.migrate import REPLACE
from fluent.migrate.helpers import TERM_REFERENCE


def migrate(ctx):
    """Bug 2024150 - Migrate deniedPortAccess to Fluent, part {index}."""
    ctx.add_transforms(
        "toolkit/toolkit/neterror/netError.ftl",
        "toolkit/toolkit/neterror/netError.ftl",
        [
            FTL.Message(
                id=FTL.Identifier("fp-neterror-denied-port-access"),
                value=REPLACE(
                    "browser/chrome/overrides/appstrings.properties",
                    "deniedPortAccess",
                    {"Firefox": TERM_REFERENCE("brand-short-name")},
                ),
            )
        ],
    )
