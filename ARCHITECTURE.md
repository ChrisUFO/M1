<!-- See COPYING.txt for license details. -->

# M1 Project Architecture

## Overview

The M1 firmware is organized into the following main components:

| Directory | Description |
|-----------|-------------|
| `Core/` | Application entry, FreeRTOS tasks, HAL configuration |
| `m1_csrc/` | M1 application logic (display, NFC, RFID, IR, Sub-GHz, firmware update) |
| `Battery/` | Battery monitoring |
| `NFC/` | NFC (13.56 MHz) support |
| `lfrfid/` | LF RFID (125 kHz) support |
| `Sub_Ghz/` | Sub-GHz radio support |
| `Infrared/` | IR transmit/receive |
| `Esp_spi_at/` | ESP32 SPI AT command interface |
| `USB/` | USB CDC and MSC classes |
| `Drivers/` | STM32 HAL, CMSIS, u8g2, and other drivers |
| `FatFs/` | FAT file system for storage |
| `Middlewares/` | FreeRTOS |

## Build System

- **STM32CubeIDE:** Uses `.project` and `.cproject`
- **VS Code / CMake:** Uses `CMakeLists.txt` and `CMakePresets.json`

## Versioning

This project uses **firmware versioning** (MAJOR.MINOR.BUILD.RC) rather than semantic versioning:

### Version Definition

Version is defined in `m1_csrc/m1_fw_update_bl.h`:
```c
#define FW_VERSION_MAJOR    0
#define FW_VERSION_MINOR    8
#define FW_VERSION_BUILD    2
#define FW_VERSION_RC       0
#define FW_VERSION_FORK_TAG "ChrisUFO"
```

### Version Format

- **Filename:** `M1_v{MAJOR}.{MINOR}.{BUILD}-{FORK_TAG}.bin`
- **Example:** `M1_v0.8.2-ChrisUFO.bin`
- **Display:** "Version 0.8.2" on splash screen and About menu

### Changelog

See [CHANGELOG.md](CHANGELOG.md) for version history. Format follows [Keep a Changelog](https://keepachangelog.com/) with sections for:
- Added (new features)
- Changed (modifications to existing features)
- Fixed (bug fixes)
- Removed (deprecated features)

### Version Bump Process

1. Update `FW_VERSION_BUILD` in `m1_csrc/m1_fw_update_bl.h`
2. Rebuild - version is automatically extracted by build scripts
3. Update [CHANGELOG.md](CHANGELOG.md) with new version entry
4. Commit both files together

### Fork Tag

The `FW_VERSION_FORK_TAG` distinguishes this fork from upstream:
- **Upstream:** "MonstaTek"
- **This fork:** "ChrisUFO"

This tag appears in:
- Splash screen (below version number)
- About menu
- Firmware filename
- CLI `version` command output
