#!/usr/bin/env python3
import re
import shutil
from pathlib import Path
import subprocess


def read_file(file_path: Path):
    return file_path.read_text(encoding="utf-8")


def write_file(file_path: Path, lines):
    file_path.parent.mkdir(parents=True, exist_ok=True)
    file_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def grep_functions(file_path: Path):
    content = read_file(file_path)

    lines = [
        line
        for line in content.splitlines()
        if "link_name" not in line and '"]' not in line and "/**" not in line
    ]

    content = "\n".join(lines)

    content = re.sub(r",\n *", ", ", content)
    content = re.sub(r":\n *", ": ", content)
    content = re.sub(r"\n *->", " ->", content)
    content = re.sub(r"^\}$", "", content, flags=re.MULTILINE)
    content = re.sub(r"^ *pub", "pub", content, flags=re.MULTILINE)
    content = re.sub(r";\n", "\n", content)

    return [line for line in content.splitlines() if line.strip().startswith("pub fn")]


def grep_heur(file_path: Path):
    def filter_pre(line: str) -> bool:
        return (
            "Handle" in line
            and "roxyHandler" not in line
            and "JS::IdVector" not in line
            and "pub fn Unbox" not in line
            and "CopyAsyncStack" not in line
            and "MutableHandleObjectVector" not in line
        )

    def replace_in_line(line: str) -> str:
        return (
            line.replace("root::", "")
            .replace("JS::", "")
            .replace("js::", "")
            .replace("mozilla::", "")
            .replace("Handle<*mut JSObject>", "HandleObject")
        )

    def filter_post(line: str) -> bool:
        return (
            # We are only wrapping handles in args not in results
            "-> Handle" not in line and "-> MutableHandle" not in line
        )

    return list(
        filter(
            filter_post,
            map(replace_in_line, filter(filter_pre, grep_functions(file_path))),
        )
    )


def grep_heur2(file_path: Path):
    def filter_pre(line: str) -> bool:
        return (
            ("Handle" in line or "JSContext" in line)
            and "roxyHandler" not in line
            and "JS::IdVector" not in line
            and "pub fn Unbox" not in line
            and "CopyAsyncStack" not in line
            and "MutableHandleObjectVector" not in line
            and "Opaque" not in line
            and "pub fn JS_WrapPropertyDescriptor1" not in line
            and "pub fn EncodeWideToUtf8" not in line
            and "pub fn JS_NewContext" not in line  # returns jscontext
            # gc module causes problems in macro
            and "pub fn NewMemoryInfo" not in line
            and "pub fn GetGCContext" not in line
            and "pub fn SetDebuggerMalloc" not in line
            and "pub fn GetDebuggerMallocSizeOf" not in line
            and "pub fn FireOnGarbageCollectionHookRequired" not in line
            and "pub fn ShouldAvoidSideEffects" not in line
            # vargs
            and "..." not in line
            and "VA(" not in line
        )

    def replace_in_line(line: str) -> str:
        line = (
            line.replace("root::", "")
            .replace("JS::", "")
            .replace("js::", "")
            .replace("mozilla::", "")
            .replace("*mut JSContext", "&mut JSContext")
            .replace("*const JSContext", "&JSContext")
        )
        if (
            "JS_GetRuntime" in line
            or "JS_GetParentRuntime" in line
            or "JS_GetGCParameter" in line
            or "*const AutoRequireNoGC" in line
        ):
            line = line.replace("&mut JSContext", "&JSContext")
        return line

    def filter_post(line: str) -> bool:
        return (
            # We are only wrapping handles in args not in results
            "-> Handle" not in line and "-> MutableHandle" not in line
        )

    return list(
        filter(
            filter_post,
            map(replace_in_line, filter(filter_pre, grep_functions(file_path))),
        )
    )


def find_latest_version_of_file_and_parse(
    input_file: str, out_module: str, heur_fn, extra: str = ""
):
    target_dir = Path("target")
    files = list(target_dir.rglob(input_file))
    if not files:
        raise FileNotFoundError(f"No file found matching {input_file} in target/")

    newest_file = max(files, key=lambda f: f.stat().st_mtime)

    wrap_file = target_dir / f"wrap_{input_file}"
    shutil.copy(newest_file, wrap_file)

    subprocess.run(
        ["rustfmt", str(wrap_file), "--config", "max_width=1000"], check=True
    )

    lines = heur_fn(wrap_file)
    out_file = Path("mozjs/src") / f"{out_module}{extra}_wrappers.in.rs"
    wrapped_lines = [f"wrap!({out_module}: {line});" for line in lines]
    write_file(out_file, wrapped_lines)


find_latest_version_of_file_and_parse("jsapi.rs", "jsapi", grep_heur)
find_latest_version_of_file_and_parse("gluebindings.rs", "glue", grep_heur)
find_latest_version_of_file_and_parse("jsapi.rs", "jsapi", grep_heur2, "2")
find_latest_version_of_file_and_parse("gluebindings.rs", "glue", grep_heur2, "2")
