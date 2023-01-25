# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import string
import textwrap

import buildconfig


def generate_bool(name):
    value = buildconfig.substs.get(name)
    return f"pub const {name}: bool = {'true' if value else 'false'};\n"


def escape_rust_string(value):
    """escape the string into a Rust literal"""
    # This could be more generous, but we're only escaping paths with it.
    unescaped = string.ascii_letters + string.digits + "/$+-_~ "
    result = ""
    for ch in str(value):
        if ch in unescaped:
            result += ch
        elif ch == "\r":
            result += "\\r"
        elif ch == "\n":
            result += "\\n"
        elif ch == "\\":
            result += "\\\\"
        elif ch == '"':
            result += '\\"'
        else:
            result += "\\u{%x}" % ord(ch)
    return '"%s"' % result


def generate(output):
    # Write out a macro which can be used within `include!`-like methods to
    # reference the topobjdir.
    output.write(
        textwrap.dedent(
            f"""
            /// Macro used to name a path in the objdir for use with macros like `include!`
            #[macro_export]
            macro_rules! objdir_path {{
                ($path:literal) => {{
                    concat!({escape_rust_string(buildconfig.topobjdir + "/")}, $path)
                }}
            }}

            /// Macro used to name a path in the srcdir for use with macros like `include!`
            #[macro_export]
            macro_rules! srcdir_path {{
                ($path:literal) => {{
                    concat!({escape_rust_string(buildconfig.topsrcdir + "/")}, $path)
                }}
            }}

            /// The objdir path for use in build scripts
            pub const TOPOBJDIR: &str = {escape_rust_string(buildconfig.topobjdir)};
            /// The srcdir path for use in build scripts
            pub const TOPSRCDIR: &str = {escape_rust_string(buildconfig.topsrcdir)};

            """
        )
    )

    # Finally, write out some useful booleans from the buildconfig.
    output.write(generate_bool("MOZ_FOLD_LIBS"))
    output.write(generate_bool("NIGHTLY_BUILD"))
    output.write(generate_bool("RELEASE_OR_BETA"))
    output.write(generate_bool("EARLY_BETA_OR_EARLIER"))
    output.write(generate_bool("MOZ_DEV_EDITION"))
    output.write(generate_bool("MOZ_ESR"))
    output.write(generate_bool("MOZ_DIAGNOSTIC_ASSERT_ENABLED"))
