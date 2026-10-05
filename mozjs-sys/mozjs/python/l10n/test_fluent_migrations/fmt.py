import importlib.util
import logging
import os
import platform
import re
import shutil
import stat
import sys
from collections.abc import Iterable
from datetime import datetime, timedelta
from difflib import unified_diff
from subprocess import check_call

from compare_locales.merge import merge_channels
from compare_locales.paths.configparser import TOMLParser
from compare_locales.paths.files import ProjectFiles
from fluent.migrate.repo_client import RepoClient, git
from fluent.migrate.transforms import Source
from fluent.migrate.util import fold
from fluent.migrate.validator import Validator
from fluent.syntax import FluentParser, FluentSerializer
from fluent.syntax import ast as FTL
from mach.util import get_state_dir
from mozpack.path import join, normpath

L10N_SOURCE_NAME = "l10n-source"
L10N_SOURCE_REPO = "https://github.com/mozilla-l10n/firefox-l10n-source.git"

PULL_AFTER = timedelta(days=2)


def handle_rmtree_error(func, path, exc_info):
    """
    Custom error handler for shutil.rmtree().
    Attempts to change file permissions if a permission error occurs.
    """
    if func == os.unlink and isinstance(exc_info[0], PermissionError):
        print(
            f"Permission error encountered for: {path}. Attempting to change permissions."
        )
        try:
            os.chmod(path, stat.S_IWRITE)  # Make the file writable
            func(path)  # Retry the removal
        except Exception as e:
            print(f"Failed to remove {path} even after changing permissions: {e}")
            raise  # Re-raise the original exception if retry fails
    else:
        raise exc_info[0].with_traceback(exc_info[1], exc_info[2])


def remove_readonly(func, path, _):
    "Clear the readonly bit and reattempt the removal"
    os.chmod(path, stat.S_IWRITE)
    func(path)


def inspect_migration(path):
    """Validate recipe and extract some metadata."""
    return Validator.validate(path)


def prepare_directories(cmd):
    """
    Ensure object dir exists,
    and that repo dir has a relatively up-to-date clone of l10n-source or gecko-strings.

    We run this once per mach invocation, for all tested migrations.
    """
    obj_dir = join(cmd.topobjdir, "python", "l10n")
    if not os.path.exists(obj_dir):
        os.makedirs(obj_dir)

    repo_dir = join(get_state_dir(), L10N_SOURCE_NAME)
    marker = join(repo_dir, ".git", "l10n_pull_marker")

    try:
        last_pull = datetime.fromtimestamp(os.stat(marker).st_mtime)
        skip_clone = datetime.now() < last_pull + PULL_AFTER
    except OSError:
        skip_clone = False
    if not skip_clone:
        if os.path.exists(repo_dir):
            check_call(["git", "pull", L10N_SOURCE_REPO], cwd=repo_dir)
        else:
            check_call(["git", "clone", L10N_SOURCE_REPO, repo_dir])
        with open(marker, "w") as fh:
            fh.flush()

    return obj_dir, repo_dir


def diff_resources(left_path, right_path):
    parser = FluentParser(with_spans=False)
    serializer = FluentSerializer(with_junk=True)
    lines = []
    for p in (left_path, right_path):
        with open(p, encoding="utf-8") as fh:
            res = parser.parse(fh.read())
            lines.append(serializer.serialize(res).splitlines(True))
    sys.stdout.writelines(
        chunk for chunk in unified_diff(lines[0], lines[1], left_path, right_path)
    )


def entries_by_id(text):
    """Map each message/term identifier to its serialized form.
    Anything that isn't a message or term (comments, junk) is
    skipped.
    """
    parser = FluentParser(with_spans=False)
    serializer = FluentSerializer()
    entries = {}
    for entry in parser.parse(text).body:
        if isinstance(entry, FTL.Message):
            id = entry.id.name
        elif isinstance(entry, FTL.Term):
            id = "-" + entry.id.name
        else:
            continue
        entries[id] = serializer.serialize_entry(entry)
    return entries


def source_deps(node):
    """Collect the `(path, key)` of every Source transform within `node`."""

    def add(acc, cur):
        if isinstance(cur, Source):
            acc.add((cur.path, cur.key))
        return acc

    return fold(add, node, set())


def declared_targets(recipe_path):
    """Collect the messages declared in the recipe.

    The recipe is run against a context that only records `add_transforms`
    calls, capturing every declared message even when the actual migration
    for a message fails, never reaching the output.

    Returns `(targets, self_migrations)` where `targets` maps each target (and
    reference) path to the set of declared message/term identifiers, and
    `self_migrations` lists the `(target_path, id)` whose transform copies
    the message, partially or entirely, from the same ID in the same file.
    Cannot just check the output for the latter, since the migration will be
    a no-op (the correct result will be in the file).
    """
    targets = {}
    self_migrations = []

    class RecordingContext:
        def add_transforms(self, target, reference, transforms):
            ids = set()
            for node in transforms:
                id = "-" + node.id.name if isinstance(node, FTL.Term) else node.id.name
                ids.add(id)
                for path, key in source_deps(node):
                    if path == target and key.split(".", 1)[0] == id:
                        self_migrations.append((target, id))
                        break
            for key in (target, reference):
                targets.setdefault(key, set()).update(ids)

    spec = importlib.util.spec_from_file_location(
        "fluent_migration_recipe", recipe_path
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.migrate(RecordingContext())
    return targets, self_migrations


# Bit flags OR'd together into the return value of a migration test, so the
# exit status records which kinds of failure occurred, not just that one did.
ERR_REFERENCE_PATH = 1  # A reference path was not normalized
ERR_SELF_MIGRATION = 2  # A message is migrated from the same ID in the same file
ERR_NOT_MIGRATED = 4  # A declared message is missing from the migrated output
ERR_MISMATCH = 8  # A migrated message differs from the reference beyond capitalization
ERR_COMMIT_MESSAGE = 16  # Missing/wrong bug number, or a commit without "part {index}"


def summarize_diff(report, ref, ref_path, out_path, declared):
    """Collect findings from the diff between the migrated output and reference.

    `declared` is the set of message identifiers included in the recipe and
    it is used to classify these differences:

    - Messages that differ but aren't declared by the recipe are reported first
      as "can be ignored" (e.g. unrelated pending changes in quarantine, or
      brand new strings that cannot be migrated).
    - Messages in the recipe missing from the output are reported as errors:
      the recipe meant to migrate them but didn't, likely because of an error.
    - Declared messages present in the output but differing from the reference
      only in capitalization are reported as warnings, since that is often
      acceptable; any other difference is reported as an error.
    - Attempt to migrate an ID to the same ID in the same file is reported
      as an error.

    Findings are appended to `report` for later rendering rather than logged
    inline. Returns a bitmask of the error cases encountered: `ERR_NOT_MIGRATED`
    if a declared message failed to migrate, `ERR_MISMATCH` if a migrated
    message differs from the reference beyond capitalization, 0 if neither.
    """
    with open(ref_path, encoding="utf-8") as fh:
        ref_entries = entries_by_id(fh.read())
    with open(out_path, encoding="utf-8") as fh:
        out_entries = entries_by_id(fh.read())

    diff_ids = {
        id
        for id in set(ref_entries) | set(out_entries)
        if ref_entries.get(id) != out_entries.get(id)
    }

    ignored = sorted(diff_ids - declared)
    missing = sorted(id for id in declared if id not in out_entries)
    mismatched = sorted(id for id in declared & diff_ids if id in out_entries)

    rv = 0

    if ignored:
        report.append((
            logging.INFO,
            {"file": ref, "ids": ", ".join(ignored)},
            "{file}: the following messages differ but are not part of the "
            "migration recipe, so they can be ignored: {ids}",
        ))
    for id in missing:
        rv |= ERR_NOT_MIGRATED
        report.append((
            logging.ERROR,
            {"file": ref, "id": id},
            "{file}: message {id} is part of the migration recipe but was not "
            "migrated; the recipe likely has errors",
        ))
    for id in mismatched:
        if id not in ref_entries:
            report.append((
                logging.WARN,
                {"file": ref, "id": id},
                "{file}: migrated message {id} is not present in the reference",
            ))
        elif ref_entries[id].casefold() == out_entries[id].casefold():
            report.append((
                logging.WARN,
                {"file": ref, "id": id},
                "{file}: migrated message {id} differs from the reference only "
                "in capitalization",
            ))
        else:
            rv |= ERR_MISMATCH
            report.append((
                logging.ERROR,
                {"file": ref, "id": id},
                "{file}: migrated message {id} differs from the reference",
            ))
    return rv


def render_report(report):
    """Print collected findings as a single summary at the end of the run.

    Findings are ordered by severity, ignorable ones first and errors last, so
    the most important messages end up closest to the prompt. Each finding is a
    `(level, fields, template)` tuple. The summary is printed to stdout, the
    same stream as the diffs, so it stays grouped after them even when output is
    piped or redirected (mach's logger writes to a separate stream).
    """
    if not report:
        return
    labels = {
        logging.INFO: (0, "INFO"),
        logging.WARNING: (1, "WARNING"),
        logging.ERROR: (2, "ERROR"),
    }
    print("\nFluent migration test summary:")
    for level, fields, template in sorted(
        report, key=lambda f: (labels.get(f[0], (1,))[0], f[1].get("file", ""), f[2])
    ):
        label = labels.get(level, (1, "INFO"))[1]
        print(f"  {label}: {template.format(**fields)}")


def test_migration(
    cmd,
    obj_dir: str,
    repo_dir: str,
    report: list,
    to_test: list[str],
    references: Iterable[str],
):
    """Test the given recipe.

    This creates a workdir by l10n-merging gecko-strings and the m-c source,
    to mimic gecko-strings after the patch to test landed.
    It then runs the recipe with a gecko-strings clone as localization, both
    dry and wet.
    It inspects the generated commits, and shows a diff between the merged
    reference and the generated content.
    The diff is intended to be visually inspected. Some changes might be
    expected, in particular when formatting of the en-US strings is different.
    """
    rv = 0
    migration_name = os.path.splitext(os.path.split(to_test)[1])[0]
    work_dir = join(obj_dir, migration_name)

    paths = os.path.normpath(to_test).split(os.sep)
    # Migration modules should be in a sub-folder of l10n.
    migration_module = (
        ".".join(paths[paths.index("l10n") + 1 : -1]) + "." + migration_name
    )

    if os.path.exists(work_dir):
        # in python 3.12+ we can use onexc=
        pyver = platform.python_version()
        major, minor, _ = pyver.split(".")
        # 3.12 deprecated onerror and introduced onexc.
        if int(major) >= 3 and int(minor) >= 12:
            shutil.rmtree(work_dir, onexc=remove_readonly)
        else:
            shutil.rmtree(work_dir, onerror=handle_rmtree_error)

    os.makedirs(join(work_dir, "reference"))
    l10n_toml = join(cmd.topsrcdir, cmd.substs["MOZ_BUILD_APP"], "locales", "l10n.toml")
    pc = TOMLParser().parse(l10n_toml, env={"l10n_base": work_dir})
    pc.set_locales(["reference"])
    files = ProjectFiles("reference", [pc])
    ref_root = join(work_dir, "reference")
    for ref in references:
        if ref != normpath(ref):
            report.append((
                logging.ERROR,
                {"file": to_test, "ref": ref},
                'Reference path "{ref}" needs to be normalized for {file}',
            ))
            rv |= ERR_REFERENCE_PATH
            continue
        full_ref = join(ref_root, ref)
        m = files.match(full_ref)
        if m is None:
            raise ValueError("Bad reference path: " + ref)
        m_c_path = m[1]
        g_s_path = join(work_dir, L10N_SOURCE_NAME, ref)
        resources = [
            b"" if not os.path.exists(f) else open(f, "rb").read()
            for f in (g_s_path, m_c_path)
        ]
        ref_dir = os.path.dirname(full_ref)
        if not os.path.exists(ref_dir):
            os.makedirs(ref_dir)
        open(full_ref, "wb").write(merge_channels(ref, resources))
    l10n_root = join(work_dir, "en-US")
    git(work_dir, "clone", repo_dir, l10n_root)
    client = RepoClient(l10n_root)
    old_tip = client.head()
    run_migration = [
        cmd._virtualenv_manager.python_path,
        "-m",
        "fluent.migrate.tool",
        "--lang",
        "en-US",
        "--reference-dir",
        ref_root,
        "--localization-dir",
        l10n_root,
        "--dry-run",
        migration_module,
    ]
    cmd.run_process(run_migration, cwd=work_dir, line_handler=print)
    # drop --dry-run
    run_migration.pop(-2)
    cmd.run_process(run_migration, cwd=work_dir, line_handler=print)
    tip = client.head()
    try:
        targets, self_migrations = declared_targets(to_test)
    except Exception as e:
        report.append((
            logging.ERROR,
            {"file": to_test, "error": str(e)},
            "Could not inspect declared targets for {file}: {error}",
        ))
        targets, self_migrations = {}, []
    for target_path, id in self_migrations:
        rv |= ERR_SELF_MIGRATION
        report.append((
            logging.ERROR,
            {"file": target_path, "id": id},
            "{file}: message {id} is migrated from itself (same ID in the same file)",
        ))
    if old_tip == tip:
        report.append((
            logging.WARN,
            {"file": to_test},
            "No migration applied for {file}",
        ))
        return rv
    for ref in references:
        ref_path = join(ref_root, ref)
        out_path = join(l10n_root, ref)
        diff_resources(ref_path, out_path)
        rv |= summarize_diff(report, ref, ref_path, out_path, targets.get(ref, set()))
    messages = client.log(old_tip, tip)
    bug = re.search("[0-9]{5,}", migration_name)
    # Just check first message for bug number, they're all following the same pattern
    if bug is None or bug.group() not in messages[0]:
        rv |= ERR_COMMIT_MESSAGE
        report.append((
            logging.ERROR,
            {"file": to_test},
            "Missing or wrong bug number for {file}",
        ))
    if any(f"part {n + 1}" not in msg for n, msg in enumerate(messages)):
        rv |= ERR_COMMIT_MESSAGE
        report.append((
            logging.ERROR,
            {"file": to_test},
            'Commit messages should have "part {{index}}" for {file}',
        ))
    return rv
