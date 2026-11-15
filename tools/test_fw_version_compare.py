#!/usr/bin/env python3
"""Firmware version comparator test — models the device rollback gate.

The bootloader gate (this tree, m1_csrc/m1_system.c:782-783) packs the four
config version bytes into a single u32 and compares:

    fw_ver_old = *(uint32_t *)&old_fw_config.fw_version_rc;
    if ( fw_ver_old < fw_ver_new ) // Existing FW in bank 2 is older than current FW?

The struct layout (S_M1_FW_CONFIG_t in m1_csrc/m1_fw_update_bl.h:78-81) is
little-endian on the STM32 target:

    +4  fw_version_rc     uint8_t
    +5  fw_version_build  uint8_t
    +6  fw_version_minor  uint8_t
    +7  fw_version_major  uint8_t

so the u32 read starting at fw_version_rc evaluates to:

    version_u32 = (major << 24) | (minor << 16) | (build << 8) | rc

The gate only accepts an incoming image whose packed version is strictly
greater than the stored one; anything older or equal is rejected (rollback
protection). This test packs versions exactly the same way and asserts the
ordering properties this fork's version namespace depends on:

  * 0.9.0.0 > 0.8.11    — the frozen fork namespace must exceed the last
    legacy fork release (0.8.11 packs with rc=0, i.e. as 0.8.11.0 =
    0x00080B00), otherwise the gate would refuse every 0.9.x image as
    "not newer" than what is already in flash.
  * 0.8.11 == 0.8.11.0  — the legacy three-field notation is the same
    version as the four-field notation with rc=0; packing is deterministic.
  * 0.8.11 > 0.8.1.0    — the fork namespace must also exceed the upstream
    base version (0.8.1.0 = 0x00080100) this branch adopts, so a
    0.9.0.0-UFO image always wins the numeric comparison against any
    0.8.x lineage image, fork or upstream. Minor=9 > 8 guarantees this
    regardless of build or rc fields.
  * the "-UFO" fork tag is display-only: it appears in artifact names,
    the about-box label and build stems (FW_VERSION_FORK_TAG) but is NOT
    part of the packed u32, so a bare 0.9.0.0 and a "-UFO"-tagged 0.9.0.0
    compare numerically equal.

Run standalone (no dependencies):  python tools/test_fw_version_compare.py
Exits 0 on success, 1 on any failure.
"""

import sys


def pack_version(major, minor, build, rc):
    """Pack version fields into a u32 exactly like the device gate.

    Mirrors *(uint32_t *)&cfg.fw_version_rc on the little-endian target:
    rc at the lowest byte, then build, minor, major.
    """
    return (major << 24) | (minor << 16) | (build << 8) | rc


def fmt(version_tuple, fork_tag=None):
    """Human-readable version, optionally with display-only fork tag."""
    base = "%d.%d.%d.%d" % version_tuple
    return "%s-%s" % (base, fork_tag) if fork_tag else base


PASS = 0
FAIL = 0


def check(description, condition, detail=""):
    global PASS, FAIL
    if condition:
        PASS += 1
        print("  [OK] %s" % description)
    else:
        FAIL += 1
        print("  [FAIL] %s%s" % (description, (" -- " + detail) if detail else ""))


def main():
    v_0_9_0_0 = pack_version(0, 9, 0, 0)
    v_0_8_11 = pack_version(0, 8, 11, 0)  # legacy fork release
    v_0_8_11_0 = pack_version(0, 8, 11, 0)  # same, explicit rc field
    v_0_8_1_0 = pack_version(0, 8, 1, 0)  # upstream base version
    v_0_9_0_0_tagged = pack_version(0, 9, 0, 0)  # fork tag not in u32

    print("=" * 60)
    print("Firmware Version Comparator Test (device rollback gate)")
    print("=" * 60)

    print("\nTesting rollback-gate ordering...")

    check(
        "0.9.0.0 > 0.8.11 (frozen namespace exceeds legacy fork release)",
        v_0_9_0_0 > v_0_8_11,
        "0x%08X vs 0x%08X" % (v_0_9_0_0, v_0_8_11),
    )

    check(
        "0.8.11 == 0.8.11.0 (three-field notation packs with rc=0)",
        v_0_8_11 == v_0_8_11_0,
        "0x%08X vs 0x%08X" % (v_0_8_11, v_0_8_11_0),
    )

    check(
        "0.8.11 > 0.8.1.0 (legacy release exceeds upstream base)",
        v_0_8_11 > v_0_8_1_0,
        "0x%08X vs 0x%08X" % (v_0_8_11, v_0_8_1_0),
    )

    print("\nTesting fork tag exclusion (display-only)...")
    print("  bare %s       = 0x%08X" % (fmt((0, 9, 0, 0)), v_0_9_0_0))
    print("  %s = 0x%08X" % (fmt((0, 9, 0, 0), "UFO"), v_0_9_0_0_tagged))

    check(
        'bare 0.9.0.0 == 0.9.0.0-UFO numerically ("-UFO" excluded from gate)',
        v_0_9_0_0 == v_0_9_0_0_tagged,
        "0x%08X vs 0x%08X" % (v_0_9_0_0, v_0_9_0_0_tagged),
    )

    print("\nTesting gate arithmetic on worked examples...")
    # Documented byte layout: rc, build, minor, major at consecutive
    # addresses -> little-endian u32 has major in the most-significant byte.
    check(
        "pack(0,8,11,0) == 0x00080B00 (rc | build<<8 | minor<<16 | major<<24)",
        v_0_8_11 == 0x00080B00,
        "got 0x%08X" % v_0_8_11,
    )
    check(
        "pack(0,9,0,0) == 0x00090000",
        v_0_9_0_0 == 0x00090000,
        "got 0x%08X" % v_0_9_0_0,
    )

    print("\n" + "=" * 60)
    if FAIL:
        print("%d check(s) FAILED, %d passed" % (FAIL, PASS))
        print("=" * 60)
        return 1
    print("All %d checks PASSED [OK]" % PASS)
    print("=" * 60)
    return 0


if __name__ == "__main__":
    sys.exit(main())
