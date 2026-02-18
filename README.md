<!-- See COPYING.txt for license details. -->

# M1 Firmware — ChrisUFO Fork

> **This is a community fork of the [Monstatek/M1](https://github.com/Monstatek/M1) repository.**
> It adds the **Universal Remote** feature on top of the upstream codebase.
> A pull request to merge this back upstream is open at
> [ChrisUFO/M1#1](https://github.com/ChrisUFO/M1/pull/1).

Firmware for the M1 NFC/RFID multi-protocol device, built on STM32H5.

## Overview

The M1 firmware provides support for:

- **NFC** (13.56 MHz)
- **LF RFID** (125 kHz)
- **Sub-GHz** (315–915 MHz)
- **Infrared** (IR transmit/receive) — including **Universal Remote** (this fork)
- **Battery** monitoring
- **Display** (ST7586s ERC240160)
- **USB** (CDC, MSC)

## Hardware

- **MCU:** STM32H573VIT6 (32-bit, 2MB Flash, 100LQFP)
- **Hardware revision:** 2.x

See [HARDWARE.md](HARDWARE.md) for more details.

## Documentation

- [Build Tool (mbt)](documentation/mbt.md) – Build with STM32CubeIDE or VS Code
- [Architecture](ARCHITECTURE.md) – Project structure
- [Development](DEVELOPMENT.md) – Development guidelines
- [Changelog](CHANGELOG.md) – Release history

## Building

### Prerequisites

- **STM32CubeIDE 1.17+** (recommended), or
- **VS Code** with ARM GCC 14.2+, CMake Tools, Cortex-Debug, and Ninja

The `CMakePresets.json` expects the ARM toolchain at:
```
C:/Program Files (x86)/Arm/GNU Toolchain mingw-w64-i686-arm-none-eabi/bin/
```
Adjust the path in `CMakePresets.json` if your toolchain is installed elsewhere.

### Build steps

**STM32CubeIDE:**
Open the project and build in the IDE. Output: `./Release/MonstaTek_M1_v0801-ChrisUFO.elf`

**Command line (CMake + Ninja):**
```bash
cmake --preset gcc-14_2_build-release
cmake --build out/build/gcc-14_2_build-release
```

Output directory: `out/build/gcc-14_2_build-release/`

| File | Use |
|------|-----|
| `MonstaTek_M1_v0801-ChrisUFO.bin` | SD card firmware update |
| `MonstaTek_M1_v0801-ChrisUFO.hex` | STM32CubeProgrammer / JLink |
| `MonstaTek_M1_v0801-ChrisUFO.elf` | Debug sessions |

## Installing Firmware via SD Card

1. Copy `MonstaTek_M1_v0801-ChrisUFO.bin` to your SD card (any folder)
2. Insert the SD card into the M1
3. On the device navigate to **Settings → Firmware Update → Image file**
4. Browse to and select `MonstaTek_M1_v0801-ChrisUFO.bin`
5. Confirm — the device flashes and reboots automatically

## Universal Remote (this fork)

The Universal Remote feature lets the M1 browse and transmit IR commands from
[Flipper Zero-compatible `.ir` files](https://github.com/Lucaslhm/Flipper-IRDB)
stored on the SD card.

### On-device usage

Navigate to **Infrared → Universal Remote** on the device.

- Use **Up/Down** to scroll through categories, brands, devices, and commands
- Press **OK** to transmit the selected IR command
- Press **Back** to go up a level

### IR database SD card setup

The device expects `.ir` files organised as follows on the SD card:

```
SD card root:
  IR/
    <Category>/
      <Brand>/
        <Device>.ir
```

**Example:**
```
IR/
  TV/
    Samsung/
      Samsung_BN59-01315J.ir
    LG/
      LG_AKB75095307.ir
    Sony/
      Sony_RMT-TX100D.ir
  AC/
    Daikin/
      Daikin_ARC433B69.ir
    Mitsubishi/
      Mitsubishi_MSZ.ir
  Projector/
    Epson/
      Epson_EH-TW.ir
```

### Downloading the IR database

The [Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB) project (MIT License)
provides a large community-maintained database of `.ir` files compatible with this
feature. Download the repository and copy the desired category folders into the
`IR/` directory on your SD card.

```bash
git clone https://github.com/Lucaslhm/Flipper-IRDB
# Then copy e.g.:
cp -r Flipper-IRDB/TV /path/to/sdcard/IR/
cp -r Flipper-IRDB/AC /path/to/sdcard/IR/
```

### Supported IR protocols

NEC, NECext, NEC42, NEC16, RC5, RC5X, RC6, RC6A, Samsung32, Samsung48,
SIRC, SIRC15, SIRC20, Kaseikyo, Denon, Sharp, JVC, Panasonic, LGAIR

## Contributing

Contributions are welcome. See [CONTRIBUTING.md](.github/CONTRIBUTING.md) and the [Code of Conduct](.github/CODE_OF_CONDUCT.md).

To contribute to the upstream project, please open pull requests against
[Monstatek/M1](https://github.com/Monstatek/M1).

## License

See [LICENSE](LICENSE) for details.
