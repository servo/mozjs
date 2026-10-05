# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
from unittest import TestCase, mock

import mozunit

from mozbuild.toolchains import toolchain_task_definitions


class TestToolchainTaskDefinitions(TestCase):
    def _raw_fn(self):
        # Bypass mach_func_cache (disk) and functools.cache (memory)
        return toolchain_task_definitions.__wrapped__.__wrapped__

    def _capture_params(self, env_patch, clear=False):
        captured_params = {}
        original_dump = json.dump

        def spy_dump(obj, fp, **kwargs):
            if "level" in obj:
                captured_params.update(obj)
            return original_dump(obj, fp, **kwargs)

        def fake_subprocess_run(cmd, *_args, **_kwargs):
            output_file = cmd[cmd.index("--output-file") + 1]
            with open(output_file, "w") as f:
                original_dump(
                    {
                        "toolchain-test": {
                            "attributes": {
                                "kind": "toolchain",
                                "toolchain-resources": [],
                            }
                        }
                    },
                    f,
                )
            return mock.Mock(returncode=0)

        with mock.patch(
            "mozbuild.toolchains.subprocess.run", side_effect=fake_subprocess_run
        ):
            with mock.patch("mozbuild.toolchains.json.dump", side_effect=spy_dump):
                with mock.patch.dict(os.environ, env_patch, clear=clear):
                    self._raw_fn()()

        return captured_params

    def test_moz_scm_level_passed_to_taskgraph(self):
        params = self._capture_params({"MOZ_SCM_LEVEL": "1"})
        self.assertEqual(params["level"], "1")

    def test_moz_scm_level_defaults_to_3(self):
        env_without_scm_level = os.environ.copy()
        env_without_scm_level.pop("MOZ_SCM_LEVEL", None)
        params = self._capture_params(env_without_scm_level, clear=True)
        self.assertEqual(params["level"], "3")


if __name__ == "__main__":
    mozunit.main()
