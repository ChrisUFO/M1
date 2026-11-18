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
created lazily by its first user (the settings store creates it on first
save; the IR browser's favorites/recent stores below do the same on their
first write) so a fresh card without user changes does not grow an empty
folder.

User-side content lives outside those folders and is never provisioned; the
IR database browser reads the Flipper IRDB layout under `0:/IR`
(see [IR database provisioning](ir-provisioning.md) and the Infrared
subsystem section below).

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
  or a file larger than the loader's 1 KiB bound makes `settings.cfg`
  unreadable → the same bounded read + CRC + parse runs on `settings.bak`:
  a validating backup is adopted (values loaded, saving re-enabled, one
  log line, both files left in place — the next save heals
  `settings.cfg` without unlinking the backup until the heal succeeds),
  otherwise compiled defaults, one log line, and the retained `.bak` is
  left in place; unknown keys are skipped; out-of-range values clamp to
  field bounds; malformed numerals or string values containing control
  characters reset that field to its default, and one additional log
  line names how many fields were reset (the values silently revert and
  the next save makes that permanent). An interrupted save is
  reconciled at
  load: an orphaned `settings.tmp` is promoted only when `settings.cfg`
  is missing (the CRC gate then has the final word on it); when
  `settings.cfg` exists and passes a CRC probe, the orphaned `.tmp` is
  discarded — but when the cfg fails the probe beside a *validating*
  `.tmp` (e.g. a heal save interrupted after its tmp write), the `.tmp`
  is promoted over the corrupt cfg — and the `.bak`
  is restored only when neither cfg nor tmp survives.
- *Save*: write `settings.tmp` fully and close it, unlink the superseded
  `.bak`, rotate the previous `settings.cfg` to `settings.bak`, then
  rename `settings.tmp` to `settings.cfg`. Any failure leaves the
  previous good `settings.cfg` in place (or restores it), so a save can
  never destroy the newest good copy. While it exists, `.bak` is the
  last known-good copy -- but it is unlinked during every save attempt
  (FatFs `f_rename` cannot overwrite an existing destination), so a
  failed rotation destroys the older backup while `settings.cfg`
  survives. The first save after a `.bak` recovery is the exception:
  `settings.cfg` is then the known-corrupt file and `.bak` the *only*
  good copy, so it is never unlinked -- the corrupt cfg is displaced
  directly and the tmp renamed over it, keeping `.bak` untouched until
  the next save's rotation. Every failure path in that heal mode
  leaves either the validating `.bak` or the complete promotable
  `settings.tmp` on the card (a failed promotion deliberately retains
  the tmp; the next boot's reconciliation promotes it), so a retry or
  a reboot always recovers instead of dead-ending the store. Saving is
  refused while the boot load could not account for
  the card's content (SD error, or `settings.cfg` and `.bak` both
  unreadable): the RAM image is then compiled defaults, and writing it
  could clobber a live `settings.cfg` after a late card insertion --
  the store loads exactly
  once per boot. A reboot with the card readable re-enables saving
  whenever that load then accounts for the card (valid file, corrupt
  file recovered from `.bak`, or a provably file-less card); when both
  files stay unreadable, rebooting re-reads the same corrupt pair, so
  the unreadable file must be removed or replaced before saving works
  again.
- *Ownership*: the UI task is the single writer (setters, save, reset);
  any task may read via `settings_get()`. Boot-time consumers run after
  `settings_init()` in `m1_system_init_task()`. SD-absent is safe: defaults,
  no retries, no error spam.

Consumers (PR-2 screens and their apply points):

- *LCD brightness* (`brightness_pct`): applied at boot after
  `m1_tasks_init()` (the feedback manager's init would clobber an earlier
  apply) via `fb_set_brightness_pct()`; edited live in Settings > LCD &
  Notifications with immediate preview. Default 100 is behavior-identical.
- *Screen timeout* (`screen_timeout_s`): `lcd_saver_update()`
  (m1_system.c) derives its inactivity window from the stored value at
  every periodic check — `0` means Off (the display never sleeps; a
  session already asleep when Off takes effect wakes on the next check),
  otherwise 10–120 s apply without a reboot. The compile-time
  `LCD_SAVER_PERIOD` constant is gone; the compiled default (45 s) lives
  only in the store's defaults table.
- *LED notifications* (`led_notifications`): applied at boot alongside
  brightness via `fb_set_led_notifications_enabled()`. The gate
  suppresses only the LED legs of the notification-class composite
  alerts (`fb_alert_success()`/`fb_alert_notify()` in
  m1_feedback_orchestration.c). Never gated: error alerts, power/boot/
  battery lifecycle cues, radio/USB/payload state indications, and
  firmware-update feedback (critical or state feedback, not user
  notifications). Sound keeps its own Sound gate; backlight behavior is
  unaffected. Default (on) is behavior-identical.
- *Device name* (`device_name`): edited via the virtual keyboard in
  Settings > System (bounded by the store's 32-char field; the keyboard
  editor itself caps entry at 20), shown in the System screen and on the
  About page.
- *Reset to defaults* (Settings > System): restores the compiled
  defaults in RAM, re-applies the live feedback settings, persists the
  defaults over `settings.cfg` through the normal atomic save (this is
  the entire "clear the core config" story — the boot-signed flash
  metadata sector is never read or written), then reboots via
  `power_reboot()`.

Settings screens share one modal idiom (documented at the top of
`m1_csrc/m1_settings.c`): UP/DOWN select a row, LEFT/RIGHT move the
value toward its minimum/maximum, OK activates a row action, BACK exits
through one atomic `settings_save()` for the whole session, skipped
entirely when the session edited nothing (a refused save of real edits
is displayed, not hidden). Modal button loops check the button-event
receive result and never act on an empty receive (same guard as the
virtual keyboard), so a stale or uninitialized event struct cannot
trigger an action. Future settings screens (Sub-GHz radio,
Bluetooth) copy this pattern.

**Needs validation:** Document the remaining SD-card directory layout details,
filename limits, concurrent USB/device access policy, and recovery behavior
after removal or I/O failure.

### IR favorites and recent (`0:/System/ir_favorites.txt`, `ir_recent.txt`)

The IR file browser keeps two plain-text device lists (issue #43,
`Infrared/m1_ir_browser.c`; format and user behavior in
[IR provisioning](ir-provisioning.md)):

- one full device path per line, newest first (the legacy fork format);
- LRU caps enforced by rewriting the whole file on every change:
  20 favorites, 10 recent;
- loads are tolerant (missing/unreadable file, blank, corrupt and
  over-long lines are skipped; an unreadable store renders an empty
  list), and the rewrite is bounded to the cap — no unbounded memory or
  file growth;
- integrity mechanism: none beyond tolerance (plain user data, no CRC);
  deleting the files is the supported reset;
- writer ownership: the browser UI task only, between transmits inside
  its IR-session (the transmit path itself performs no store writes; the
  recent store is written when a frame completes, not when it starts);
  no other module reads or writes these files.

## Real-time clock

The RTC calendar is a shared peripheral with one mediated access path:
`m1_csrc/m1_rtc.c/.h` (issue #7). Pure date/time math (leap years,
days-in-month, 2000–2099 validation, weekday, field stepping,
formatting) lives in `m1_csrc/m1_rtc_date.c/.h`, free of HAL/FreeRTOS
includes so the host suite compiles the real sources.

- *Ownership*: the calendar is written only from the UI task (the
  Settings > Date & Time editor). The low-power/power paths own the
  RTC **wakeup timer**; the boot failsafe and ESP version record own
  backup registers DR0–DR3 (`S_M1_BK_REGS_t` / `m1_esp_version.c`).
  `m1_rtc` touches none of those and never re-initializes the backup
  domain (MX_RTC_Init already ran; HAL_RTC_Init reconfigures
  prescalers without clearing the calendar).
- *Clock source*: LSI, as the shipped CubeMX/RCC configuration
  selects. Switching to LSE would require a backup-domain reset that
  wipes DR0–DR3 (boot-failsafe state and the ESP version cache), so it
  is out of scope by constraint. LSI is a factory-trimmed RC (~32 kHz):
  expect minutes-per-day drift — a settable clock, not wall-clock
  accuracy.
- *Persistence semantics*: the battery-backed RTC **is** the timebase;
  there is no `settings.cfg` duplication (time is not a settings field,
  and Reset defaults leaves the clock alone). Reads use the mandatory
  GetTime-then-GetDate shadow-register order and return only validated
  values. With VBAT holding the backup domain, the calendar (and
  DR0–DR3) survive a power cycle — but LSI is a VDD-domain oscillator,
  so the unpowered interval is not counted: the clock resumes where it
  stopped. Full backup-domain loss (no/dead VBAT) resets the calendar;
  `m1_rtc_get_datetime()` then reports "not set" and the System screen
  shows "Time not set" until the user sets it.
- *Validation*: writes pass the pure validator first (year 2000–2099,
  month/day bounds leap-aware, 24-hour range) — impossible dates never
  reach the hardware; the editor's stepping keeps its edit copy
  possible by construction (day re-clamped across month/year steps).
  HAL_RTC_SetTime/SetDate own calendar init-mode entry/exit; seconds
  restart at 00 (minute granularity) and the true weekday is computed
  and stored.

## Infrared subsystem

The IR subsystem is deliberately single-owner (issue #43):

- *Ownership*: every IRMP/IRSND access — remote learning, saved-remote
  replay, the upstream curated universal remotes (`0:/infrared/db`),
  the `0:/IR` file browser, the cycle-all actions and the CLI transmit
  test — runs while holding the module-level busy guard
  `m1_ir_subsys_acquire()`/`m1_ir_subsys_release()`
  (`m1_csrc/m1_infrared.c/.h`). The UI paths execute in
  `subfunc_handler_task` and are serialized by the menu dispatcher; the guard
  extends that serialization to the CLI task, which reaches the transmit path
  through `m1_mtest`. A failed acquire means another IR function is active:
  callers show a busy notice and back out. Learn, playback and cycle-all can
  therefore never overlap.
- *Protocol map*: `ir_protocols_mapping_table` in
  `Infrared/m1_ir_remotes.c` is the one protocol map; `.ir` protocol names
  resolve through `ir_protocol_map_lookup()`. Unknown names follow the
  strict-skip policy — the block is skipped and counted, never transmitted
  as garbage. No fork aliasing map exists.
- *Data layer*: `Infrared/m1_ir_file.c/.h` is the streaming Flipper-`.ir`
  loader — an index pass per device (names/types/offsets, no command
  buffering, no per-file cap) and on-demand single-block parse for transmit
  with a stale-index name check. The browser (`Infrared/m1_ir_browser.c/.h`)
  is the UI-task consumer: bounded recursive `0:/IR` scan (convention and
  bounds in [IR provisioning](ir-provisioning.md)), 6-row paginated
  category/device/button lists, search, favorites/recent stores under
  `0:/System` (see the storage model above), and transmit through the same
  payload/encoder path as the universal remotes. It uses static workspaces
  only — no dynamic allocation in screen loops.
- *Cycle-all vs the default press* (issue #43): the universal-remote
  session `infrared_universal_all_remotes(remote_type, cycle_all)` is
  exposed for the explicit Infrared → Universal Remotes → Cycle all …
  submenu actions (one per category). Those run it with
  `cycle_all = 1`: the upstream behavior — after the first OK the session
  transmits through every device of the category database with its
  pause/resume (OK) and stop (BACK) UI and the device-count progress bar,
  re-acquiring the guard itself. The four remote entries run it with
  `cycle_all = 0`: every OK press transmits exactly one frame (the next
  device's code each press; END_OF_FILE wraps to the first device) — the
  default per-press behavior everywhere in this firmware, matching the
  browser's one-frame-per-button.

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
see "Storage model"); IR favorites/recent (`0:/System/ir_favorites.txt` /
`ir_recent.txt`, see "Storage model"); Wi-Fi saved networks
(`m1_csrc/m1_wifi.c`, versioned CSV). New persistent formats must document
their file layout, integrity mechanism, failure/recovery behavior, and
writer ownership.
