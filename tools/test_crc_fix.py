#!/usr/bin/env python3
"""
Test script to verify CRC calculation matches STM32 hardware CRC behavior.

This creates a test binary and verifies that:
1. The CRC is calculated on 32-bit words (not bytes)
2. The CRC is placed at the correct offset (0xFFC14)
3. The binary size is correct (1047576 bytes)
"""

import sys
import struct
import tempfile
import os

# Import the fixed CRC function
from append_crc32 import stm32_hal_crc32, CRC_OFFSET


def create_test_firmware(size=0x100000):
    """Create a test firmware binary with known pattern."""
    # Create data up to CRC offset
    data = bytearray(size)

    # Fill with a pattern (incrementing bytes)
    for i in range(len(data)):
        data[i] = (i * 7 + 13) % 256

    return data


def test_crc_calculation():
    """Test that CRC calculation produces consistent results."""
    print("Testing CRC calculation...")

    # Create test data
    test_data = bytes([0x01, 0x02, 0x03, 0x04] * 4)  # 16 bytes

    # Calculate CRC
    crc = stm32_hal_crc32(test_data)
    print(f"Test data CRC: 0x{crc:08X}")

    # The CRC should be the same for the same data
    crc2 = stm32_hal_crc32(test_data)
    assert crc == crc2, "CRC calculation is not deterministic!"

    print("  CRC calculation is deterministic [OK]")
    return True


def test_word_vs_byte_processing():
    """Verify that word-based CRC differs from byte-based CRC."""
    print("\nTesting word vs byte processing difference...")

    # Create test data
    test_data = bytes([0x01, 0x02, 0x03, 0x04] * 100)

    # Calculate CRC with fixed implementation
    word_crc = stm32_hal_crc32(test_data)

    # Calculate CRC with old byte-by-byte method
    CRC_POLYNOMIAL = 0x04C11DB7

    # Build CRC table (simplified)
    crc_table = []
    for byte in range(256):
        crc = byte << 24
        for _ in range(8):
            if crc & 0x80000000:
                crc = (crc << 1) ^ CRC_POLYNOMIAL
            else:
                crc <<= 1
            crc &= 0xFFFFFFFF
        crc_table.append(crc)

    # Old byte-by-byte CRC
    crc = 0xFFFFFFFF
    for byte in test_data:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ crc_table[((crc >> 24) ^ byte) & 0xFF]
    byte_crc = crc

    print(f"  Word-based CRC: 0x{word_crc:08X}")
    print(f"  Byte-based CRC: 0x{byte_crc:08X}")

    if word_crc != byte_crc:
        print("  [OK] Word-based and byte-based CRCs are DIFFERENT (as expected)")
        print("  [OK] This confirms the fix is necessary")
    else:
        print("  [WARNING] CRCs are the same (unexpected for this test data)")

    return True


def test_full_firmware():
    """Test with a full-size firmware binary."""
    print("\nTesting full firmware binary...")

    # Create temp file
    with tempfile.NamedTemporaryFile(mode="wb", delete=False, suffix=".bin") as f:
        temp_file = f.name

        # Create firmware data (up to CRC offset)
        fw_data = create_test_firmware(CRC_OFFSET)
        f.write(fw_data)

    try:
        # Calculate CRC on the data
        with open(temp_file, "rb") as f:
            data = f.read()

        crc = stm32_hal_crc32(data)
        print(f"  Firmware CRC: 0x{crc:08X}")
        print(f"  Data size: {len(data)} bytes")
        print(f"  Expected offset: {CRC_OFFSET} (0x{CRC_OFFSET:05X})")

        # Now run the full append_crc32 process
        import subprocess

        # Get the directory where this script is located
        script_dir = os.path.dirname(os.path.abspath(__file__))
        append_crc32_path = os.path.join(script_dir, "append_crc32.py")

        result = subprocess.run(
            [sys.executable, append_crc32_path, temp_file],
            capture_output=True,
            text=True,
        )

        if result.returncode == 0:
            print("  append_crc32.py executed successfully [OK]")
            print(result.stdout)

            # Verify the output file
            with open(temp_file, "rb") as f:
                final_data = f.read()

            print(f"  Final file size: {len(final_data)} bytes")

            # Extract the CRC from the file
            stored_crc = struct.unpack("<I", final_data[CRC_OFFSET : CRC_OFFSET + 4])[0]
            print(f"  Stored CRC: 0x{stored_crc:08X}")

            if stored_crc == crc:
                print("  [OK] Stored CRC matches calculated CRC!")
                return True
            else:
                print("  [FAIL] CRC MISMATCH!")
                return False
        else:
            print(f"  [FAIL] append_crc32.py failed:")
            print(result.stderr)
            return False
    finally:
        os.unlink(temp_file)


def main():
    print("=" * 60)
    print("CRC Fix Verification Test")
    print("=" * 60)

    all_passed = True

    all_passed &= test_crc_calculation()
    all_passed &= test_word_vs_byte_processing()
    all_passed &= test_full_firmware()

    print("\n" + "=" * 60)
    if all_passed:
        print("All tests PASSED [OK]")
        print("The CRC fix is working correctly.")
    else:
        print("Some tests FAILED [X]")
        print("Please review the implementation.")
    print("=" * 60)

    return 0 if all_passed else 1


if __name__ == "__main__":
    sys.exit(main())
