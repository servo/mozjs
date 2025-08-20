# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

config = {
    "stage_platform": "android-geckoview-docs",
    "mozconfig_platform": "android-arm",
    "mozconfig_variant": "nightly-android-lints",
    # geckoview-docs doesn't produce a package. So don't collect package metrics.
    "disable_package_metrics": True,
    "postflight_build_mach_commands": [
        [
            "android",
            "geckoview-docs",
            "--archive",
            "--upload",
            "mozilla/geckoview",
            "--upload-branch",
            "gh-pages",
            "--javadoc-path",
            "javadoc/{project}",
            "--upload-message",
            "Update {project} documentation to rev {revision}",
        ],
    ],
}
