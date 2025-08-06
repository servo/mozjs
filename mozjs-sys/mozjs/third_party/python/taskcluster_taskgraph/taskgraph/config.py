# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import logging
import os
import sys
from dataclasses import dataclass
from typing import Dict

from voluptuous import All, Any, Extra, Length, Optional, Required

from .util import path
from .util.caches import CACHES
from .util.python_path import find_object
from .util.schema import Schema, optionally_keyed_by, validate_schema
from .util.yaml import load_yaml

logger = logging.getLogger(__name__)

graph_config_schema = Schema(
    {
        # The trust-domain for this graph.
        # (See https://firefox-source-docs.mozilla.org/taskcluster/taskcluster/taskgraph.html#taskgraph-trust-domain)  # noqa
        Required("trust-domain"): str,
        Required("task-priority"): optionally_keyed_by(
            "project",
            Any(
                "highest",
                "very-high",
                "high",
                "medium",
                "low",
                "very-low",
                "lowest",
            ),
        ),
        Optional(
            "task-deadline-after",
            description="Default 'deadline' for tasks, in relative date format. "
            "Eg: '1 week'",
        ): optionally_keyed_by("project", str),
        Optional(
            "task-expires-after",
            description="Default 'expires-after' for level 1 tasks, in relative date format. "
            "Eg: '90 days'",
        ): str,
        Required("workers"): {
            Required("aliases"): {
                str: {
                    Required("provisioner"): optionally_keyed_by("level", str),
                    Required("implementation"): str,
                    Required("os"): str,
                    Required("worker-type"): optionally_keyed_by("level", str),
                }
            },
        },
        Required("taskgraph"): {
            Optional(
                "register",
                description="Python function to call to register extensions.",
            ): str,
            Optional("decision-parameters"): str,
            Optional(
                "cached-task-prefix",
                description="The taskcluster index prefix to use for caching tasks. "
                "Defaults to `trust-domain`.",
            ): str,
            Optional(
                "cache-pull-requests",
                description="Should tasks from pull requests populate the cache",
            ): bool,
            Optional(
                "index-path-regexes",
                description="Regular expressions matching index paths to be summarized.",
            ): [str],
            Optional(
                "run",
                description="Configuration related to the 'run' transforms.",
            ): {
                Optional(
                    "use-caches",
                    description="List of caches to enable, or a boolean to "
                    "enable/disable all of them.",
                ): Any(bool, list(CACHES.keys())),
            },
            Required("repositories"): All(
                {
                    str: {
                        Required("name"): str,
                        Optional("project-regex"): str,
                        Optional("ssh-secret-name"): str,
                        # FIXME
                        Extra: str,
                    }
                },
                Length(min=1),
            ),
        },
        Extra: object,
    }
)
"""Schema for GraphConfig"""


@dataclass(frozen=True, eq=False)
class GraphConfig:
    _config: Dict
    root_dir: str

    _PATH_MODIFIED = False

    def __getitem__(self, name):
        return self._config[name]

    def __contains__(self, name):
        return name in self._config

    def get(self, name, default=None):
        return self._config.get(name, default)

    def register(self):
        """
        Add the project's taskgraph directory to the python path, and register
        any extensions present.
        """
        if GraphConfig._PATH_MODIFIED:
            if GraphConfig._PATH_MODIFIED == self.root_dir:
                # Already modified path with the same root_dir.
                # We currently need to do this to enable actions to call
                # taskgraph_decision, e.g. relpro.
                return
            raise Exception("Can't register multiple directories on python path.")
        GraphConfig._PATH_MODIFIED = self.root_dir
        sys.path.insert(0, self.root_dir)
        register_path = self["taskgraph"].get("register")
        if register_path:
            find_object(register_path)(self)

    @property
    def vcs_root(self):
        if path.split(self.root_dir)[-1:] != ["taskcluster"]:
            raise Exception(
                "Not guessing path to vcs root. Graph config in non-standard location."
            )
        return os.path.dirname(self.root_dir)

    @property
    def taskcluster_yml(self):
        return os.path.join(self.vcs_root, ".taskcluster.yml")


def validate_graph_config(config):
    validate_schema(graph_config_schema, config, "Invalid graph configuration:")


def load_graph_config(root_dir):
    config_yml = os.path.join(root_dir, "config.yml")
    if not os.path.exists(config_yml):
        raise Exception(f"Couldn't find taskgraph configuration: {config_yml}")

    logger.debug(f"loading config from `{config_yml}`")
    config = load_yaml(config_yml)

    validate_graph_config(config)
    return GraphConfig(config, root_dir=root_dir)
