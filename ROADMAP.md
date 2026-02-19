# M1 Firmware — Development Roadmap

This document defines the phased implementation plan for completing all stub and
disabled features in the M1 firmware. Each phase is ordered by the combination of
**pen-testing value** and **implementation effort**, with the highest-value /
lowest-effort work first.

For each item we identify exactly which open-source code can be ported or adapted
so we are not building from scratch.

---

## Current State Summary

| Feature | Status |
|---------|--------|
| Sub-GHz Record / Replay / Freq Reader / Regional Info | ✅ Done |
| Infrared Universal Remote / Learn / Saved | ✅ Done |
| LF RFID Read / Saved / Add Manually | ✅ Done |
| NFC Read / Saved | ✅ Done |
| WiFi Scan AP | ✅ Done |
| BLE Scan / Advertise | ✅ Done |
| GPIO Manual / 3.3 V / 5 V | ✅ Done |
| Settings: About / Firmware Update | ✅ Done |
| **Sub-GHz Radio Settings** | ⚠️ Stub |
| **LF RFID 125 kHz Utilities** | ⚠️ Stub |
| **NFC Tools** | ⚠️ Stub |
| **WiFi Config** | ⚠️ Stub |
| **Bluetooth Config** | ⚠️ Stub |
| **GPIO USB–UART Bridge** | ⚠️ Stub |
| **Settings: LCD & Notifications** | 🚫 Disabled |
| **Settings: System** | 🚫 Disabled |

---

## Phase 1 — Quick Wins (Low Effort, High Value)

These items have the most infrastructure already in place. Each can be completed
in a focused sprint with minimal new code.

---

### 1.1 GPIO: USB–UART Bridge

**File:** `m1_csrc/m1_gpio.c` → `gpio_usb_uart_bridge()`  
**Effort:** Low (1–2 days)  
**Value:** High — serial console access to target hardware is a fundamental
hardware pen-testing primitive (reading boot logs, dropping to U-Boot shell,
accessing debug UARTs on IoT devices).

#### What needs to be done
1. Add a UI screen that lets the user select baud rate (9600 / 115200 / 230400 / custom).
2. On confirm, configure the STM32 UART peripheral at the chosen baud rate.
3. Route bytes bidirectionally between the USB CDC interface and the UART peripheral.
4. Show a live "bridge active" status screen with TX/RX byte counters.
5. Press **Back** to tear down the bridge and restore normal USB CDC operation.

#### What to leverage
| Source | What to take |
|--------|-------------|
| `m1_csrc/m1_usb_cdc_msc.c` | Existing USB CDC TX/RX callbacks — just redirect the data |
| STM32H5 HAL `HAL_UART_*` | Already used elsewhere in the project; add a UART→CDC ISR |
| [Flipper Zero `usb_uart_bridge`](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/main/gpio/usb_uart_bridge.c) | Reference for the baud-rate selection UI and the bidirectional byte-pump loop |

---

### 1.2 Sub-GHz: Radio Settings

**File:** `m1_csrc/m1_sub_ghz.c` → `sub_ghz_radio_settings()`  
**Effort:** Low (1–2 days)  
**Value:** High — custom modulation (OOK vs FSK, bandwidth, deviation, output power)
is required for capturing and replaying signals that don't match the default
configuration, including many rolling-code remotes and proprietary protocols.

#### What needs to be done
1. Add a settings screen with the following adjustable parameters:
   - **Modulation:** OOK / 2-FSK / 4-FSK / GFSK
   - **Bandwidth:** selectable from SI4463 filter table (e.g. 58 kHz, 100 kHz, 200 kHz)
   - **Deviation** (FSK only): e.g. 20 kHz, 47 kHz
   - **Output power:** −10 dBm to +20 dBm
   - **Frequency:** override the default (already set per region)
2. Persist settings to SD card / flash (use existing `m1_core_config` pattern).
3. Apply settings to the SI4463 before Record/Replay operations.

#### What to leverage
| Source | What to take |
|--------|-------------|
| `Sub_Ghz/m1_sub_ghz_api.c` | Existing SI4463 command wrappers |
| `Sub_Ghz/radio_config/` | Existing radio config headers — add new presets |
| [Flipper Zero `subghz_setting`](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/lib/subghz/subghz_setting.c) | Settings model: preset list, load/save from file |
| [Flipper Zero `subghz_scene_radio_settings`](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/main/subghz/scenes/subghz_scene_radio_settings.c) | UI scene reference |
| [SI4463 datasheet / EZRadioPRO API](https://www.silabs.com/documents/public/application-notes/AN633.pdf) | Register-level modulation configuration |

---

### 1.3 Settings: LCD & Notifications

**File:** `m1_csrc/m1_settings.c` → `settings_lcd_and_notifications()`  
**File:** `m1_csrc/m1_menu.c` — re-enable `menu_Settings_LCD_and_Notifications`  
**Effort:** Low (1 day)  
**Value:** Medium — needed for product completeness; display brightness and LED
notification control are expected by end users.

#### What needs to be done
1. Uncomment `menu_Settings_LCD_and_Notifications` in `m1_menu.c`.
2. Implement the settings screen with:
   - **Display brightness** (PWM duty cycle via `m1_lcd.c`)
   - **Screen timeout** (seconds before display off)
   - **LED notifications** on/off (via `m1_lp5814.c` / `m1_led_indicator.c`)
3. Persist to flash using the existing `m1_core_config` / `m1_settings` pattern.

#### What to leverage
| Source | What to take |
|--------|-------------|
| `m1_csrc/m1_lcd.c` | Existing brightness/backlight control |
| `m1_csrc/m1_led_indicator.c` | LED on/off/pattern API |
| `m1_csrc/m1_core_config.c` | Persist settings to flash |
| [Flipper Zero `notification_settings`](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/settings/notification_settings/notification_settings_app.c) | Reference for the settings UI pattern |

---

### 1.4 Settings: System

**File:** `m1_csrc/m1_settings.c` → `settings_system()`  
**File:** `m1_csrc/m1_menu.c` — re-enable `menu_Settings_System`  
**Effort:** Low (1–2 days)  
**Value:** Medium — device name, date/time, and factory reset are expected
product features.

#### What needs to be done
1. Uncomment `menu_Settings_System` in `m1_menu.c`.
2. Implement the settings screen with:
   - **Device name** (editable string, stored in flash; use `m1_virtual_kb.c`)
   - **Date / Time** (RTC read/write via STM32H5 HAL RTC)
   - **Reset to defaults** (clear `m1_core_config` flash region, reboot)
   - **Storage info** (SD card free/total via FatFs `f_getfree`)
3. Persist to flash using the existing `m1_core_config` / `m1_settings` pattern.

#### What to leverage
| Source | What to take |
|--------|-------------|
| `m1_csrc/m1_virtual_kb.c` | On-screen keyboard for device name entry |
| `m1_csrc/m1_core_config.c` | Flash persistence |
| `m1_csrc/m1_sdcard.c` | FatFs `f_getfree` for storage info |
| STM32H5 HAL RTC | `HAL_RTC_GetTime` / `HAL_RTC_SetTime` |

---

## Phase 2 — Medium Effort, High Value

These items require new logic but can leverage existing libraries already present
in the codebase.

---

### 2.1 LF RFID: 125 kHz Utilities

**File:** `m1_csrc/m1_rfid.c` → `rfid_125khz_utilities()`  
**Effort:** Medium (3–5 days)  
**Value:** High — T5577 raw write and facility-code brute-force are the two most
important LF RFID pen-testing primitives.

#### Recommended sub-features (in order)

| Sub-feature | Description |
|-------------|-------------|
| **T5577 Raw Config** | Write arbitrary T5577 block data; set modulation, data rate, PSK carrier |
| **Facility Code Brute-force** | Iterate card numbers within a fixed facility code and emulate each (H10301 / EM4100) |
| **Read RAW** | Capture raw pulse timings without protocol decode (for unknown protocols) |
| **Clone** | One-tap copy of a read card to a T5577 blank |

#### What to leverage
| Source | What to take |
|--------|-------------|
| `lfrfid/t5577.c` | Already present — T5577 block write primitives |
| `lfrfid/lfrfid_protocol_h10301.c` | H10301 encode for brute-force card generation |
| `lfrfid/lfrfid_protocol_em4100.c` | EM4100 encode |
| [Flipper Zero `lfrfid_worker`](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/lib/lfrfid/lfrfid_worker.c) | Worker thread pattern for background read/write |
| [Flipper Zero `lfrfid_app_scene_extra_actions`](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/main/lfrfid/scenes/lfrfid_app_scene_extra_actions.c) | Utilities menu scene reference |
| [Proxmark3 `hf_mf_chk`](https://github.com/RfidResearchGroup/proxmark3) | Brute-force pattern reference (adapt to LF) |

#### Implementation notes
- The brute-force loop should run in a FreeRTOS task (follow the existing
  `m1_rfid` task pattern) so the UI remains responsive.
- Add a progress bar showing current card number / total.
- Allow the user to set the facility code and card number range before starting.
- Log any "access granted" responses (reader beep / door open) detected via the
  antenna coil feedback.

---

### 2.2 NFC: NFC Tools

**File:** `m1_csrc/m1_nfc.c` → `nfc_tools()`  
**Effort:** Medium (4–7 days)  
**Value:** High — Mifare Classic dictionary attack and NDEF inspection are the
most-requested NFC pen-testing features.

#### Recommended sub-features (in order)

| Sub-feature | Description |
|-------------|-------------|
| **Card Info Dump** | Full UID, SAK, ATQA, ATS dump to screen and SD card |
| **NDEF Read** | Parse and display NDEF records (Text, URI, Smart Poster) |
| **Mifare Classic Dict Attack** | Try a dictionary of known keys against all sectors |
| **Mifare Classic Read All** | After key recovery, dump all 16/40 sectors to `.mfd` file |
| **Mifare Ultralight Read** | Dump all pages of Ultralight / NTAG |

#### What to leverage
| Source | What to take |
|--------|-------------|
| `NFC/Middlewares/` | ST RFAL stack — already handles ISO14443A/B, Mifare Classic auth (`rfalMifareClassicAuthenticateSector`) |
| `m1_csrc/m1_nfc.c` | Existing read pipeline — extend rather than replace |
| [Flipper Zero `nfc_worker`](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/lib/nfc/nfc_worker.c) | Worker thread pattern for background card operations |
| [Flipper Zero Mifare Classic dict](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/assets/resources/nfc/assets/mf_classic_dict.nfc) | Community key dictionary (port to SD card file) |
| [Flipper Zero NDEF parser](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/lib/nfc/protocols/mf_ultralight/mf_ultralight.c) | NDEF parsing reference |
| [libnfc](https://github.com/nfc-tools/libnfc) | Protocol reference for Mifare Classic sector auth flow |
| [mfoc](https://github.com/nfc-tools/mfoc) | Offline nested attack algorithm reference |

#### Implementation notes
- Store the key dictionary as a plain-text file on the SD card
  (`NFC/mf_classic_dict.txt`) so users can add custom keys without reflashing.
- Run the dictionary attack in a background FreeRTOS task with a progress bar
  (sector N / 16 or 40, keys tried N / total).
- Save the recovered dump as a `.mfd` binary file (Mifare Classic standard format,
  compatible with mfoc / libnfc tooling on a PC).
- NDEF parsing: implement a minimal parser for TNF 0x01 (Well Known) records
  covering Text (`T`) and URI (`U`) types — covers 90 % of real-world tags.

---

## Phase 3 — Medium Effort, Medium Value

These items improve the wireless attack surface but require ESP32 co-processor
coordination.

---

### 3.1 Bluetooth: Config

**File:** `m1_csrc/m1_bt.c` → `bluetooth_config()`  
**Effort:** Low–Medium (2–3 days)  
**Value:** Medium — custom BLE advertisement payloads enable proximity spoofing
attacks (Apple "Find My" spam, Samsung Galaxy pairing spam, Microsoft Swift Pair).

#### Recommended sub-features

| Sub-feature | Description |
|-------------|-------------|
| **Advertisement Name** | Set the BLE device name broadcast in advertisements |
| **Advertisement Payload** | Select from preset attack payloads (Apple, Samsung, Microsoft, custom) |
| **Advertisement Interval** | Set interval in ms (faster = more aggressive spam) |
| **Saved Payloads** | Browse / load custom payload files from SD card |

#### What to leverage
| Source | What to take |
|--------|-------------|
| `m1_csrc/m1_bt.c` | Existing advertise function — extend with payload parameter |
| ESP32-C6 SPI-AT | `AT+BLEADVPARAM`, `AT+BLEADVDATA` commands |
| [ECTO-1A/AppleJuice](https://github.com/ECTO-1A/AppleJuice) | Apple proximity advertisement payload bytes |
| [Flipper Zero `ble_spam`](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/external/ble_spam) | Multi-vendor payload library and cycling logic |
| [Willy-JL/Flipper-Zero-Unleashed](https://github.com/Willy-JL/Flipper-Zero-Unleashed) | Extended BLE spam payloads |

---

### 3.2 WiFi: Config

**File:** `m1_csrc/m1_wifi.c` → `wifi_config()`  
**Effort:** Low–Medium (2–3 days)  
**Value:** Medium — joining a network enables HTTP-based attacks, captive portal
testing, and OTA firmware updates for the ESP32.

#### Recommended sub-features

| Sub-feature | Description |
|-------------|-------------|
| **Join Network** | Select from scan results, enter password via virtual keyboard, connect |
| **Saved Networks** | Store credentials on SD card (encrypted), auto-connect toggle, delete |
| **Connection Status** | Show connection state, SSID, IP (when available), RSSI, disconnect |
| **Forget Network** | Remove saved credentials |

#### What to leverage
| Source | What to take |
|--------|-------------|
| `m1_csrc/m1_wifi.c` | Existing scan pipeline — reuse SSID list for network selection |
| `m1_csrc/m1_virtual_kb.c` | Password entry |
| ESP32-C6 SPI-AT | `AT+CWJAP` (join), `AT+CWQAP` (disconnect), `AT+CIPSTA` (IP info) |
| [Flipper Zero `wifi_marauder`](https://github.com/0xchocolate/flipperzero-wifi-marauder) | Reference for ESP32 AT command orchestration from a Flipper app |

#### Security note
Saved WiFi credentials are stored encrypted on SD card with AES-256-CBC and a
device-unique key derived from MCU UID (`HAL_GetUID`).

---

## Phase 4 — Future / Advanced Features

These items go beyond completing existing stubs and add new pen-testing
capabilities. They are listed here for planning purposes and should each become
their own GitHub issue before implementation begins.

| Feature | Effort | Value | Notes |
|---------|--------|-------|-------|
| **Sub-GHz RAW Record/Replay** | Medium | High | Capture raw OOK pulse timings without protocol decode; replay bit-for-bit. Flipper Zero `.sub` RAW format is a good target. |
| **Sub-GHz Rolling Code Analysis** | High | High | De Bruijn sequence generation for brute-force; KeeLoq decode reference in Flipper Zero `subghz/protocols/keeloq.c` |
| **NFC Mifare Classic Nested Attack** | High | High | Offline nested authentication attack (mfoc algorithm); requires precise timing — may need FPGA or dedicated NFC IC |
| **NFC Emulation (full)** | High | High | Emulate Mifare Classic with arbitrary sector data; ST RFAL supports card emulation mode |
| **WiFi Deauth / Probe Capture** | Medium | High | Requires ESP32-C6 monitor mode; use [ESP32 WiFi Penetration Tool](https://github.com/risinek/esp32-wifi-penetration-tool) firmware on the co-processor |
| **BLE GATT Scanner** | Medium | Medium | Enumerate GATT services/characteristics on discovered BLE devices; reference [Flipper Zero BLE scanner](https://github.com/flipperdevices/flipperzero-firmware/tree/dev/applications/main/bt) |
| **IR Brute-force** | Low | Medium | Iterate through all codes for a given protocol (e.g. NEC address 0x00–0xFF); useful for unknown remotes |
| **Infrared Analyzer** | Low | Medium | Display decoded protocol/address/command in real time without saving; already 90 % done in Learn New Remote |
| **USB HID Emulation** | High | High | Emulate keyboard/mouse over USB; inject keystrokes (Rubber Ducky style); STM32H5 USB HID class support |
| **SD Card File Manager** | Low | Low | Full file manager (copy, move, delete, rename) beyond the existing file browser |

---

## Open-Source Reference Index

A consolidated list of all external repositories referenced in this roadmap.

| Repository | License | Used for |
|------------|---------|---------|
| [flipperdevices/flipperzero-firmware](https://github.com/flipperdevices/flipperzero-firmware) | GPL-3.0 | Sub-GHz, NFC, LF RFID, BLE patterns and algorithms |
| [Lucaslhm/Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB) | MIT | IR command database |
| [ECTO-1A/AppleJuice](https://github.com/ECTO-1A/AppleJuice) | MIT | Apple BLE proximity advertisement payloads |
| [Willy-JL/Flipper-Zero-Unleashed](https://github.com/Willy-JL/Flipper-Zero-Unleashed) | GPL-3.0 | Extended BLE spam payloads |
| [0xchocolate/flipperzero-wifi-marauder](https://github.com/0xchocolate/flipperzero-wifi-marauder) | GPL-3.0 | ESP32 AT command orchestration reference |
| [risinek/esp32-wifi-penetration-tool](https://github.com/risinek/esp32-wifi-penetration-tool) | MIT | ESP32 WiFi monitor mode / deauth firmware |
| [nfc-tools/libnfc](https://github.com/nfc-tools/libnfc) | LGPL-3.0 | NFC protocol reference |
| [nfc-tools/mfoc](https://github.com/nfc-tools/mfoc) | GPL-2.0 | Mifare Classic offline nested attack algorithm |
| [RfidResearchGroup/proxmark3](https://github.com/RfidResearchGroup/proxmark3) | GPL-3.0 | LF/HF RFID attack algorithm reference |
| [josevcm/nfc-laboratory](https://github.com/josevcm/nfc-laboratory) | GPL-3.0 | NFC protocol analysis reference |

> **License note:** When porting code from GPL-licensed repositories, the resulting
> firmware must also be distributed under a GPL-compatible license. The M1 firmware
> is already licensed under GPL (see `COPYING.txt`). Always verify the license of
> any code before porting.

---

## GitHub Issues to Create

Each phase item below should become a tracked GitHub issue before work begins.
Use these as the issue titles:

- [ ] `feat(gpio): implement USB–UART bridge` *(Phase 1.1)*
- [ ] `feat(subghz): implement Radio Settings screen` *(Phase 1.2)*
- [ ] `feat(settings): implement LCD & Notifications settings` *(Phase 1.3)*
- [ ] `feat(settings): implement System settings` *(Phase 1.4)*
- [ ] `feat(rfid): implement 125 kHz Utilities (T5577 raw, brute-force, clone)` *(Phase 2.1)*
- [ ] `feat(nfc): implement NFC Tools (dump, NDEF, Mifare Classic dict attack)` *(Phase 2.2)*
- [ ] `feat(bt): implement Bluetooth Config (advertisement payload editor)` *(Phase 3.1)*
- [x] `feat(wifi): implement WiFi Config (join network, saved credentials)` *(Phase 3.2)*
