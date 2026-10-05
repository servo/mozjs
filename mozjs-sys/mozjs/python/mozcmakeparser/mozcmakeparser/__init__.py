# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import logging
import os
import re
from string import Template

from colorama import Fore, Style, init
from pyparsing import (
    CharsNotIn,
    Forward,
    Group,
    Literal,
    QuotedString,
    Suppress,
    Word,
    ZeroOrMore,
    alphanums,
    alphas,
)

# Initialize colorama
init(autoreset=True)


# Define a custom formatter with colors and indentation
class ColoredFormatter(logging.Formatter):
    COLORS = {
        "DEBUG": Fore.CYAN,
        "INFO": Fore.GREEN,
        "WARNING": Fore.YELLOW,
        "ERROR": Fore.RED,
        "CRITICAL": Fore.RED + Style.BRIGHT,
    }

    def format(self, record):
        log_color = self.COLORS.get(record.levelname, "")
        record.msg = f"{log_color}{record.msg}{Style.RESET_ALL}"
        return super().format(record)


logger = logging.getLogger("cmakeparser")
logger.setLevel(logging.DEBUG)
console_handler = logging.StreamHandler()
# Change this to enable more logging
console_handler.setLevel(logging.ERROR)
formatter = ColoredFormatter("%(asctime)s - %(name)s - %(levelname)s - %(message)s")
console_handler.setFormatter(formatter)
logger.addHandler(console_handler)


def log(level, message, indent=0):
    indent_str = "  " * indent
    logger.log(level, f"{indent_str}{message}")


indentlevel = 0


def debug(msg):
    log(logging.DEBUG, msg, indentlevel)


def info(msg):
    log(logging.INFO, msg, indentlevel)


def warn(msg):
    log(logging.WARNING, msg, indentlevel)


def error(msg):
    log(logging.ERROR, msg, indentlevel)


# Grammar for CMake
comment = Literal("#") + ZeroOrMore(CharsNotIn("\n"))
quoted_argument = QuotedString('"', "\\", multiline=True)
unquoted_argument = CharsNotIn('\n ()#"\\')
argument = quoted_argument | unquoted_argument | Suppress(comment)
arguments = Forward()
arguments << (argument | (Literal("(") + ZeroOrMore(arguments) + Literal(")")))
identifier = Word(alphas, alphanums + "_")
command = Group(identifier + Literal("(") + ZeroOrMore(arguments) + Literal(")"))
file_elements = command | Suppress(comment)
cmake = ZeroOrMore(file_elements)


def extract_arguments(parsed):
    """Extract the command arguments skipping the parentheses"""
    return parsed[2 : len(parsed) - 1]


def match_block(command, parsed, start):
    """Find the end of block starting with the command"""
    depth = 0
    end = start + 1
    endcommand = "end" + command
    while parsed[end][0] != endcommand or depth > 0:
        if parsed[end][0] == command:
            depth += 1
        elif parsed[end][0] == endcommand:
            depth -= 1
        end = end + 1
        if end == len(parsed):
            error(f"error: eof when trying to match block statement: {parsed[start]}")
    return end


def parse_if(parsed, start):
    """Parse if/elseif/else/endif into a list of conditions and commands"""
    depth = 0
    conditions = []
    condition = [extract_arguments(parsed[start])]
    start = start + 1
    end = start

    while parsed[end][0] != "endif" or depth > 0:
        command = parsed[end][0]
        if command == "if":
            depth += 1
        elif command == "else" and depth == 0:
            condition.append(parsed[start:end])
            conditions.append(condition)
            start = end + 1
            condition = [["TRUE"]]
        elif command == "elseif" and depth == 0:
            condition.append(parsed[start:end])
            conditions.append(condition)
            condition = [extract_arguments(parsed[end])]
            start = end + 1
        elif command == "endif":
            depth -= 1
        end = end + 1
        if end == len(parsed):
            print(f"error: eof when trying to match if statement: {parsed[start]}")
    condition.append(parsed[start:end])
    conditions.append(condition)
    return end, conditions


def substs(variables, values):
    """Substitute variables into values"""
    new_values = []
    for value in values:
        t = Template(value)
        new_value = t.safe_substitute(variables)

        # Safe substitute leaves unrecognized variables in place.
        # We replace them with the empty string.
        new_values.append(re.sub(r"\$\{\w+\}", "", new_value))
    return new_values


def append(sources, source_file, pwd, flavor):
    if flavor == "llama":
        sources.append(os.path.join(*pwd, source_file))
    else:
        sources.append(source_file)


def evaluate(variables, cache_variables, parsed, pwd, flavor):
    """Evaluate a list of parsed commands, returning sources to build"""
    i = 0
    sources = []
    while i < len(parsed):
        command = parsed[i][0]
        arguments = substs(variables, extract_arguments(parsed[i]))

        if command == "foreach":
            end = match_block(command, parsed, i)
            for argument in arguments[1:]:
                # ; is also a valid divider, why have one when you can have two?
                cleaned_argument = argument.replace(";", " ")
                for value in cleaned_argument.split():
                    variables[arguments[0]] = value
                    cont_eval, new_sources = evaluate(
                        variables, cache_variables, parsed[i + 1 : end], pwd, flavor
                    )
                    sources.extend(new_sources)
                    if not cont_eval:
                        return cont_eval, sources
        elif command == "function":
            # for now we just execute functions inline at point of declaration
            # as this is sufficient to build libaom
            pass
        elif command == "if":
            i, conditions = parse_if(parsed, i)
            for condition in conditions:
                if evaluate_boolean(variables, condition[0]):
                    cont_eval, new_sources = evaluate(
                        variables, cache_variables, condition[1], pwd, flavor
                    )
                    sources.extend(new_sources)
                    if not cont_eval:
                        return cont_eval, sources
                    break
        elif command == "include":
            if arguments:
                try:
                    print(f"including: {arguments[0]}")
                    sources.extend(
                        parse(variables, cache_variables, pwd, arguments[0], flavor)
                    )
                except OSError:
                    warn(f"warning: could not include: {arguments[0]}")
        elif command == "list":
            try:
                action = arguments[0]
                variable = arguments[1]
                values = arguments[2:]
                if action == "APPEND":
                    if variable not in variables:
                        variables[variable] = " ".join(values)
                    else:
                        variables[variable] += " " + " ".join(values)
            except (IndexError, KeyError):
                pass
        elif command == "option":
            variable = arguments[0]
            value = arguments[2]
            # Allow options to be override without changing CMake files
            if variable not in variables:
                variables[variable] = value
        elif command == "return":
            return False, sources
        elif command == "set":
            variable = arguments[0]
            values = arguments[1:]
            # CACHE variables are not set if already present
            try:
                cache = values.index("CACHE")
                values = values[0:cache]
                if variable not in variables:
                    variables[variable] = " ".join(values)
                cache_variables.append(variable)
            except ValueError:
                variables[variable] = " ".join(values)
        # we need to emulate the behavior of these function calls
        # because we don't support interpreting them directly
        # see bug 1492292
        elif command in ["set_aom_config_var", "set_aom_detect_var"]:
            variable = arguments[0]
            value = arguments[1]
            if variable not in variables:
                variables[variable] = value
            cache_variables.append(variable)
        elif command == "set_aom_option_var":
            # option vars cannot go into cache_variables
            variable = arguments[0]
            value = arguments[2]
            if variable not in variables:
                variables[variable] = value
        # Those two functions are also emulated
        elif command == "ggml_add_backend":
            # Find + evaluate a CMakeLists.txt in a particular subdir, if
            # enabled
            backend = arguments[0].lower()
            if backend in variables["MOZ_GGML_BACKENDS"]:
                subdir = "ggml-" + arguments[0].lower()
                parse_target = os.path.join(*pwd, subdir + "/CMakeLists.txt")
                sources.extend(parse(variables, cache_variables, pwd, parse_target))
        elif command in ["ggml_add_cpu_backend_variant_impl"]:
            # execute ggml/src/ggml-cpu/CMakeLists.txt in current dir
            sources.extend(
                parse(
                    variables,
                    cache_variables,
                    pwd,
                    os.path.join(*pwd, "ggml-cpu/CMakeLists.txt"),
                )
            )
        elif command == "add_asm_library":
            try:
                sources.extend(variables[arguments[1]].split(" "))
            except (IndexError, KeyError):
                pass
        elif command == "add_intrinsics_object_library":
            try:
                source_files = variables[arguments[3]]
                for source_file in source_files.split(" "):
                    append(sources, source_file, pwd, flavor)
            except (IndexError, KeyError):
                pass
        elif command == "add_library":
            if len(arguments) != 3 or arguments[2] != "ggml":
                for source in arguments[1:]:
                    for source_file in source.split(" "):
                        # ALIAS and OBJECT don't need to be handled in our case,
                        # we simply need a list of source files.
                        if source_file not in {"ALIAS", "OBJECT"}:
                            append(sources, source_file, pwd, flavor)
        elif command == "target_sources":
            for source in arguments[1:]:
                for source_file in source.split(" "):
                    if source_file != "PRIVATE":
                        append(sources, source_file, pwd, flavor)
        elif command == "add_subdirectory":
            pwd.append(arguments[0])
            parse_target = os.path.join(*pwd, "CMakeLists.txt")
            sources.extend(parse(variables, cache_variables, pwd, parse_target, flavor))
            pwd.pop()
        elif command == "MOZDEBUG":
            info(f">>>> MOZDEBUG: {' '.join(arguments)}")
        i += 1
    return True, sources


def evaluate_boolean(variables, arguments):
    """Evaluate a boolean expression"""
    if not arguments:
        return False

    argument = arguments[0]

    if argument == "NOT":
        return not evaluate_boolean(variables, arguments[1:])

    if argument == "(":
        i = 0
        depth = 1
        while depth > 0 and i < len(arguments):
            i += 1
            if arguments[i] == "(":
                depth += 1
            if arguments[i] == ")":
                depth -= 1
        return evaluate_boolean(variables, arguments[1:i])

    def evaluate_constant(argument):
        try:
            as_int = int(argument)
            if as_int != 0:
                return True
            else:
                return False
        except ValueError:
            upper = argument.upper()
            if upper in ["ON", "YES", "TRUE", "Y"]:
                return True
            elif upper in ["OFF", "NO", "FALSE", "N", "IGNORE", "", "NOTFOUND"]:
                return False
            elif upper.endswith("-NOTFOUND"):
                return False
        return None

    def lookup_variable(argument):
        # If statements can have old-style variables which are not demarcated
        # like ${VARIABLE}. Attempt to look up the variable both ways.
        try:
            if re.search(r"\$\{\w+\}", argument):
                try:
                    t = Template(argument)
                    value = t.substitute(variables)
                    try:
                        # Attempt an old-style variable lookup with the
                        # substituted value.
                        return variables[value]
                    except KeyError:
                        return value
                except ValueError:
                    # TODO: CMake supports nesting, e.g. ${${foo}}
                    return None
            else:
                return variables[argument]
        except KeyError:
            return None

    lhs = lookup_variable(argument)
    if lhs is None:
        # variable resolution failed, treat as string
        lhs = argument

    if len(arguments) > 1:
        op = arguments[1]
        if op == "AND":
            return evaluate_constant(lhs) and evaluate_boolean(variables, arguments[2:])
        elif op == "MATCHES":
            rhs = lookup_variable(arguments[2])
            if not rhs:
                rhs = arguments[2]
            return not re.match(rhs, lhs) is None
        elif op == "OR":
            return evaluate_constant(lhs) or evaluate_boolean(variables, arguments[2:])
        elif op == "STREQUAL":
            rhs = lookup_variable(arguments[2])
            if not rhs:
                rhs = arguments[2]
            return lhs == rhs
    else:
        lhs = evaluate_constant(lhs)
        if lhs is None:
            lhs = lookup_variable(argument)

    return lhs


def parse(variables, cache_variables, pwd, filename, flavor):
    """Parse a CMakeLists.txt file and extract source files.

    Args:
        variables (dict): Dictionary of CMake variables for substitution
        (in/out)
        cache_variables (list): List of variables that should be cached (not
        set if already present)
        pwd (list): Current working directory path components
        filename (str): Path to the CMakeLists.txt file to parse
        flavor (str): Parser flavor - either 'llama' or 'libaom', this
        changes whether `cwd` is prefixed to the source file or not.

    Returns:
        list: List of source file paths extracted from the CMakeLists.txt
    """
    parsed = cmake.parseFile(filename)
    cont_eval, sources = evaluate(variables, cache_variables, parsed, pwd, flavor)
    return sources
