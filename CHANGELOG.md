<!-- See COPYING.txt for license details. -->

# Changelog

All notable changes to the M1 project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to firmware versioning (MAJOR.MINOR.BUILD.RC).

## [v0.8.5] - 2026-02-19

### Changed

- **Build Hardening**:
  - Enforced `-Werror` for the `m1_core` application target.
  - Isolated third-party drivers into a separate `m1_drivers` target with relaxed diagnostics.
  - Firmware version bumped to `0.8.5`.

### Fixed

- **Static Analysis & Code Quality**:
  - Resolved hundreds of compiler warnings (signedness, type safety, logic, and safety).
  - Fixed standard library compatibility by adding missing syscalls in `libc_compat.c`.
  - Fixed critical toolchain issues on Windows causing binary archives to be misinterpreted during linkage.
  - Automated ESP32 binary resource generation using `bin2array.cmake`.
  - Preserved code stubs and future-work logic using standardized `(void)` casts and `#if 0` blocks.

## [v0.8.4] - 2026-02-19

### Added

- **IR Universal Remote Enhancements**:
  - **Dashboard UI**: New start screen with Favorites, Recent, and Search options.
  - **Pagination**: Support for folders with virtually unlimited entries in the IR database.
  - **Search**: Fast text-based searching through the IR database folder structure.
  - **Recent History**: Automatic tracking of the last 10 remotes used.
  - **Favorites**: Manual pinning of frequently used remotes for instant access.
  - **Fast Scroll**: Use Left/Right keys to jump multiple pages in lists.

### Changed

- Firmware version bumped to `0.8.4`.

### Fixed

- **Issue #14**: Fixed 32-item limit in IR database browsing.

## [v0.8.3] - 2026-02-19

### Added

- **USB DFU Mode** (`m1_csrc/m1_dfu.c`):
  - Reboot directly into STM32 ROM USB DFU mode via Settings or CLI.
  - Confirmation flow with safety timeout and status feedback.
- **Documentation**:
  - `ARCHITECTURE.md` updated to match current repository structure.
  - User documentation for DFU flow, troubleshooting, and tested matrix.

### Changed

- **Build System Improvements**:
  - CMake post-build pipeline now warns instead of failing if `arm-none-eabi-objcopy` is missing.
  - Fixed false success reporting on build failures.
  - Version format standardized: `M1_v{MAJOR}.{MINOR}.{BUILD}-ChrisUFO`.

### Fixed

- **Critical Fixes**:
  - Corrected STM32H573 bootloader address to `0x0BF90000`.
  - Fixed buffer overflow build errors in `m1_sub_ghz.c` and `m1_sdcard_man.h`.
  - CRC32 calculation now matches STM32 HAL (32-bit word processing).

## [v0.8.2] - 2026-02-18

### Added

- **WiFi Configuration** (`m1_csrc/m1_wifi.c`):
  - Full WiFi join flow: scan → selection → password entry → connection.
  - Scrolling character selector with 3 modes (abc/ABC/!@#) and case memory.
  - Saved networks management (list, persistent status, delete with confirmation).
- **WiFi Credential Storage** (`m1_csrc/m1_wifi_cred.c/.h`):
  - AES-256-CBC encryption for saved WiFi passwords using STM32H5 CRYP hardware.
  - Device-specific encryption keys derived from MCU UID.
- **Enhanced CLI Commands** (8 new commands):
  - `version`, `status`, `battery`, `sdcard`, `wifi`, `memory`, `reboot`, `log`.

### Changed

- Firmware version bumped to `0.8.2`.
- Filename format changed from `MonstaTek_*` to `M1_*` to reflect fork.

### Fixed

- Fixed duplicate function definitions in WiFi modules.

## [v0.8.1] - 2026-02-17

### Added

- **Universal Remote** (`m1_csrc/m1_ir_universal.c/.h`):
  - Flipper Zero-compatible `.ir` file support (parsed signal format).
  - 3-level SD card browser (Category → Brand → Device → Commands).
  - 20+ IR protocol mappings (NEC, Samsung, SIRC, RC5/6, etc.).
  - Transmit UI with real-time feedback.
- **Fork Branding**:
  - Added fork tag (`FW_VERSION_FORK_TAG "ChrisUFO"`).

### Changed

- Firmware version bumped to `0.8.1`.
- `m1_csrc/m1_infrared.c`: Enabled universal remote stub logic.

## [v0.8.0] - 2026-02-05

### Added

- Forked from [Monstatek/M1](https://github.com/Monstatek/M1) upstream v0.8.0.
- Initial ChrisUFO fork release.
- STM32H573VIT6 support and hardware revision 2.x support.
- Base firmware features: NFC, RFID, Sub-GHz, Infrared, Battery monitoring.
