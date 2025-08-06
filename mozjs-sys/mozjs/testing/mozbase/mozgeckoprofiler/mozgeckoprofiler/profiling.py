# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import json
import os
import shutil
import tempfile

from mozlog import get_proxy_logger

from .symbolication import ProfileSymbolicator

LOG = get_proxy_logger("profiler")


def save_gecko_profile(profile, filename):
    with open(filename, "w") as f:
        json.dump(profile, f)


def symbolicate_profile_json(profile_path, firefox_symbols_path):
    """
    Symbolicate a single JSON profile.
    """
    temp_dir = tempfile.mkdtemp()
    missing_symbols_zip = os.path.join(temp_dir, "missingsymbols.zip")

    windows_symbol_path = os.path.join(temp_dir, "windows")
    os.mkdir(windows_symbol_path)

    symbol_paths = {"FIREFOX": firefox_symbols_path, "WINDOWS": windows_symbol_path}

    symbolicator = ProfileSymbolicator(
        {
            # Trace-level logging (verbose)
            "enableTracing": 0,
            # Fallback server if symbol is not found locally
            "remoteSymbolServer": "https://symbolication.services.mozilla.com/symbolicate/v4",
            # Maximum number of symbol files to keep in memory
            "maxCacheEntries": 2000000,
            # Frequency of checking for recent symbols to
            # cache (in hours)
            "prefetchInterval": 12,
            # Oldest file age to prefetch (in hours)
            "prefetchThreshold": 48,
            # Maximum number of library versions to pre-fetch
            # per library
            "prefetchMaxSymbolsPerLib": 3,
            # Default symbol lookup directories
            "defaultApp": "FIREFOX",
            "defaultOs": "WINDOWS",
            # Paths to .SYM files, expressed internally as a
            # mapping of app or platform names to directories
            # Note: App & OS names from requests are converted
            # to all-uppercase internally
            "symbolPaths": symbol_paths,
        }
    )

    LOG.info(
        "Symbolicating the performance profile... This could take a couple "
        "of minutes."
    )

    try:
        with open(profile_path, "r", encoding="utf-8") as profile_file:
            profile = json.load(profile_file)
        symbolicator.dump_and_integrate_missing_symbols(profile, missing_symbols_zip)
        symbolicator.symbolicate_profile(profile)
        # Overwrite the profile in place.
        save_gecko_profile(profile, profile_path)
    except MemoryError:
        LOG.error(
            "Ran out of memory while trying"
            " to symbolicate profile {0}".format(profile_path)
        )
    except Exception as e:
        LOG.error("Encountered an exception during profile symbolication")
        LOG.error(e)

    shutil.rmtree(temp_dir)
