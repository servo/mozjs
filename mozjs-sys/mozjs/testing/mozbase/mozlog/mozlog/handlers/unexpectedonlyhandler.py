# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from .base import BaseHandler


class UnexpectedOnlyHandler(BaseHandler):
    """Handler that suppresses process_output messages for passing tests,
    only emitting them for tests that produce unexpected results.

    :param inner: The underlying handler used to emit messages.
    """

    def __init__(self, inner):
        BaseHandler.__init__(self, inner)
        self.inner = inner
        self._buffering = False
        self._buffer = []
        self._has_unexpected = False

    def __call__(self, data):
        action = data.get("action")

        if action == "test_start":
            self._buffering = True
            self._buffer = []
            self._has_unexpected = False
            self.inner(data)

        elif action == "test_status":
            status = data.get("status")
            expected = data.get("expected", status)
            if status != expected:
                self._has_unexpected = True
            self.inner(data)

        elif action == "test_end":
            status = data.get("status")
            expected = data.get("expected", status)

            has_failure = (
                status not in ("PASS", "OK", "FAIL")
                or status != expected
                or self._has_unexpected
            )

            if has_failure:
                for buffered_data in self._buffer:
                    self.inner(buffered_data)

            self._buffer = []
            self._buffering = False
            self._has_unexpected = False
            self.inner(data)

        elif action == "process_output":
            if self._buffering:
                self._buffer.append(data)
            else:
                self.inner(data)

        else:
            self.inner(data)
