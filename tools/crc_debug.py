#!/usr/bin/env python3
"""
Debug CRC calculation to match STM32 HAL CRC peripheral
"""

import struct

# STM32 CRC-32 polynomial
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


def stm32_crc_words_little_endian(data):
    """
    Calculate CRC32 as STM32 HAL does with CRC_INPUTDATA_FORMAT_WORDS.
    Processes data as 32-bit little-endian words.
    """
    crc = 0xFFFFFFFF

    # Pad data to 4-byte boundary
    padded = data + b"\x00" * (4 - len(data) % 4) if len(data) % 4 else data

    # Process as 32-bit little-endian words (matching STM32 memory access)
    for i in range(0, len(padded), 4):
        word = struct.unpack("<I", padded[i : i + 4])[0]

        # Process word MSB first
        for byte_shift in [24, 16, 8, 0]:
            byte = (word >> byte_shift) & 0xFF
            crc = ((crc << 8) & 0xFFFFFFFF) ^ _CRC_TABLE[((crc >> 24) ^ byte) & 0xFF]

    return crc


def stm32_crc_bytes_reversed_words(data):
    """
    Alternative: Process bytes but reverse each word's byte order
    """
    crc = 0xFFFFFFFF

    # Process 4 bytes at a time, but reverse byte order within each word
    for i in range(0, len(data), 4):
        chunk = data[i : i + 4]
        # Pad if necessary
        if len(chunk) < 4:
            chunk = chunk + b"\x00" * (4 - len(chunk))

        # Reverse the bytes in the word (big-endian word)
        for byte in reversed(chunk):
            crc = ((crc << 8) & 0xFFFFFFFF) ^ _CRC_TABLE[((crc >> 24) ^ byte) & 0xFF]

    return crc


def stm32_crc_simple_bytes(data):
    """
    Simple byte-by-byte processing
    """
    crc = 0xFFFFFFFF
    for byte in data:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ _CRC_TABLE[((crc >> 24) ^ byte) & 0xFF]
    return crc


# Read firmware
with open("distribution/MonstaTek_M1_v0801-ChrisUFO.bin", "rb") as f:
    data = f.read()

# Calculate CRC on first 1047572 bytes (up to CRC location)
fw_data = data[:0xFFC14]
crc_in_file = struct.unpack("<I", data[0xFFC14 : 0xFFC14 + 4])[0]

print("CRC Calculation Methods:")
print("=" * 50)
print(f"CRC in file:              0x{crc_in_file:08X}")
print()
print(f"Method 1 (LE words):      0x{stm32_crc_words_little_endian(fw_data):08X}")
print(f"Method 2 (reversed words):0x{stm32_crc_bytes_reversed_words(fw_data):08X}")
print(f"Method 3 (simple bytes):  0x{stm32_crc_simple_bytes(fw_data):08X}")
print()

# Test with a known small example
test_data = b"123456789"
print("Test with '123456789':")
print(f"Method 1: 0x{stm32_crc_words_little_endian(test_data):08X}")
print(f"Method 2: 0x{stm32_crc_bytes_reversed_words(test_data):08X}")
print(f"Method 3: 0x{stm32_crc_simple_bytes(test_data):08X}")
print("Expected CRC-32: 0xCBF43926 (standard)")
