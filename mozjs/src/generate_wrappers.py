#!/usr/bin/env python3
import re
import shutil
from pathlib import Path
import subprocess
import gzip


no_gc = set()

with gzip.open("target/noGC.txt.gz", "rt") as f:
    for line in f:
        no_gc.add(line.split(maxsplit=1)[0].split("$", maxsplit=1)[0])


def read_file(file_path: Path):
    return file_path.read_text(encoding="utf-8")


def write_file(file_path: Path, lines):
    file_path.parent.mkdir(parents=True, exist_ok=True)
    file_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def grep_functions(file_path: Path) -> list[tuple[str, str | None]]:
    content = read_file(file_path)

    # Match:
    #   - optional #[link_name = "..."]
    #   - followed by pub fn ...;
    pattern = re.compile(
        r'(?:#\s*\[\s*link_name\s*=\s*"(?P<link>[^"]+)"\s*\]\s*)?'
        r"(?P<sig>pub\s+fn[^;{]+)\s*;",
        re.MULTILINE,
    )

    return [
        (
            re.sub(r"\s+", " ", m.group("sig").strip()),
            (m.group("link") or "").removeprefix("\\u{1}") or None,
        )
        for m in pattern.finditer(content)
    ]


def grep_heur(file_path: Path) -> list[str]:
    def no_link_name(fn: tuple[str, str | None]) -> str:
        sig, _ = fn
        return sig

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
            map(
                replace_in_line,
                filter(filter_pre, map(no_link_name, grep_functions(file_path))),
            ),
        )
    )


# print(grep_functions(Path("./target/wrap_jsapi.rs")))
# exit(0)


def grep_heur2(file_path: Path) -> list[str]:
    def filter_pre(fn: tuple[str, str | None]) -> bool:
        sig, _ = fn
        return (
            ("Handle" in sig or "JSContext" in sig)
            and "roxyHandler" not in sig
            and "JS::IdVector" not in sig
            and "pub fn Unbox" not in sig
            and "CopyAsyncStack" not in sig
            and "MutableHandleObjectVector" not in sig
            and "Opaque" not in sig
            and "pub fn JS_WrapPropertyDescriptor1" not in sig
            and "pub fn EncodeWideToUtf8" not in sig
            and "pub fn JS_NewContext" not in sig  # returns jscontext
            # gc module causes problems in macro
            and "pub fn NewMemoryInfo" not in sig
            and "pub fn GetGCContext" not in sig
            and "pub fn SetDebuggerMalloc" not in sig
            and "pub fn GetDebuggerMallocSizeOf" not in sig
            and "pub fn FireOnGarbageCollectionHookRequired" not in sig
            and "pub fn ShouldAvoidSideEffects" not in sig
            # vargs
            and "..." not in sig
            and "VA(" not in sig
        )

    def replace_in_line(fn: tuple[str, str | None]) -> str:
        sig, link_name = fn
        sig = (
            sig.replace("root::", "")
            .replace("JS::", "")
            .replace("js::", "")
            .replace("mozilla::", "")
            .replace("*mut JSContext", "&mut JSContext")
            .replace("*const JSContext", "&JSContext")
        )
        if link_name in no_gc or "NewCompileOptions" in sig or "CurrentGlobal" in sig:
            sig = sig.replace("&mut JSContext", "&JSContext")
        return sig

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
