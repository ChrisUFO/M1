#!/usr/bin/env python3
"""
Append STM32-compatible CRC32 to firmware binary.

The M1 device expects a 4-byte CRC32 appended to the firmware .bin file.
The CRC is calculated using the same settings as STM32 HAL CRC:
- Polynomial: 0x4C11DB7 (CRC-32)
- Initial value: 0xFFFFFFFF
- No input/output inversion
- 32-bit word input
"""

import sys
import struct
import zlib


def calculate_stm32_crc(data):
    """
    Calculate CRC32 compatible with STM32 HAL CRC peripheral.

    The STM32 uses:
    - CRC-32 polynomial (0x4C11DB7)
    - Initial value: 0xFFFFFFFF
    - No bit reversal

    Python's zlib.crc32 uses the same polynomial but with different init,
    so we need to adjust.
    """
    # STM32 HAL CRC calculates on 32-bit words
    # Pad to 4-byte boundary if needed
    padding_needed = (4 - (len(data) % 4)) % 4
    if padding_needed:
        data = data + b"\x00" * padding_needed

    # Calculate CRC32 using standard CRC-32
    # STM32: init=0xFFFFFFFF, xorout=0x00000000
    # zlib: init=0x00000000, xorout=0xFFFFFFFF
    # So we need to invert the result
    crc = zlib.crc32(data, 0)
    crc = crc & 0xFFFFFFFF
    crc = crc ^ 0xFFFFFFFF  # Invert to match STM32 output

    return crc


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <firmware.bin>", file=sys.stderr)
        sys.exit(1)

    bin_file = sys.argv[1]

    # Read the binary file
    with open(bin_file, "rb") as f:
        data = f.read()

    # Calculate CRC32
    crc = calculate_stm32_crc(data)

    # Append CRC to file (little-endian)
    with open(bin_file, "ab") as f:
        f.write(struct.pack("<I", crc))

    print(f"Appended CRC32: 0x{crc:08X}")
    print(f"Firmware file updated: {bin_file}")


if __name__ == "__main__":
    main()
