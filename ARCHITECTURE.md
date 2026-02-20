<!-- See COPYING.txt for license details. -->

# M1 Project Architecture

## Overview

The firmware is an STM32H573 + FreeRTOS application with menu-driven features for RF, IR, storage, and maintenance.

| Directory | Description |
|-----------|-------------|
| `Core/` | MCU startup, interrupt vectors, FreeRTOS integration, CLI transport task |
| `m1_csrc/` | Application layer: menu/UI flow, feature modules, system state, firmware update paths |
| `Battery/` | Battery service and power telemetry |
| `NFC/` | NFC stack and device-specific integration |
| `lfrfid/` | LF RFID logic and protocol handling |
| `Sub_Ghz/` | Sub-GHz radio APIs, protocol logic, and configs |
| `Infrared/` | IR send/receive and IRMP/IRSND integration |
| `Esp_spi_at/` | ESP32 SPI-AT transport and hosted interface examples |
| `Esp32_serial_flasher/` | ESP32-C6 serial flashing support used by ESP32 update flow |
| `USB/` | USB device stack (CDC/MSC composite support) |
| `Drivers/` | STM32 HAL/CMSIS, BSP drivers, u8g2, and hardware support code |
| `FatFs/` | SD-card filesystem stack |
| `Middlewares/` | FreeRTOS and middleware dependencies |
| `cmake/` | CMake target composition (`cmake/m1_01/CMakeLists.txt`) |
| `tools/` | Build/release helper scripts (e.g., CRC append) |
| `scripts/` | Repository maintenance scripts |
| `documentation/` | Developer and build documentation |
| `distribution/` | Packaged build artifacts for release/flash |

## Runtime Architecture

- **RTOS model:** FreeRTOS tasks + queues.
- **Global device state:** `m1_device_stat` in `m1_csrc/m1_system.c`.
- **Primary queues:**
  - `main_q_hdl` for system/menu events (`m1_csrc/m1_tasks.c`)
  - `button_events_q_hdl` for keypad events (`m1_csrc/m1_system.c`)
- **Task bootstrap path:**
  - `Core/Src/app_freertos.c` -> `m1_system_init()`
  - `m1_csrc/m1_sys_init.c` performs peripheral init, task init, and startup status handling.

## Menu and Feature Structure

Main menu graph is defined in `m1_csrc/m1_menu.c`.

Current Settings submenu:

| Menu Item | Status | Notes |
|-----------|--------|-------|
| Storage | Enabled | SD card about/explore/mount/unmount/format |
| Power | Enabled | Battery info, reboot, power off |
| Firmware update | Enabled | SD-card image update + USB DFU mode entry |
| ESP32 update | Enabled | ESP32 serial flashing workflow |
| About | Enabled | Firmware and device info |
| LCD & Notifications | Disabled in menu | Function exists, not linked in active menu |
| System | Disabled in menu | Function exists, not linked in active menu |

## Firmware Update Paths

### 1) SD-card firmware update

- Located in `m1_csrc/m1_fw_update.c` + `m1_csrc/m1_fw_update_bl.c`.
- Flow: select `.bin` from storage -> validate image + CRC + metadata -> flash/swap bank.
- Startup operation-state persistence uses backup registers (`startup_config_write()` / `startup_config_handler()`).

### 2) USB DFU mode (ROM bootloader)

- Entry points:
  - Settings -> Firmware update -> USB DFU mode (`m1_csrc/m1_menu.c`)
  - CLI `dfu` command (`Core/Src/cli_app.c`, `m1_csrc/m1_cli.c`)
- Control flow:
  - Request flag persisted in backup registers (`DEV_OP_STATUS_USB_DFU_REQUEST`).
  - Device resets.
  - Startup handler detects request and jumps to STM32 system-memory bootloader.
- System memory bootloader jump implementation: `firmware_update_enter_usb_dfu()` in `m1_csrc/m1_fw_update.c`.

## USB Architecture

- Application-mode USB stack is in `m1_csrc/m1_usb_cdc_msc.c` and `USB/`.
- Build-time mode selection supports CDC, MSC, or CDC+MSC composite.
- USB DFU transport is provided by STM32 ROM bootloader (not by application USB class runtime).

## Build System

- **STM32CubeIDE:** Uses `.project` and `.cproject`
- **VS Code / CMake:** Uses `CMakeLists.txt` and `CMakePresets.json`
- **Zero-Warning Core Architecture:** The build is split into two primary static libraries:
  - `m1_core`: Contains all ChrisUFO application logic. Built with strict `-Werror` enforcement.
  - `m1_drivers`: Contains third-party middleware (HAL, FreeRTOS, RFAL). Built with relaxed diagnostics to isolate legacy warnings.

## Versioning

This project uses **firmware versioning** (MAJOR.MINOR.BUILD.RC) rather than semantic versioning:

### Version Definition

Version is defined in `m1_csrc/m1_fw_update_bl.h`:
```c
#define FW_VERSION_MAJOR    0
#define FW_VERSION_MINOR    8
#define FW_VERSION_BUILD    4
#define FW_VERSION_RC       0
#define FW_VERSION_FORK_TAG "ChrisUFO"
```

### Version Format

- **Filename:** `M1_v{MAJOR}.{MINOR}.{BUILD}-{FORK_TAG}.bin`
- **Example:** `M1_v0.8.4-ChrisUFO.bin`
- **Display:** "Version 0.8.4" on splash screen and About menu

### Secure Boot Sequence

To protect against corrupted flash banks (Issue #20), the M1 employs an early boot integrity check:
1. `SystemInit()` in `system_stm32h5xx.c` invokes `boot_recovery_check()` before any RAM initialization.
2. The STM32H5 Hardware CRC unit computes the CRC-32 of the active firmware bank (using sizes derived from `S_M1_FW_CONFIG_t`).
3. If the payload is invalid, the bootloader writes `0xDEADBEEF` to `RTC->BKP1R` and securely toggles the `SWAP_BANK` option byte.
4. A system reset safely swaps to the alternate hardware bank. If both banks fail, `RTC->BKP1R` forces a direct fallback to ROM USB DFU mode.

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

---

## Hardware Specifications

### Display

The M1 uses a **JHD12864-G386BTW** monochrome LCD module:

| Specification | Value |
|--------------|-------|
| Resolution | **128 x 64 pixels** |
| Interface | SPI (hspi1) |
| Controller | ST7586S |
| Pins | MOSI (PA7), MISO (PA6), CLK (PA5), CS (PA3) |

**Display Coordinates:**
- Origin (0, 0) is at the **top-left** corner
- X ranges: 0 to 127 (128 pixels wide)
- Y ranges: 0 to 63 (64 pixels tall)

**Important:** When implementing UI screens, ensure all elements fit within these bounds. For example, a text element at y=100 would be off-screen since the display only has 64 vertical pixels.

**Key Defines:**
- `M1_LCD_DISPLAY_WIDTH` = 128 (in `m1_lcd.h`)
- `M1_LCD_DISPLAY_HEIGHT` = 64 (in `m1_lcd.h`)

---

## Menu Structure & Feature Status

This section documents the M1 menu structure, implementation status of each feature, and development priorities.

### Legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Fully functional |
| ⚠️ | Stub / placeholder (shows "firmware update" screen or empty loop) |
| 🚫 | Disabled — code exists but item is **commented out** of the menu |

### 📡 Sub-GHz

| Menu Item | Status | Notes |
|-----------|--------|-------|
| Record | ✅ | Full pipeline: SI4463 capture → decode → save to SD card |
| Replay | ✅ | Browse SD card files, transmit saved signal |
| Frequency Reader | ✅ | Scans spectrum, shows strongest frequency |
| Regional Information | ✅ | Displays regional frequency band info |
| ~~Radio Settings~~ | 🚫 | Function exists but **not in menu** — accessible via future update |

**Location:** `m1_csrc/m1_sub_ghz.c`

**Note:** Radio Settings function exists but is not included in the current Sub-GHz submenu (only 4 items shown).

---

### 🔴 Infrared

| Menu Item | Status | Notes |
|-----------|--------|-------|
| Universal Remote | ✅ | Dashboard UI with Search, Favorites, Recent, and Paginated browsing |
| Learn New Remote | ✅ | IRMP decode, displays protocol/address/command, saves to SD card |
| Saved Remotes | ✅ | Browse saved signals, replay last learned signal |

**Location:** `m1_csrc/m1_infrared.c`, `m1_csrc/m1_ir_universal.c`

---

### 🔑 LF RFID (125 kHz)

| Menu Item | Status | Notes |
|-----------|--------|-------|
| Read | ✅ | EM4100 and H10301 decode |
| Saved | ✅ | Emulate, write to T5577, edit, rename, delete, info |
| Add Manually | ✅ | Enter card data manually |
| 125 kHz Utilities | ⚠️ | `rfid_125khz_utilities()` — no utility screens implemented |

**Location:** `m1_csrc/m1_rfid.c`

---

### 📶 NFC (13.56 MHz)

| Menu Item | Status | Notes |
|-----------|--------|-------|
| Read | ✅ | ISO14443A/B/F/V, Ultralight/NTAG, Mifare Classic |
| Saved | ✅ | Emulate, edit UID, rename, delete, info |
| NFC Tools | ⚠️ | `nfc_tools()` — no tools implemented |

**Location:** `m1_csrc/m1_nfc.c`

---

### 📶 WiFi

| Menu Item | Status | Notes |
|-----------|--------|-------|
| Join Network | ✅ | Full pipeline: Scan → Select AP → Enter Password → Connect via AT+CWJAP |
| Saved Networks | ✅ | List saved credentials with auto-fill on reconnection |
| Connection Status | ✅ | Shows current connection status (SSID, IP, RSSI) |

**Location:** `m1_csrc/m1_wifi.c`, `m1_csrc/m1_wifi_cred.c`, `m1_csrc/m1_crypto.c`

**Implementation Details:**
- Password entry uses scrolling character selector with 3 modes (lowercase, uppercase, special chars)
- **Credentials encrypted with AES-256-CBC** using device UID as key
- IV generation uses entropy from HAL_GetTick() and device UID (see Issue #16 for hardware RNG)
- Auto-saves credentials after successful connection
- Uses ESP32 AT command `AT+CWJAP` for connection
- File validation with magic number, version, and checksum

---

### 🔵 Bluetooth

| Menu Item | Status | Notes |
|-----------|--------|-------|
| Scan | ✅ | BLE device scan, shows device name and RSSI |
| Advertise | ✅ | BLE advertisement broadcast |
| Bluetooth Config | ⚠️ | `bluetooth_config()` — no config UI |

**Location:** `m1_csrc/m1_bt.c`

---

### 🔌 GPIO

| Menu Item | Status | Notes |
|-----------|--------|-------|
| Manual Control | ✅ | Toggle individual GPIO pins |
| 3.3 V Power | ✅ | Enable/disable 3.3 V rail |
| 5 V Power | ✅ | Enable/disable 5 V rail |
| USB–UART Bridge | ⚠️ | `gpio_usb_uart_bridge()` — no bridge UI |

**Location:** `m1_csrc/m1_gpio.c`

---

### ⚙️ Settings

| Menu Item | Status | Notes |
|-----------|--------|-------|
| Storage | ✅ | SD card: About, Explore, Mount, Unmount, Format |
| Power | ✅ | Battery Info, Reboot, Power Off |
| ~~LCD & Notifications~~ | 🚫 | Function exists but menu entry commented out |
| ~~System~~ | 🚫 | Function exists but menu entry commented out |
| Firmware Update | ✅ | Browse SD card for `.bin`, flash via bootloader or enter ROM USB DFU mode |
| ESP32 Update | ✅ | ESP32-C6 WiFi/BT module firmware update |
| About | ✅ | Shows firmware version and device info |

**Location:** `m1_csrc/m1_settings.c`, `m1_csrc/m1_menu.c`

---

## Recommended Implementation Order

Based on pen-testing value and implementation effort:

| Priority | Feature | Effort | Value |
|----------|---------|--------|-------|
| 1 | **USB–UART Bridge** | Low | High | Critical for hardware hacking |
| 2 | **Sub-GHz Radio Settings** | Low | High | Custom modulation for rolling-code |
| 3 | **125 kHz Utilities** | Medium | High | T5577 raw write + facility-code brute-force |
| 4 | **NFC Tools** | Medium | High | Mifare Classic dict attack |
| 5 | **Settings: LCD & Notifications** | Low | Medium | Quality-of-life |
| 6 | **Settings: System** | Low | Medium | Product completeness |
| 7 | **Bluetooth Config** | Low | Medium | BLE spoofing/spam |
| 8 | **WiFi Config** | Low–Med | Medium | Network join, HTTP attacks |

## References

- [Flipper Zero Firmware](https://github.com/flipperdevices/flipperzero-firmware) - Reference for LF RFID, NFC, Sub-GHz
- [nfc-laboratory](https://github.com/josevcm/nfc-laboratory) - NFC protocol analysis
- [AppleJuice](https://github.com/ECTO-1A/AppleJuice) - BLE advertisement spam
- [Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB) - IR database
