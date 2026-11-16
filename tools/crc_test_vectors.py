#!/usr/bin/env python3
"""Deterministic CRC parity test-vector generator (issue #40).

The CRC emission parity harness (scripts/verify_crc_parity.sh) needs several
full-size application images (exactly 0xFFC14 bytes -- the crop end / CRC append
offset) to compare the two interchangeable emitters. Committing ~1 MiB binaries
for that is wasteful and invites bit-rot, so the images are *generated* from the
spec below: every run on every host produces byte-identical files.

Spec (all images are exactly 0xFFC14 bytes):

    zeros         0x00 in every byte
    ff            0xFF in every byte
    ramp          byte i = i mod 256
    xorshift32    xorshift32 stream (seed 0x12345678), serialized as 32-bit
                  little-endian words: x ^= x << 13; x ^= x >> 17; x ^= x << 5
                  (all shifts masked to 32 bits)

Usage:
    python tools/crc_test_vectors.py --out <dir>
        Write <dir>/<name>.bin for every spec and print "<name> <sha256>" per
        vector on stdout (the input digest golden records are keyed on).

    python tools/crc_test_vectors.py --list
        Print the spec table and exit.

The inputs are algorithm-agnostic; expected *output* digests and CRC words live
in scripts/crc_parity_golden.txt and were produced by the authoritative
srec_cat pipeline (see that file's header).
"""

import argparse
import hashlib
import os
import sys

CROP_END = 0x000FFC14  # 1047572 == application image size / CRC append offset


def _zeros() -> bytes:
    return bytes(CROP_END)


def _ff() -> bytes:
    return b"\xff" * CROP_END


def _ramp() -> bytes:
    return bytes(i & 0xFF for i in range(CROP_END))


def _xorshift32() -> bytes:
    x = 0x12345678
    out = bytearray(CROP_END)
    for off in range(0, CROP_END, 4):
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        out[off : off + 4] = x.to_bytes(4, "little")
    return bytes(out)


# Ordered name -> (builder, human description). Keep names stable: the golden
# data file is keyed on them.
SPECS = [
    ("zeros", _zeros, "0x00 fill"),
    ("ff", _ff, "0xFF fill"),
    ("ramp", _ramp, "byte i = i mod 256"),
    ("xorshift32", _xorshift32, "xorshift32 seed 0x12345678, 32-bit LE words"),
]


def build(name: str) -> bytes:
    for spec_name, builder, _desc in SPECS:
        if spec_name == name:
            image = builder()
            assert len(image) == CROP_END, (name, len(image))
            return image
    raise KeyError(name)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", help="directory to write <name>.bin files into")
    ap.add_argument("--list", action="store_true", help="print the spec and exit")
    args = ap.parse_args(argv)

    if args.list:
        for name, _builder, desc in SPECS:
            print(f"{name}\t{CROP_END} bytes\t{desc}")
        return 0

    if not args.out:
        ap.error("--out <dir> is required (or --list)")

    os.makedirs(args.out, exist_ok=True)
    for name, _builder, _desc in SPECS:
        image = build(name)
        with open(os.path.join(args.out, name + ".bin"), "wb") as f:
            f.write(image)
        print(f"{name} {hashlib.sha256(image).hexdigest()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
