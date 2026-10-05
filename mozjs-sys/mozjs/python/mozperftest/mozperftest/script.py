# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import io
import json
import os
import re
import subprocess
import textwrap
import traceback
from collections import defaultdict
from enum import Enum
from html.parser import HTMLParser
from pathlib import Path

import esprima

# list of metadata, each item is the name and if the field is mandatory
METADATA = [
    ("setUp", False),
    ("tearDown", False),
    ("test", True),
    ("owner", True),
    ("author", False),
    ("name", True),
    ("description", True),
    ("longDescription", False),
    ("options", False),
    ("supportedBrowsers", False),
    ("supportedPlatforms", False),
    ("filename", True),
    ("tags", False),
]

# Metadata variable names to search for in test scripts
METADATA_NAMES = ["perfMetadata", "evalMetadata"]


_INFO = """\
%(filename)s
%(filename_underline)s

:owner: %(owner)s
:name: %(name)s
"""


XPCSHELL_FUNCS = "add_task", "run_test", "run_next_test"


class BadOptionTypeError(Exception):
    """Raised when an option defined in a test has an incorrect type."""

    pass


class MissingFieldError(Exception):
    def __init__(self, script, field):
        super().__init__(f"Missing metadata {field}")
        self.script = script
        self.field = field


class MissingMetadataError(Exception):
    def __init__(self, script):
        metadata_list = "`, `".join(METADATA_NAMES)
        super().__init__(f"Missing `{metadata_list}` variable")
        self.script = script


class ParseError(Exception):
    def __init__(self, script, exception):
        super().__init__(f"Cannot parse {script}")
        self.script = script
        self.exception = exception

    def __str__(self):
        output = io.StringIO()
        traceback.print_exception(
            type(self.exception),
            self.exception,
            self.exception.__traceback__,
            file=output,
        )
        return f"{self.args[0]}\n{output.getvalue()}"


class ScriptType(Enum):
    xpcshell = 1
    browsertime = 2
    mochitest = 3
    custom = 4
    alert = 5
    eval_mochitest = 6


class HTMLScriptParser(HTMLParser):
    def handle_data(self, data):
        if self.script_content is None:
            self.script_content = []
        if any(name in data for name in METADATA_NAMES):
            self.script_content.append(data)
        if any(func_name in data for func_name in XPCSHELL_FUNCS):
            self.script_content.append(data)


class ScriptInfo(defaultdict):
    """Loads and parses a Browsertime test script."""

    def __init__(self, path):
        super().__init__()
        try:
            self.script = Path(path).resolve()
            if self.script.suffix == ".html":
                self._parse_html_file()
            elif self.script.suffix == ".sh":
                self._parse_shell_script()
            elif str(path).isdigit():
                self._parse_alert_test(int(path))
            else:
                self._parse_js_file()
        except Exception as e:
            raise ParseError(path, e)

        # If the fields found, don't match our known ones, then an error is raised
        for field, required in METADATA:
            if not required or self.script_type == ScriptType.alert:
                continue
            if field not in self:
                raise MissingFieldError(path, field)

    def _parse_alert_test(self, alert_summary_id):
        self.script = int(alert_summary_id)
        self.script_content = ""
        self.script_type = ScriptType.alert

    def _set_script_content(self):
        self["filename"] = str(self.script)
        self.script_content = self.script.read_text()

    def _parse_js_file(self):
        self.script_type = ScriptType.browsertime
        self._set_script_content()
        self._parse_script_content()

        if self.get("options", {}).get("default", {}).get("manifest_flavor"):
            # Only mochitest tests have a manifest flavor
            self["test"] = "mochitest"
            if self.get("eval"):
                self.script_type = ScriptType.eval_mochitest
            else:
                self.script_type = ScriptType.mochitest

    def _get_node_builtins(self):
        """
        Returns a set of global variables/functions built into Node.js. (e.g. console, setTimeout)
        """
        js = """
        const builtins = Object.getOwnPropertyNames(globalThis);
        console.log(JSON.stringify(builtins));
        """
        result = subprocess.run(
            ["node", "-e", js], check=True, capture_output=True, text=True
        )
        return set(json.loads(result.stdout))

    def _classify_globals(self):
        """
        Analyzes the test script to find global functions, variables, and object method calls.
        Returns:
            tuple:
                - undeclared_funcs (set[str]): Function names that are called but not declared.
                - undeclared_vars (set[str]): Identifiers used but not declared or built-in.
                - member_calls (dict[str, set[str]]): Object method calls (e.g., Assert.ok → {'Assert': {'ok'}}).
        """
        # A set of function names that are called directly.
        # Example: add_task() → 'add_task'
        undeclared_funcs = set()

        # A set of variable names that are declared via const, let, or var.
        # Example: const metadata = "abc"; → 'metadata'
        declared_vars = set()

        # A set of all identifiers used in the script.
        # This includes variable names, function names, and objects.
        used_identifiers = set()

        # A mapping of object names to the methods accessed on them.
        # Example: Assert.ok() → {'Assert': {'ok'}}
        member_calls = defaultdict(set)

        def walk(node):
            if isinstance(node, list):
                for child in node:
                    walk(child)
            elif isinstance(node, dict):
                node_type = node.get("type")

                if node_type == "CallExpression":
                    callee = node.get("callee", {})
                    if callee["type"] == "Identifier":
                        # Simple function call: add_task()
                        undeclared_funcs.add(callee["name"])
                    elif callee.get("type") == "MemberExpression":
                        # Member function call: Assert.ok()
                        obj = callee.get("object")
                        prop = callee.get("property")
                        if (
                            obj
                            and prop
                            and obj.get("type") == "Identifier"
                            and prop.get("type") == "Identifier"
                        ):
                            member_calls[obj["name"]].add(prop["name"])
                    walk(callee)
                    for arg in node.get("arguments", []):
                        walk(arg)
                elif node_type == "VariableDeclaration":
                    # Variable declaration like: const x = ...;
                    for decl in node.get("declarations", []):
                        id_node = decl.get("id")
                        if id_node["type"] == "Identifier" and "init" in decl:
                            declared_vars.add(decl["id"]["name"])
                        elif id_node["type"] == "ArrayPattern":
                            for element in id_node.get("elements", []):
                                if element and element.get("type") == "Identifier":
                                    declared_vars.add(element["name"])
                        if "init" in decl and decl["init"]:
                            walk(decl["init"])
                elif node_type == "Identifier":
                    # Any usage of an identifier (variable, function, etc.)
                    used_identifiers.add(node["name"])
                else:
                    for child in node.values():
                        walk(child)

        walk(self.parsed.toDict())

        js_builtins = self._get_node_builtins()
        # Remove built-in objects from member_calls
        filtered_member_calls = {
            obj: methods
            for obj, methods in member_calls.items()
            if obj not in js_builtins
        }
        return (
            undeclared_funcs,
            used_identifiers - declared_vars - js_builtins - undeclared_funcs,
            filtered_member_calls,
        )

    def _get_perf_metadata_from_node(self):
        """
        Runs the JS script in a Node.js VM with mocked globals to safely extract metadata.
        Returns:
            dict: Parsed metadata including `__function_keys__` and `__metadata_name__`.
        """
        global_funcs, global_vars, member_calls = self._classify_globals()
        stub_declarations = "\n".join(
            [f"globalThis.{name} = function(){{}};" for name in sorted(global_funcs)]
            + [
                (
                    f"globalThis.{name} = '{name}';"
                    if name.isupper()
                    else f"globalThis.{name} = {{}};"
                )
                for name in sorted(global_vars)
            ]
        )
        for obj, funcs in member_calls.items():
            func_defs = ", ".join(f"{f}: () => true" for f in sorted(funcs))
            stub_declarations += f"\nglobalThis.{obj} = {{ {func_defs} }};"

        metadata_names_json = json.dumps(METADATA_NAMES)
        inject_statements = "\n".join(
            f"            if (typeof {name} !== 'undefined') globalThis.{name} = {name};"
            for name in METADATA_NAMES
        )
        context_init = ",\n            ".join(
            f"{name}: undefined" for name in METADATA_NAMES
        )
        metadata_chain = " ||\n            ".join(
            [f"context.{name}" for name in METADATA_NAMES]
            + [f"context.module.exports.{name}" for name in METADATA_NAMES]
        )

        js_code = f"""
        const vm = require('vm');

        const injectMetadata = `
{inject_statements}
        `
        const fileCode = {json.dumps(self.script_content)};
        const stubGlobals = {json.dumps(stub_declarations)};
        const finalScript = fileCode + injectMetadata

        const context = {{
            {context_init},
            module: {{ exports: {{}} }},
            console: console,
        }};

        vm.createContext(context);
        vm.runInContext(stubGlobals, context);
        vm.runInContext(finalScript, context);

        const metadata =
            {metadata_chain} ||
            context.module.exports;

        const metadataNames = {metadata_names_json};
        let metadataName = null;
        for (const name of metadataNames) {{
            if (context[name] || context.module.exports[name]) {{
                metadataName = name;
                break;
            }}
        }}

        const functionKeys = Object.entries(metadata)
            .filter(([key, val]) => typeof val === 'function')
            .map(([key]) => key);

        const result = {{
            ...metadata,
            __function_keys__: functionKeys,
            __metadata_name__: metadataName
        }};

        if (!result) throw new Error('metadata not found');
        console.log(JSON.stringify(result));
        """

        process = subprocess.run(
            ["node", "-e", js_code],
            check=False,
            capture_output=True,
            text=True,
        )

        if process.returncode != 0:
            raise RuntimeError(f"Node.js error: {process.stderr.strip()}")

        return json.loads(process.stdout)

    def _parse_script_content(self):
        self.parsed = esprima.parseScript(self.script_content)
        parsed_metadata_dynamic = False
        metadata_name = None
        try:
            metadata = self._get_perf_metadata_from_node()
            for key, value in metadata.items():
                if key == "__function_keys__":
                    for func_name in value:
                        self[func_name] = func_name
                elif key == "__metadata_name__":
                    metadata_name = value
                else:
                    self[key] = value
            parsed_metadata_dynamic = True
        except Exception as e:
            print(
                f"Failed to parse metadata dynamically, using static fallback. Error: {e}"
            )

        # looking for the exports statement
        found_metadata = False
        for stmt in self.parsed.body:
            #  detecting if the script has add_task()
            if (
                stmt.type == "ExpressionStatement"
                and stmt.expression is not None
                and stmt.expression.callee is not None
                and stmt.expression.callee.type == "Identifier"
                and stmt.expression.callee.name in XPCSHELL_FUNCS
            ):
                self["test"] = "xpcshell"
                self.script_type = ScriptType.xpcshell
                continue

            # plain xpcshell tests functions markers
            if stmt.type == "FunctionDeclaration" and stmt.id.name in XPCSHELL_FUNCS:
                self["test"] = "xpcshell"
                self.script_type = ScriptType.xpcshell
                continue

            if parsed_metadata_dynamic:
                continue

            # is this a metadata plain var?
            if stmt.type == "VariableDeclaration":
                for decl in stmt.declarations:
                    if (
                        decl.type != "VariableDeclarator"
                        or decl.id.type != "Identifier"
                        or decl.id.name not in METADATA_NAMES
                        or decl.init is None
                    ):
                        continue
                    found_metadata = True
                    metadata_name = decl.id.name
                    self.scan_properties(decl.init.properties)
                    continue

            # or the module.exports map?
            if (
                stmt.type != "ExpressionStatement"
                or stmt.expression.left is None
                or stmt.expression.left.property is None
                or stmt.expression.left.property.name != "exports"
                or stmt.expression.right is None
                or stmt.expression.right.properties is None
            ):
                continue

            # now scanning the properties
            found_metadata = True
            for name in METADATA_NAMES:
                if any(
                    prop.key.name == name
                    for prop in stmt.expression.right.properties
                    if hasattr(prop, "key") and hasattr(prop.key, "name")
                ):
                    metadata_name = name
                    break
            if not metadata_name:
                metadata_name = METADATA_NAMES[0]
            self.scan_properties(stmt.expression.right.properties)

        if not (found_metadata or parsed_metadata_dynamic):
            raise MissingMetadataError(self.script)

        if metadata_name == "evalMetadata":
            self["eval"] = True

    def _parse_html_file(self):
        self._set_script_content()

        html_parser = HTMLScriptParser()
        html_parser.script_content = None
        html_parser.feed(self.script_content)

        if not html_parser.script_content:
            raise MissingMetadataError(self.script)

        # Pass through all the scripts and gather up the data such as
        # the test itself, and the metadata. These can be in separate
        # scripts, but later scripts override earlier ones if there
        # are redefinitions.
        found_metadata = False
        for script_content in html_parser.script_content:
            self.script_content = script_content
            try:
                self._parse_script_content()
                found_metadata = True
            except MissingMetadataError:
                pass
        if not found_metadata:
            raise MissingMetadataError()

        # Mochitest gets detected as xpcshell during parsing
        # since they use similar methods to run tests
        self["test"] = "mochitest"
        self.script_type = ScriptType.mochitest

    def _parse_shell_script(self):
        self._set_script_content()

        for line in self.script_content.split(os.linesep):
            if not line.startswith("#"):
                continue

            stripped_line = line[1:].strip()
            if stripped_line.lower().startswith("name:"):
                self._parse_shell_property("name", stripped_line)
            elif stripped_line.lower().startswith("owner:"):
                self._parse_shell_property("owner", stripped_line)
            elif stripped_line.lower().startswith("description:"):
                self._parse_shell_property("description", stripped_line)
            elif stripped_line.lower().startswith("options:"):
                self._parse_shell_property(
                    "options", stripped_line.replace("#noqa", "")
                )
                self["options"] = json.loads(self["options"])

        self["test"] = "custom-script"
        self.script_type = ScriptType.custom

    def _parse_shell_property(self, prop, line):
        self[prop] = line[len(prop) + 1 :].strip()

    def parse_value(self, value):
        if value.type == "Identifier":
            return value.name

        if value.type == "Literal":
            return value.value

        if value.type == "TemplateLiteral":
            # ugly
            value = value.quasis[0].value.cooked.replace("\n", " ")
            return re.sub(r"\s+", " ", value).strip()

        if value.type == "ArrayExpression":
            return [self.parse_value(e) for e in value.elements]

        if value.type == "ObjectExpression":
            elements = {}
            for prop in value.properties:
                sub_name, sub_value = self.parse_property(prop)
                elements[sub_name] = sub_value
            return elements

        raise ValueError(value.type)

    def parse_property(self, property):
        return property.key.name, self.parse_value(property.value)

    def scan_properties(self, properties):
        for prop in properties:
            name, value = self.parse_property(prop)
            self[name] = value

    def __str__(self):
        """Used to generate docs."""

        def _render(value, level=0):
            if not isinstance(value, (list, tuple, dict)):
                if not isinstance(value, str):
                    value = str(value)
                # line wrapping
                return "\n".join(textwrap.wrap(value, break_on_hyphens=False))

            # options
            if isinstance(value, dict):
                if level > 0:
                    return ",".join([f"{k}:{v}" for k, v in value.items()])

                res = []
                for key, val in value.items():
                    if isinstance(val, bool):
                        res.append(f" --{key.replace('_', '-')}")
                    else:
                        val = _render(val, level + 1)  # noqa
                        res.append(f" --{key.replace('_', '-')} {val}")

                return "\n".join(res)

            # simple flat list
            return ", ".join([_render(v, level + 1) for v in value])

        options = ""
        d = defaultdict(lambda: "N/A")
        for field, value in self.items():
            if field == "longDescription":
                continue
            if field == "filename":
                d[field] = self.script.name
                continue
            if field == "options":
                for plat in "default", "linux", "mac", "win":
                    if plat not in value:
                        continue
                    options += f":{plat.capitalize()} options:\n\n::\n\n{_render(value[plat])}\n"
            else:
                d[field] = _render(value)

        d["filename_underline"] = "=" * len(d["filename"])
        info = _INFO % d
        if "tags" in self:
            info += f":tags: {','.join(self['tags'])}\n"
        info += options
        info += f"\n**{self['description']}**\n"
        if "longDescription" in self:
            desc = " ".join(self["longDescription"].splitlines())
            desc = re.sub(r"\s{2,}", " ", desc).strip()
            info += f"\n{desc}\n"

        return info

    def __missing__(self, key):
        return "N/A"

    @classmethod
    def detect_type(cls, path):
        return cls(path).script_type

    def update_args(self, **args):
        """Updates arguments with options from the script."""
        from mozperftest.utils import simple_platform

        # Order of precedence:
        #   cli options > platform options > default options
        options = self.get("options", {})
        result = options.get("default", {})
        result.update(options.get(simple_platform(), {}))
        result.update(args)

        # XXX this is going away, see https://bugzilla.mozilla.org/show_bug.cgi?id=1675102
        for opt, val in result.items():
            if opt.startswith("visualmetrics") or "metrics" not in opt:
                continue
            if not isinstance(val, list):
                raise BadOptionTypeError("Metrics should be defined within a list")
            for metric in val:
                if not isinstance(metric, dict):
                    raise BadOptionTypeError(
                        "Each individual metrics must be defined within a JSON-like object"
                    )

        if self.script_type == ScriptType.xpcshell:
            result["flavor"] = "xpcshell"
        if self.script_type == ScriptType.mochitest:
            result["flavor"] = "mochitest"
        if self.script_type == ScriptType.eval_mochitest:
            result["flavor"] = "eval-mochitest"
        if self.script_type == ScriptType.custom:
            result["flavor"] = "custom-script"

        return result
