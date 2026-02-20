<!-- See COPYING.txt for license details. -->

# Changelog

All notable changes to the M1 project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to firmware versioning (MAJOR.MINOR.BUILD.RC).

## [v0.8.8] - 2026-02-20

### Fixed
- **Firmware Update**: Fixed a bug where SD card firmware updates would fail with "Invalid image file!" because the file browser lost the selected filename context.
- **Issue #27 (USB CDC CLI RX Race)**: Replaced direct ISR overwrite of `logdb_rx_buffer` in `CDC_Receive_FS()` with stream-buffered handoff to CLI task, preventing dropped/truncated commands under burst USB traffic.
- **Issue #26 (IR Universal Fragmentation Risk)**: Removed transient heap allocation churn in Universal Remote search/list flows by switching to pre-allocated workspace buffers and guarded access.
- **Issue #22 (Sub-GHz ISR Queue Pressure)**: Reduced `TIM1_CC_IRQHandler()` queue flood behavior by gating raw-capture notifications with pending-state throttling while keeping ring-buffer capture continuous.
- **CRC Validation Scripts**: Reworked `tools/test_crc.py` and `tools/test_crc_fix.py` to use current firmware artifact naming and robust trailer-based CRC validation.

### Changed
- **Architecture Docs**: Updated `ARCHITECTURE.md` runtime notes to reflect deterministic buffering patterns for USB CLI, IR Universal, and Sub-GHz capture paths.
- **CLI Diagnostics**: Added `cdcstats` and `cdcreset` commands plus expanded `status`/`memory` reporting for USB CLI RX telemetry.
- **Validation Docs**: Added `documentation/stability_phase5_validation.md` with reproducible hardware test matrix and reporting checklist.

## [v0.8.7] - 2026-02-20

### Added
- **Hardware Integration**:
  - Implemented boot-time Secure Firmware Integrity check via STM32H5 Hardware CRC unit (Issue #20).
  - Added infinite boot-loop protection leveraging RTC Backup Registers to recover gracefully into USB DFU mode.
- **CLI Commands**:
  - Added new basic testing command `9` to purposely corrupt the firmware magic number.

### Fixed
- **USB DFU Mode Entry**:
  - Fixed "DFU MODE FAILED!" by disabling interrupts and SysTick before tearing down clocks in `firmware_update_enter_usb_dfu()`. FreeRTOS SysTick firing during `HAL_RCC_DeInit()` caused a HardFault → reset.
- **Boot/Artifact Integrity Pipeline**:
  - Fixed `.bin` generation to use `objcopy --gap-fill 0xFF` so CRC input matches erased flash semantics.
  - Fixed `.hex` generation to be produced from the post-processed `.bin` (after `append_crc32.py`) instead of directly from `.elf`.
  - Resolved non-booting firmware caused by mismatched flash image content vs embedded `fw_image_size`/CRC metadata in early boot verification.
- **Code Review Hardening**:
  - Removed duplicate `SYS_CONFIG_MAGIC_NUMBER` definition (now single source in `m1_fw_update_bl.h`).
  - Added defensive parentheses to `FW_CRC_ADDRESS` macro to prevent operator precedence bugs.
  - Added NULL guard for `button_events_q_hdl` in firmware corruption handler to prevent undefined behavior during early boot.
  - Added `bl_validate_fw_header()` check before rollback bank swap to prevent bricking on corrupted alternate bank.
- **Linking/Startup Symbol Retention**:
  - Updated final link strategy to force-retain `m1_core` objects while keeping `m1_drivers` linked normally, avoiding dropped startup/ISR overrides without introducing duplicate libc/syscall symbol conflicts.
- **WiFi UI Layout (128x64 LCD)**:
  - Adjusted WiFi Config menu row spacing so all three items render fully on-screen instead of clipping below the bottom edge.
  - Reflowed WiFi Connection Status text rows to keep status, SSID, IP, and RSSI visible above footer controls.
  - Compressed AP detail rows in scan list from separate Channel/Auth lines into a single `Ch/Auth` line to prevent overflow on 64px height.
- **Documentation**:
  - Documented the boot-critical artifact pipeline and flashing guidance in `README.md` and `ARCHITECTURE.md`.

## [v0.8.6] - 2026-02-19
- Cleaned up leftover FCC test placeholders and associated dead code.
- Optimized Sub-GHz initialization by removing unused test configurations.

### Removed
- Unused `SI446x_Start_Tx_CW` function.
- Placeholder `sub_ghz_radio_settings` function.
- Test-only GPIO debugging hooks.

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
