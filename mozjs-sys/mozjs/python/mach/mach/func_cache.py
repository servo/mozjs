# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import functools
import hashlib
import inspect
import os
import sys
from pathlib import Path

from mozfile import json

from mach.util import get_state_dir

_topsrcdir = Path(__file__).resolve().parent.parent.parent.parent
_cache_dir = Path(get_state_dir()) / "mach_func_cache"
_debug = "MACH_FUNC_CACHE_DEBUG" in os.environ
_INPUTS_FILENAME = "_dynamic_inputs.json"


def _log(msg):
    if _debug:
        print(f"mach_func_cache> {msg}", file=sys.stderr)


def _derive_cache_name(fn):
    """Derive a human-readable cache directory name from a function's
    file path (relative to topsrcdir) and qualified name.

    For example, a function ``toolchain_task_definitions`` in
    ``python/mozbuild/mozbuild/toolchains.py`` produces:
    ``python-mozbuild-mozbuild-toolchains.py-toolchain_task_definitions``
    """
    fn_file = Path(inspect.getfile(fn)).resolve()
    try:
        rel = fn_file.relative_to(_topsrcdir)
    except ValueError:
        rel = fn_file
    return "-".join(list(rel.parts) + [fn.__qualname__])


def _is_glob_pattern(path):
    return "*" in path or "?" in path


def _resolve_inputs(inputs):
    """Resolve input paths to individual files, expanding globs and
    recursing into directories.
    """
    result = set()
    for input_path in sorted(inputs):
        if _is_glob_pattern(input_path):
            for filepath in _topsrcdir.glob(input_path):
                if filepath.is_file() and not filepath.name.endswith((
                    ".pyc",
                    ".pyd",
                    ".pyo",
                )):
                    result.add(filepath)
            continue

        full_path = _topsrcdir / input_path
        if full_path.is_dir():
            for root, _dirs, files in os.walk(full_path):
                for f in files:
                    if f.endswith((".pyc", ".pyd", ".pyo")):
                        continue
                    result.add(Path(root) / f)
        elif full_path.is_file():
            result.add(full_path)
    return sorted(result)


def _hash_inputs(inputs, env_vars=None, python_version=False, arg_key=None):
    """Compute a SHA-256 hash over the contents of input files/directories,
    plus optional environment variables, Python version, and argument key.

    Args:
        inputs: List of file or directory paths relative to topsrcdir.
            Paths may contain glob patterns (``*`` or ``?``), which will
            be expanded against topsrcdir.
        env_vars: Optional list of environment variable names whose values
            should be included in the hash.
        python_version: If True, include sys.version in the hash.
        arg_key: Optional JSON-serialized argument string to include in
            the hash.
    """
    h = hashlib.sha256()

    for filepath in _resolve_inputs(inputs):
        rel = filepath.relative_to(_topsrcdir)
        h.update(str(rel).encode())
        with open(filepath, "rb") as fh:
            h.update(fh.read())

    if env_vars:
        for var in sorted(env_vars):
            val = os.environ.get(var, "")
            h.update(f"env\0{var}\0{val}".encode())

    if python_version:
        h.update(f"python\0{sys.version}".encode())

    if arg_key:
        h.update(f"args\0{arg_key}".encode())

    return h.hexdigest()


def _prune_cache_dir(cache_dir, max_entries):
    """Remove the oldest entries beyond max_entries, based on mtime."""
    entries = sorted(
        (p for p in cache_dir.iterdir() if p.name != _INPUTS_FILENAME),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    while len(entries) > max_entries:
        entries.pop().unlink()


def mach_func_cache(
    inputs, dynamic_inputs=None, env_vars=None, python_version=False, max_entries=10
):
    """Decorator that caches a function's return value on disk, keyed by the
    content hash of the specified input files/directories.

    Also caches in-memory for the lifetime of the process, like
    ``functools.cache``.

    The cached result is stored as JSON. Function arguments, if any, must be
    both hashable (for in-memory caching) and JSON-serializable (for disk
    caching), and are included in the cache key. A ``TypeError`` will be
    raised at call time if these requirements are not met.

    Args:
        inputs: List of file or directory paths relative to topsrcdir whose
            contents determine the cache key.
        dynamic_inputs: Optional callable that takes the function's result and
            returns a list of additional file paths (relative to topsrcdir,
            may contain glob patterns) to include in the cache key. These are
            saved to ``_dynamic_inputs.json`` so that subsequent runs can
            include their contents in the hash and skip recomputation when
            none of the inputs have changed.
        env_vars: Optional list of environment variable names to include in
            the cache key.
        python_version: If True, include the Python version in the cache key.
        max_entries: Maximum number of cached results to keep per function.
            Oldest entries are pruned on write. Defaults to 10.
    """

    def decorator(fn):
        if "MACH_NO_FUNC_CACHE" in os.environ:
            return fn

        cache_name = _derive_cache_name(fn)

        @functools.cache
        @functools.wraps(fn)
        def wrapper(*args, **kwargs):
            cache_dir = _cache_dir / cache_name
            inputs_file = cache_dir / _INPUTS_FILENAME

            arg_key = None
            if args or kwargs:
                try:
                    arg_key = json.dumps((args, kwargs), sort_keys=True)
                except TypeError as e:
                    raise TypeError(
                        "mach_func_cache: arguments must be "
                        f"JSON-serializable, got: {e}"
                    )

            have_dynamic = dynamic_inputs and inputs_file.is_file()
            if not dynamic_inputs or have_dynamic:
                all_inputs = list(inputs)
                if have_dynamic:
                    with open(inputs_file) as f:
                        all_inputs.extend(json.load(f))
                    _log(
                        f"{fn.__qualname__}: loaded "
                        f"{len(all_inputs) - len(inputs)} dynamic inputs"
                    )

                full_hash = _hash_inputs(all_inputs, env_vars, python_version, arg_key)

                cache_file = cache_dir / f"{full_hash}.json"
                if cache_file.is_file():
                    _log(f"{fn.__qualname__}: cache hit ({full_hash[:12]})")
                    os.utime(cache_file)
                    with open(cache_file) as f:
                        return json.load(f)

                _log(f"{fn.__qualname__}: cache miss ({full_hash[:12]})")
            else:
                _log(
                    f"{fn.__qualname__}: no saved dynamic inputs, skipping cache lookup"
                )

            result = fn(*args, **kwargs)

            all_inputs = list(inputs)
            if dynamic_inputs:
                new_extra = dynamic_inputs(result)
                all_inputs.extend(new_extra)
                _log(f"{fn.__qualname__}: extracted {len(new_extra)} dynamic inputs")

            full_hash = _hash_inputs(all_inputs, env_vars, python_version, arg_key)

            cache_dir.mkdir(parents=True, exist_ok=True)
            cache_file = cache_dir / f"{full_hash}.json"
            with open(cache_file, "w") as f:
                json.dump(result, f)
            _prune_cache_dir(cache_dir, max_entries)

            if dynamic_inputs:
                with open(inputs_file, "w") as f:
                    json.dump(new_extra, f)

            _log(f"{fn.__qualname__}: saved result ({full_hash[:12]})")

            return result

        return wrapper

    return decorator
