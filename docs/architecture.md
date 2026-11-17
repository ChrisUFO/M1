# M1 firmware architecture

## Scope

This page describes the firmware structure visible in the repository. It is a
maintainer guide, not a complete product requirements document.

## System context

The M1 is an STM32H573-based handheld multi-protocol device. The STM32 firmware
owns the user interface, storage, power monitoring, NFC, LF RFID, Sub-GHz,
infrared, USB, and communication with an ESP32-C6 co-processor used for Wi-Fi
and Bluetooth functions.

```text
Buttons ─┐
         v
      UI/menu ──────────────┬── NFC / ST25R3916
         │                  ├── LF RFID
         │                  ├── Sub-GHz / Si4463
         │                  ├── Infrared
         │                  ├── ESP32-C6 / Wi-Fi / BLE
         │                  └── External GPIO
         v
 Display and status         Storage / SD card
         │                         │
         └──────── STM32H573 ──────┴── USB CDC / MSC
                      │
             Battery / charger / fuel gauge
```

## Source boundaries

| Area | Primary location | Responsibility |
| --- | --- | --- |
| Platform startup | `Core/` | HAL initialization, interrupts, CMSIS-RTOS startup |
| Product application | `m1_csrc/` | UI, system state, storage, update, power, connectivity |
| Battery | `Battery/`, `m1_csrc/m1_bq*.c` | Battery data, charger, fuel gauge |
| NFC | `NFC/`, `m1_csrc/m1_nfc.c` | ST RFAL integration, card operations, storage |
| LF RFID | `lfrfid/`, `m1_csrc/m1_rfid.c` | Protocol decode/emulation/write and UI |
| Sub-GHz | `Sub_Ghz/`, `m1_csrc/m1_sub_ghz.c` | Radio control, capture, decode, replay |
| Infrared | `Infrared/`, `m1_csrc/m1_infrared.c` | IR receive, decode, transmit, remote data |
| ESP32 | `Esp_spi_at/`, `Esp32_serial_flasher/`, `m1_csrc/m1_esp32*` | Hosted SPI communication and ESP32 update |
| USB | `USB/`, `m1_csrc/m1_usb_cdc_msc.c` | CDC console/bridge and MSC storage |
| Storage | `FatFs/`, `m1_csrc/m1_sdcard*`, `m1_csrc/m1_storage.c` | SD-card detection, mounting, file operations |
| Third-party platform | `Drivers/`, `Middlewares/` | STM32 HAL, CMSIS, FreeRTOS, display libraries |

## Startup and runtime model

`Core/Src/app_freertos.c` creates the initial CMSIS-RTOS threads. System
initialization then creates product tasks through `m1_system_init_task()` and
`m1_tasks_init()`.

The statically visible product tasks include:

| Task | Created in | Purpose | Priority evidence |
| --- | --- | --- | --- |
| `m1_system_init_task` | `m1_sys_init.c` | Ordered product initialization | Highest product priority |
| `system_periodic_task` | `m1_tasks.c` | Periodic system and input handling | Normal; timing-sensitive for Sub-GHz capture |
| `sdcard_detection_task` | `m1_tasks.c` | Card insertion/removal handling | Idle + 10 |
| `menu_main_handler_task` | `m1_tasks.c` | Main menu events | Idle + 8 |
| `subfunc_handler_task` | `m1_tasks.c` | Active feature/subfunction execution | Normal |
| `log_db_handler_task` | `m1_tasks.c` | Buffered logging | Idle + 3 |
| `idle_handler_task` | `m1_tasks.c` | Product idle processing | Idle + 1 |
| `m1_runonce_task` | `m1_tasks.c` | One-time deferred work | Idle + 1 |
| `m1_ser2usb_task` | `m1_tasks.c` | Serial-to-USB transfer | Idle + 1 |
| CLI task | `app_freertos.c`, `m1_cli.c` | Command console | Idle + 5 |
| NFC worker | `m1_nfc.c` | NFC worker operations | Defined by NFC initialization |
| LF RFID workers | `lfrfid/lfrfid.c` | LF RFID control and receive | System-init priority |
| ESP32 SPI control | ESP SPI implementation | ESP32 transactions | Idle + 10 |

Tasks may be created conditionally or during subsystem initialization. Treat
this table as a code-derived inventory, not a timing guarantee.

## Event flow and shared resources

- `main_q_hdl` is the central application-event queue. It holds up to 256
  `S_M1_Main_Q_t` items.
- `button_events_q_hdl` carries button state and is consumed by menu and feature
  handlers.
- `sdcard_det_q_hdl` carries SD-card detection events.
- NFC and LF RFID have subsystem worker queues.
- Logging uses `log_q_hdl` plus a write mutex.
- I2C access is serialized by `mutex_i2c_trans`.
- shared RF SPI access is serialized by `mutex_rf_spi_trans`.
- NFC context access uses `s_ctx_mtx`.
- ESP32 and USB paths use additional semaphores/queues.

### Concurrency invariant

Peripheral access must go through the owning subsystem or its synchronization
wrapper. Direct HAL access from multiple tasks is unsafe unless explicitly
proven otherwise.

**Needs validation:** NFC SPI ownership is unresolved. The engineering log says
the unprotected path can be accessed concurrently, but replacing it with the
current mutex wrapper can deadlock. Resolve the lock hierarchy before declaring
the NFC path thread-safe.

## Storage model

FatFs provides the filesystem layer. The default drive path is `0:/`. Storage
supports application data, captured protocol data, and firmware images. USB MSC
also exposes storage, which means firmware and USB code must coordinate media
ownership and card-change events.

Canonical firmware-owned folders (`m1_sdcard_provision.h` is the single source
of truth for their paths) are provisioned at mount time: `0:/nfc`,
`0:/nfc/system`, `0:/nfc/recover`, `0:/rfid`, `0:/subghz`, `0:/wifi`,
`0:/ble`, `0:/infrared`, `0:/infrared/db`. `0:/System` is also canonical but
created lazily by its first user (the settings store creates it on first save)
so a fresh card without user changes does not grow an empty folder.

### User settings (`0:/System/settings.cfg`)

Durable user settings live in `0:/System/settings.cfg`
(`m1_csrc/m1_settings_store.c/.h`) — never in flash: the boot-signed metadata
span (`S_M1_FW_CONFIG_t`, sector 127) is off-limits to application settings.

File format (version 1):

- first line `#ver=1`;
- one `key=value` line per field, CRLF endings, keys in the module's field
  table (`brightness_pct`, `screen_timeout_s`, `led_notifications`,
  `device_name`, `subghz_*`, `bt_*`);
- final line `crc=XXXXXXXX`: CRC-32C (Castagnoli, polynomial 0x1EDC6F41,
  the shared `m1_crc32c.c` helper) over every byte preceding that line.

Semantics:

- *Load* (once per boot, after SD init and before task creation): missing
  file → compiled defaults; CRC mismatch, truncation, unsupported version,
  or a file larger than the loader's 1 KiB bound → compiled defaults, one
  log line, and the retained `.bak` is left in place; unknown keys are
  skipped; out-of-range values clamp to field bounds; malformed numerals
  reset that field to its default. An interrupted save is reconciled at
  load: an orphaned `settings.tmp` is promoted only when `settings.cfg`
  is missing (the CRC gate then has the final word on it); when
  `settings.cfg` exists, the orphaned `.tmp` is discarded, and the `.bak`
  is restored only when neither cfg nor tmp survives.
- *Save*: write `settings.tmp` fully and close it, unlink the superseded
  `.bak`, rotate the previous `settings.cfg` to `settings.bak`, then
  rename `settings.tmp` to `settings.cfg`. Any failure leaves the
  previous good `settings.cfg` in place (or restores it), so a save can
  never destroy the newest good copy. While it exists, `.bak` is the
  last known-good copy -- but it is unlinked during every save attempt
  (FatFs `f_rename` cannot overwrite an existing destination), so a
  failed rotation destroys the older backup while `settings.cfg`
  survives. Saving is refused while the boot load could not account for
  the card (SD error, or a corrupt/unsupported-version file): the RAM
  image is then compiled defaults, and writing it could clobber a live
  `settings.cfg` after a late card insertion -- the store loads exactly
  once per boot, so a reboot with the card readable re-enables saving.
- *Ownership*: the UI task is the single writer (setters, save, reset);
  any task may read via `settings_get()`. Boot-time consumers run after
  `settings_init()` in `m1_system_init_task()`. SD-absent is safe: defaults,
  no retries, no error spam.

**Needs validation:** Document the remaining SD-card directory layout details,
filename limits, concurrent USB/device access policy, and recovery behavior
after removal or I/O failure.

## Firmware update

The STM32 uses dual 1 MiB flash banks and option-byte bank swapping. Update code
validates metadata and CRC, writes the inactive bank, and can request rollback.
Every boot additionally validates the running bank against the fixed-span CRC the
build embedded in it, keeps failure/boot-attempt and swap counters in the
backup registers, and shows a `FW CORRUPT!` diagnostic banner (not a lock-out —
the unvalidated image still runs) when the image does not validate.
USB DFU entry exists on three routes — the PE11 hardware strap sampled before
HAL init, a persisted software request (`DEV_OP_STATUS_USB_DFU_REQUEST`,
honoured once and cleared before the jump), and a bounded 5 s offer shown on the
same recovery banner (edge-triggered OK acceptance, so a stuck key falls
through) — all ending in the same non-returning ROM jump with its
interrupt/SysTick/NVIC/VTOR/cache teardown (every flag wait iteration-capped,
clock tree back to the HSI reset state with a verified post-condition, thread
stack switched from PSP to MSP with the ROM initial SP; a clock teardown or
watchdog-sync failure resets instead of jumping) and a 32.768 s
watchdog safety net (LSI/256, reload 4095). None of these paths writes option
bytes.
See [Firmware image and update](protocols/firmware-update.md).

## Architectural risks

1. Shared-bus ownership is not uniformly expressed at API boundaries.
2. Several UI functions consume or reset global queues directly.
3. Runtime behavior is distributed across large feature source files.
4. SD-card access spans device storage, firmware update, and USB MSC.
5. ESP32 firmware and STM32 firmware compatibility is not versioned in one place.
6. Task priority requirements are encoded in comments rather than verified tests.

## Change checklist

Update this page when a change adds a task, queue, shared peripheral, subsystem,
startup dependency, persistent format, or inter-processor interface.

Persistent formats documented here: `0:/System/settings.cfg` (user settings,
see "Storage model"); Wi-Fi saved networks (`m1_csrc/m1_wifi.c`, versioned CSV).
New persistent formats must document their file layout, integrity mechanism,
failure/recovery behavior, and writer ownership.
