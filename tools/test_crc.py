#!/usr/bin/env python3
"""Test CRC calculation to match STM32 hardware CRC"""

import struct

CRC_POLYNOMIAL = 0x04C11DB7


def _init_crc_table():
    """Initialize CRC lookup table for STM32-compatible CRC-32."""
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
    """
    Calculate CRC32 as STM32 hardware does.
    Processes data as 32-bit little-endian words.
    """
    crc = 0xFFFFFFFF

    # Pad data to 4-byte boundary
    padded = data + b"\x00" * (4 - len(data) % 4) if len(data) % 4 else data

    # Process as 32-bit little-endian words
    for i in range(0, len(padded), 4):
        # Read word in little-endian order (same as STM32)
        word = struct.unpack("<I", padded[i : i + 4])[0]

        # Process word MSB first
        for byte_shift in [24, 16, 8, 0]:
            byte = (word >> byte_shift) & 0xFF
            crc = ((crc << 8) & 0xFFFFFFFF) ^ _CRC_TABLE[((crc >> 24) ^ byte) & 0xFF]

    return crc


def stm32_hal_crc32_bytes(data):
    """
    Calculate CRC32 processing bytes in file order.
    This is INCORRECT for STM32 but let's compare.
    """
    crc = 0xFFFFFFFF

    for byte in data:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ _CRC_TABLE[((crc >> 24) ^ byte) & 0xFF]

    return crc


# Read the firmware file
with open("distribution/MonstaTek_M1_v0801-ChrisUFO.bin", "rb") as f:
    data = f.read()

# Calculate CRC on first 1047572 bytes (up to CRC offset)
fw_data = data[:1047572]

crc_words = stm32_hal_crc32_words(fw_data)
crc_bytes = stm32_hal_crc32_bytes(fw_data)

print(f"CRC (word-based, should match STM32): 0x{crc_words:08X}")
print(f"CRC (byte-based, probably wrong):       0x{crc_bytes:08X}")
print(f"CRC in file at offset 0xFFC14:          0x{data[1047572:1047576].hex()}")

# Show the actual CRC bytes in little-endian
crc_in_file = struct.unpack("<I", data[1047572:1047576])[0]
print(f"CRC in file (as uint32):                0x{crc_in_file:08X}")
