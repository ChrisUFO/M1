#!/usr/bin/env python3
"""
Append STM32-compatible CRC32 to firmware binary.

The M1 device uses STM32 HAL CRC which processes data as 32-bit words:
- Polynomial: 0x4C11DB7 (CRC-32)
- Initial value: 0xFFFFFFFF
- No input/output inversion (non-reflected)
- Processes 32-bit words (not individual bytes)
- Uses the actual STM32 HAL CRC algorithm
"""

import sys
import struct


def stm32_hal_crc32(data):
    """
    Calculate CRC32 compatible with STM32 HAL CRC peripheral.

    The STM32 HAL CRC:
    - Uses polynomial 0x04C11DB7
    - Initial value: 0xFFFFFFFF
    - Processes data as 32-bit words
    - No reflection on input or output

    When STM32 processes words, it takes 4 bytes at a time and
    calculates CRC on the 32-bit value.
    """
    # CRC-32 polynomial
    POLYNOMIAL = 0x04C11DB7

    # STM32 default initial value
    crc = 0xFFFFFFFF

    # Process data as 32-bit words (4 bytes at a time)
    for i in range(0, len(data), 4):
        # Get next 4 bytes as a 32-bit word (little-endian from file)
        word = data[i : i + 4]
        if len(word) < 4:
            # Pad with zeros if needed
            word = word + b"\x00" * (4 - len(word))

        # Convert to 32-bit integer (little-endian)
        val = struct.unpack("<I", word)[0]

        # XOR with current CRC
        crc ^= val

        # Process 32 bits
        for _ in range(32):
            if crc & 0x80000000:
                crc = (crc << 1) ^ POLYNOMIAL
            else:
                crc <<= 1
            crc &= 0xFFFFFFFF

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

    # Pad to 4-byte boundary if needed
    padding_needed = (4 - (len(data) % 4)) % 4
    if padding_needed:
        data = data + b"\x00" * padding_needed

    # Calculate CRC32 using STM32 HAL algorithm
    crc = stm32_hal_crc32(data)

    # Remove padding before writing
    data = data[:-padding_needed] if padding_needed else data

    # Append CRC to file (little-endian)
    with open(bin_file, "wb") as f:
        f.write(data)
        f.write(struct.pack("<I", crc))

    print(f"Appended CRC32: 0x{crc:08X}")
    print(f"Firmware file updated: {bin_file}")


if __name__ == "__main__":
    main()
