# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import fluent.syntax.ast as FTL
from fluent.migrate.helpers import VARIABLE_REFERENCE
from fluent.migrate.transforms import COPY, REPLACE

def migrate(ctx):
  """Bug 2031711 - Convert some toolbox.properties to Fluent, part {index}"""

  ctx.add_transforms(
        "devtools/client/toolbox-options.ftl",
        "devtools/client/toolbox-options.ftl",
        [
            FTL.Message(
                id=FTL.Identifier("options-tool-not-supported-marker"),
                value=REPLACE(
                    "devtools/client/toolbox.properties",
                    "options.toolNotSupportedMarker",
                    {
                        "%1$S": VARIABLE_REFERENCE("toolLabel"),
                    },
                ),
            ),
            FTL.Message(
                id=FTL.Identifier("options-auto-theme-label"),
                value=COPY(
                    "devtools/client/toolbox.properties",
                    "options.autoTheme.label",
                ),
            ),
            FTL.Message(
                id=FTL.Identifier("options-deprecation-notice"),
                value=COPY(
                    "devtools/client/toolbox.properties",
                    "options.deprecationNotice",
                ),
            ),
        ],
  )
