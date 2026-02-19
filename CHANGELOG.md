<!-- See COPYING.txt for license details. -->

# Changelog

All notable changes to the M1 project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to firmware versioning (MAJOR.MINOR.BUILD.RC).

## [v0.8.3] - 2026-02-19

### Added

- **USB DFU Mode** (`m1_csrc/m1_dfu.c`):
  - Reboot directly into STM32 ROM USB DFU mode via Settings or CLI.
  - Confirmation flow for safety.
  - Architecture documentation updated for DFU flow.

### Fixed

- Buffer overflow build errors in `m1_sub_ghz.c` and `m1_sdcard_man.h`.
- STM32H573 bootloader address corrected to `0x0BF90000`.

## [v0.8.2] - 2026-02-18

### Added

- **WiFi Configuration** (`m1_csrc/m1_wifi.c`):
  - Full WiFi join flow: scan → select AP → enter password → connect
  - Password entry with scrolling character selector (3 modes: abc/ABC/!@#)
  - Position memory per mode for seamless case switching (m→M)
  - Prev/next character indicators for visual navigation feedback
  - Show password toggle (long press DOWN)
  - Signal strength bars in AP list (visual indicator)
  - Saved networks management (list, delete with confirmation)
  - Connection status screen with disconnect button
  - Support for connecting to open and secured networks (WPA/WPA2)
  
- **WiFi Credential Storage** (`m1_csrc/m1_wifi_cred.c/.h`):
  - AES-256-CBC encryption for saved WiFi passwords (replaced XOR)
  - Device-specific encryption key derived from STM32H5 UID
  - Support for up to 10 saved networks
  - File format with magic number, version, and checksum validation
  - Credentials stored on SD card in `WIFI/networks.bin`
  
- **AES-256 Crypto Module** (`m1_csrc/m1_crypto.c/.h`):
  - Hardware-accelerated encryption using STM32H5 CRYP peripheral
  - AES-256-CBC with random IV generation
  - PKCS7 padding support
- **Enhanced CLI Commands** (8 new commands):
  - `version` - Show detailed firmware version with fork tag
  - `status` - System status and active bank
  - `battery` - Real-time battery level, voltage, and capacity
  - `sdcard` - SD card status
  - `wifi` - ESP32 WiFi module status
  - `memory` - Memory usage statistics
  - `reboot` - Software reset via serial console
  - `log` - Debug log messages
- **Serial Console Documentation**:
  - USB CDC serial console at 9600 baud
  - Comprehensive debugging via CLI
  - Real-time debug messages during firmware updates
- **Build System Improvements**:
  - Automatic version extraction from header file
  - CRC32 calculation compatible with STM32 HAL
  - Build script now properly detects failures
  - Version format: `M1_v{MAJOR}.{MINOR}.{BUILD}-ChrisUFO`
- **Debugging Infrastructure**:
  - Detailed logging in firmware update process
  - File validation debug messages (filename, size, CRC)
  - Serial console troubleshooting guide

### Changed

- Firmware version bumped to `0.8.2`
- CMakeLists.txt: Automatic version extraction from `m1_fw_update_bl.h`
- Build script: Fixed false "successful" reporting on build failures
- Version display: Now shows full version "Version 0.8.2" on splash screen
- Filename format: Changed from `MonstaTek_*` to `M1_*` to reflect fork

### Fixed

- CRC calculation now matches STM32 HAL (32-bit word processing)
- Build script now correctly reports build failures
- Fixed duplicate function definitions in WiFi modules

## [v0.8.1] - 2026-02-17

### Added

- **Universal Remote** feature (`m1_csrc/m1_ir_universal.c/.h`):
  - Flipper Zero-compatible `.ir` file support (parsed signal format)
  - SD card IR database layout: `0:/IR/<Category>/<Brand>/<Device>.ir`
  - 3-level SD card browser (Category → Brand → Device → Commands)
  - 20+ IR protocol mappings: NEC, NECext, NEC42, RC5, RC5X, RC6, RC6A,
    Samsung32, Samsung48, SIRC, SIRC15, SIRC20, Kaseikyo, Denon, Sharp,
    JVC, Panasonic, NEC16, LGAIR
  - Scrollable command list UI on 128×64 OLED (u8g2, 5×8 font, 6 rows)
  - IR transmit via IRSND with on-screen "Sending…" feedback
  - Graceful error display when SD card is absent or directory is empty
- Fork tag added (`FW_VERSION_FORK_TAG "ChrisUFO"`)

### Changed

- `m1_csrc/m1_infrared.c`: `infrared_universal_remotes()` now delegates to
  `ir_universal_run()` instead of being a no-op stub
- `cmake/m1_01/CMakeLists.txt`: added `m1_ir_universal.c` to build
- Firmware version bumped to `0.8.1`
- `ARCHITECTURE.md` updated to reflect fork structure.

## [v0.8.0] - 2026-02-05

### Added

- Forked from [Monstatek/M1](https://github.com/Monstatek/M1) upstream v0.8.0
- Initial ChrisUFO fork release
- STM32H573VIT6 support (2MB Flash, 100LQFP)
- Hardware revision 2.x support
- Base firmware with NFC, RFID, Sub-GHz, Infrared, Battery monitoring

### Added

- Forked from [Monstatek/M1](https://github.com/Monstatek/M1) upstream v0.8.0
- Initial ChrisUFO fork release
- STM32H573VIT6 support (2MB Flash, 100LQFP)
- Hardware revision 2.x support
- Base firmware with NFC, RFID, Sub-GHz, Infrared, Battery monitoring
