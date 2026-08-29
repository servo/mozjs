# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import logging

SERIALIZED_RECORD_PREFIX = "MOZLOG "


class SerializedRecordFormatter(logging.Formatter):
    """A logging.Formatter which produces serialized logging.LogRecords."""

    def format(self, record):
        s = json.dumps(record.__dict__)
        return SERIALIZED_RECORD_PREFIX + s


def output_serialized_records(logger):
    """Add a handler to the given logger to output serialized records to stderr."""
    handler = logging.StreamHandler()
    handler.setFormatter(SerializedRecordFormatter())
    logger.addHandler(handler)


def serialize_root_logger():
    """Configure the root logger to output serialized records."""
    output_serialized_records(logging.root)


def is_serialized_record(line):
    """Return whether the given line is a serialized log record."""
    return line.startswith(SERIALIZED_RECORD_PREFIX)


def read_serialized_record(line):
    """Read a serialized log record.

    Returns the logging.LogRecord, or None if a log record wasn't found.
    """
    if is_serialized_record(line):
        try:
            return logging.makeLogRecord(
                json.loads(line[len(SERIALIZED_RECORD_PREFIX) :])
            )
        except (json.JSONDecodeError, UnicodeDecodeError):
            # In case of malformed JSON data, we assume the record was not a
            # valid serialized log record.
            pass

    return None
