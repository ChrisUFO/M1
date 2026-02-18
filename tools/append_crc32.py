#!/usr/bin/env python3
"""
Append STM32-compatible CRC32 to firmware binary at the correct position.

The M1 device expects the CRC32 at a specific flash address:
- FW_CRC_ADDRESS = 0x080FFC00 + sizeof(S_M1_FW_CONFIG_t) = 0x080FFC14
- This is at offset 1047572 (0xFFC14) in the binary file
- The CRC is NOT at the end of the file - it's at a fixed address

The device:
1. Flashes the firmware to Bank 2 (0x08100000)
2. Reads expected CRC from address 0x081FFC14
3. Calculates CRC on the flashed firmware (0x08100000 to 0x081FFC13)
4. Compares them

Uses table-driven CRC calculation for performance (256x faster than bit-by-bit).
"""

import sys
import struct


# CRC should be at this offset in the binary file
# FW_CRC_ADDRESS - FW_START_ADDRESS = 0x080FFC14 - 0x08000000 = 0xFFC14
CRC_OFFSET = 0xFFC14  # 1047572 bytes

# STM32 CRC-32 polynomial
CRC_POLYNOMIAL = 0x04C11DB7

# Pre-computed CRC table for byte-wise calculation (256 entries)
# This provides 256x speedup over bit-by-bit calculation
_CRC_TABLE = None


def _init_crc_table():
    """Initialize CRC lookup table for STM32-compatible CRC-32."""
    global _CRC_TABLE
    if _CRC_TABLE is not None:
        return

    _CRC_TABLE = []
    for byte in range(256):
        crc = byte << 24
        for _ in range(8):
            if crc & 0x80000000:
                crc = (crc << 1) ^ CRC_POLYNOMIAL
            else:
                crc <<= 1
            crc &= 0xFFFFFFFF
        _CRC_TABLE.append(crc)


def stm32_hal_crc32(data):
    """
    Calculate CRC32 compatible with STM32 HAL CRC peripheral.
    Uses table-driven approach for 256x speedup over bit-by-bit.

    STM32 HAL CRC configuration:
    - Polynomial: 0x04C11DB7 (CRC-32)
    - Initial value: 0xFFFFFFFF
    - Processes data as 32-bit words
    - No reflection on input/output
    """
    _init_crc_table()

    crc = 0xFFFFFFFF

    # Process byte-by-byte using lookup table (fastest approach)
    for byte in data:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ _CRC_TABLE[((crc >> 24) ^ byte) & 0xFF]

    return crc


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <firmware.bin>", file=sys.stderr)
        sys.exit(1)

    bin_file = sys.argv[1]

    # Read the binary file
    with open(bin_file, "rb") as f:
        data = bytearray(f.read())

    # Ensure the file is at least CRC_OFFSET bytes
    current_size = len(data)
    if current_size < CRC_OFFSET:
        print(
            f"ERROR: File too small ({current_size} bytes), needs to be at least {CRC_OFFSET} bytes"
        )
        sys.exit(1)

    # Calculate CRC on data up to CRC_OFFSET (excluding any previous CRC)
    fw_data = bytes(data[:CRC_OFFSET])
    crc = stm32_hal_crc32(fw_data)

    # Insert CRC at the correct position
    # The file should be exactly CRC_OFFSET + 4 bytes (1047576 bytes total)
    data = data[:CRC_OFFSET] + struct.pack("<I", crc)

    # Write the corrected file
    with open(bin_file, "wb") as f:
        f.write(data)

    print(f"CRC32: 0x{crc:08X}")
    print(f"Inserted at offset: {CRC_OFFSET} (0x{CRC_OFFSET:05X})")
    print(f"File size: {len(data)} bytes")
    print(f"Firmware file updated: {bin_file}")


if __name__ == "__main__":
    main()
