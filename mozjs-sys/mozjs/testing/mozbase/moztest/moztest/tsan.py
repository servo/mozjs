# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import re


class TSANErrorParser:
    """Parses ThreadSanitizer reports out of a process output stream and emits
    one ``tsan_error`` structured-log action per completed report.

    A TSan report spans many lines, opens with a
    ``WARNING|ERROR: ThreadSanitizer: <kind>`` line and closes with a
    ``SUMMARY: ThreadSanitizer: ...`` line. In between it carries one or more
    labeled sub-stacks (e.g. "Mutex M1 acquired here while holding mutex M0 in
    main thread:", "Write of size 8 ... by main thread:"). Each sub-stack is
    surfaced separately so a consumer can render it as its own profiler marker
    (a marker holds a single stack).

    Reports from different emitting processes can interleave in a merged output
    stream, so in-progress state is keyed by the emitting process id.
    """

    # Matches every report header, e.g.
    #   "WARNING: ThreadSanitizer: data race (pid=6257)"
    #   "==2708==ERROR: ThreadSanitizer: SEGV on unknown address 0x0 (pc ...)"
    # The pid is taken from "(pid=N)" when present, else from a leading "==N==".
    headerRegExp = re.compile(
        r"(?:==(?P<procpid>\d+)==)?(?:WARNING|ERROR): ThreadSanitizer: "
        r"(?P<kind>.+?)(?: \(pid=(?P<pid>\d+)\))?$"
    )
    # Trailing descriptive noise that follows the kind in signal reports
    # (SEGV/SIGBUS/...): "<kind> on unknown address 0x.. (pc 0x.. bp ..)".
    kindNoiseRegExp = re.compile(r" (?:on unknown address|\(pc 0x).*$")
    summaryRegExp = re.compile(r"SUMMARY: ThreadSanitizer: (?P<rest>.+)$")
    descriptionRegExp = re.compile(
        r"^  (?P<description>Cycle in lock order graph: .+)$"
    )
    labelRegExp = re.compile(r"^  (?P<label>\S.*):$")
    frameRegExp = re.compile(
        r"^\s*#(?P<n>\d+) (?P<body>.+) "
        r"\((?P<module>[^()\s]+)\+(?P<modoffset>0x[0-9a-fA-F]+)\)"
        r"(?: \(BuildId: [0-9a-fA-F]+\))?\s*$"
    )
    # A trailing token is a source location if it has a path separator or looks
    # like "name.ext", optionally followed by ":line" / ":line:col".
    fileLocRegExp = re.compile(r"(?:/|.+\.[A-Za-z0-9_]+(?::\d+){0,2}$)")
    fileLineColRegExp = re.compile(
        r"(?P<file>.+?)(?::(?P<line>\d+)(?::(?P<col>\d+))?)?$"
    )
    # Rust frames aren't demangled cleanly and carry a trailing ::h<hex> that
    # changes with compiler versions; see bug 1507350.
    rustRegExp = re.compile(r"::h[a-f0-9]{16}$")

    def __init__(self, logger, scope=None):
        self.logger = logger
        self.scope = scope
        # pid -> in-progress report dict
        self._reports = {}

    def log(self, line, pid=None, scope=None):
        line = line.rstrip("\r\n")

        header = self.headerRegExp.search(line)
        if header:
            # A new report ends any report still open on this stream.
            self._finish(pid)
            report_pid = header.group("pid") or header.group("procpid")
            self._reports[pid] = {
                "kind": self.kindNoiseRegExp.sub("", header.group("kind")),
                "pid": int(report_pid) if report_pid else None,
                "description": None,
                "scope": scope if scope is not None else self.scope,
                "stacks": [],
                "current": None,
            }
            return

        report = self._reports.get(pid)
        if report is None:
            return

        summary = self.summaryRegExp.search(line)
        if summary:
            report["signature"] = self._signature(report["kind"], summary.group("rest"))
            self._finish(pid)
            return

        description = self.descriptionRegExp.match(line)
        if description:
            report["description"] = description.group("description")
            return

        frame = self.frameRegExp.match(line)
        if frame:
            # Some reports (e.g. SEGV) list frames with no preceding label
            # line; collect them under an implicit, unlabeled stack.
            if report["current"] is None:
                report["current"] = {"label": "", "stack": []}
                report["stacks"].append(report["current"])
            report["current"]["stack"].append(self._frame(frame))
            return

        label = self.labelRegExp.match(line)
        if label:
            report["current"] = {"label": label.group("label"), "stack": []}
            report["stacks"].append(report["current"])

    def flush(self):
        """Emit any reports left open by truncated output."""
        for pid in list(self._reports):
            self._finish(pid)

    def _finish(self, pid):
        report = self._reports.pop(pid, None)
        if report is None:
            return
        self.logger.tsan_error(
            kind=report["kind"],
            signature=report.get("signature", report["kind"]),
            pid=report["pid"],
            description=report["description"],
            stacks=[
                {"label": s["label"], "stack": s["stack"]} for s in report["stacks"]
            ],
            scope=report["scope"],
        )

    def _frame(self, match):
        body = match.group("body")
        func, fileloc = self._split_body(body)
        structured = {
            "function": self.rustRegExp.sub("", func),
            "module": match.group("module"),
            "module_offset": match.group("modoffset"),
        }
        if fileloc:
            fm = self.fileLineColRegExp.match(fileloc)
            structured["file"] = fm.group("file")
            if fm.group("line"):
                structured["line"] = int(fm.group("line"))
            if fm.group("col"):
                structured["column"] = int(fm.group("col"))
        return structured

    def _split_body(self, body):
        match = re.match(r"(?P<func>.*) (?P<fileloc>\S+)$", body)
        if match and self.fileLocRegExp.match(match.group("fileloc")):
            return match.group("func"), match.group("fileloc")
        return body, None

    def _signature(self, kind, rest):
        if kind and rest.startswith(kind):
            rest = rest[len(kind) :].strip()
        # Reduce a leading absolute path to its basename for readability,
        # keeping any ":line:col" and trailing " in <function>".
        match = re.match(r"/\S+/(?P<tail>[^/\s].*)$", rest)
        if match:
            return match.group("tail")
        return rest
