#!/bin/sh
# check_no_raw_bkp_writes.sh -- structural guard for issue #41-A.
#
# Backup-register state (magic number, update status, boot/swap counters) must
# be reached only through startup_config_write()/startup_config_read() in
# m1_csrc/m1_system.c. A raw TAMP->BKP access bypasses that accessor's byte
# -> DR0/DR1 word mapping: it can write the wrong register, clobber a
# neighbouring byte field inside the 8-byte block, or reach DR2/DR3, which the
# ESP32 version record owns (m1_esp_version.c). That is exactly the defect the
# fork's DFU port carried into this base: raw TAMP->BKP1R writes colliding with
# bu_regs.device_op_status on DR1 (spurious UPDATE FAILED / sticky DFU).
#
# Allowlist: the STM32 HAL itself (Drivers/STM32H5xx_HAL_Driver), which
# implements HAL_RTCEx_BKUPWrite/Read on TAMP->BKP0R. Nothing in the
# application may appear there.
#
# Usage: scripts/check_no_raw_bkp_writes.sh [--root DIR]
# Exit:  0 = clean, 1 = violation or unusable scan root.
set -u

ROOT=$(dirname "$0")/..
if [ "${1:-}" = "--root" ]; then
    ROOT=${2:-}
    if [ -z "$ROOT" ]; then
        echo "FAIL: --root needs a directory argument" >&2
        exit 1
    fi
fi
if [ ! -d "$ROOT" ]; then
    echo "FAIL: scan root '$ROOT' is not a directory" >&2
    exit 1
fi

# TAMP->BKP, tolerating whitespace around the arrow.
PATTERN='TAMP[[:space:]]*->[[:space:]]*BKP'

VIOLATIONS=$(grep -rnE --include='*.c' --include='*.h' "$PATTERN" "$ROOT" 2>/dev/null \
    | grep -v '/Drivers/STM32H5xx_HAL_Driver/')

if [ -n "$VIOLATIONS" ]; then
    echo "FAIL: raw backup-register access outside the bu_regs accessors:" >&2
    printf '%s\n' "$VIOLATIONS" >&2
    echo "      Use startup_config_write()/startup_config_read() with the" >&2
    echo "      BK_REGS_SELECT_* selectors (m1_csrc/m1_system.c)." >&2
    exit 1
fi

echo "ok: no raw TAMP->BKP access in application sources"
exit 0
