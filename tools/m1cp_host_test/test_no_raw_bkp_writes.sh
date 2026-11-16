#!/bin/sh
# Exercises scripts/check_no_raw_bkp_writes.sh (issue #41-A structural guard).
#
# The guard is only worth having if it actually fails: this checks the real tree
# passes, a seeded raw TAMP->BKP write in a scratch tree is detected, a clean
# scratch tree passes, and the HAL's own implementation stays allowlisted.
# Portable POSIX sh (MinGW Git Bash + POSIX sh).
set -u

# Resolve the guard from THIS script's location, never from the caller's cwd:
# `sh tools/m1cp_host_test/test_no_raw_bkp_writes.sh` run from the repo root must
# behave exactly like `make -C tools/m1cp_host_test test`. With a cwd-relative
# path the guard was not found from the repo root, every case read the non-zero
# exit as "violation detected" and three assertions failed -- including the
# alarming-looking "HAL implementation falsely flagged" (audit finding D2).
SELF_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
REPO_ROOT=$(CDPATH= cd -- "$SELF_DIR/../.." && pwd) || exit 1
GUARD="$REPO_ROOT/scripts/check_no_raw_bkp_writes.sh"

if [ ! -f "$GUARD" ]; then
    echo "FAIL: guard script not found at $GUARD" >&2
    exit 1
fi

fails=0

tmp=$(mktemp -d 2>/dev/null) || tmp="$SELF_DIR/bkp_guard_$$"
mkdir -p "$tmp" || exit 1
tmp=$(CDPATH= cd -- "$tmp" && pwd) || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM

say_ok()  { echo "  ok   $1"; }
say_bad() { echo "  FAIL $1"; fails=$((fails + 1)); }

# 1. The real tree must pass (no application source touches TAMP->BKP).
if sh "$GUARD"; then
    say_ok "real tree: no raw TAMP->BKP access"
else
    say_bad "real tree: guard reported a violation"
fi

# 2. A seeded violation must be detected.
mkdir -p "$tmp/m1_csrc"
printf 'void seeded(void) { TAMP->BKP1R = 0xDEADBEEF; }\n' > "$tmp/m1_csrc/seeded.c"
if sh "$GUARD" --root "$tmp" >/dev/null 2>&1; then
    say_bad "seeded raw backup-register write was NOT detected"
else
    say_ok "seeded raw backup-register write detected"
fi

# 3. A clean tree passes (no false positives).
rm -f "$tmp/m1_csrc/seeded.c"
printf 'void clean(void) { }\n' > "$tmp/m1_csrc/clean.c"
if sh "$GUARD" --root "$tmp" >/dev/null 2>&1; then
    say_ok "clean tree passes"
else
    say_bad "clean tree rejected (false positive)"
fi

# 4. The HAL implementation is allowlisted.
mkdir -p "$tmp/Drivers/STM32H5xx_HAL_Driver/Src"
printf 'void hal(void) { *(__IO uint32_t *)(&(TAMP->BKP0R)) = 1U; }\n' \
    > "$tmp/Drivers/STM32H5xx_HAL_Driver/Src/hal.c"
if sh "$GUARD" --root "$tmp" >/dev/null 2>&1; then
    say_ok "HAL implementation allowlisted"
else
    say_bad "HAL implementation falsely flagged"
fi

echo
if [ "$fails" -eq 0 ]; then
    echo "ALL PASS (bkp guard)"
    exit 0
fi
echo "FAILURES ($fails)"
exit 1
