#!/bin/sh
# Host-test aggregator: one entry point for the host-runnable tests wired
# into it (python+gcc extraction harnesses under m1_csrc/test/).
#
# Current wired set: the settings-store suite, the settings-screens suite
# (PR-2), the backlight-startup/saver suite (PR-2), the feedback manager
# suite incl. the LED-notification gate (PR-2), the RTC date/time suite
# (PR-3), the streaming .ir loader suite (PR-4), the IRMP golden-decode
# suite incl. the transmit-path vectors (PR-4), the IR browser suite
# (PR-5), the IR favorites/recent + cycle-all suite (PR-6), the
# Sub-GHz Radio Settings suite incl. WDS provenance binding (PR-7) and
# the crypto + HW RNG suite incl. NIST/RFC/CAVP vectors and the RNG
# failure seam (PR-8). The remaining pre-existing host tests under
# m1_csrc/test/ and m1_csrc/tests/ are NOT run by this script yet;
# later units append their own host tests to the list below by
# design, so a green run means exactly the listed tests passed --
# nothing more.
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

CC_HOST=${CC_HOST:-}
if [ -z "$CC_HOST" ]; then
    for c in cc gcc clang; do
        if command -v "$c" >/dev/null 2>&1; then CC_HOST=$c; break; fi
    done
fi
[ -n "$CC_HOST" ] || {
    echo "run_host_tests: no host C compiler (cc/gcc/clang) found" >&2
    exit 1
}

# Host tests, in dependency-sensible order. Later units append here.
PYTHON_TESTS="
m1_csrc/test/m1_settings_store_test.py
m1_csrc/test/m1_settings_screens_test.py
m1_csrc/test/m1_rtc_test.py
m1_csrc/test/m1_ir_file_test.py
m1_csrc/test/m1_ir_golden_decode_test.py
m1_csrc/test/m1_ir_browser_test.py
m1_csrc/test/m1_ir_favorites_test.py
m1_csrc/test/m1_subghz_radio_settings_test.py
m1_csrc/test/m1_crypto_test.py
m1_csrc/tests/test_backlight_startup.py
"

echo "=== Python/gcc host tests ==="
for t in $PYTHON_TESTS; do
    echo "--- $t"
    "$PYTHON" "$t"
done

# Feedback manager host suite: compiles the real manager/sequences/
# orchestration sources with the fake-adapter harness (no FreeRTOS, no
# hardware). Sanitizers when the toolchain has the runtime, plain build
# otherwise (matches the per-test harness behavior above).
echo "--- m1_csrc/tests/test_feedback_manager.c"
OUT=$(mktemp)
trap 'rm -f "$OUT"' EXIT
if ! "$CC_HOST" -std=c99 -Wall -Wextra -Werror -DM1_FEEDBACK_HOST_TEST \
        -I m1_csrc \
        m1_csrc/m1_feedback_manager.c m1_csrc/m1_feedback_sequences.c \
        m1_csrc/m1_feedback_orchestration.c \
        m1_csrc/tests/test_feedback_manager.c -o "$OUT" 2>/dev/null; then
    "$CC_HOST" -std=c99 -Wall -Wextra -Werror -DM1_FEEDBACK_HOST_TEST \
        -I m1_csrc \
        m1_csrc/m1_feedback_manager.c m1_csrc/m1_feedback_sequences.c \
        m1_csrc/m1_feedback_orchestration.c \
        m1_csrc/tests/test_feedback_manager.c -o "$OUT"
fi
"$OUT"

echo "=== All wired host tests passed (see list above for coverage) ==="
