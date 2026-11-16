#!/usr/bin/env python3
"""Detect #include case-sensitivity mismatches and case-only path collisions.

The firmware is authored on Windows and macOS, whose filesystems are
case-insensitive. A ``#include "Foo.h"`` that names ``foo.h`` on disk therefore
compiles locally and fails on Linux / WSL ext4 / case-sensitive CI. This script
makes that class of breakage machine-checkable and is wired into
``.git/hooks/pre-commit``.

Checks performed over the tracked file set (``git ls-files``):

1. Include-literal case. Every ``#include`` literal, quoted (``"..."``) or
   angle-bracket (``<...>``), is resolved by basename, case-insensitively. If
   the literal's basename names a tracked file whose real spelling differs in
   case, it is reported as a mismatch.
2. Case-only path collisions. Two tracked paths that differ in nothing but case
   cannot coexist on a case-insensitive filesystem and are reported.

Includes that do not resolve by basename at all (system headers, third-party
paths outside the repository) are skipped by design.

Usage::

    python tools/check_case_sensitivity.py [--repo-root PATH] [--fix]

Exit codes: 0 = clean, 1 = mismatches or collisions found.
"""

from __future__ import annotations

import argparse
import collections
import os
import re
import subprocess
import sys
from pathlib import Path

# Extensions scanned for include directives.
SOURCE_EXTENSIONS = frozenset(
    {".c", ".h", ".cc", ".cpp", ".cxx", ".hh", ".hpp", ".hxx", ".inl", ".ino"}
)

QUOTED_INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"')
ANGLE_INCLUDE_RE = re.compile(r"^[ \t]*#[ \t]*include[ \t]*<([^>]+)>")


def match_include(line: str) -> tuple[re.Match, str] | None:
    """Match an include directive; returns (match, delimiter), literal in group 1."""
    for opener, regex in (('"', QUOTED_INCLUDE_RE), ("<", ANGLE_INCLUDE_RE)):
        match = regex.match(line)
        if match:
            return match, opener
    return None


def find_repo_root() -> Path:
    """Default repo root: the parent of the directory holding this script."""
    return Path(__file__).resolve().parent.parent


def list_tracked_files(repo_root: Path) -> list[str]:
    """Tracked paths, posix-style, sorted for deterministic output."""
    proc = subprocess.run(
        ["git", "ls-files"],
        cwd=str(repo_root),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.returncode != 0:
        sys.stderr.write(
            "error: 'git ls-files' failed in %s:\n%s\n"
            % (repo_root, proc.stderr.decode("utf-8", "replace").strip())
        )
        raise SystemExit(2)
    return sorted(p for p in proc.stdout.decode("utf-8", "replace").splitlines() if p)


def index_by_basename(files: list[str]) -> dict[str, list[str]]:
    """Map lowercased basename -> tracked paths using that basename."""
    index: dict[str, list[str]] = collections.defaultdict(list)
    for path in files:
        index[os.path.basename(path).lower()].append(path)
    return index


def find_collisions(files: list[str]) -> list[list[str]]:
    """Groups of tracked paths that collide when compared case-insensitively."""
    groups: dict[str, list[str]] = collections.defaultdict(list)
    for path in files:
        groups[path.lower()].append(path)
    return [sorted(g) for _, g in sorted(groups.items()) if len(g) > 1]


def read_text(path: Path) -> str:
    """Read preserving every byte that decodes (CRLF, stray high bytes)."""
    with open(path, "r", encoding="utf-8", errors="surrogateescape", newline="") as fh:
        return fh.read()


def write_text(path: Path, text: str) -> None:
    with open(path, "w", encoding="utf-8", errors="surrogateescape", newline="") as fh:
        fh.write(text)


def scan_includes(
    repo_root: Path, files: list[str], basename_index: dict[str, list[str]]
) -> list[tuple[str, int, str, str, list[str]]]:
    """Return (path, lineno, delimiter, literal, disk_basenames) per mismatch."""
    mismatches: list[tuple[str, int, str, str, list[str]]] = []
    for rel in files:
        if os.path.splitext(rel)[1].lower() not in SOURCE_EXTENSIONS:
            continue
        abspath = repo_root / rel.replace("/", os.sep)
        try:
            text = read_text(abspath)
        except OSError as exc:
            sys.stderr.write("warning: cannot read %s: %s\n" % (rel, exc))
            continue
        for lineno, line in enumerate(text.splitlines(), 1):
            matched = match_include(line)
            if not matched:
                continue
            match, opener = matched
            literal = match.group(1)
            basename = os.path.basename(literal.replace("\\", "/"))
            candidates = basename_index.get(basename.lower(), [])
            if not candidates:
                continue  # not a repository header; nothing to check
            disk_basenames = sorted({os.path.basename(c) for c in candidates})
            if basename not in disk_basenames:
                mismatches.append((rel, lineno, opener, literal, disk_basenames))
    return mismatches


def apply_fixes(
    repo_root: Path, mismatches: list[tuple[str, int, str, str, list[str]]]
) -> tuple[int, list[str]]:
    """Rewrite only include literals. Returns (applied_count, notes).

    Skips ambiguous cases: a basename that maps to several distinct on-disk
    spellings gives no single correct answer, so it is reported for a human.
    """
    by_file: dict[str, dict[int, tuple[str, str]]] = collections.defaultdict(dict)
    notes: list[str] = []
    for rel, lineno, _opener, literal, disk_basenames in mismatches:
        if len(disk_basenames) != 1:
            notes.append(
                "%s:%d: ambiguous include %r matches %s; not auto-fixed"
                % (rel, lineno, literal, ", ".join(repr(b) for b in disk_basenames))
            )
            continue
        correct = disk_basenames[0]
        normalized = literal.replace("\\", "/")
        prefix = normalized[: len(normalized) - len(os.path.basename(normalized))]
        by_file[rel][lineno] = (literal, prefix + correct)

    applied = 0
    for rel, replacements in sorted(by_file.items()):
        abspath = repo_root / rel.replace("/", os.sep)
        try:
            text = read_text(abspath)
        except OSError as exc:
            notes.append("%s: cannot read for fixing: %s" % (rel, exc))
            continue
        lines = text.splitlines(keepends=True)
        changed = False
        for lineno, (old_literal, new_literal) in sorted(replacements.items()):
            idx = lineno - 1
            if idx >= len(lines):
                notes.append("%s:%d: line vanished during fix" % (rel, lineno))
                continue
            # Preserve the exact original spacing of the directive keyword.
            matched = match_include(lines[idx])
            if not matched or matched[0].group(1) != old_literal:
                notes.append("%s:%d: directive changed during fix" % (rel, lineno))
                continue
            directive_match = matched[0]
            head = lines[idx][: directive_match.start(1)]
            tail = lines[idx][directive_match.end(1) :]
            lines[idx] = head + new_literal + tail
            applied += 1
            changed = True
        if changed:
            write_text(abspath, "".join(lines))
    return applied, notes


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Report #include case mismatches and case-only path collisions in "
            "tracked files."
        )
    )
    parser.add_argument(
        "--repo-root",
        default=None,
        help="repository root (default: parent directory of this script)",
    )
    parser.add_argument(
        "--fix",
        action="store_true",
        help="rewrite unambiguous include literals in place (never filenames)",
    )
    args = parser.parse_args(argv)

    repo_root = Path(args.repo_root).resolve() if args.repo_root else find_repo_root()
    if not repo_root.is_dir():
        sys.stderr.write("error: repo root is not a directory: %s\n" % repo_root)
        return 2

    files = list_tracked_files(repo_root)
    basename_index = index_by_basename(files)
    collisions = find_collisions(files)

    mismatches = scan_includes(repo_root, files, basename_index)

    applied = 0
    notes: list[str] = []
    if args.fix and mismatches:
        applied, notes = apply_fixes(repo_root, mismatches)
        if applied:
            # Re-scan so the reported state and exit code describe the tree
            # as it now stands on disk.
            mismatches = scan_includes(repo_root, files, basename_index)

    for rel, lineno, opener, literal, disk_basenames in mismatches:
        disk = (
            disk_basenames[0]
            if len(disk_basenames) == 1
            else " | ".join(disk_basenames)
        )
        closer = '"' if opener == '"' else ">"
        print(
            '%s:%d: include %s%s%s but on disk "%s"'
            % (rel, lineno, opener, literal, closer, disk)
        )

    for group in collisions:
        print("collision: paths differ only by case: %s" % " | ".join(group))

    for note in notes:
        print(note)

    scanned = sum(
        1 for f in files if os.path.splitext(f)[1].lower() in SOURCE_EXTENSIONS
    )
    if args.fix:
        print("applied %d fix(es)" % applied)
    print(
        "summary: %d mismatch(es), %d collision(s) across %d tracked file(s) "
        "(%d C/C++ source(s) scanned)"
        % (len(mismatches), len(collisions), len(files), scanned)
    )

    if mismatches or collisions:
        print("FAIL: case-sensitivity problems found")
        return 1
    print("OK: no case-sensitivity problems found")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
