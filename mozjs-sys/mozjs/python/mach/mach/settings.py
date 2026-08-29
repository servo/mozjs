# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from textwrap import dedent

from mach.decorators import SettingsProvider


def _get_log_formatters():
    from mozlog.commandline import log_formatters

    return list(log_formatters)


def _get_log_levels():
    from mozlog.structuredlog import log_levels

    return [level.lower() for level in log_levels]


@SettingsProvider
class MachSettings:
    @classmethod
    def config_settings(cls):
        def telemetry_config_settings():
            return [
                (
                    "build.telemetry",
                    "boolean",
                    "Enable submission of build system telemetry "
                    '(Deprecated, replaced by "telemetry.is_enabled")',
                ),
                (
                    "mach_telemetry.is_enabled",
                    "boolean",
                    "Build system telemetry is allowed",
                    False,
                ),
                (
                    "mach_telemetry.is_set_up",
                    "boolean",
                    "The telemetry setup workflow has been completed "
                    "(e.g.: user has been prompted to opt-in)",
                    False,
                ),
                (
                    "mach_telemetry.is_employee",
                    "nullable_boolean",
                    "Cached value for whether the user is a Mozilla employee "
                    "(None=unknown, True=employee, False=not an employee)",
                    None,
                ),
            ]

        def dispatch_config_settings():
            return [
                (
                    "alias.*",
                    "string",
                    """
        Create a command alias of the form `<alias>=<command> <args>`.
        Aliases can also be used to set default arguments:
        <command>=<command> <args>
        """.strip(),
                ),
            ]

        def run_config_settings():
            return [
                (
                    "runprefs.*",
                    "string",
                    dedent("""
        Pass a pref into Firefox when using `mach run`, of the form `foo.bar=value`.
        Prefs will automatically be cast into the appropriate type. Integers can be
        single quoted to force them to be strings.
        """),
                ),
            ]

        def try_config_settings():
            desc = "The default selector to use when running `mach try` without a subcommand."
            choices = [
                "fuzzy",
                "chooser",
                "auto",
                "again",
                "empty",
                "syntax",
                "coverage",
                "release",
                "scriptworker",
                "compare",
                "perf",
            ]

            return [
                ("try.default", "string", desc, "auto", {"choices": choices}),
                (
                    "try.maxhistory",
                    "int",
                    "Maximum number of pushes to save in history.",
                    10,
                ),
                (
                    "try.nobrowser",
                    "boolean",
                    "Do not automatically open a browser during authentication.",
                    False,
                ),
                (
                    "try.noartifact",
                    "boolean",
                    "Do not autodetect artifact mode base on mozconfig. The '--artifact' flag must be used explicitly if artifact try pushes are desired.",
                    False,
                ),
                (
                    "try.pushremote",
                    "string",
                    "Remote name or url to push to.",
                    "ssh://hg.mozilla.org/try",
                ),
            ]

        def taskgraph_config_settings():
            return [
                (
                    "taskgraph.diffcmd",
                    "string",
                    "The command to run with `./mach taskgraph --diff`",
                    "diff --report-identical-files "
                    "--label={attr}@{base} --label={attr}@{cur} -U20",
                    {},
                )
            ]

        def test_config_settings():
            format_desc = (
                "The default format to use when running tests with `mach test`."
            )
            level_desc = (
                "The default log level to use when running tests with `mach test`."
            )
            return [
                (
                    "test.format",
                    "string",
                    format_desc,
                    "mach",
                    {"choices": _get_log_formatters},
                ),
                (
                    "test.level",
                    "string",
                    level_desc,
                    "info",
                    {"choices": _get_log_levels},
                ),
            ]

        settings = []
        settings.extend(telemetry_config_settings())
        settings.extend(dispatch_config_settings())
        settings.extend(run_config_settings())
        settings.extend(try_config_settings())
        settings.extend(taskgraph_config_settings())
        settings.extend(test_config_settings())

        return settings
