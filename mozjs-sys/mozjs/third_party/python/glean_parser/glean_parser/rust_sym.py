# -*- coding: utf-8 -*-

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Outputter to generate Rust code for metrics.
"""

from pathlib import Path
from typing import Any, Dict, Optional

from . import __version__
from . import metrics
from . import pings
from . import util
from .rust import (
    rust_datatypes_filter,
    ctor,
    type_name,
    extra_type_name,
    structure_type_name,
    extra_keys,
    Category,
)


def output_rust(
    objs: metrics.ObjectTree, output_dir: Path, options: Optional[Dict[str, Any]] = None
) -> None:
    """
    Given a tree of objects, output Rust code to `output_dir`.

    :param objs: A tree of objects (metrics and pings) as returned from
        `parser.parse_objects`.
    :param output_dir: Path to an output directory to write to.
    :param options: options dictionary, not currently used for Rust
    """

    if options is None:
        options = {}

    template = util.get_jinja2_template(
        "rust_sym.jinja2",
        filters=(
            ("rust", rust_datatypes_filter),
            ("snake_case", util.snake_case),
            ("camelize", util.camelize),
            ("type_name", type_name),
            ("extra_type_name", extra_type_name),
            ("structure_type_name", structure_type_name),
            ("ctor", ctor),
            ("extra_keys", extra_keys),
        ),
    )

    filename = "glean_metrics.rs"
    filepath = output_dir / filename
    categories = []

    for category_key, category_val in objs.items():
        contains_pings = any(
            isinstance(obj, pings.Ping) for obj in category_val.values()
        )

        cat = Category(category_key, category_val, contains_pings)
        categories.append(cat)

    with filepath.open("w", encoding="utf-8") as fd:
        fd.write(
            template.render(
                parser_version=__version__,
                categories=categories,
                extra_metric_args=util.extra_metric_args,
                common_metric_args=util.common_metric_args,
            )
        )
