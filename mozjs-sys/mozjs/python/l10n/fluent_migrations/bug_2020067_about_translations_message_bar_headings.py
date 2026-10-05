# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import re
import fluent.syntax.ast as FTL
from fluent.migrate.transforms import COPY_PATTERN, TransformPattern


class STRIP_PUNCTUATION(TransformPattern):
    def visit_TextElement(self, node):
        node.value = re.sub(r"[.।。]+$", "", node.value)
        return node


def migrate(ctx):
    """Bug 2020067 - Add new about:translations message bar IDs without punctuation in headings, part {index}."""

    source = "toolkit/toolkit/about/aboutTranslations.ftl"
    target = source

    ctx.add_transforms(
        target,
        source,
        [
            FTL.Message(
                id=FTL.Identifier("about-translations-unsupported-info-message-2"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("heading"),
                        value=STRIP_PUNCTUATION(
                            source, "about-translations-unsupported-info-message.heading"
                        ),
                    ),
                    FTL.Attribute(
                        id=FTL.Identifier("message"),
                        value=COPY_PATTERN(
                            source, "about-translations-unsupported-info-message.message"
                        ),
                    ),
                ],
            ),
            FTL.Message(
                id=FTL.Identifier("about-translations-language-load-error-message-2"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("heading"),
                        value=STRIP_PUNCTUATION(
                            source, "about-translations-language-load-error-message.heading"
                        ),
                    ),
                    FTL.Attribute(
                        id=FTL.Identifier("message"),
                        value=COPY_PATTERN(
                            source, "about-translations-language-load-error-message.message"
                        ),
                    ),
                ],
            ),
        ],
    )
