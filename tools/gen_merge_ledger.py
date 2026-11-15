#!/usr/bin/env python3
# See COPYING.txt for license details.
"""Generate docs/merge/LEDGER.md — the merge-1 fork-file disposition ledger.

Enumerates every file that differs between the pre-merge fork point
(93acd751, the commit the fork diverged from upstream) and the archived fork
tip (tag legacy/v0.8.11), classifies each as both-changed vs ours-only against
upstream's evolution, and emits one ledger section per file together with
name-status, whitespace-ignored numstat, hunk summaries and fork origin SHAs.

The per-file DISPOSITIONS table below is the single source of truth for the
audit prose; re-running this script regenerates the ledger deterministically:

    python tools/gen_merge_ledger.py            # writes docs/merge/LEDGER.md
    python tools/gen_merge_ledger.py --check    # verify the checked-in ledger
                                               # matches regeneration exactly

The hard invariant: the enumeration must yield exactly EXPECTED_FILES entries
(83 both-changed + 30 ours-only). Anything else fails loud — do not proceed
with a wrong basis.

Gate: cmake/m1_01/verify_merge_ledger.sh independently re-derives the file set
from git and fails on any unknown/missing disposition.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

FORK_POINT = (
    "93acd751de4acc2a77589060cd0e762dbbec2bc5"  # upstream commit the fork diverged from
)
LEGACY = "legacy/v0.8.11"  # archived fork tip (immutable tag)
UPSTREAM = "upstream/main"  # upstream evolution since fork point
MAIN = "main"  # adopted new base (post PR-1/PR-2)

EXPECTED_FILES = 113
EXPECTED_BOTH_CHANGED = 83
EXPECTED_OURS_ONLY = 30

DISPOSITION_VALUES = (
    "retained-by-upstream",
    "carried-by-patch",
    "upstream-equivalent",
    "intentionally-dropped",
)


def git(*args: str) -> str:
    return subprocess.run(
        ["git", "-C", str(REPO), *args],
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    ).stdout


def enumerate_files():
    """Return (ordered file list, name-status map, class map, numstat map)."""
    fork_files = git("diff", "--name-only", FORK_POINT, LEGACY).splitlines()
    upstream_files = set(git("diff", "--name-only", FORK_POINT, UPSTREAM).splitlines())

    name_status = {}
    for line in git("diff", "--name-status", FORK_POINT, LEGACY).splitlines():
        status, path = line.split("\t", 1)
        name_status[path] = status

    classes = {}
    for f in fork_files:
        classes[f] = "ours-only" if f not in upstream_files else "both-changed"

    numstat = {}
    for line in git("diff", "-w", "--numstat", FORK_POINT, LEGACY).splitlines():
        add, del_, path = line.split("\t")
        numstat[path] = (add, del_)

    return fork_files, name_status, classes, numstat


def hunk_summaries(path: str, limit: int = 3):
    """First `limit` hunk headers of the -w fork delta, plus a total count."""
    diff = git("diff", "-w", FORK_POINT, LEGACY, "--", path)
    headers = re.findall(r"^@@ [^@]*@@.*$", diff, flags=re.MULTILINE)
    total = len(headers)
    shown = headers[:limit]
    more = total - len(shown)
    return shown, total, more


def origin_log(path: str, limit: int = 5):
    """Fork commits that touched the file (origin evidence for carries)."""
    log = git(
        "log", "--oneline", "--format=%h %s", f"{FORK_POINT}..{LEGACY}", "--", path
    )
    lines = log.splitlines()
    more = len(lines) - limit
    shown = lines[:limit]
    if more > 0:
        shown.append(f"(+{more} more)")
    return shown


# ---------------------------------------------------------------------------
# Per-file dispositions. THIS TABLE IS THE AUDIT — one disposition per file,
# evidence cited (file:line / SHA / command). Anything absent here renders as
# `unknown` and fails the gate.
#
# Entry fields:
#   disposition : one of DISPOSITION_VALUES
#   carry       : issue number(s) of the P4 carry patch (carried-by-patch only)
#   tags        : e.g. "recovered-warning-fix" (mined by PR-4 / issue #39)
#   evidence    : multi-line markdown, cite-heavy
# ---------------------------------------------------------------------------

DISPOSITIONS = {}


def entry_body(f, name_status, cls, numstat):
    d = DISPOSITIONS.get(f)
    disposition = d["disposition"] if d else "unknown"
    carry = d.get("carry") if d else None
    tags = d.get("tags", []) if d else []
    evidence = d.get("evidence", "_pending_") if d else "_pending_"

    add, del_ = numstat.get(f, ("-", "-"))
    status = name_status.get(f, "?")
    hunks, total, more = hunk_summaries(f)
    origins = origin_log(f)

    lines = [f"### `{f}`", ""]
    lines.append(f"- Status: {status} ({cls}; -w delta +{add}/-{del_}; {total} hunks)")
    lines.append(f"- Disposition: **{disposition}**")
    if carry:
        lines.append(f"- Carry patch: {carry}")
    if tags:
        lines.append(f"- Tags: {', '.join(tags)}")
    lines.append("- Fork origin: " + (", ".join(origins) if origins else "(none)"))
    if hunks:
        hlist = [f"`{h}`" for h in hunks]
        if more > 0:
            hlist.append(f"*+{more} more hunks*")
        lines.append("- Hunk summary (-w): " + "; ".join(hlist))
    lines.append("")
    lines.append(evidence)
    lines.append("")
    return "\n".join(lines)


SUMMARY_INTRO = """\
This ledger dispositions every fork-changed file of merge 1 (upstream 0.8.1.0
adoption, issues #36–#39). Basis:

- Fork point (merge base vs upstream): `93acd751` (`git merge-base` of the
  pre-graft fork history; upstream's "Initial Upload" tree the fork diverged
  from)
- Fork tip: tag `legacy/v0.8.11` → `d99e2f8` (immutable archive, PR #47)
- New base: `main` = upstream pin `2af1f8a` + foundation re-land
  (`git merge-base main upstream/main` == `2af1f8a`)
- Enumeration: `git diff --name-only 93acd751 legacy/v0.8.11` — hard invariant
  **113 files** (83 both-changed + 30 ours-only), asserted by
  `tools/gen_merge_ledger.py` and by the gate `cmake/m1_01/verify_merge_ledger.sh`.

Disposition vocabulary (exactly one per file):

| Disposition | Meaning |
|---|---|
| `retained-by-upstream` | The fork-relevant content is already present in the new base — written by upstream, or re-landed onto it by the PR-1/PR-2 foundation work. Evidence: `main` file:line. |
| `carried-by-patch` | Content the new base lacks and worth keeping; names the P4 carry issue (#39–#44/#46) and cites fork origin SHAs. |
| `upstream-equivalent` | The file differs, but upstream achieves the fork's intent differently/better on the new base; evidence cited on both sides. |
| `intentionally-dropped` | Not carried; written reason below against the maintainer's evidentiary bar. |

Hunk classification uses whitespace-ignored diffs throughout
(`git diff -w 93acd751 legacy/v0.8.11`) so EOL/restyle noise cannot hide real
deltas; per-file numstat and hunk headers above are `-w` values. The fork's
"Zero-Warning Core" restyle (811b346) embedded genuine warning fixes; every
genuine fix found is tagged `recovered-warning-fix` below — issue #39 (PR-4)
mines exactly this tag.
"""

# Phase 3/4/5 sections are appended after the per-file entries; see the
# FEATURE_VERDICTS / EVIDENCE_RECERTURE / VENDOR_DELTAS blocks added in the
# respective phase commits.


def count_by(files, key):
    counts = {}
    for f in files:
        d = DISPOSITIONS.get(f)
        v = d["disposition"] if d else "unknown"
        counts[v] = counts.get(v, 0) + 1
    return counts


def generate() -> str:
    files, name_status, classes, numstat = enumerate_files()

    n = len(files)
    both = sum(1 for f in files if classes[f] == "both-changed")
    ours = n - both
    if (n, both, ours) != (EXPECTED_FILES, EXPECTED_BOTH_CHANGED, EXPECTED_OURS_ONLY):
        sys.stderr.write(
            f"FATAL: enumeration invariant violated: files={n} (expect {EXPECTED_FILES}), "
            f"both-changed={both} (expect {EXPECTED_BOTH_CHANGED}), ours-only={ours} "
            f"(expect {EXPECTED_OURS_ONLY}). Basis changed — STOP and re-derive.\n"
        )
        raise SystemExit(1)

    counts = count_by(files, None)
    warnfix = sum(
        1
        for f in files
        if "recovered-warning-fix" in DISPOSITIONS.get(f, {}).get("tags", [])
    )

    out = []
    out.append("<!-- See COPYING.txt for license details. -->\n")
    out.append("# Merge ledger — fork-changed file dispositions (issue #38)\n")
    out.append(SUMMARY_INTRO)
    out.append("## Summary\n")
    out.append("| Metric | Count |")
    out.append("|---|---|")
    out.append(f"| Fork-changed files (hard invariant) | **{n}** |")
    out.append(f"| Both-changed (also touched upstream since fork point) | {both} |")
    out.append(f"| Ours-only (upstream untouched since fork point) | {ours} |")
    for v in DISPOSITION_VALUES:
        out.append(f"| {v} | {counts.get(v, 0)} |")
    out.append(f"| unknown (must be 0 — gate) | {counts.get('unknown', 0)} |")
    out.append(f"| Entries tagged `recovered-warning-fix` (feed for #39) | {warnfix} |")
    out.append("")
    out.append(
        "*Regenerate deterministically with `python tools/gen_merge_ledger.py`; "
        "the DISPOSITIONS table in that script is the single source of truth. "
        "Gate: `bash cmake/m1_01/verify_merge_ledger.sh`.*\n"
    )

    out.append("## Per-file dispositions\n")
    for f in files:
        out.append(entry_body(f, name_status, classes[f], numstat))

    extra = globals().get("EXTRA_SECTIONS", "")
    if extra:
        out.append(extra)

    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--output", default=str(REPO / "docs/merge/LEDGER.md"))
    ap.add_argument(
        "--check",
        action="store_true",
        help="verify the checked-in ledger matches regeneration (no write)",
    )
    args = ap.parse_args()

    content = generate()

    if args.check:
        current = Path(args.output).read_text(encoding="utf-8")
        if current != content:
            sys.stderr.write(
                "FATAL: docs/merge/LEDGER.md is stale vs gen_merge_ledger.py "
                "(rerun the generator and commit both).\n"
            )
            raise SystemExit(1)
        print("ledger check: OK (regeneration matches checked-in ledger)")
        return

    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    Path(args.output).write_text(content, encoding="utf-8", newline="\n")
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
