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
- **Display** (JHD12864-G386BTW, 128x64 pixels, SPI)
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

**Quick Build (Recommended):**
```bash
./build          # Build firmware and copy to distribution/
./build clean    # Clean build artifacts and rebuild
```

This script automatically:
- Configures the build (if needed)
- Compiles the firmware
- Appends the required CRC32 checksum
- Copies all output files to `distribution/`

**STM32CubeIDE:**
Open the project and build in the IDE. Output: `./Release/MonstaTek_M1_v0802-ChrisUFO.elf`

**Manual CMake build:**
```bash
cmake --preset gcc-14_2_build-release
cmake --build out/build/gcc-14_2_build-release
python tools/append_crc32.py out/build/gcc-14_2_build-release/MonstaTek_M1_v0802-ChrisUFO.bin
```

**Output files** (in `distribution/` or `out/build/gcc-14_2_build-release/`):

| File | Use |
|------|-----|
| `MonstaTek_M1_v0802-ChrisUFO.bin` | STM32 firmware (includes CRC32) |
| `MonstaTek_M1_v0802-ChrisUFO.hex` | STM32CubeProgrammer / JLink |
| `MonstaTek_M1_v0802-ChrisUFO.elf` | Debug sessions |

## Development & Debugging

### Flashing Firmware via ST-Link (Recommended for Development)

For rapid development and debugging, use an ST-Link programmer connected to the M1's GPIO header pins. This avoids repeatedly opening the case to disconnect the battery.

#### ST-Link Connection

Connect to the GPIO header (pins 1-18):

| ST-Link | M1 GPIO Pin | Function |
|---------|-------------|----------|
| VCC (3.3V) | Pin 9 (+3.3v) | Power |
| GND | Pin 8 or 18 (GND) | Ground |
| SWDIO | Pin 11 (PA13) | Data |
| SWCLK | Pin 10 (PA14) | Clock |

#### Quick Development Workflow

1. **Connect ST-Link** to GPIO pins
2. **Connect USB** for power and serial console
3. **Open serial terminal** (PuTTY/Tera Term) at **9600 baud** - keep open for logs
4. **Build firmware:**
   ```bash
   ./build
   ```
5. **Flash with STM32CubeProgrammer:**
   - Click **"Connect"**
   - Click **"Open File"** → Select `M1_v*.bin`
   - Set Address: `0x08000000`
   - Click **"Program"**
6. **Reset via ST-Link:**
   - Click **"Reset"** button in STM32CubeProgrammer
   - **OR** use CLI command `reboot` in serial terminal

**Pro tip:** Keep the serial terminal open during testing to see debug messages in real-time.

### Serial Console (USB CDC)

The M1 exposes a USB serial console for debugging and control.

**Connection Settings:**
- **Port:** COM3 (or whatever port the M1 appears as)
- **Baud Rate:** 9600
- **Data Bits:** 8
- **Stop Bits:** 1
- **Parity:** None
- **Flow Control:** None

**Terminal Programs:**
- **Windows:** PuTTY, Tera Term, or Arduino Serial Monitor
- **Command Line:** `screen /dev/ttyACM0 9600` (Linux/Mac)

### Serial Console Commands

With USB connected and a terminal at **9600 baud**, type `help` for available commands:

**System Commands:**
- `version` - Show detailed firmware version (e.g., "0.8.2.0-ChrisUFO")
- `status` - System status, active bank, and build info
- `reboot` - Software reset (no need to disconnect battery!)
- `memory` - Show RAM/Flash usage statistics
- `log` - Display recent debug log messages

**Hardware Status:**
- `sdcard` - SD card mount status and capacity
- `wifi` - ESP32 WiFi module status
- `battery` - Battery level and charging status

**Utility:**
- `cls` - Clear the terminal screen
- `mtest` - Multi-purpose test command
- `help` - List all available commands

**Example Session:**
```
cli> version
M1 Firmware Version:
  0.8.2.0-ChrisUFO

cli> status
System Status:
  Firmware: v0.8.2.0
  Active Bank: 1

cli> reboot
Rebooting...
```

### Troubleshooting with Serial Console

The serial console is invaluable for debugging firmware updates and other issues:

**Debugging Firmware Update Failures:**
1. Keep the serial terminal open at 9600 baud
2. Navigate to Settings → Firmware Update → Image file
3. Select a .bin file
4. Watch the debug output showing:
   - Filename validation
   - File size checks
   - CRC validation
   - Error messages if something fails

**Common Debug Messages:**
```
FW Update: File selected=1
Filename: 'M1_v0.8.2-ChrisUFO.bin' (len=22)
Dot found at index 18, ext len=4
Extension: '.bin'
File type OK
Full path: 0:/M1_v0.8.2-ChrisUFO.bin
File opened successfully
FW file size: 1047576 bytes
```

---

## Installing Firmware via SD Card

The M1 has two separate firmware components that can be updated:

### STM32 Firmware (Main Firmware)

This is the primary firmware that runs the M1 device.

**File:** `MonstaTek_M1_v0802-ChrisUFO.bin` (includes embedded CRC32 checksum)

1. Copy the `.bin` file to your SD card (any folder)
2. Insert the SD card into the M1
3. Navigate to **Settings → Firmware Update → Image file**
4. Browse to and select `MonstaTek_M1_v0802-ChrisUFO.bin`
5. Confirm — the device validates the embedded CRC32, flashes the firmware, and reboots automatically

**Note:** The build system automatically appends a 4-byte CRC32 checksum to the end of the `.bin` file for validation during the update process.

### STM32 ROM USB DFU Mode

You can also flash STM32 firmware over USB using the built-in STM32 ROM DFU bootloader.

On device:
- Open **Settings -> Firmware update -> USB DFU mode**
- Confirm entry (RIGHT/OK)

CLI shortcut:
```bash
dfu
```

On host (example with dfu-util):
```bash
dfu-util -a 0 -D MonstaTek_M1_v0802-ChrisUFO.bin
```

If `dfu-util` does not detect the device, use STM32CubeProgrammer in USB mode and verify USB cable/data support.

### ESP32 Firmware (WiFi/Bluetooth Module)

This firmware is for the ESP32-C6 wireless module and uses a different update mechanism.

**Files required:**
- `esp32_firmware.bin` (the ESP32 firmware image)
- `esp32_firmware.md5` (MD5 checksum file)

1. Copy both files to your SD card under `/0:/esp32/`
2. Navigate to **Settings → ESP32 Firmware Update**
3. Select the firmware file
4. The device validates the MD5 checksum before flashing

**Note:** The ESP32 firmware requires both `.bin` and `.md5` files with matching names.

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

**⚠️ Important: Temporary Workaround for Large Categories**

The M1 can currently display a maximum of **32 items per folder** due to memory 
constraints. Categories with many manufacturers (e.g., TV with 100+) will show
"IR Error: No Memory".

**Current workaround** - Group manufacturers by starting letter until pagination
is implemented (see [Issue #14](https://github.com/ChrisUFO/M1/issues/14)):

```
IR/
  TV/
    A/
      Apple/
    B/
      Bose/
    S/
      Samsung/
```

**Quick setup script:**

```bash
cd Flipper-IRDB/TV
mkdir -p {A..Z}
for dir in */; do
    first_char="${dir:0:1}"
    if [[ $first_char =~ [A-Z] ]]; then
        mv "$dir" "$first_char/"
    fi
done
```

> **Note:** This is a temporary workaround. [Issue #14](https://github.com/ChrisUFO/M1/issues/14) 
> tracks the implementation of pagination which will remove this limitation.

### Supported IR protocols

NEC, NECext, NEC42, NEC16, RC5, RC5X, RC6, RC6A, Samsung32, Samsung48,
SIRC, SIRC15, SIRC20, Kaseikyo, Denon, Sharp, JVC, Panasonic, LGAIR

## Menu Structure & Feature Status

This section documents every menu item in the firmware, its current implementation
status, and — for stubs — the estimated effort and pen-testing value of completing it.

### Legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Fully functional |
| ⚠️ | Stub / placeholder (shows "firmware update" screen or empty loop) |
| 🚫 | Disabled — code exists but item is **commented out** of the menu |

---

### 📡 Sub-GHz

| Menu Item | Status | Notes |
|-----------|--------|-------|
| Record | ✅ | Full pipeline: SI4463 capture → decode → save to SD card |
| Replay | ✅ | Browse SD card files, transmit saved signal |
| Frequency Reader | ✅ | Scans spectrum, shows strongest frequency |
| Regional Information | ✅ | Displays regional frequency band info |
| ~~Radio Settings~~ | 🚫 | Function exists but **not in menu** — only 4 items in submenu |

**Menu Structure:** Sub-GHz → {Record, Replay, Frequency Reader, Regional Information}

**Missing feature:** Radio Settings — Function exists but is not included in the current menu. Adding it would enable custom modulation for rolling-code attacks. *Low effort, High value*.

---

### 🔴 Infrared

| Menu Item | Status | Notes |
|-----------|--------|-------|
| Universal Remote | ✅ | Browse Flipper-IRDB `.ir` files on SD card, transmit commands (this fork) |
| Learn New Remote | ✅ | IRMP decode, displays protocol/address/command, saves to SD card |
| Saved Remotes | ✅ | Browse saved signals, replay last learned signal |

---

### 🔑 LF RFID (125 kHz)

| Menu Item | Status | Notes |
|-----------|--------|-------|
| Read | ✅ | EM4100 and H10301 decode |
| Saved | ✅ | Emulate, write to T5577, edit, rename, delete, info |
| Add Manually | ✅ | Enter card data manually |
| 125 kHz Utilities | ⚠️ | `rfid_125khz_utilities()` is an empty loop — no utility screens implemented |

**Stub effort/value:** 125 kHz Utilities — *Medium effort* (implement brute-force facility code scanner, raw read/write, T5577 config), *High value* (T5577 raw write and facility-code brute-force are core pen-testing primitives; Flipper Zero's `lfrfid` and `lfrfid_worker` are directly portable since the M1 already uses the same `lfrfid` library).

---

### 📶 NFC (13.56 MHz)

| Menu Item | Status | Notes |
|-----------|--------|-------|
| Read | ✅ | ISO14443A/B/F/V, Ultralight/NTAG, Mifare Classic |
| Saved | ✅ | Emulate, edit UID, rename, delete, info |
| NFC Tools | ⚠️ | `nfc_tools()` calls `m1_gui_let_update_fw()` — no tools implemented |

**Stub effort/value:** NFC Tools — *Medium effort* (implement card-info dump, NDEF read, Mifare Classic sector auth brute-force), *High value* (Mifare Classic dictionary attack and NDEF inspection are the most-requested NFC pen-testing features; Flipper Zero's `nfc` app and [nfc-laboratory](https://github.com/josevcm/nfc-laboratory) are good references; the M1 already has the ST RFAL stack).

---

### 📶 WiFi

| Menu Item | Status | Notes |
|-----------|--------|-------|
| Scan AP | ✅ | ESP32-C6 scan — shows SSID, BSSID, RSSI, channel, auth type |
| WiFi Config | ✅ | Join network, manual hidden SSID entry, encrypted saved credentials, status view, disconnect |

**Implemented:** WiFi Config now supports AP scan selection, manual SSID entry, password entry with show/hide toggle, encrypted credential storage, saved-network management (delete + auto-connect toggle), and status/disconnect flow.

---

### 🔵 Bluetooth

| Menu Item | Status | Notes |
|-----------|--------|-------|
| Scan | ✅ | BLE device scan, shows device name and RSSI |
| Advertise | ✅ | BLE advertisement broadcast |
| Bluetooth Config | ⚠️ | `bluetooth_config()` calls `m1_gui_let_update_fw()` — no config UI |

**Stub effort/value:** Bluetooth Config — *Low effort* (expose advertisement name/interval/payload settings), *Medium value* (custom advertisement payloads enable BLE spoofing attacks; Apple/Samsung proximity spam is a popular pen-testing demo).

---

### 🔌 GPIO

| Menu Item | Status | Notes |
|-----------|--------|-------|
| Manual Control | ✅ | Toggle individual GPIO pins |
| 3.3 V Power | ✅ | Enable/disable 3.3 V rail |
| 5 V Power | ✅ | Enable/disable 5 V rail |
| USB–UART Bridge | ⚠️ | `gpio_usb_uart_bridge()` calls `m1_gui_let_update_fw()` — no bridge UI |

**Stub effort/value:** USB–UART Bridge — *Low effort* (route USB CDC ↔ UART peripheral; HAL plumbing already exists in `m1_usb_cdc_msc.c`), *High value* (UART bridge is essential for serial console access to target devices during hardware pen-testing).

---

### ⚙️ Settings

| Menu Item | Status | Notes |
|-----------|--------|-------|
| Storage | ✅ | SD card management: About, Explore, Mount, Unmount, Format |
| Power | ✅ | Battery Info, Reboot, Power Off |
| LCD & Notifications | 🚫 | Menu entry commented out — placeholder only |
| System | 🚫 | Menu entry commented out — placeholder only |
| Firmware Update | ✅ | Browse SD card for `.bin`, flash via bank-swap updater, or enter ROM USB DFU mode |
| ESP32 Update | ✅ | ESP32-C6 WiFi/BT module firmware update via SD card |
| About | ✅ | Shows firmware version and device info |

**Menu Structure:** Settings → {Storage, Power, Firmware Update, ESP32 Update, About}

**Disabled item effort/value:**
- **LCD & Notifications** — *Low effort* (re-enable menu entry, implement brightness/contrast/notification LED controls), *Medium value* (quality-of-life; needed before shipping).
- **System** — *Low effort* (re-enable menu entry, implement device name, date/time, reset-to-defaults), *Medium value* (needed for a complete product experience).

---

### 🏆 Recommended Implementation Order

Based on pen-testing value and implementation effort, here is the suggested priority:

| Priority | Feature | Effort | Value | Rationale |
|----------|---------|--------|-------|-----------|
| 1 | **USB–UART Bridge** | Low | High | HAL already wired; one-screen UI; critical for hardware hacking |
| 2 | **Sub-GHz Radio Settings** | Low | High | Unlocks custom modulation for rolling-code research |
| 3 | **125 kHz Utilities** | Medium | High | T5577 raw write + facility-code brute-force; `lfrfid` lib already present |
| 4 | **NFC Tools** | Medium | High | Mifare Classic dict attack; ST RFAL stack already present |
| 5 | **Settings: LCD & Notifications** | Low | Medium | Re-enable + implement; needed for product completeness |
| 6 | **Settings: System** | Low | Medium | Re-enable + implement; needed for product completeness |
| 7 | **Bluetooth Config** | Low | Medium | BLE spoofing/spam payloads |
| 8 | **WiFi Config** | Low–Med | Medium | Network join via existing SPI-AT `AT+CWJAP` command |

### 🔗 Useful Open-Source References

| Feature | Reference |
|---------|-----------|
| LF RFID utilities / T5577 | [flipperdevices/flipperzero-firmware — lfrfid](https://github.com/flipperdevices/flipperzero-firmware/tree/dev/lib/lfrfid) |
| NFC / Mifare Classic attack | [flipperdevices/flipperzero-firmware — nfc](https://github.com/flipperdevices/flipperzero-firmware/tree/dev/applications/main/nfc) |
| Sub-GHz modulation / RAW | [flipperdevices/flipperzero-firmware — subghz](https://github.com/flipperdevices/flipperzero-firmware/tree/dev/applications/main/subghz) |
| BLE advertisement spam | [ECTO-1A/AppleJuice](https://github.com/ECTO-1A/AppleJuice) |
| NFC protocol analysis | [josevcm/nfc-laboratory](https://github.com/josevcm/nfc-laboratory) |
| IR database | [Lucaslhm/Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB) |

---

## Contributing

Contributions are welcome. See [CONTRIBUTING.md](.github/CONTRIBUTING.md) and the [Code of Conduct](.github/CODE_OF_CONDUCT.md).

To contribute to the upstream project, please open pull requests against
[Monstatek/M1](https://github.com/Monstatek/M1).

## License

See [LICENSE](LICENSE) for details.
