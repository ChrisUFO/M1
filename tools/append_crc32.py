#!/usr/bin/env python3
"""
Append STM32-compatible CRC32 to firmware binary.

The M1 device expects a 4-byte CRC32 appended to the firmware .bin file.
The CRC is calculated using STM32 HAL CRC settings:
- Polynomial: 0x4C11DB7 (CRC-32)
- Initial value: 0xFFFFFFFF (STM32 default)
- No input/output inversion
- 32-bit word input

This matches the STM32 HAL CRC configuration:
- DefaultPolynomialUse = ENABLE
- DefaultInitValueUse = ENABLE
- InputDataInversionMode = NONE
- OutputDataInversionMode = DISABLE
"""

import sys
import struct


def stm32_crc32(data):
    """
    Calculate CRC32 compatible with STM32 HAL CRC peripheral.

    Uses the standard CRC-32 algorithm with STM32 defaults:
    - Polynomial: 0x04C11DB7
    - Initial value: 0xFFFFFFFF
    - Final XOR: 0x00000000 (no final XOR)
    """
    # CRC-32 polynomial
    POLYNOMIAL = 0x04C11DB7

    # STM32 default initial value
    crc = 0xFFFFFFFF

    # Process each byte
    for byte in data:
        crc ^= byte << 24
        for _ in range(8):
            if crc & 0x80000000:
                crc = (crc << 1) ^ POLYNOMIAL
            else:
                crc <<= 1
            crc &= 0xFFFFFFFF

    return crc


def calculate_stm32_crc(data):
    """
    Calculate CRC32 on 32-bit words (STM32 format).

    The STM32 processes data as 32-bit words. We need to ensure
    the data is aligned to 4-byte boundary before calculating.
    """
    # Pad to 4-byte boundary if needed
    padding_needed = (4 - (len(data) % 4)) % 4
    if padding_needed:
        data = data + b"\x00" * padding_needed

    # Calculate CRC
    crc = stm32_crc32(data)

    return crc


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <firmware.bin>", file=sys.stderr)
        sys.exit(1)

    bin_file = sys.argv[1]

    # Read the binary file
    with open(bin_file, "rb") as f:
        data = f.read()

    # Check if a CRC was already appended (last 4 bytes)
    if len(data) >= 4:
        last_4 = struct.unpack("<I", data[-4:])[0]
        # If last 4 bytes are zeros, likely a previous CRC run appended extra
        if last_4 == 0:
            # Find where actual firmware ends (last non-zero byte)
            fw_end = len(data) - 4
            for i in range(len(data) - 5, max(0, len(data) - 100), -1):
                if data[i] != 0:
                    fw_end = i + 1
                    break
            data = data[:fw_end]
            print(f"Truncated file to {len(data)} bytes (removed trailing zeros)")

    # Calculate CRC32
    crc = calculate_stm32_crc(data)

    # Append CRC to file (little-endian)
    with open(bin_file, "wb") as f:
        f.write(data)
        f.write(struct.pack("<I", crc))

    print(f"Appended CRC32: 0x{crc:08X}")
    print(f"Firmware file updated: {bin_file}")


if __name__ == "__main__":
    main()
