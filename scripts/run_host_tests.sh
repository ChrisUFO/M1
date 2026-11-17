#!/bin/sh
# Host-test aggregator: one entry point for the host-runnable tests wired
# into it (python+gcc extraction harnesses under m1_csrc/test/).
#
# Current wired set: the settings-store suite. The pre-existing host tests
# under m1_csrc/test/ and m1_csrc/tests/ are NOT run by this script yet;
# later units append their tests to the list below by design, so a green
# run means exactly the listed tests passed -- nothing more.
#
# CI runs this script (see .github/workflows/host-tests.yml). Append new
# tests to the list below as they land -- keep one line per test so a
# failure names the exact entry point.
#
# Requirements: a POSIX sh, python3, and a C compiler (cc/gcc/clang).
# From the repository root: sh scripts/run_host_tests.sh
set -eu

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
cd "$ROOT"

PYTHON=${PYTHON:-python3}
command -v "$PYTHON" >/dev/null 2>&1 || PYTHON=python
command -v "$PYTHON" >/dev/null 2>&1 || {
    echo "run_host_tests: no python3/python interpreter found" >&2
    exit 1
}

# Host tests, in dependency-sensible order. Later units append here.
PYTHON_TESTS="
m1_csrc/test/m1_settings_store_test.py
"

echo "=== Python/gcc host tests ==="
for t in $PYTHON_TESTS; do
    echo "--- $t"
    "$PYTHON" "$t"
done

echo "=== All wired host tests passed (see list above for coverage) ==="
