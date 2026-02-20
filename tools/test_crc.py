#!/usr/bin/env python3
"""Validate CRC behavior against a firmware binary."""

import argparse
import glob
import os
import struct
import sys

CRC_POLYNOMIAL = 0x04C11DB7


def _init_crc_table():
    table = []
    for byte in range(256):
        crc = byte << 24
        for _ in range(8):
            if crc & 0x80000000:
                crc = (crc << 1) ^ CRC_POLYNOMIAL
            else:
                crc <<= 1
            crc &= 0xFFFFFFFF
        table.append(crc)
    return table


_CRC_TABLE = _init_crc_table()


def stm32_hal_crc32_words(data):
    crc = 0xFFFFFFFF
    padded = data + b"\x00" * (4 - len(data) % 4) if len(data) % 4 else data
    for i in range(0, len(padded), 4):
        word = struct.unpack("<I", padded[i : i + 4])[0]
        for byte_shift in [24, 16, 8, 0]:
            byte = (word >> byte_shift) & 0xFF
            crc = ((crc << 8) & 0xFFFFFFFF) ^ _CRC_TABLE[((crc >> 24) ^ byte) & 0xFF]
    return crc


def stm32_hal_crc32_bytes(data):
    crc = 0xFFFFFFFF
    for byte in data:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ _CRC_TABLE[((crc >> 24) ^ byte) & 0xFF]
    return crc


def _find_default_bin(repo_root):
    patterns = [
        os.path.join(repo_root, "distribution", "M1_v*-ChrisUFO.bin"),
        os.path.join(repo_root, "out", "build", "*", "M1_v*-ChrisUFO.bin"),
    ]

    candidates = []
    for pattern in patterns:
        candidates.extend(glob.glob(pattern))

    if not candidates:
        return None

    candidates.sort(key=os.path.getmtime, reverse=True)
    return candidates[0]


def main():
    parser = argparse.ArgumentParser(
        description="Validate firmware CRC placement and value"
    )
    parser.add_argument("--bin", dest="bin_path", help="Path to firmware .bin file")
    args = parser.parse_args()

    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    bin_path = args.bin_path or _find_default_bin(repo_root)
    if not bin_path:
        print(
            "No firmware binary found. Build first or pass --bin path.", file=sys.stderr
        )
        return 1

    with open(bin_path, "rb") as f:
        data = f.read()

    if len(data) < 4:
        print(f"File too small for CRC trailer: {bin_path}", file=sys.stderr)
        return 1

    crc_offset = len(data) - 4
    fw_data = data[:crc_offset]
    crc_words = stm32_hal_crc32_words(fw_data)
    crc_bytes = stm32_hal_crc32_bytes(fw_data)
    crc_in_file = struct.unpack("<I", data[crc_offset:])[0]

    print(f"Binary: {bin_path}")
    print(f"Size:   {len(data)} bytes")
    print(f"CRC offset: 0x{crc_offset:08X}")
    print(f"CRC (word-based STM32): 0x{crc_words:08X}")
    print(f"CRC (byte-based old):   0x{crc_bytes:08X}")
    print(f"CRC in file:            0x{crc_in_file:08X}")

    if crc_in_file != crc_words:
        print(
            "[FAIL] File CRC does not match STM32 word-based calculation",
            file=sys.stderr,
        )
        return 1

    print("[OK] CRC matches STM32 word-based calculation")
    return 0


if __name__ == "__main__":
    sys.exit(main())
