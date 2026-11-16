#!/usr/bin/env python3
"""Emulated srec_cat STM32-CRC emitter (Windows verification adapter, issue #49).

Reproduces, in pure Python, the repo's srecord CRC emission step

    srec_cat <bin> -binary -crop 0x0 0x000FFC14 \
        -STM32_Little_Endian 0x000FFC14 -o <out> -binary

which cannot run on hosts without the `srecord` suite (the documented
Windows emitter issue, documentation/merge1-baseline-evidence.md section 3).

Algorithm (STM32 hardware CRC unit == CRC-32/MPEG-2):
  * polynomial 0x04C11DB7, initial value 0xFFFFFFFF,
  * NO input/output reflection, NO final XOR,
  * fed 32-bit little-endian words (the STM32 CRC peripheral consumes
    whole words; trailing bytes, if any, are zero-padded),
  * the 32-bit result is appended little-endian at the crop end.

Reference vectors (baseline evidence section 3, cross-checked against the
device rollback gate layout):
    b"\\x00\\x00\\x00\\x00" -> 0xC704DD7B
    b"\\x04\\x03\\x02\\x01" -> 0x793737CD

Usage:
    python tools/emulate_srec_cat_crc.py <in.bin> <out_wCRC.bin> [--crop-end 0xFFC14]
    python tools/emulate_srec_cat_crc.py --self-test
"""

import argparse
import sys

POLY = 0x04C11DB7
INIT = 0xFFFFFFFF


def stm32_crc(data: bytes) -> int:
    """CRC-32/MPEG-2 over little-endian 32-bit words, zero-padding a tail."""
    crc = INIT
    for off in range(0, len(data), 4):
        word = int.from_bytes(data[off : off + 4].ljust(4, b"\x00"), "little")
        crc ^= word
        for _ in range(32):
            crc = (
                ((crc << 1) ^ POLY) & 0xFFFFFFFF
                if crc & 0x80000000
                else (crc << 1) & 0xFFFFFFFF
            )
    return crc


SELF_TEST_VECTORS = [
    (bytes.fromhex("00000000"), 0xC704DD7B),
    (bytes.fromhex("04030201"), 0x793737CD),
]


def self_test() -> int:
    ok = True
    for data, want in SELF_TEST_VECTORS:
        got = stm32_crc(data)
        status = "PASS" if got == want else "FAIL"
        ok = ok and got == want
        print(
            f"{status}: stm32_crc({data.hex()}) = 0x{got:08X} (expected 0x{want:08X})"
        )
    return 0 if ok else 1


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("infile", nargs="?", help="input .bin image")
    ap.add_argument("outfile", nargs="?", help="output _wCRC.bin image")
    ap.add_argument(
        "--crop-end",
        type=lambda x: int(x, 0),
        default=0x000FFC14,
        help="crop end == append offset (default 0xFFC14; the linker places "
        ".FW_CONFIG_SECTION at 0x080FFC00 size 0x14, so every correct image "
        "ends exactly here)",
    )
    ap.add_argument(
        "--self-test", action="store_true", help="run reference vectors and exit"
    )
    args = ap.parse_args(argv)

    if args.self_test:
        return self_test()

    if not args.infile or not args.outfile:
        ap.error("infile and outfile are required (or --self-test)")

    with open(args.infile, "rb") as f:
        image = f.read()
    if len(image) < args.crop_end:
        print(
            f"error: image is {len(image)} bytes, shorter than crop end 0x{args.crop_end:X}",
            file=sys.stderr,
        )
        return 2
    crop = image[: args.crop_end]
    crc = stm32_crc(crop)
    with open(args.outfile, "wb") as f:
        f.write(crop)
        f.write(crc.to_bytes(4, "little"))
    print(
        f"in={args.infile} size={len(image)} (0x{len(image):X}) "
        f"crop=[0x0,0x{args.crop_end:X}) CRC=0x{crc:08X} "
        f"out={args.outfile} size={args.crop_end + 4} (0x{args.crop_end + 4:X})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
