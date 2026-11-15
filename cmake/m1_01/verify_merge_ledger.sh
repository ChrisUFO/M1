#!/bin/sh
# verify_merge_ledger.sh -- structural gate for docs/merge/LEDGER.md (issue #38).
#
# Enforces the two hard invariants of the merge-1 audit:
#   1. FILE SET: the ledger enumerates exactly the fork-changed files
#      (git diff --name-only <fork-point> <legacy-tag>) -- no missing, no extra.
#   2. DISPOSITIONS: every file section carries exactly one valid disposition
#      (retained-by-upstream | carried-by-patch | upstream-equivalent |
#       intentionally-dropped); any 'unknown' or missing value fails.
#
# Companion of tools/gen_merge_ledger.py (which asserts the 113-file hard
# invariant at generation time). Runnable locally and in CI later; no build
# artifacts required -- only git and the repo tree.
#
# Usage: verify_merge_ledger.sh [repo-root]
set -u

ROOT="${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}"
LEDGER="$ROOT/docs/merge/LEDGER.md"

# Basis -- keep in lockstep with tools/gen_merge_ledger.py and the ledger header.
FORK_POINT="93acd751de4acc2a77589060cd0e762dbbec2bc5" # upstream commit the fork diverged from
LEGACY="legacy/v0.8.11"                                # immutable fork archive tag

fail=0

[ -f "$LEDGER" ] || { echo "verify_merge_ledger: ledger not found at $LEDGER" >&2; exit 2; }
git -C "$ROOT" rev-parse --verify --quiet "$FORK_POINT^{commit}" >/dev/null || {
    echo "verify_merge_ledger: fork-point $FORK_POINT not resolvable" >&2; exit 2; }
git -C "$ROOT" rev-parse --verify --quiet "$LEGACY^{commit}" >/dev/null || {
    echo "verify_merge_ledger: tag $LEGACY not resolvable" >&2; exit 2; }

# --- 1. file-set equality (ledger vs git) ---------------------------------
git -C "$ROOT" diff --name-only "$FORK_POINT" "$LEGACY" | LC_ALL=C sort > "$ROOT/.ledger_gate_git.$$"
# ledger file sections look like: ### `path`  (backtick-quoted, no spaces in our set)
sed -n 's/^### `\(.*\)`$/\1/p' "$LEDGER" | LC_ALL=C sort > "$ROOT/.ledger_gate_md.$$"

if ! cmp -s "$ROOT/.ledger_gate_git.$$" "$ROOT/.ledger_gate_md.$$"; then
    echo "verify_merge_ledger: FAIL -- ledger file set differs from git enumeration:" >&2
    diff "$ROOT/.ledger_gate_git.$$" "$ROOT/.ledger_gate_md.$$" | \
        sed 's/^/  /' >&2
    fail=1
else
    n=$(wc -l < "$ROOT/.ledger_gate_git.$$")
    echo "verify_merge_ledger: file set OK ($n files)"
fi
rm -f "$ROOT/.ledger_gate_git.$$" "$ROOT/.ledger_gate_md.$$"

# --- 2. disposition per section -------------------------------------------
# For each '### `path`' section, the following '- Disposition: **X**' line must
# exist before the next section header and carry a valid value.
awk '
    /^### `/                         { sub(/^### `/, ""); sub(/`$/, "");
                                       if (path != "" && disp == "") { print "  missing disposition: " path; bad = 1 }
                                       path = $0; disp = ""; next }
    path != "" && /^- Disposition: / { if (disp != "") { print "  duplicate disposition: " path; bad = 1 }
                                       d = $0
                                       sub(/^- Disposition: \*\*/, "", d); sub(/\*\*.*$/, "", d)
                                       if (d == "unknown") { print "  unknown disposition: " path; bad = 1 }
                                       else if (d != "retained-by-upstream" && d != "carried-by-patch" &&
                                                d != "upstream-equivalent" && d != "intentionally-dropped") {
                                           print "  invalid disposition \"" d "\": " path; bad = 1 }
                                       disp = d; next }
    /^## /                           { if (path != "" && disp == "") { print "  missing disposition: " path; bad = 1 }
                                       path = ""; next }
    END                              { if (path != "" && disp == "") { print "  missing disposition: " path; bad = 1 }
                                       exit bad ? 1 : 0 }
' "$LEDGER" || fail=1

if [ "$fail" -eq 0 ]; then
    echo "verify_merge_ledger: PASS (zero unknown/missing dispositions)"
else
    echo "verify_merge_ledger: FAIL (see above)" >&2
fi
exit $fail
