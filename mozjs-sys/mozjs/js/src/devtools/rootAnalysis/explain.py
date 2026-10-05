#!/usr/bin/python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.


import argparse
import json
import pathlib
import re
from html import escape

SRCDIR = pathlib.Path(__file__).parent.parent.parent.absolute()

parser = argparse.ArgumentParser(
    description="Convert the JSON output of the hazard analysis into various text files describing the results.",
    formatter_class=argparse.ArgumentDefaultsHelpFormatter,
)
parser.add_argument("--verbose", type=bool, default=False, help="verbose output")

inputs = parser.add_argument_group("Input")
inputs.add_argument(
    "rootingHazards",
    nargs="?",
    default="rootingHazards.json",
    help="JSON input file describing the output of the hazard analysis",
)

outputs = parser.add_argument_group("Output")
outputs.add_argument(
    "gcFunctions",
    nargs="?",
    default="gcFunctions.txt",
    help="file containing a list of functions that can GC",
)
outputs.add_argument(
    "hazards",
    nargs="?",
    default="hazards.txt",
    help="file containing the rooting hazards found",
)
outputs.add_argument(
    "extra",
    nargs="?",
    default="unnecessary.txt",
    help="file containing unnecessary roots",
)
outputs.add_argument(
    "refs",
    nargs="?",
    default="refs.txt",
    help="file containing a list of unsafe references to unrooted values",
)
outputs.add_argument(
    "html",
    nargs="?",
    default="hazards.html",
    help="HTML-formatted file with the hazards found",
)

args = parser.parse_args()


# Imitate splitFunction from utility.js.
def splitfunc(full):
    idx = full.find("$")
    if idx == -1:
        return (full, full)
    return (full[0:idx], full[idx + 1 :])


def print_header(outfh):
    print(
        """\
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<style>
input {
  position: absolute;
  opacity: 0;
  z-index: -1;
}
tt {
  background: #eee;
}
.gccall {
  font-weight: bold;
  font-style: italic;
  color: #060;
}
.tab-label {
  cursor: s-resize;
}
.tab-label a {
  color: #222;
}
.tab-label:hover {
  background: #eee;
}
.tab-label::after {
  content: " \\25B6";
  color: #75f;
  transition: all 0.35s;
}
.accorntent {
  max-height: 0;
  padding: 0 1em;
  color: #2c3e50;
  overflow: hidden;
  background: white;
  transition: all 0.35s;
}

input:checked + .tab-label::after {
  transform: rotate(90deg);
  content: " \\25BC";
}
input:checked + .tab-label {
  cursor: n-resize;
}
input:checked ~ .accorntent {
  max-height: 100vh;
}
</style>
</head>
<body>""",
        file=outfh,
    )


def parse_function(readable):
    """Do a very simple parse of a full function declaration, splitting it into
    a prefix, the function name, and a suffix."""

    s = readable

    # Remove all template parameter lists, recursively.
    while True:
        s, count = re.subn(r"<[^<]*>", "", s)
        if count == 0:
            break

    if m := re.search(r"((?:~?\w+|operator[^(]+))\(", s):
        # Grab the function name just before the first open paren.
        funcName = m.group(1)

        # Expand the function name to include any namespaces (and class names,
        # as long as they aren't templatized).
        name_start = readable.index(funcName)
        name_end = name_start + len(funcName)
        s = readable[: name_end + 1]
        m = re.search(r"((?:[\w:]+|operator[^(]+))\(", s)
        if m:
            funcName = m.group(1)
    elif m := re.search(r"\:\d+$", s):
        # Field call, like "IDL_foo:0"
        funcName = s
    elif m := re.search(r"[^:]:(\w+)$", s):
        # <filename>:<cfunc>
        funcName = m.group(1)
    elif m := re.match(r"\w+$", s):
        # <cfunc>
        funcName = s
    else:
        # There is a wide variety of messy other formats for function names. We
        # are unlikely to run across them in this code, but fall back to the
        # full string.
        print(f"FAILED function_parse on {s}", file=sys.stderr)
        funcName = s

    name_start = readable.index(funcName)

    return {
        "prefix": readable[:name_start],
        "name": funcName,
        "suffix": readable[name_start + len(funcName) :],
        "call": funcName + "()",
    }


def brief_function(readable):
    '''Convert eg "Result<int> js::evil::foo(const Danger<float,
    Error<false>>&)" to just "js::evil::foo()"'''
    return parse_function(readable)["call"]


def sourcelink(symbol=None, readable=None, loc=None, range=None):
    if symbol:
        if symbol.startswith("_Z") or readable is None:
            return f"https://searchfox.org/mozilla-central/search?q=symbol:{symbol}"
        info = parse_function(readable)
        return f"https://searchfox.org/mozilla-central/search?q={info['name']}"
    elif range:
        filename, lineno = loc.split(":")
        [f0, l0] = range[0]
        [f1, l1] = range[1]
        if f0 == f1 and l1 > l0:
            return f"../{filename}?L={l0}-{l1 - 1}#{l0}"
        else:
            return f"../{filename}?L={l0}#{l0}"
    elif loc:
        filename, lineno = loc.split(":")
        return f"../{filename}?L={lineno}#{lineno}"
    else:
        raise Exception("missing argument to sourcelink()")


def quoted_dict(d):
    return {k: escape(v) for k, v in d.items() if type(v) is str}


def func_link(result):
    return "<code>{prefix}<a href='{src}'>{name}</a>{suffix}</code>".format(
        **parse_function(escape(result["readable"])),
        src=sourcelink(symbol=result["mangled"], readable=result["readable"]),
    )


num_hazards = 0
num_refs = 0
num_missing = 0

try:
    with open(args.rootingHazards) as rootingHazards, open(
        args.hazards, "w"
    ) as hazards, open(args.extra, "w") as extra, open(args.refs, "w") as refs, open(
        args.html, "w"
    ) as html:
        current_gcFunction = None

        hazardousGCFunctions = set()

        results = json.load(rootingHazards)
        print_header(html)

        when = min((r for r in results if r["record"] == "time"), key=lambda r: r["t"])[
            "iso"
        ]
        line = f"Time: {when}"
        print(line, file=hazards)
        print(line, file=extra)
        print(line, file=refs)

        checkboxCounter = 0
        hazard_results = []
        missing = []
        seen_time = False
        for result in results:
            if result["record"] == "unrooted":
                hazard_results.append(result)
                gccall_mangled, _ = splitfunc(result["gccall"])
                hazardousGCFunctions.add(gccall_mangled)
                if not result.get("expected"):
                    num_hazards += 1

            elif result["record"] == "unnecessary":
                print(
                    "\nFunction '{mangled}' has unnecessary root '{variable}' of type {type} at {loc}".format(
                        **result
                    ),
                    file=extra,
                )

            elif result["record"] == "address":
                print(
                    (
                        "\nFunction '{functionName}'"
                        " takes unsafe address of unrooted '{variable}'"
                        " at {loc}"
                    ).format(**result),
                    file=refs,
                )
                num_refs += 1

            elif result["record"] == "missing":
                print(
                    "\nFunction '{functionName}' expected hazard(s) but none were found at {loc}".format(
                        **result
                    ),
                    file=hazards,
                )
                missing.append(result)
                num_missing += 1

        readable2mangled = {}
        with open(args.gcFunctions) as gcFunctions:
            gcExplanations = {}  # gcFunction => stack showing why it can GC

            current_func = None
            explanation = []
            for line in gcFunctions:
                if m := re.match(r"^GC Function: (.*)", line):
                    if current_func:
                        gcExplanations[splitfunc(current_func)[0]] = explanation
                    functionName = m.group(1)
                    mangled, readable = splitfunc(functionName)
                    if mangled not in hazardousGCFunctions:
                        current_func = None
                        continue
                    current_func = functionName
                    if readable != mangled:
                        readable2mangled[readable] = mangled
                    # TODO: store the mangled name here, and change
                    # gcFunctions.txt -> gcFunctions.json and key off of the mangled name.
                    explanation = [readable]
                elif current_func:
                    explanation.append(line.strip())
            if current_func:
                gcExplanations[splitfunc(current_func)[0]] = explanation

        print(
            f"<details open><summary>Found {num_hazards} hazards, {num_missing} expected hazards missing.</summary>",
            file=html,
        )
        print("<ol>", file=html)

        for result in missing:
            print(
                "<li>MISSING expected hazard in <a href='{loc_url}'><tt>{call_short}</tt></a> at {loc}".format(
                    loc_url=sourcelink(range=result["range"], loc=result["loc"]),
                    call_short=brief_function(result["readable"]),
                    loc=result["loc"],
                ),
                file=html,
            )

        # Put expected results last, so they can be hidden by default.
        expected_banner_shown = False
        for result in sorted(hazard_results, key=lambda r: r.get("expected")):
            (result["gccall_mangled"], result["gccall_readable"]) = splitfunc(
                result["gccall"]
            )
            # Attempt to extract out the function name. Won't handle `Foo<int, Bar<int>>::Foo()`.
            if m := re.search(r"((?:\w|:|<[^>]*?>)+)\(", result["gccall_readable"]):
                result["gccall_short"] = m.group(1) + "()"
            else:
                result["gccall_short"] = result["gccall_readable"]
            if result.get("expected"):
                print("\nThis is expected, but ", end="", file=hazards)
            else:
                print("\nFunction ", end="", file=hazards)
            print(
                "'{readable}' has unrooted '{variable}'"
                " of type '{type}' live across GC call '{gccall_readable}' at {loc}".format(
                    **result
                ),
                file=hazards,
            )
            for edge in result["trace"]:
                print("    {lineText}: {edgeText}".format(**edge), file=hazards)
            explanation = gcExplanations.get(result["gccall_mangled"])
            explanation = explanation or gcExplanations.get(
                readable2mangled.get(
                    result["gccall_readable"], result["gccall_readable"]
                ),
                [],
            )
            if explanation:
                print("GC Function: " + explanation[0], file=hazards)
                for func in explanation[1:]:
                    print("   " + func, file=hazards)
            print(file=hazards)

            if result.get("expected"):
                if not expected_banner_shown:
                    expected_banner_shown = True
                    print("</ol></details>\n", file=html)
                    print(
                        "<details><summary>Expected Hazards</summary>\n<ol>", file=html
                    )

            cfgid = f"CFG_{checkboxCounter}"
            gcid = f"GC_{checkboxCounter}"
            checkboxCounter += 1
            print(
                (
                    "<li><ul>\n"
                    "<li>Function {func}\n"
                    "<li>has unrooted <tt>{variable}</tt> of type '<tt>{type}</tt>'\n"
                    "<li><input type='checkbox' id='{cfgid}'><label class='tab-label' for='{cfgid}'>"
                    "live across GC call"
                    "</label>\n"
                    "<div class='accorntent'>\n"
                ).format(
                    **quoted_dict(result),
                    func=func_link(result),
                    cfgid=cfgid,
                ),
                file=html,
            )
            for edge in result["trace"]:
                lineText = escape(edge["lineText"])
                edgeText = escape(edge["edgeText"]).replace(
                    "[[GC call]]", "<span class=gccall><-- GC call</span>"
                )
                print(
                    f"<pre>    {lineText}: {edgeText}</pre>",
                    file=html,
                )
            print("</div>", file=html)
            qresult = quoted_dict(result)
            print(
                "<li><input type='checkbox' id='{gcid}'><label class='tab-label' for='{gcid}'>"
                "to <a href='{loc_url}'><tt>{gccall_short}</tt></a>"
                "</label>\n"
                "<div class='accorntent'>".format(
                    **qresult,
                    loc_url=sourcelink(range=result["gcrange"], loc=result["loc"]),
                    gcid=gcid,
                ),
                file=html,
            )
            for func in explanation:
                print(f"<pre>{escape(func)}</pre>", file=html)
            print("</div>", file=html)
            print(
                "<li>at {loc}".format(**qresult),
                file=html,
            )
            print("<hr></ul>", file=html)

        print("</ol></details>\n", file=html)

        print(
            f"<details><summary>Found {num_refs} unsafe references</summary>\n",
            file=html,
        )
        if num_refs > 0:
            print("<ol>\n", file=html)
            for result in [r for r in results if r["record"] == "address"]:
                print(
                    (
                        "<li>\n"
                        "Function {func} "
                        "takes unsafe address of unrooted <tt>{variable}</tt> at <a href='{loc_url}'>{loc}</a>\n"
                    ).format(
                        **quoted_dict(result),
                        func=func_link(result),
                        loc_url=sourcelink(loc=result["loc"]),
                    ),
                    file=html,
                )
            print("</ol>\n", file=html)
        print("</details>\n", file=html)

        print("</body>\n</html>", file=html)

except OSError as e:
    print(f"Failed: {e}")

if args.verbose:
    print(f"Wrote {args.hazards}")
    print(f"Wrote {args.extra}")
    print(f"Wrote {args.refs}")
    print(f"Wrote {args.html}")

print(f"Found {num_hazards} hazards {num_refs} unsafe references {num_missing} missing")
