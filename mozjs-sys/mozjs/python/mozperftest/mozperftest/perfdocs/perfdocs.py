# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import os
import pathlib


def run_perfdocs(config, logger=None, paths=None, generate=True, regen_variants=False):
    """
    Build up performance testing documentation dynamically by combining
    text data from YAML files that reside in `perfdoc` folders
    across the `testing` directory. Each directory is expected to have
    an `index.rst` file along with `config.yml` YAMLs defining what needs
    to be added to the documentation.

    The YAML must also define the name of the "framework" that should be
    used in the main index.rst for the performance testing documentation.

    The testing documentation list will be ordered alphabetically once
    it's produced (to avoid unwanted shifts because of unordered dicts
    and path searching).

    Note that the suite name headings will be given the H4 (---) style so it
    is suggested that you use H3 (===) style as the heading for your
    test section. H5 will be used be used for individual tests within each
    suite.

    Usage for verification: "./mach perfdocs"
    Usage for generation: "./mach perfdocs --generate"

    For validation, see the Verifier class for a description of how
    it works.

    The run will fail if the valid result from validate_tree is not
    False, implying some warning/problem was logged.

    :param dict config: The configuration given by mozlint.
    :param StructuredLogger logger: The StructuredLogger instance to be used to
        output the linting warnings/errors.
    :param list paths: The paths that are being tested. Used to filter
        out errors from files outside of these paths.
    :param bool generate: If true, the docs will be (re)generated.
    """
    from mozperftest.perfdocs.logger import PerfDocLogger

    if not os.environ.get("WORKSPACE", None):
        # Navigate up from python/mozperftest/mozperftest/perfdocs/perfdocs.py
        # to the repo root (4 levels up)
        top_dir = pathlib.Path(__file__).absolute().parents[4].resolve()
    else:
        top_dir = pathlib.Path(os.environ.get("WORKSPACE")).resolve()

    PerfDocLogger.LOGGER = logger
    PerfDocLogger.TOP_DIR = top_dir

    # Convert all the paths to relative ones
    target_dir = [p for p in map(pathlib.Path, paths) if p.exists()]
    rel_paths = []
    for path in target_dir:
        try:
            rel_paths.append(path.relative_to(top_dir))
        except ValueError:
            rel_paths.append(path)

    PerfDocLogger.PATHS = rel_paths

    import datetime as _real_datetime
    import types

    # Import the variant transform module so we can patch its datetime
    # reference before generating tasks. Without this, variants whose
    # expiration date has passed would be dropped from the task
    # graph, causing infra-like failures.
    import gecko_taskgraph.transforms.test.variant as _variant_module
    from tryselect.tasks import generate_tasks

    _orig_datetime = _variant_module.datetime
    try:
        if not regen_variants:

            class _OldDateTime(_real_datetime.datetime):
                @classmethod
                def today(cls):
                    return _real_datetime.datetime(2000, 1, 1)

            _variant_module.datetime = types.SimpleNamespace(datetime=_OldDateTime)

        task_graph = generate_tasks(full=True, disable_target_task_filter=True).tasks
    finally:
        _variant_module.datetime = _orig_datetime

    # Late import because logger isn't defined until later
    from mozperftest.perfdocs.generator import Generator
    from mozperftest.perfdocs.verifier import Verifier

    # Run the verifier first
    verifier = Verifier(top_dir, task_graph)
    verifier.validate_tree()

    if not PerfDocLogger.FAILED:
        # Even if the tree is valid, we need to check if the documentation
        # needs to be regenerated, and if it does, we throw a linting error.
        # `generate` dictates whether or not the documentation is generated.
        generator = Generator(verifier, generate=generate, workspace=top_dir)
        generator.generate_perfdocs()

    return PerfDocLogger.FAILED
