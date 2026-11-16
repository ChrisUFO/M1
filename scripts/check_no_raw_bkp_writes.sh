#!/bin/sh
# check_no_raw_bkp_writes.sh -- structural guard for issue #41-A.
#
# Backup-register state (magic number, update status, boot/swap counters) must
# be reached only through startup_config_write()/startup_config_read() in
# m1_csrc/m1_system.c. Two bypasses are checked, because they are equivalent:
#
#   1. a raw TAMP->BKP dereference, and
#   2. the HAL API aimed straight at the block's words:
#      HAL_RTCEx_BKUPWrite/Read(&hrtc, RTC_BKP_DR0|RTC_BKP_DR1, ...).
#
# Either one skips the accessor's byte -> DR0/DR1 word mapping: it can write
# the wrong register, clobber a neighbouring byte field inside the 8-byte block,
# or reach DR2/DR3, which the ESP32 version record owns (m1_esp_version.c). That
# is exactly the defect the fork's DFU port carried into this base: raw
# TAMP->BKP1R writes colliding with bu_regs.device_op_status on DR1 (spurious
# UPDATE FAILED / sticky DFU).
#
# Allowlist: the STM32 HAL itself (Drivers/STM32H5xx_HAL_Driver), which
# implements HAL_RTCEx_BKUPWrite/Read on TAMP->BKP0R -- nothing in the
# application may appear there -- and m1_csrc/m1_system.c, which houses the two
# accessors (both patterns are legal there and nowhere else). DR2/DR3 uses are
# never matched by the HAL pattern: they belong to the ESP32 version record.
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
RAW_PATTERN='TAMP[[:space:]]*->[[:space:]]*BKP'

# HAL_RTCEx_BKUPWrite/Read whose argument list names DR0/DR1. The trailing
# ([^0-9]|$) keeps RTC_BKP_DR10..15 and DR20..31 from matching DR1/DR2.
HAL_PATTERN='HAL_RTCEx_BKUP(Write|Read)[^;]*(RTC_BKP_DR0|RTC_BKP_DR1)([^0-9]|$)'

VIOLATIONS=$(grep -rnE --include='*.c' --include='*.h' "$RAW_PATTERN" "$ROOT" 2>/dev/null \
    | grep -v '/Drivers/STM32H5xx_HAL_Driver/')

VIOLATIONS_HAL=$(grep -rnE --include='*.c' --include='*.h' "$HAL_PATTERN" "$ROOT" 2>/dev/null \
    | grep -v '/Drivers/STM32H5xx_HAL_Driver/' \
    | grep -v '/m1_csrc/m1_system\.c:')

if [ -n "$VIOLATIONS" ]; then
    echo "FAIL: raw backup-register access outside the bu_regs accessors:" >&2
    printf '%s\n' "$VIOLATIONS" >&2
    echo "      Use startup_config_write()/startup_config_read() with the" >&2
    echo "      BK_REGS_SELECT_* selectors (m1_csrc/m1_system.c)." >&2
    exit 1
fi

if [ -n "$VIOLATIONS_HAL" ]; then
    echo "FAIL: HAL backup-register API reaches DR0/DR1 outside the bu_regs accessors:" >&2
    printf '%s\n' "$VIOLATIONS_HAL" >&2
    echo "      Use startup_config_write()/startup_config_read() with the" >&2
    echo "      BK_REGS_SELECT_* selectors; DR2/DR3 belong to the ESP32 version" >&2
    echo "      record (m1_esp_version.c)." >&2
    exit 1
fi

echo "ok: backup-register access is confined to the bu_regs accessors"
exit 0
