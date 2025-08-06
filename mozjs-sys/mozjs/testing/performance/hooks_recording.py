# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import json
import os
import platform
from pathlib import Path

from mozperftest.test.browsertime import add_option, add_options

# Uncomment the manual_login line if you need to do a manual login.
# The extra browsertime arguments get overwritten below so they
# need to be set here. The value is the time you need to do a login.
options = [
    ("pageCompleteWaitTime", "10000"),
    # ("browsertime.manual_login", 100000),
]

next_site = None

RECORDING_LIST = Path(Path(__file__).parent, "pageload_sites.json")

# Uncomment this to record tp7 desktop sites in CI or locally.
# This is still a WIP (Bug 1831310) and meant to be used by the
# perftest team.
# RECORDING_LIST = Path(Path(__file__).parent, "tp7_desktop_sites.json")

SCM_1_LOGIN_SITES = ("facebook", "netflix")


def before_iterations(kw):
    global next_site
    print("Setting up next site to record.")

    with RECORDING_LIST.open() as f:
        site_list = json.load(f)
    # currently can't record websites that require user interactions(Logins)
    if kw.get("android"):
        site_list = site_list["mobile"]
    else:
        site_list = site_list["desktop"]

    def __should_record(test):
        # If a test page selection was provided, only select those
        # tests and exclude all the others
        specified_tests = kw["proxy_perftest_page"]
        if specified_tests is not None:
            if test.get("name") == specified_tests:
                if test.get("login"):
                    print(f"WARNING: You selected a login test: {test.get('name')}")
                return True
            else:
                return False

        # Only perform login recordings in automation or when
        # RAPTOR_LOGINS is defined
        record = False
        if not test.get("login") or test.get("login-test"):
            record = True
            if not (
                "MOZ_AUTOMATION" in os.environ or "RAPTOR_LOGINS" in os.environ
            ) and test.get("login-test"):
                record = False
                print(
                    f"Skipping login test `{test.get('name')}` "
                    f"because login info cannot be obtained."
                )

        # When pushing to Try, only attempt login recording using the
        # taskcluster secrets that are associated with SCM level 1 as defined
        # in `SCM_LVL_1_SITES`.
        if test.get("login"):
            if "MOZ_AUTOMATION" in os.environ.keys():
                if (
                    os.environ.get("MOZ_SCM_LEVEL") == 1
                    and test.get("name") not in SCM_1_LOGIN_SITES
                ):
                    print(
                        f"Skipping login test `{test.get('name')}` "
                        f"Because SCM = `{os.environ.get('MOZ_SCM_LEVEL') }`"
                        f"and there is no secret available at this level"
                    )
                    return False
                return True
            elif "RAPTOR_LOGINS" in os.environ:
                # Leave it to the user to have properly set up a local json file with
                # the login websites of interest
                return True

        return record

    sites = [test_site for test_site in site_list if __should_record(test_site)]

    if not sites:
        raise Exception("No tests were selected for recording!")

    def next_site():
        for site in sites:
            yield site

    next_site = next_site()

    # Set the number of test-iterations to the number of builds
    kw["test_iterations"] = len(sites)
    return kw


def before_runs(env):
    global next_site
    print("Running before_runs")
    add_options(env, options)

    if next_site:
        test_site = next(next_site)
        print("Next site: %s" % test_site)

        if env.get_arg("android"):
            platform_name = "android"
            app_name = env.get_arg("android-app-name").split(".")[-1]
        else:
            platform_name = platform.system().lower()
            app_name = "firefox"

        # bug 1883701 linux uses a different version for now
        name = [
            "mitm8" if platform_name == "linux" else "mitm11",
            platform_name,
            "gve" if app_name == "geckoview_example" else app_name,
            test_site["name"],
        ]

        recording_file = "%s.zip" % "-".join(name)

        env.set_arg("proxy-mode", "record")
        env.set_arg(
            "proxy-file",
            recording_file,
        )

        add_options(env, options, overwrite=True)
        add_option(env, "browsertime.url", test_site.get("test_url"))
        add_option(env, "browsertime.screenshot", "true")
        add_option(env, "browsertime.testName", test_site.get("name"))
        add_option(env, "browsertime.testType", test_site.get("type", "pageload"))
        add_option(
            env, "browsertime.login", "true" if test_site.get("login") else "false"
        )

        prefs = test_site.get("preferences", {})
        for pref, val in prefs.items():
            add_option(env, "firefox.preference", f"{pref}:{val}")

        # Add prefs that will attempt to remove cookie banners
        add_option(
            env, "firefox.preference", "cookiebanners.bannerClicking.enabled:true"
        )
        add_option(env, "firefox.preference", "cookiebanners.service.mode:2")

        second_url = test_site.get("secondary_url", None)
        if second_url:
            add_option(env, "browsertime.secondary_url", second_url)

        inject_deterministic = test_site.get("inject_deterministic", True)
        env.set_arg("proxy-deterministic", inject_deterministic)

        dismiss_cookie_prompt = test_site.get("dismiss_cookie_prompt", [])
        if dismiss_cookie_prompt:
            parsed_cmds = [
                ":::".join([str(i) for i in item])
                for item in dismiss_cookie_prompt
                if item
            ]
            add_option(
                env, "browsertime.dismiss_cookie_prompt", ";;;".join(parsed_cmds)
            )

        cmds = test_site.get("test_cmds", [])
        if cmds:
            parsed_cmds = [":::".join([str(i) for i in item]) for item in cmds if item]
            add_option(env, "browsertime.commands", ";;;".join(parsed_cmds))

        print("Recording %s to file: %s" % (test_site.get("test_url"), recording_file))
