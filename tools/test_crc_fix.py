#!/usr/bin/env python3
"""Regression tests for append_crc32.py behavior."""

import os
import struct
import subprocess
import sys
import tempfile

from append_crc32 import stm32_hal_crc32


def create_test_firmware(size=0x100000):
    data = bytearray(size)
    for i in range(len(data)):
        data[i] = (i * 7 + 13) % 256
    return data


def test_crc_calculation():
    print("Testing CRC calculation...")
    test_data = bytes([0x01, 0x02, 0x03, 0x04] * 4)
    crc = stm32_hal_crc32(test_data)
    crc2 = stm32_hal_crc32(test_data)
    assert crc == crc2, "CRC calculation is not deterministic"
    print(f"  Deterministic CRC: 0x{crc:08X} [OK]")
    return True


def test_word_vs_byte_processing():
    print("\nTesting word vs byte processing difference...")
    test_data = bytes([0x01, 0x02, 0x03, 0x04] * 100)
    word_crc = stm32_hal_crc32(test_data)

    crc_polynomial = 0x04C11DB7
    crc_table = []
    for byte in range(256):
        crc = byte << 24
        for _ in range(8):
            if crc & 0x80000000:
                crc = (crc << 1) ^ crc_polynomial
            else:
                crc <<= 1
            crc &= 0xFFFFFFFF
        crc_table.append(crc)

    crc = 0xFFFFFFFF
    for byte in test_data:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ crc_table[((crc >> 24) ^ byte) & 0xFF]
    byte_crc = crc

    print(f"  Word-based CRC: 0x{word_crc:08X}")
    print(f"  Byte-based CRC: 0x{byte_crc:08X}")
    assert word_crc != byte_crc, "Expected word and byte CRC methods to differ"
    print("  Difference confirmed [OK]")
    return True


def test_append_crc_end_to_end():
    print("\nTesting append_crc32.py end-to-end...")
    with tempfile.NamedTemporaryFile(mode="wb", delete=False, suffix=".bin") as f:
        temp_file = f.name
        original_data = create_test_firmware(0x100000)
        f.write(original_data)

    try:
        original_size = len(original_data)

        script_dir = os.path.dirname(os.path.abspath(__file__))
        append_crc32_path = os.path.join(script_dir, "append_crc32.py")

        result = subprocess.run(
            [sys.executable, append_crc32_path, temp_file],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            print(result.stderr)
            return False

        with open(temp_file, "rb") as f:
            final_data = f.read()

        if len(final_data) != original_size + 4:
            print(
                f"[FAIL] Unexpected final file size {len(final_data)}; expected {original_size + 4}"
            )
            return False

        stored_crc = struct.unpack("<I", final_data[-4:])[0]
        expected_crc = stm32_hal_crc32(final_data[:-4])
        print(f"  Expected CRC: 0x{expected_crc:08X}")
        print(f"  Stored CRC:   0x{stored_crc:08X}")
        if stored_crc != expected_crc:
            print("[FAIL] Stored CRC mismatch")
            return False

        print("  append_crc32.py validation [OK]")
        return True
    finally:
        os.unlink(temp_file)


def main():
    print("=" * 60)
    print("CRC Fix Verification Test")
    print("=" * 60)

    all_passed = True
    all_passed &= test_crc_calculation()
    all_passed &= test_word_vs_byte_processing()
    all_passed &= test_append_crc_end_to_end()

    print("\n" + "=" * 60)
    if all_passed:
        print("All tests PASSED [OK]")
    else:
        print("Some tests FAILED [X]")
    print("=" * 60)
    return 0 if all_passed else 1


if __name__ == "__main__":
    sys.exit(main())
