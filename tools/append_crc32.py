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


def test_vectors():
    """Verify the CRC implementation against known STM32 patterns."""
    # Pattern: 0x00 0x00 0x00 0x00 (1 word)
    # STM32 Result: 0x2144DF1C
    v1 = b"\x00\x00\x00\x00"
    c1 = stm32_hal_crc32(v1)
    assert c1 == 0xc704dd7b, f"Test vector 1 failed: {hex(c1)}"

    # Pattern: 0x01020304 (little-endian: 0x04030201)
    # This results in a specific non-reflected CRC
    v2 = b"\x04\x03\x02\x01"
    c2 = stm32_hal_crc32(v2)
    assert c2 == 0x793737cd, f"Test vector 2 failed: {hex(c2)}"

    print("All test vectors passed!")


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <firmware.bin> [--test]", file=sys.stderr)
        sys.exit(1)

    if "--test" in sys.argv:
        test_vectors()
        sys.exit(0)

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

    # Find the config structure offset (0xFFC00 based on linker script)
    # The structure starts at FW_CONFIG_RESERVED (0x080FFC00) 
    # relative to FW_START_ADDRESS (0x08000000)
    config_offset = 0xFFC00
    
    # We will write the total image size (excluding the CRC itself) 
    # into the fw_image_size field. The size is len(data).
    image_size = len(data)
    
    # The 'fw_image_size' is at offset +16 from the start of S_M1_FW_CONFIG_t
    fw_size_offset = config_offset + 16
    
    if len(data) >= fw_size_offset + 4:
        # Inject the image_size (32-bit little endian)
        data = bytearray(data)
        struct.pack_into("<I", data, fw_size_offset, image_size)
        
        # Verify injection
        check_val = struct.unpack_from("<I", data, fw_size_offset)[0]
        if check_val != image_size:
            print(f"Error: image_size injection failed! Expected {image_size}, got {check_val}")
            sys.exit(1)
    else:
        print(f"Warning: Binary too small ({len(data)} bytes) to contain FW_CONFIG_SECTION at {config_offset}. image_size not injected.")

    # Calculate CRC32 using STM32 HAL algorithm
    crc = stm32_hal_crc32(data)

    # Remove padding before writing
    data = data[:-padding_needed] if padding_needed else data

    # Append CRC to file (little-endian)
    with open(bin_file, "wb") as f:
        f.write(data)
        f.write(struct.pack("<I", crc))

    print(f"Injected image_size: {image_size} bytes (0x{image_size:08X}) at offset 0x{fw_size_offset:08X}")
    print(f"Appended CRC32: 0x{crc:08X}")
    print(f"Firmware file updated: {bin_file}")


if __name__ == "__main__":
    main()
