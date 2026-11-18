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
| ESP32 | `Esp_spi_at/`, `Esp32_serial_flasher/`, `m1_csrc/m1_esp32*`, `m1_csrc/m1_esp_feature_gate.c/.h` | Hosted SPI communication, ESP32 update, UART console session, and the live firmware feature gate (see "ESP32 co-processor interface") |
| USB | `USB/`, `m1_csrc/m1_usb_cdc_msc.c` | CDC console/bridge and MSC storage |
| Storage | `FatFs/`, `m1_csrc/m1_sdcard*`, `m1_csrc/m1_storage.c` | SD-card detection, mounting, file operations |
| Crypto + HW RNG | `m1_csrc/m1_crypto.c`, `m1_csrc/m1_rng.c` | Soft AES-256/CBC/CMAC/UID-KDF primitives and the mandatory hardware RNG wrapper (see "Crypto and hardware RNG") |
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

File format (version 2):

- first line `#ver=2`;
- one `key=value` line per field, CRLF endings, keys in the module's field
  table (`brightness_pct`, `screen_timeout_s`, `led_notifications`,
  `device_name`, `subghz_*`, `bt_*`);
- final line `crc=XXXXXXXX`: CRC-32C (Castagnoli, polynomial 0x1EDC6F41,
  the shared `m1_crc32c.c` helper) over every byte preceding that line.

A v1 file written by pre-Radio-Settings firmware still loads: its
serialized `subghz_power_dbm=0` was the then-inert default rather than a
user choice, so the load migrates it to the +20 dBm default (stock TX
power); every other field is kept, and the next save rewrites the file as
v2 — under which an explicit 0 dBm choice is preserved verbatim.

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

Sub-GHz Radio Settings (PR-7, issue #3) consumes the store's `subghz_*`
fields and copies the same modal idiom (screen in
`m1_csrc/m1_sub_ghz.c`, decision logic in its host-tested
`M1_SUBGHZ_RADIO_SETTINGS_PURE_CORE` region):

- *Parameter model*: modulation `Auto/OOK/2-FSK/2-GFSK/4-FSK/4-GFSK`,
  RX bandwidth `Auto/10/100/200 kHz` (the three WDS-generated sets the
  repo's `Sub_Ghz/radio_config/` headers actually contain; nominal
  10.75/109.93/219.87 kHz as generated), FSK deviation
  `Auto/2/20/47/200 kHz` (Auto = the band family's WDS FSK value), power
  `-10..+20 dBm` nominal (default +20 ≡ the stock `PA_PWR_LVL 0x7F`),
  frequency override = an exact-Hz entry of the Record-RAW list or Off.
  Every default is behavior-identical to pre-PR-7 radio operation.
- *Where the override lives*: `SI446x_Apply_Modem_Config()`
  (`Sub_Ghz/m1_sub_ghz_api.c`) patches the SI4463 property API
  (`MODEM_MOD_TYPE`, `MODEM_FREQ_DEV`, the decimation/AFC/spike/DSA/CHFLT
  bandwidth set) on top of the just-loaded WDS base configuration; the
  dBm→PA map and all encodings are documented at that implementation and
  provenance-bound to the WDS headers by the host suite
  (`m1_csrc/test/m1_subghz_radio_settings_test.py`).
- *Apply hook*: `sub_ghz_apply_user_radio_overrides()` runs in
  `subfunc_handler_task` (never ISR), invoked by both opmode setters —
  the single funnel of Record/Replay/Scan/Frequency Reader — after every
  preset load/tune and immediately before the radio enters RX/TX. An
  active frequency override replaces the tune target
  (`radio_tune_exact_hz()`); the region gate
  (`sub_ghz_fcc_ism_band_check_hz()`) is enforced at save time in the
  screen and re-enforced at apply time in the resolver, and the TX
  boundary keeps its own region check on the effective frequency.
  Power values are nominal: RF verification on a meter/receiver is the
  hardware-acceptance follow-up.

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
- loads are tolerant (missing/unreadable file, blank, corrupt, over-long
  and mis-shaped lines are skipped; a stored line must be a `0:/IR/….ir`
  device path, so a rewrite torn by card-full or card removal never
  enters the workspace and the next bounded rewrite drops it; an
  unreadable store renders an empty list), and the rewrite is bounded to
  the cap — no unbounded memory or file growth;
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

## Crypto and hardware RNG

Credential-protection primitives (issue #42, landed PR-8 before any
consumer) are split into a pure core and one hardware-coupled wrapper:

- `m1_csrc/m1_crypto.c/.h` — software AES-256 block/CBC (FIPS-197 /
  SP 800-38A), AES-CMAC (RFC 4493 / SP 800-38B), a UID-based key
  derivation (SP 800-108 KDF in CMAC mode, domain-separated for
  WiFi credentials), constant-time tag compare, and
  `m1_secure_zero()` (volatile stores + compiler barrier). No HAL,
  FreeRTOS, or allocation: the host suite compiles the real sources
  against NIST/RFC/CAVP known-answer vectors.
- `m1_csrc/m1_rng.c/.h` — the mandatory STM32H5 hardware RNG wrapper.
  Kernel clock HSI48, selected in `HAL_RNG_MspInit()`
  (`Core/Src/stm32h5xx_hal_msp.c`); initialized once from
  `m1_system_init_task()` after SD/settings and before
  `m1_tasks_init()`, with one generate probe.

- *Ownership*: single-consumer by design — the WiFi credential path
  (the only planned user) runs in the UI task; the wrapper is not
  ISR-safe and callers serialize access.
- *Failure semantics*: any RNG init or generate failure latches
  permanently; every later `m1_rng_init()`/`m1_rng_bytes()` call
  refuses (partial output zeroized).
  There is no time-based or pseudo-random fallback anywhere in the
  tree (host suite pins the per-file counts of the decorative
  `rand()`/`srand()` sites — file name suffixes and synthetic NFC
  UIDs — which never touch keys or IVs).
- *Threat model*: the UID-derived key hides credentials from
  removed-SD-card casual reading only; see
  [security](security.md#wi-fi-credential-crypto-and-hardware-rng)
  for the honest limits (no secure element, no TLS, no SPI-link
  encryption, no side-channel hardening).

## ESP32 co-processor interface

The STM32 talks to the ESP32-C6 co-processor over two links, and the
distinction matters for capability decisions:

- *ESP32 UART console* — the session every shipped Wi-Fi/BT/capture
  tool uses (`m1_csrc/m1_esp_uart_transport.c`: `esp32_uart_write()`
  plus `esp32_uart_read_complete_prompt()` against the `">> "` prompt;
  commands like `scan -a`, `list -s`, `mode -b`, `stop`). The console
  firmware itself is maintained outside this repository.
- *Persisted version record* — `m1_csrc/m1_esp_version.c` keeps "the
  version the STM32 last installed on the ESP32" in RTC/TAMP backup
  registers DR2/DR3 (VBAT domain) for the Web Manager's flash-skip
  decision. The vendor SPI-AT master sources under `Esp_spi_at/`
  (compiled TUs incl. `ctrl_api.h`'s control enums) exist in-tree but
  no shipped application feature issues SPI control requests.

*Feature gating* (issue #42/#11 prerequisite, PR-10): an ESP32-side
feature (Wi-Fi join handler, BLE advertise command) may only run when
the **live** ESP32 firmware answers for itself.
`m1_csrc/m1_esp_feature_gate.c/.h` performs the live handshake at
connect time — a bounded `"\r\n"` prompt ping, then the read-only
`version` console verb, with the reply marker
`ESPFW <major>.<minor>.<patch>.<rc>` as the shared contract with the
external console firmware (until an image answers, the gate honestly
reports ABSENT, which is a refusal, not a fault) — and normalizes the
answer into four states against a per-feature minimum table:
ABSENT (no live answer), OLDER, EQUIVALENT, NEWER. Only EQUIVALENT
and NEWER may execute a gated feature (`esp_feature_gate_allows()`).
The DR2/DR3 record is *never* trusted for a capability decision
(VBAT-dependent and blind to external re-flashes); it is surfaced in
the query result as diagnostics only (`cache_disagrees` makes a stale
record visible), and the gate never writes it. Task-context only,
caller owns the ESP32 session, no dynamic allocation.

*Wi-Fi join* (issue #42 completion, PR-11) rides the same UART
console session: the verb `join "<ssid>" "<psk>"` + CRLF (empty SSID
= hidden network, empty PSK = open), reply read to the `">> "` prompt
and classified by **line-anchored** markers — a line beginning
`JOINED` (any suffix, e.g. an IP, ignored) means associated, a line
beginning `JOIN_FAIL` means refused, anything else is an honest
timeout. Markers count only at a line start (buffer start or after
`\n`): the console echoes the typed command, and a grammar-legal SSID
may itself contain a marker substring, so a mid-line occurrence —
notably the echo — never classifies. The grammar is enforced on the
STM32 before anything is sent (SSID 0..32 / PSK 8..63
printable-no-quote chars, or the exact 64-hex PMK form), and the
transport is gated through the live handshake above — the join
minimum is pinned to `1.1.0.0`, exactly the version the paired
in-repo ESP application source reports
(`ESP32C6_FW_VERSION_LINE` in `Esp_spi_at/.../esp_at_list.h`), so an
older or non-answering ESP32 shows the gate diagnostic and no join
command is built or sent (the flash-gate: until the paired image is
flashed, the feature refuses cleanly). The in-repo SPI-AT example
carries the matching handler `wifi_connect_ap()`
(`CTRL_REQ_CONNECT_AP` → `AT+CWJAP` → `CTRL_RESP_CONNECT_AP` with
mapped `CTRL_ERR_*` codes, PSK wiped from its command buffer after
the exchange); `m1_csrc/test/m1_wifi_join_test.py` pins the two
version constants together so they cannot drift. The plaintext PSK
lifecycle (editor buffer, decrypted buffer, static command buffer,
ESP-side command) and the accepted link exposure are documented in
[security/wifi-credential-threat-model.md](security/wifi-credential-threat-model.md).
Reconnect policy: exactly one attempt per boot at first Wi-Fi-menu
entry, first credentialed row in store order, re-verified and
re-decrypted at join time, 15 s budget.

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
(`m1_csrc/m1_wifi.c`, versioned CSV with an optional per-network encrypted
credential column — layout, integrity mechanism, failure/recovery behavior,
and writer ownership documented in
[security/wifi-credential-threat-model.md](security/wifi-credential-threat-model.md)).
New persistent formats must document
their file layout, integrity mechanism, failure/recovery behavior, and
writer ownership.
