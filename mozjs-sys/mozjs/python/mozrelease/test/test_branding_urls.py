# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import re
from pathlib import Path

import mozunit

TOPSRCDIR = Path(__file__).parent.parent.parent.parent

BRANDING_FILES = [
    "browser/branding/official/pref/firefox-branding.js",
    "browser/branding/nightly/pref/firefox-branding.js",
    "browser/branding/aurora/pref/firefox-branding.js",
]

RELEASE_NOTES_PREFS = [
    "app.releaseNotesURL",
    "app.releaseNotesURL.aboutDialog",
    "app.releaseNotesURL.prompt",
]


def test_release_notes_urls_use_firefox_com():
    for rel_path in BRANDING_FILES:
        content = (TOPSRCDIR / rel_path).read_text()
        for pref in RELEASE_NOTES_PREFS:
            matches = re.findall(
                rf'pref\("{re.escape(pref)}",\s*"([^"]+)"',
                content,
            )
            for url in matches:
                assert url.startswith("https://www.firefox.com/"), (
                    f"{rel_path}: {pref} should point to firefox.com, got: {url}"
                )


if __name__ == "__main__":
    mozunit.main()
