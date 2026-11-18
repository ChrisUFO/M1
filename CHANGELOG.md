<!-- See COPYING.txt for license details. -->

# Changelog

All notable changes to the M1 project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Fork: Wi-Fi join flow + ESP32 join handler (issue #42 completion,
  PR-11 of the Merge 3 series) — the user-facing join/reconnect
  experience over the PR-9 encrypted-credential store and the PR-10
  live feature gate, plus the ESP-side station-connect handler.
  - STM32 join (`m1_csrc/m1_wifi.c/.h`): a new Wi-Fi → Join Network
    leaf (saved-network picker + live-scan picker), a Join Network
    action in the Networks flow, and a Join action in the saved
    browser, all funneling into one gated transport: console verb
    `join "<ssid>" "<psk>"` + CRLF over the ESP32 UART session every
    shipped Wi-Fi tool uses, reply read to the `">> "` prompt and
    classified by markers (`JOINED…` / `JOIN_FAIL`; anything else is
    an honest timeout). Grammar is enforced before anything is sent:
    SSID 0..32 and PSK 8..63 printable-no-quote chars, or the exact
    64-hex PMK form — so a secret that cannot be transmitted safely
    (embedded quote/CR/LF) is refused, never mangled onto the wire.
  - PSK entry: PLAN named `m1_vkb_get_filename`, but that keyboard is
    filename-oriented (20-char cap, lowercase+digits+`_.` charset,
    plaintext field) and cannot take a WPA secret; the smallest
    password-style editor on the existing key/char UI ships instead —
    a masked rotor over the full 94-char printable-minus-quote
    alphabet (exactly the join-legal charset, host-test-pinned), with
    in-editor 8..64 validation, `<` delete / `>` save pseudo-cells,
    and BACK-cancel that zeroizes the buffer. Only the cursor cell
    ever shows a live character.
  - Gate + flash-gate: the join transport runs the PR-10 live
    handshake first — the ESP32 must answer `version` with
    `ESPFW` ≥ the join minimum (bumped to 1.1.0.0, the paired image
    version). ABSENT/OLDER show the gate's diagnostic ("ESP not
    responding" / "ESP fw unknown" / "ESP update required") and send
    nothing. A stale DR2/DR3 record still never gates.
  - ESP-side handler (`Esp_spi_at/.../esp_app_main.c`, same style as
    the scan handlers): `wifi_connect_ap()` serves
    `CTRL_REQ_CONNECT_AP` — validates the SSID/PSK grammar, sets
    station mode, issues `AT+CWJAP="<ssid>","<pwd>"`, and answers
    `CTRL_RESP_CONNECT_AP` with mapped response codes
    (`SUCCESS`/`CTRL_ERR_INVALID_ARGUMENT`/`CTRL_ERR_INVALID_PASSWORD`
    /`CTRL_ERR_NO_AP_FOUND`/`CTRL_ERR_NOT_CONNECTED`/
    `CTRL_ERR_REQUEST_TIMEOUT`) within the 45 s
    `DEFAULT_CTRL_RESP_CONNECT_AP_TIMEOUT`; the paired-firmware
    version constant (`ESP32C6_FW_VERSION_LINE`, `ESPFW 1.1.0.0`) is
    defined in `esp_at_list.h` and pinned to the STM32 gate minimum
    by the host suite.
  - Reconnect: exactly one attempt per boot, on first Wi-Fi-menu
    entry (the ESP32 is deliberately not powered at raw boot) —
    reconnect candidate is the first credentialed row in store order,
    re-verified and re-decrypted at join time, bounded by a 15 s
    budget, with clear failure states ("Reconnect failed", "Saved PSK
    unusable") and a silent zero-cost no-op when nothing is
    credentialed.
  - PSK hygiene: the plaintext exists only in the editor buffer, the
    decrypted `wifi_cred_get_psk()` buffer, the static command buffer,
    and — transiently, when the console echoes the typed command — the
    reply buffer; each is zeroized with `m1_secure_zero()` on every
    exit path (the ESP-side handler wipes its `AT+CWJAP` command
    before free on every path that built it; the DMA staging region is
    wiped after every actual transmit; the SPI TX stream-buffer
    storage — where the command's `xStreamBufferSend` copy landed — is
    scrubbed best-effort after the exchange, only when the driver's
    single-flight semaphore is free and the ring is genuinely empty,
    so a timeout against a wedged slave that never pulled the write
    can leave the plaintext in the ring until later traffic
    overwrites it). Reply
    classification matches `JOINED`/`JOIN_FAIL`
    only at a line start, so a marker substring inside an echoed SSID
    can never forge or mask a result. Credentials are sealed through
    the PR-9 store only after a successful join; the PSK is never
    logged and never appears in an error string.
  - Tests + docs: host suite `m1_csrc/test/m1_wifi_join_test.py`
    (real extracted join core linked with the real crypto zeroization:
    PSK/SSID grammar battery, rotor round trip proving every
    editor-producible PSK is join-legal, exact command construction
    incl. hidden/open forms and the no-truncation overflow wipe,
    reply classification, reconnect pick; the ESP pairing/version-gate
    battery compiled from the real gate core — the in-repo ESP app's
    version line parses to exactly the join minimum, pre-pairing and
    dead consoles refuse; static wiring checks incl. gate-before-send,
    buffer zeroization, no logging in the join section, menu wiring,
    and runner wiring), wired into `scripts/run_host_tests.sh`;
    `docs/security/wifi-credential-threat-model.md` extended with the
    join-path plaintext exposure; `docs/architecture.md` inter-
    processor interface note for the `join` verb and the paired
    `CTRL_REQ_CONNECT_AP` handler. On-device join/reboot-reconnect
    verification is tracked as a hardware-acceptance follow-up.
- Fork: ESP32 feature-gate utility — live firmware handshake, four
  states, per-feature minimums (PR-10 of the Merge 3 series; the
  shared mechanism consumed by the future Wi-Fi join flow and BLE
  advertisement editor, which carry no consumers yet by design) —
  new `m1_csrc/m1_esp_feature_gate.c/.h`. Every gate decision comes
  from a version the ESP32 reports over the shipped UART console
  session RIGHT NOW: a bounded `"\r\n"` prompt ping, then the
  read-only `version` console verb, reply parsed for the
  `ESPFW <major>.<minor>.<patch>.<rc>` marker that the external ESP32
  console firmware is expected to answer with (the vendor SPI-AT
  `CTRL_REQ_GET_FW_VERSION` enum has no shipped requester and is
  deliberately not used — no parallel protocol). The answer is
  normalized into four states against a per-feature minimum-version
  table (versions compared field-by-field, numerically, rc as a plain
  fourth number): ABSENT (no live answer — console dead, round trip
  failed, or no recognizable version; the honest unknown), OLDER,
  EQUIVALENT, NEWER; only EQUIVALENT/NEWER may execute a gated
  feature (`esp_feature_gate_allows()`), and each state/reason maps
  to short user-facing diagnostics ("ESP not responding", "ESP link
  failed", "ESP fw unknown", "ESP update required", "ESP fw
  supported", "ESP fw newer"). The persisted DR2/DR3 record
  (`m1cp_esp_version_get()`) is VBAT-dependent and describes what the
  STM32 last installed, not what is running, so it NEVER gates: it is
  reported in the query result as diagnostics only —
  `cache_disagrees` makes a stale record visible when the live answer
  disagrees — and the gate never writes it (the record's meaning
  feeds the Web Manager's flash-skip decision). Both feature minimums
  start at a version no shipped ESP32 image reports, so the gate
  refuses until the paired firmware lands; PR-11 (join handler) and
  PR-12 (advertise command) own their entries. Console-touching
  operations sit behind a host-injectable seam (same technique as
  `m1_rng.c`), no dynamic allocation, task-context contract documented
  in the module header; inter-processor interface note added to
  `docs/architecture.md`. Host suite
  `m1_csrc/test/m1_esp_feature_gate_test.py` (real gate core + real
  version record, simulated ESP32 console: version-compare vectors,
  reply-parsing battery incl. garbled/embedded markers and negatives,
  the full four-state query matrix, and the stale-cache battery — a
  record claiming `{9,9,9,9}`/`{2,0,0,0}` never lifts a dead console
  or an older/absent live answer, and a cleared record with current
  live firmware still allows) plus 7 static source checks, wired into
  `scripts/run_host_tests.sh`.
- Fork: Wi-Fi credential encryption at rest on the saved-networks store
  (issue #42 part 2, PR-9 of the Merge 3 series) — `saved_networks.csv`
  rows gain an OPTIONAL 8th `credential` column carrying
  `base64(iv‖ct):base64(cmac)`: AES-256-CBC over a fixed 64-byte
  zero-padded PSK frame (no padding scheme, exact-PSK-length never
  leaks into the ciphertext size, both WPA forms 8..63-char passphrase
  and 64-hex PMK fit), per-record IV from the hardware RNG (a refusing
  RNG refuses the save — no fallback), AES-CMAC over iv‖ct under the
  PR-8 UID-derived key, constant-time tag compare, and strict base64
  helpers (no dynamic allocation, host-testable). `FMT_VER` stays 1:
  legacy 7-column rows (and rows with an empty 8th) remain valid and
  unencrypted-by-absence — no migration shim, no second store
  (`m1_wifi_cred.c` is not imported). Tampered/truncated/wrong-CMAC/
  foreign-UID fields mark that network's credential unusable (skipped
  by the future join flow) while the row stays a valid recon target
  and the store still boots clean; plaintext PSK and key buffers are
  zeroized with `m1_secure_zero()` on every path, and
  `wifi_cred_set_psk()` absorbs (zeroizes) the caller's buffer. The
  join-flow API (`wifi_cred_set_psk`/`wifi_cred_get_psk`, decrypt at
  join time with re-verification) ships in `m1_wifi.h` for PR-11 — no
  menu/UI yet, by design. Honest threat model in the new
  `docs/security/wifi-credential-threat-model.md` (protected:
  removed-SD-card casual access only; exclusions: physical attacker
  with SWD/firmware knowledge, plaintext SPI link to the ESP32, secure
  element, 802.1X; CMAC is integrity-not-confidentiality; UID-key
  limits), linked from `docs/security.md`. Host suite
  `m1_csrc/test/m1_wifi_persistence_hardening_test.py` extended with a
  compiled 114-check encrypted-credential battery (real codec + real
  `m1_crypto.c`, injectable device-UID and RNG seams: round-trip,
  7/8-column compatibility, ciphertext tamper, wrong CMAC, truncation,
  foreign-UID recovery, oversized/garbage fields, store-not-bricked,
  zeroization contracts) plus 10 static source checks, and wired into
  `scripts/run_host_tests.sh`. The credential column is written
  CSV-quoted (like every other free-text column), so hostile or
  foreign-device field text round-trips verbatim and every rewritten
  row keeps exactly 8 columns.
- Fork: crypto + hardware RNG foundation (issue #42 part 1, PR-8 of the
  Merge 3 series) — new pure-core module `m1_csrc/m1_crypto.c/.h`
  (software AES-256 block/CBC per FIPS-197/NIST SP 800-38A with
  explicit IV length and no padding assumptions, AES-CMAC per
  RFC 4493/SP 800-38B with constant-time tag compare, a documented
  UID-based key derivation per SP 800-108 KDF-in-CMAC-mode with domain
  separation, and compiler-barriered `m1_secure_zero()`), plus the
  mandatory STM32H5 hardware RNG wrapper `m1_csrc/m1_rng.c/.h`: official
  ST HAL RNG driver vendored at tag v1.3.0 (byte-verified match with
  the existing driver tree), `HAL_RNG_MODULE_ENABLED` enabled, HSI48
  selected as the RNG kernel clock in `HAL_RNG_MspInit()`, initialized
  deterministically in `m1_system_init_task()` before any consumer
  task. Any RNG init or generate failure latches permanently and every
  later `m1_rng_init()`/`m1_rng_bytes()` call refuses — there is no
  time-based or pseudo-random fallback anywhere
  (host-suite-proven; the pre-existing decorative `rand()`/`srand()`
  sites are count-pinned per file — file-name suffixes and synthetic
  NFC UIDs, never keys or IVs).
  Threat-model honesty is documented in `docs/security.md` and the
  module header: the UID-derived key hides credentials from
  removed-SD-card reading only — no secure element, no TLS, no SPI-link
  encryption, no side-channel hardening. Host suite
  `m1_csrc/test/m1_crypto_test.py` (wired into
  `scripts/run_host_tests.sh`) compiles the real sources against NIST
  SP 800-38A AES-256 ECB/CBC vectors, all four RFC 4493 CMAC examples,
  ten NIST CAVP `CMACGenAES256.rsp` vectors, plus an injectable
  RNG-failure seam proving the refusal semantics and a zeroization
  dead-store test run at `-O0`/`-O2`. The store integration is the
  PR-9 entry above (this unit shipped the primitive layer only).
- Fork: Sub-GHz Radio Settings (issue #3) — the Sub-GHz menu gains a
  *Radio Settings* leaf with a modal screen (the established 128×64
  settings idiom) for modulation (Auto/OOK/2-FSK/2-GFSK/4-FSK/4-GFSK),
  RX bandwidth (Auto/10/100/200 kHz), FSK deviation (Auto/2/20/47/200
  kHz — the row is visible only for the FSK-family modulations),
  nominal output power (−10…+20 dBm) and an exact-Hz frequency override
  (Off or one of the Record-RAW frequencies). Values persist through the
  SD settings store and are applied by `SI446x_Apply_Modem_Config()`
  (new in `Sub_Ghz/m1_sub_ghz_api.c`: a generic SET_PROPERTY writer plus
  a runtime modem patch on top of the loaded WDS base configuration) via
  the idempotent `sub_ghz_apply_user_radio_overrides()` hook, invoked by
  both opmode setters after every preset load/tune and immediately
  before the radio enters RX/TX — task context only, never ISR. At the
  compiled defaults (band-preset modulation/bandwidth/deviation, +20 dBm
  ≡ the stock PA_PWR_LVL 0x7F, no frequency override) the hook writes
  nothing and scan/record/replay behavior is unchanged. The frequency
  override is region-gated by `sub_ghz_fcc_ism_band_check_hz()` at BOTH
  save time (the screen refuses disallowed frequencies with a notice)
  and apply time (the resolver ignores a now-disallowed override,
  fail-closed), and the TX boundary keeps its own region check on the
  effective frequency. Provenance is explicit: the bandwidth presets are
  the repo's own WDS-generated 10/100/200 kHz filter+decimation sets
  (copied verbatim, host-test-bound to the headers; nominal achieved
  widths 10.75/109.93/219.87 kHz), the deviation encoding is verified
  against the repo's WDS FSK headers (2 kHz→0x000083 @outdiv 8,
  200 kHz→0x00199A @outdiv 4), the per-band outdiv table is bound to
  the base headers' own MODEM_CLKGEN_BAND bytes for all nine bands
  (0x0B→12, 0x09→6, 0x0A→8, 0x08→4 — pinning the previously inferred
  0x09→6 as header-consistent evidence), and the dBm→PA map is a
  documented NOMINAL linear-in-code approximation anchored at
  0x7F≡+20 dBm —
  absolute power accuracy is deferred to hardware acceptance (follow-up
  issue). New host suite
  `m1_csrc/test/m1_subghz_radio_settings_test.py` (118 checks: compiled
  pure cores for the driver/screen decision logic, WDS-header provenance
  binding, override-ordering/region-gate/no-ISR static seams, menu and
  store-default wiring), wired into `scripts/run_host_tests.sh`;
  `settings_exit_save()`/`settings_str_clip()` are exported through
  `m1_settings.h` as the shared modal-screen helpers. The settings file
  format is bumped to `#ver=2` with a load-time migration for legacy v1
  files (PR-1…PR-6 firmware serialized the then-inert
  `subghz_power_dbm=0` wholesale; on load it maps to the +20 dBm
  default, every other field is kept, and the next save rewrites the
  file as v2 — under which an explicit 0 dBm choice loads verbatim), so
  devices upgraded across the PR-7 boundary keep stock TX power.
- Fork: IR favorites and recent lists in the SD-card IR browser, plus the
  explicit "cycle all in category" submenu action (issue #43 part 3 —
  this completes #43 together with the earlier IR units). The browser
  dashboard gains *Favorites* and *Recent* rows backed by two plain text
  stores under `0:/System` (`ir_favorites.txt` / `ir_recent.txt`, the
  legacy fork format: one full device path per line, newest first, LRU
  caps 20/10, whole-file bounded rewrites, `0:/System` ensured lazily on
  first write). Loading is tolerant — a missing, unreadable, blank,
  corrupt, over-long or mis-shaped line is skipped (stored lines must be
  `0:/IR/….ir` device paths, so a rewrite torn by card-full or card
  removal is dropped at load and self-heals on the next rewrite) and an
  unreadable store renders an empty list, never an error screen — and
  stored rows open the same
  device → button flow as every other list (a vanished path surfaces the
  normal "Read failed" notice). Holding OK in the button view for about a
  second toggles a device in the favorites store (legacy `* ` title
  marker + short notice; the long-press mark fires exactly once per hold
  and never while a frame is in flight — a long-press landing mid-frame
  skips the toggle but still swallows that hold's repeats — and the OK
  auto-repeat clicks that follow the toggle are suppressed until the key
  is released, so
  favoriting cannot machine-gun the selected button; the short-press
  transmit and LEFT/RIGHT paging are untouched); every completed frame
  records the device in the recent store (a send aborted with BACK is
  not recorded). Store loads/rewrites are filesystem-only and run between frames
  in the browser session — the transmit path itself performs no store or
  filesystem writes, so favorites/recent handling can never overlap IRMP
  TX/learn. The upstream cycle-through-every-device session
  (`infrared_universal_all_remotes`) is exposed and demoted from the
  default per-press behavior to four explicit Infrared → Universal
  Remotes → *Cycle all TVs/Audio/Projectors/ACs* submenu actions with
  the upstream pause/resume and progress UI preserved (it acquires the
  single-owner IRMP guard itself); the four universal remote screens now
  transmit exactly one frame per OK press (the next device's code each
  press, END_OF_FILE wraps around), matching the browser's
  one-frame-per-button default. Static workspaces only (+3.2 KB BSS for
  the shared store workspace); no dynamic allocation in screen loops.
  New host suite `m1_csrc/test/m1_ir_favorites_test.py` (70 checks):
  compiled pure-core store model (trim, tolerant-load rule, LRU
  insert/dedup/eviction/removal, basename labels, favorite title
  marker) plus static seams for the store wiring, corruption tolerance,
  the serialization rules and the cycle-all dispatch; browser suite
  updated for the new session signature. Format, caps and behavior are
  documented in `docs/ir-provisioning.md` and `docs/architecture.md`.
- Fork: IR file browser on the SD card (issue #43 part 2) — the Infrared
  menu gains *IR File Browser* (the four upstream category entries stay):
  a dashboard over the side-loaded Flipper IRDB tree under `0:/IR`
  (convention, side-load steps and the bounded-scan table live in
  `docs/ir-provisioning.md`), category → device → button lists with 6-row
  pagination (UP/DOWN step, LEFT/RIGHT jump a page, BACK climbs back out)
  and a virtual-keyboard search across the whole tree. Selecting a device
  indexes it through the PR-4 streaming loader (names only, no command
  buffering, no per-file cap) and every OK press re-parses exactly that
  button and transmits one frame through the upstream play path — including
  correct NEC/NECext/RC5X/SIRC15/SIRC20/RCA encodings and raw timing
  playback — never a category-wide cycle. Empty states are actionable: a
  missing or empty `0:/IR` shows a hint telling the user what to copy
  where, unreadable files and unsupported signals map to plain notices
  (every notice title is capped at 12 characters so line 1 fits beside the
  icon on the 128-px display), scan limits surface as a `+` truncation
  marker in the list title, and unknown-protocol blocks strict-skip into
  one aggregated notice per file
  (via the PR-4 hook), never a garbage transmit; when a file's every block
  is skipped the aggregate replaces the bare no-usable-buttons notice and
  explains it. Category folder names too long for the bounded category
  list are skipped during the scan (like over-long paths), so the list can
  never offer a directory it cannot re-open. Raw playback is gated on
  the loaded tick count: an empty or zero-sample `data:` line, a
  bare-leading-pause count or any even count (the loader edge cases
  tracked in issue #74), and a parsed block claiming `protocol: raw` are
  all refused with an error notice before the payload is published — a
  bare pause or polarity-flipped frame can never be transmitted. The IR
  subsystem is now
  formally single-owner: a module-level busy guard
  (`m1_ir_subsys_acquire/release`) is held by learn, saved-remote replay,
  the universal remotes, the browser session and the CLI transmit test, so
  no two IR functions can overlap (the guard is what extends the UI-task
  serialization to the CLI task). Static workspaces only — no dynamic
  allocation in screen loops. Host suite
  `m1_csrc/test/m1_ir_browser_test.py` (81 checks): compiled pure-core
  pagination/search/classification/label/notice math, a second harness
  compiling the REAL PR-4 loader + protocol map to prove the empty-data-line
  raw blocks land on the non-transmittable side of the TX gate, plus static
  seams for the menu wiring, the guard on every entry point, the
  empty-library hint, the loader hookup, the 12-char notice-title cap
  (beside-icon literals and the 9999 skip-count clamp), the over-long
  category-name skip and the zero-usable-file aggregated skip notice
  (plus the durable parsed-protocol publish: the TX payload never points
  into the browser's transmit frame). Favorites/recent and the
  explicit "cycle all in category" submenu action are deliberately left
  for the next unit.
- Fork: IR data layer — streaming `.ir` loader, strict-skip policy and an
  IRMP golden-decode regression suite (issue #43 part 1; no user-visible UX
  yet, the browser lands in the next units). The protocol table in
  `Infrared/m1_ir_remotes.c` is audited against the official Flipper
  vocabulary (NEC, NECext, RC5X, SIRC15, SIRC20, RCA all present with
  correct encoders; nothing added), recorded in the module header together
  with the intentional omissions (NEC42ext, Samsung48/64, Cassette have no
  encoder in the vendored IRMP), and exposed as the single-map lookup
  `ir_protocol_map_lookup()`; the fork's `ir_proto_map` aliasing (which
  transmitted wrong codes for NEC/NECext, RC5X and SIRC15/20) stays
  unported. New `Infrared/m1_ir_file.{c,h}`: an index pass records each
  block's name/type/protocol/address/command-presence/raw-presence and file
  offset without buffering commands (no whole-file-in-RAM parsing, no
  legacy 64-command cap), and `m1_ir_file_signal_load()` re-parses one
  block on demand for transmit through the same `ir_remote_function_play`
  data path, verifying the re-parsed block name against the index entry so
  SD content changed between index and load can never yield a
  different-but-valid signal; raw `data:` lines are token-streamed so
  unbounded line lengths parse with a fixed 512-byte chunk reader, and raw
  samples land in a caller-provided 10-µs tick buffer with the legacy
  leading-pause and
  odd-count OTA invariants (raw fields are raw-block-only: a parsed load
  publishes `raw_count == 0`, never the leading pause). Tolerance: blank
  lines, `#` comments, CRLF, unknown keys and fields in any order (one
  exception: `data:` before `type: raw` drops the line and skips the block
  as malformed — the block type is not yet known when the samples arrive);
  overlong keyword lines, bad values
  and incomplete blocks are skipped and counted as malformed — never a
  crash. Strict-skip: an unknown protocol name skips its block, increments
  the skipped counter and fires the visible-notice hook the PR-5 browser
  will render; garbage is never transmitted. The module header pins the IR
  subsystem ownership rules (IRMP/IRSND single-owner in the UI task only)
  and the `0:/IR/...` SD convention. Host tests: the loader suite
  (`m1_csrc/test/m1_ir_file_test.py`, real pure core + real table) and the
  golden-decode suite (`m1_csrc/test/m1_ir_golden_decode_test.py`), which
  compiles the untouched vendored IRMP/IRSND verbatim on the host, plays
  the previously-wrong protocols through the full
  .ir→loader→map→irsnd→OTA→irmp pipeline asserting protocol-definition-level
  wire facts (NEC inverted 8/8 copies, NECext 16-bit address verbatim,
  SIRC15/SIRC20 address widths, RC5X family, RCA demodulated by an
  independent reference), and validates 582 frames across 25 real-world
  IR-Data captures with a documented two-tier coverage policy (stale
  2004-2013 expectation conventions and upstream decoder-conflict skips
  are itemized in the test header).
- Fork: RTC date/time setting (issue #7 completion) — Settings grows an
  eighth entry, *Date & Time*: a two-line clock editor ("YYYY-MM-DD" /
  "HH:MM", selected field on an inverted segment) where LEFT/RIGHT pick
  the field and UP/DOWN step it (wrapping month/day/hour/minute,
  clamped year, day re-clamped across month/year steps so an impossible
  date cannot be entered), OK applies the whole validated date/time to
  the RTC and re-syncs from a read-back, and BACK exits without
  applying. New `m1_csrc/m1_rtc.c/.h` is the single mediated calendar
  path (GetTime-then-GetDate shadow-register order on read, validation
  before write, HAL-owned init-mode handling, true weekday stored,
  seconds restart at 00); pure date/time math (leap year,
  days-in-month, 2000–2099 validation, weekday, stepping, formatting)
  lives in `m1_csrc/m1_rtc_date.c/.h` and is host-tested against the
  real sources. The battery-backed RTC is the timebase: time is
  deliberately not duplicated into `settings.cfg` (Reset defaults
  leaves the clock alone) and backup registers DR0–DR3 stay untouched —
  which is also why the clock source stays LSI (switching to LSE needs
  a backup-domain reset). The System screen shows the current date/time
  read-only, refreshed on entry ("Time not set" after backup-domain
  loss, where the LSI/VBAT semantics — retained but not advanced across
  power-off — are documented in `docs/architecture.md`).
- Fork: LCD & Notifications and System settings screens over the SD settings
  store (issues #4 and #7 except date/time, which lands with the RTC unit) —
  Settings grows two new leading entries. *LCD & Notifications*: display
  brightness 0–100% with live preview while editing, screen timeout
  Off/10–120 s replacing the fixed compile-time 45 s saver period (Off never
  sleeps; changes apply without a reboot, and a session already asleep when
  Off takes effect wakes), and an LED notifications switch that suppresses
  only the success/notify LED flashes — error, power, boot, battery, radio,
  USB, payload and firmware-update cues are never suppressible, and sound/
  backlight keep their own settings. *System*: editable device name (virtual
  keyboard, shown in System and on the About page), SD storage total/free
  readout, and Reset defaults — restores compiled defaults, persists them
  over the settings file, and reboots; the boot-signed flash metadata sector
  is never touched. Both screens apply edits immediately and persist the
  whole editing session with one atomic save on exit, skipped when the
  session edited nothing (a browse-only visit never writes the card and
  never sees a failure notice); a refused save of real edits is
  displayed rather than hidden, and modal button loops never act on an
  empty button-event receive (virtual-keyboard seam guard). Screen
  value logic (stepping, bounds,
  labels) lives in a host-testable pure-core region of `m1_csrc/m1_settings.c`
  that future settings screens (Sub-GHz, Bluetooth) are expected to copy.
  Host coverage: new settings-screens suite; the backlight/saver suite now
  proves runtime timeout semantics (default 45 s identical, Off, custom) and
  the feedback-manager suite gains LED-notification gate cases (suppression
  scope, error/power immunity, init default); all are wired into
  `scripts/run_host_tests.sh` and CI.

- Fork: durable user-settings store foundation for the Merge 3 feature set
  (issues #3/#4/#7/#11 substrate) — new `m1_csrc/m1_settings_store.c/.h`
  keeps a RAM settings image (`S_M1_Settings_t`: LCD brightness, screen
  timeout, LED notifications, device name, plus round-trip-only Sub-GHz
  radio and Bluetooth advertisement fields their future screens will
  define) loaded once at boot from `0:/System/settings.cfg` after SD init
  and before any consumer task starts. The file format is a versioned
  `#ver=N` first line (v1 at introduction; bumped to v2 by the Radio
  Settings unit, whose power semantics required a load-time migration —
  see the Sub-GHz Radio Settings entry) + `key=value` lines + a trailing `crc=<8 hex>` CRC-32C (Castagnoli,
  0x1EDC6F41, the shared `m1_crc32c.c`) over all preceding bytes. Loading
  is tolerant and bounded (missing file → compiled defaults; CRC
  mismatch/truncation/unsupported version makes the file unreadable →
  the retained `.bak` is validated with the same checks and adopted
  when it passes — values loaded, saving re-enabled, one log line,
  both files left in place — otherwise defaults with `.bak` still in
  place; unknown keys skipped; out-of-range values clamped; malformed
  numerals and string values containing control characters reset to
  per-field defaults, with one extra log line naming how many fields
  were reset). An interrupted save is
  reconciled at load: an orphaned `settings.tmp` is promoted only when
  `settings.cfg` is missing (the CRC gate has the final word on it);
  when `settings.cfg` exists and passes a CRC probe the orphaned
  `.tmp` is discarded (when the cfg fails the probe beside a
  validating `.tmp`, the `.tmp` is promoted over the corrupt cfg),
  and the `.bak` is restored only when neither cfg nor tmp survives.
  Saving is
  a recoverable atomic promotion (`settings.tmp` fully written+closed →
  superseded `.bak` unlinked → previous `settings.cfg` rotated to
  `settings.bak` → tmp renamed over final) that can never destroy the
  last good file; while it exists, the retained `.bak` is the last
  known-good copy. The first save after a `.bak` recovery heals
  instead of rotating: it displaces the known-corrupt `settings.cfg`
  directly and keeps `.bak` — then the only good copy — untouched
  until the next save's rotation, so a failure or power cut always
  leaves either the validating `.bak` or the complete promotable
  `settings.tmp` and a retry or reboot recovers. A save is refused
  when the boot load could not
  account for the card (absent/failed SD, or `settings.cfg` and `.bak`
  both unreadable), so a defaults image can never overwrite a live
  `settings.cfg`
  after a late card insertion; the shared serialization work buffer is
  file-scope, leaving the caller's transient stack need at the FatFs
  file object and call depth. Boot applies the loaded brightness via
  `fb_set_brightness_pct()` after task creation — `fb_manager_init()`
  (reached through `m1_tasks_init()` → `m1_feedback_task_init()`)
  resets user brightness to the compiled-in default and would clobber an
  earlier apply (default 100 is behavior-identical). Settings are SD-only:
  no writes to flash sector 127
  or `S_M1_FW_CONFIG_t` anywhere in the module. Ownership: UI task is the
  single writer, any task may read. Host coverage in
  `m1_csrc/test/m1_settings_store_test.py` (pure-core parse/serialize/
  CRC/clamp tests on the extracted real code plus FatFs-stand-in
  fault-injection for atomic-save/recovery behavior), wired into the new
  `scripts/run_host_tests.sh` aggregator and
  `.github/workflows/host-tests.yml` CI entry point. The aggregator's
  wired set is the settings-store suite today; later units append their
  host tests by design, so a green CI run covers exactly the listed
  tests. `0:/System` added to
  the canonical path constants (`m1_sdcard_provision.h`), created lazily
  on first save
- Fork: automatic boot-time firmware integrity failsafe on the adopted base
  (issue #41, part A) — every boot validates the running bank's fixed
  `[0x08000000, 0x080FFC14)` span against the CRC the build embedded at
  `0x080FFC14` (`bl_crc_validate_active_bank()`, reusing the update path's
  streamed CRC engine; no magic-number scan, no image-size field, and a
  compile-time assert that the span equals the build's `0xFFC14` crop end).
  Failures are classified (metadata magic / metadata header / image CRC),
  counted in saturating `boot_attempts`, recorded in `boot_fault_signature`,
  and stop the boot at a visible `FW CORRUPT!` banner with a seam for DFU
  entry. The banner is diagnostic, not containment: the unvalidated image is
  not prevented from running and the normal UI is one key press away; no
  automatic recovery action ran until #41-B filled that seam (the bounded DFU
  offer below). A validating boot
  clears the accounting. `S_M1_BK_REGS_t`
  now exposes those fields plus `swap_attempts` inside its existing 3 reserved
  bytes (still exactly 8 bytes = DR0/DR1, statically asserted; DR2/DR3 stay
  ESP-version-owned) via generalized `startup_config_write()`/
  `startup_config_read()` accessors, `SYS_CONFIG_MAGIC_NUMBER` is bumped to
  invalidate the old layout once, and the interactive rollback path counts its
  swaps with a one-swap-per-validated-boot loop guard. Boot-path IWDG option
  bytes are inspected read-only (the legacy per-boot rewrite is deliberately
  not ported). New host tests (`test_m1cp_failsafe`, guard test) and
  `scripts/check_no_raw_bkp_writes.sh` cover the logic and ban raw `TAMP->BKP`
  access and `HAL_RTCEx_BKUPWrite/Read` aimed at DR0/DR1 outside the accessors
- Fork: USB DFU entry ported onto the adopted base (issue #41, part B, closes
  the DFU half) — the fork's ROM-bootloader jump
  (`bl_jump_to_dfu()`, `m1_fw_update_bl.c`) with its full teardown order
  (interrupts off, SysTick plus the pended SysTick/PendSV bits, a bounded
  raw-register clock teardown back to the HSI reset state, TIM6 halted, NVIC
  ICER/ICPR, `SCB->VTOR` = system memory, DSB/ISB, ICACHE off and waited on,
  testing ST's `CR.EN == 0` completion criterion as well as `SR.BUSYF`;
  DCACHE1 is not part of that teardown in this build -- its AHB1 clock gate is
  never enabled, so the step reads 0 and its branch is never taken), a 32.768 s
  IWDG safety net (LSI/256 -- `IWDG_PR` 6, divider derived from the register
  encoding -- with reload 4095) so the ROM can enumerate instead of being reset
  by the ~4 s application watchdog. That net bounds the *entire* ROM residency,
  not only an abandoned session: the `0xAAAA` feed is the last one, so connect,
  enumerate, download and verify must all finish inside 32.768 s. Then
  `CONTROL.SPSEL` cleared with MSP set to
  the ROM's initial SP, interrupts back on and the jump. The net is armed
  start-then-configure (`0xCCCC` -> `0x5555` -> `PR`/`RLR` -> `IWDG->SR` wait ->
  `0xAAAA`, the HAL's own order) so it is guaranteed on the cold-boot strap
  route, where the IWDG/LSI is still stopped because the jump runs before
  `MX_IWDG_Init()`; every hardware-flag wait in the teardown is iteration-capped
  -- the `ICACHE` `CR.EN`/`SR.BUSYF` and RCC ready/switch waits at
  `M1_DFU_TEARDOWN_SPIN_LIMIT` (they fall through to the jump), the `IWDG->SR`
  prescaler/reload synchronisation at its own millisecond-class budget
  (`M1_DFU_IWDG_SYNC_SPIN_LIMIT`, ~4x ST's `HAL_IWDG_DEFAULT_TIMEOUT`, because
  that wait is LSI-domain) and testing only the four *update* flags
  (`PVU|RVU|WVU|EWU`, the HAL's `IWDG_KERNEL_UPDATE_FLAGS`; `ONF` is the
  persistent "IWDG is running" status bit and is deliberately excluded, since
  `0xCCCC` sets it) -- so a stuck flag cannot hang the unit with
  interrupts disabled and no net armed. The two teardown steps whose failure
  would be worse than a reset fail loudly instead of falling through: the clock
  teardown verifies its post-condition (SYSCLK on HSI, PLL1 off) and the IWDG
  wait refuses to feed `0xAAAA` -- which would reload with the *previous*
  `PR`/`RLR` and silently disarm the net -- so both reset the unit through
  `bl_dfu_teardown_failed()`. Three entry routes, all
  ending in that one
  non-returning function: the PE11 (UP button) hardware strap sampled with raw
  registers in `Core/Src/main.c` before `HAL_Init()`/`MX_IWDG_Init()` (speaker
  tick on PC7, ~23 ms; PD12/PD13 driven high -- user-owned external connector
  pins 6-7, not LEDs), a persisted software request
  (`DEV_OP_STATUS_USB_DFU_REQUEST`, appended before `UNKNOWN` so existing
  values keep their numbers; written by the new public
  `firmware_update_enter_usb_dfu()` which the `dfu` CLI command will use, and
  honoured as the first op-status branch and cleared before the jump so it is
  single-shot and cannot create a DFU boot loop), and a bounded recovery-screen
  offer (`OK=USB DFU 5s`, drawn in the same screen as `FW CORRUPT!`) on a
  failed boot CRC validation. OK acceptance there is edge-triggered (the level
  at offer start seeds a released->pressed latch), so a flat battery or a
  stuck key can only delay a failed boot, never hang it and never cycle it into
  DFU; the poll loop kicks the IWDG inside the 5 s window and falls back to the
  existing banner behaviour on timeout. ROM cost of the unit: **+1712 B flash /
  +/-0 B RAM** (text 713884 -> 715596 B, data/bss unchanged; measured with
  `arm-none-eabi-size` on a clean `main` @ `8033236` build and this tree, same
  toolchain). The legacy boot-time IWDG option-byte rewrite is deliberately not
  ported: no DFU entry path programs option bytes. Decisions live in the
  host-testable `m1_csrc/m1_dfu_entry.h`; the new `test_m1cp_dfu` host target
  covers strap polarity, the IWDG register encoding and timeout, SP sanity, the
  request raise/consume transition, the edge-triggered bounded recovery policy
  (including the stuck-key case), the banner+offer screen and the teardown
  ORDER/confinement source seams. On-device DFU enumeration/rollback
  demonstration is deferred to the issue #41 hardware gate (milestone
  "Post-merge stabilization")
- Fork: USB CDC RX diagnostics restored on the adopted base's stream-buffer
  path (issue #44, part A) — the fork's `CDC_CLI_Rx*` counters were lost with its
  (deliberately not resurrected) CDC rewrite, so they now live on upstream's
  `USB/Class/CDC/Src/usbd_cdc_if.c` path (`UserRxBufferFS` -> `usbRxBuffer` ->
  `h_usb_rx_streambuf`). The API and command names are the legacy ones, but the
  metric is not: the fork counted true drops of console input on the
  `CDC_MODE_LOG_CLI` branch, whereas these observe the VCP/ESP32 bridge path and
  separate retryable back-pressure from proven loss.
  `CDC_CLI_RxDroppedBytes()` counts host bytes refused at arrival because the
  stream buffer was full (partial fit or none), counted once per byte at
  arrival so retried carry-over is never re-counted; those bytes normally stay
  queued in `usbRxBuffer` and are delivered on the next drain, so it is a
  back-pressure metric, not proof of loss. `CDC_CLI_RxLostBytes()` is the
  loss-only counter: bytes that never fit the intermediate `usbRxBuffer` plus
  bytes still pending in the stream buffer when a bridge-mode exit empties it
  (`CDC_CLI_RxDiscardPendingBytes()`, which replaces the bare
  `xStreamBufferReset(h_usb_rx_streambuf)` at both mode-switch sites:
  `m1_logdb_deinit()` and the BACK-button exit in `gpio_usb_uart_bridge()`), so
  it stays at 0 under pure back-pressure. `CDC_CLI_RxHighWatermark()` is the maximum
  occupancy observed after each send (sticky until reset; usable capacity is
  `RXSTREAMBUF_USB_SIZE - 1` = 2047 bytes); `CDC_CLI_RxBufferedBytes()` is the
  sampled pending level (a gauge, not a synchronisation signal).
  `CDC_CLI_RxResetStats()` zeroes all counters under `taskENTER_CRITICAL()` so
  the RX interrupt cannot resurrect a pre-reset value. Counters are `volatile
  uint32_t` (single aligned writes/reads, no torn values) and diagnostic only:
  no buffering, copy, back-pressure or pause decision reads them, and
  `CDC_Receive_FS()` gained no blocking call. The arithmetic is pure and
  host-tested (`m1_csrc/m1_cdc_rx_stats.h`, `test_cdc_rx_stats`: 77 assertions
  covering full buffer, partial fit, intermediate-buffer overflow, carry-over,
  refusal/loss independence, watermark monotonicity, buffered-now, reset and the
  bridge-mode discard, plus source seams that pin the ISR call site, the reset
  critical section, the counting discard helper and the absence of blocking
  calls). `cdcstats`/`cdcreset` expose them; the output is qualified ("Refused
  at arrival ... (retryable)", "Lost (unrecoverable)", "High watermark ...
  (sticky)", "Buffered now ... (sampled)") because the legacy "dropped bytes"
  label measured a different path and a different metric. ROM cost of the unit:
  **+1344 B flash / +32 B RAM** (FLASH 724368 -> 725712 B, RAM 326184 -> 326216
  B, same toolchain, `./build.sh` on this tree versus `main` @ `517b339`);
  relative to the pre-harden head `62f693b` the counter split and the corrected
  wording cost +512 B flash / +0 B RAM (the 8 -> 12 B counter struct is absorbed
  by linker packing in this build). `_wCRC.bin` is still 1047576 bytes.
  On-device "counters become non-zero during a CDC stress transfer" is
  hardware-gated by issue #63 (milestone "Post-merge stabilization"); quote
  `CDC_CLI_RxLostBytes()` alongside the refusal counter when it runs, since a
  rising refusal counter alone proves back-pressure, not loss; see
  `docs/protocols/usb-cdc.md`
- Fork: console diagnostics restored on the adopted base (issue #44, part B) —
  the eight fork CLI commands (`log`, `status`, `version`, `sdcard`, `wifi`,
  `battery`, `reboot`, `memory`) plus a new `dfu` are registered in the existing
  `xCommandList[]`, so `vRegisterCLICommands()`'s loop and the built-in `help`
  pick them up unchanged; every handler fits the 200-byte
  `configCOMMAND_INT_MAX_OUTPUT_SIZE` response except `log`, which chunks
  through FreeRTOS+CLI's multi-call protocol. `log` is deliberately not a
  straight port: the fork's capture ring saturated at 2047 bytes and then froze
  (its reader restored the tail it had just consumed) and it truncated the
  capture length to the TX-ring return value, so a full ring captured nothing;
  it now snapshots a 2048-byte linear window (`m1_csrc/m1_log_capture.h`) fed
  from the single log enqueue funnel, where every put overwrites the oldest
  bytes and only advances one free-running counter, so input is never refused
  and the window never freezes, and the newest bytes come back oldest-first.
  `reboot` and `dfu` acknowledge before they reset or jump (the response buffer
  is only sent after a handler returns, so the fork's message raced its reset);
  `battery` reports a failed gauge read instead of a 0 mV battery — including
  the state-of-charge register, which now goes through the checked
  `bq27421_readStateofCharge_percent()` rather than `bq27421_soc()` (the latter
  discards the I2C result and returns 0, so a NAK on that register alone printed
  "Level: 0 %" beside a valid voltage) — and `sdcard` probes capacity only when
  the cached status says the card is mounted, reports a failed probe with its
  own line instead of reusing the stale "OK" status, and takes the used
  percentage from a 64-bit helper (`m1_csrc/m1_sdcard_capacity.h`) because
  `used_kb * 100` in 32-bit arithmetic wrapped on any card more than ~41 GiB
  full. The `log` help string was shortened to fit the 200-byte help slot, and
  `prvHelpCommand()` now NUL-terminates the help string it copies: `strncpy()`
  wrote no terminator when a help string filled the buffer, so the caller's
  `strlen()` ran past the 200-byte output buffer — a pre-existing defect the
  289-byte `cdcstats` entry already triggered. `cdcstats` itself was shortened
  from 289 to 183 bytes at the same time, so every command's help line now fits
  the slot in full and the suite asserts that rule without an exception.
  `cmd_m1_mtest_help()` now lists every implemented `mtest` ID by decade, marks
  9 and 61 DEV-ONLY and 22/23/24 legacy-equivalent, and no longer advertises 53
  and 54, which no handler implements (both are 52). The destructive corruption
  test is re-homed to the free ID 9 behind the `M1_TEST_CMDS` dev guard and
  retargeted at the stored CRC word (`0x080FFC14`) the shipped boot failsafe
  validates, not the config magic the fork overwrote — `m1_boot_classify_fault()`
  reports metadata faults in preference to a CRC mismatch, so a magic fault
  would have masked the stage the test exists to exercise. Recorded correction
  to the issue: 22-24 needed no change, since the adopted base already
  implements direct R/G/B PWM under those IDs with the same calls and the same
  missing-parameter error, so only 9 actually collided; 5, 6, 61, 64, 68, 69,
  78 and 79 stay upstream-owned. `m1_vkb_get_password` is absent from `main`;
  the fork's addition (`legacy/v0.8.11`, fork commit `3b6c252`) is carried by
  merge patch #42 per `docs/merge/LEDGER.md` (`carried-by-patch`), so nothing is
  ported here and the acceptance criterion resolves as ledger-referenced rather
  than "absent in both trees". ROM cost of the unit: **+4728 B flash /
  +4128 B RAM** (FLASH 725680 -> 730408 B, RAM 326220 -> 330348 B, same
  toolchain, `./build.sh` on this tree versus `main` @ `598a803`; the RAM is
  the 2 KB window, the 2 KB snapshot and their bookkeeping). `_wCRC.bin` is
  still 1047576 bytes. New host targets `test_log_capture` (short, wrapped and
  oversized writes, the 2^32 cursor wrap, the no-init empty state),
  `test_cli_commands` (every table entry registered with a help string shorter
  than the response slot it is printed through, no orphan handlers, no unbounded
  write into the 200-byte buffer, the built-in `help` path's NUL terminator, the
  `reboot`/`dfu` flush-before-reset order, and a walked scan proving neither
  dev-only guard — `M1_TEST_CMDS` and `M1_SUBGHZ_CW_TEST_TX` — is defined by the
  project sources or by any build/config file in the project trees, nested
  `CMakeLists.txt`/`.cmake` included, in the forms this tree uses: in C only
  `#define` and `-D`, so `#if defined(...)` stays legal; in build/config files a
  whole-word macro name (the bare form `target_compile_definitions()` /
  `add_compile_definitions()` / `add_definitions()` takes) or the dashed
  `-D<macro>` form that shell `CFLAGS`, Makefiles and `add_definitions(-D...)`
  use) and `test_sdcard_usage` (the
  used-percentage helper at 0/32/64/128 GB used and across the ~41 GiB frontier
  the old 32-bit expression wrapped at), all three in `make test`, so a CLI
  addition cannot land without test-visible registration and the arithmetic is
  executed rather than merely read; `make test` also compiles the guarded
  `mtest 9` path (`arm-none-eabi-gcc -fsyntax-only -DM1_TEST_CMDS`) and prints
  `SKIP` with the reason when that compiler is not on `PATH`, so the dev path
  cannot rot silently; the reference page is `docs/protocols/cli.md`. No
  on-device behaviour is claimed: counter non-zero proof stays hardware-gated by
  issue #63, and the `mtest 9` recovery path belongs to the boot-recovery
  hardware gate (issue #41)
- Fork: CRC emitter parity harness (issue #40) — `scripts/verify_crc_parity.sh`
  generates four deterministic full-size (`0xFFC14`-byte) images from the
  committed spec in `tools/crc_test_vectors.py` (so no ~1 MiB binaries live in
  git) and checks each one: with `srec_cat` installed it runs both emitters and
  byte-compares the outputs, without it, it compares the Python emitter's
  SHA-256 and CRC word against `scripts/crc_parity_golden.txt`, whose values
  were captured from the authoritative `srec_cat` pipeline
- Fork: Sub-GHz Rx DMA deferred-context ingestion ported onto the adopted
  base (issue #52, closes the #22/#26/#27 stability carry from d3a0208) —
  GPDMA1 Channel 3 double-buffered RAW record capture (TIM1_CH1 request)
  replaces the per-edge TIM1_CC ISR while recording: the DMA ISR only
  queues completed half/full blocks and notifies the record task
  (deferred RTOS context switching preserved), with task-side pulse-delta
  processing, D-Cache invalidation, throttle/drop/half/total counters and
  restart-from-ISR plumbing; the scanner keeps the per-edge path. Also
  re-lands the USART1 ISR notify-value packing fix. The fork-base
  validation record is re-landed explicitly stale-marked
  (`documentation/stability_phase5_validation.md` — hardware re-validation
  on this base pending) with the carried/adapted/superseded port-notes
  accounting
- Fork: scoped warning hygiene restored on the adopted base (issue #39) —
  the genuine warning fixes embedded in the dropped zero-warning restyle are
  recovered from the merge ledger (`recovered-warning-fix` tags, 34 entries)
  as isolated commits, and `-Wall -Wextra -Werror` is enforced on the app
  sources (the `m1_csrc` translation units) only, via CMake source-file
  properties with `TARGET_DIRECTORY` scoping — vendor trees keep the relaxed
  default (see DEVELOPMENT.md "Scoped -Werror policy"); all remaining
  new-base app warnings the fork never saw are fixed in kind
- Fork: Windows verification adapters as repo artifacts (issue #49) — the
  gcc-nosan host-test compiler shim (strips ASan/UBSan flags the host gcc
  cannot link, demotes the three gcc-15 `-Werror=` classes that fire on
  harness-generated stubs) and the emulated `srec_cat` STM32 CRC emitter
  with the baseline reference vectors as self-tests, plus usage docs
  (`tools/WINDOWS_VERIFICATION.md`)
- Fork: adoption of the upstream 0.8.1.0 base (pinned `2af1f8a`, ancestry
  graft — fork history is now recorded as merged; legacy changes recoverable
  via `legacy/v0.8.11`, see DEVELOPMENT.md Merge Policy)
- Fork: EOL normalization policy (`.gitattributes`, LF repository + checkout)
  with a one-time renormalize of the upstream tree
- Fork: firmware version comparator test
  (`tools/test_fw_version_compare.py`) locking the rollback-gate ordering
  0.9.0.0 > 0.8.11 ≡ 0.8.11.0 > 0.8.1.0 and the display-only `-UFO` tag
- Fork: virgin-baseline evidence archive for the adopted base
  (`documentation/merge1-baseline-evidence.md`)
- Fork: merge ledger (`docs/merge/LEDGER.md`, issue #38) dispositioning all 113
  fork-changed files against the adopted base — 10 retained, 84 carried by named
  follow-up patches, 4 upstream-equivalent, 15 intentionally dropped with reasons,
  and 34 tagged warning-fix recovery sites for the scoped -Werror work — with
  re-captured IR-map/BKP-collision evidence and a zero-unknown gate
  (`cmake/m1_01/verify_merge_ledger.sh`); the `### Removed` entry below now
  resolves through it
- First public-release candidate
- STM32H573VIT6 support (2MB Flash, 100LQFP)
- Hardware revision 2.x support

### Changed

- Fork: `./build.sh --release` now runs the CRC emitter parity harness on the
  image the release ships (`build/<artifact>.bin`) and byte-compares the
  CRC-appended `<artifact>_wCRC.bin` — the exact bytes hashed into the manifest
  — against the emitters' derivation, before anything is copied to `artifacts/`;
  it refuses to write the release manifest if either check fails and drops any
  manifest left by an earlier run — plain `./build.sh` builds remove a stale one
  as well, since they overwrite `artifacts/` without writing a manifest — so
  emitter equivalence and golden-table integrity are gated by the only automated
  build path the repo has. The gate needs Python 3 on `PATH` even where
  `srec_cat` is installed; plain `./build.sh` builds skip it to keep local
  iteration fast (issue #40)
- Fork: version namespace frozen to 0.9.0.0-UFO, single-sourced from
  `m1_csrc/m1_fw_update_bl.h` and derived into the CMake project name,
  `build.sh` artifact stem, the about-box/boot-banner label, and the
  manager-protocol version export (was: hardcoded `MonstaTek_M1_v0800`
  stems in CMake/`build.sh`, a hardcoded `FW_BUILD_LABEL "FW 0.8.1.0"`,
  and `0.8.1.0` macro values)

### Removed

- Fork: the upstream-base adoption does not yet carry the legacy fork
  modules — this tree no longer builds the IR Universal Remote app
  (`m1_ir_universal`, Flipper Zero-compatible `.ir` file support), the
  Wi-Fi credential store (`m1_wifi_cred`) with its encryption helpers
  (`m1_crypto`), the ESP32 serial-flasher assets, and the host CRC
  tooling (`tools/append_crc32.py`, `tools/test_crc*.py`); 47 legacy
  paths in total (plus `cmake/driver_warnings.cmake`, `ROADMAP.md`,
  `Project_files.txt`, and the fork's USB DFU issue template). All are
  recoverable from `legacy/v0.8.11` and pending re-land in follow-up
  units; per-file disposition will live in `docs/merge/LEDGER.md`
  (tracked as issue #38)

### Fixed

- Fork: CRC parity evidence now enforces what it documents (issue #40) — the
  golden table's input digest column was never consulted (each generated image
  was compared against the generator's own stdout, so a corrupted recorded
  digest still printed `RESULT: PASS`) and a vector whose checks failed still
  printed `PASS:`; `scripts/verify_crc_parity.sh` now drives input provenance
  from `scripts/crc_parity_golden.txt`, fails on generator/table vector-name
  drift, and derives each vector's verdict from its own failure delta. It also
  runs the **real build image** (`build/<artifact>.bin`, or a path argument)
  through both emitters and asserts `0xFFC18` — previously every vector was
  synthetic — pins the documented short-image divergence (the Python emitter
  refuses a 400-byte input) and gates golden regeneration behind
  `--update-golden`, which refuses without `srec_cat` on `PATH` so the table
  cannot be re-baselined from the code under test. `build.sh` now also asserts
  the plain `<artifact>.bin` is exactly `0xFFC14` bytes and is a byte-identical
  prefix of the `_wCRC.bin`, closing the crop failure mode a total-size-only
  check accepts; docs and the CMake comment state the emitter equivalence for
  `0xFFC14`-byte images (the only shape the build produces) instead of
  unconditionally. `tools/m1cp_host_test/test_m1cp_fs.c` uses a portable
  `mkdir` wrapper so the aggregate host suite builds on MinGW as well as POSIX
  (9/9 targets green on Windows)
- Fork: canonical build no longer fails on hosts without SRecord (issue #40) —
  the `CMakeLists.txt` CRC step called `srec_cat` unconditionally, so on a
  Windows/Git-Bash host the POST_BUILD command failed, `build/` never got
  `_wCRC.bin`, and `build.sh` aborted with an empty `artifacts/`. The step now
  resolves exactly one emitter at configure time (srecord stays authoritative
  when it is on `PATH`, otherwise the byte-equivalent
  `tools/emulate_srec_cat_crc.py`; if neither exists, configure fails loudly
  instead of skipping the CRC) and prints the choice as a `CRC emitter:`
  status line. `build.sh` asserts the emitted `_wCRC.bin` is exactly `0xFFC18`
  (1047576) bytes in plain and `--release` mode, and the release manifest now
  hashes portably (`md5sum`/`md5 -q`, `sha256sum`/`shasum -a 256`, shared
  implementation in `scripts/portable_hash.sh`) with identical keys on every
  host
- Fork: filename/include case corrected for case-sensitive filesystems
  (issue #46) — `m1_csrc/Res_String.{c,h}` → `res_string.{c,h}` and
  `Battery/Golden_Image_0420.h` → `golden_image_0420.h` (renamed with
  `git mv`, so history is preserved), plus four include literals aligned to
  their on-disk spelling (`rfal_AnalogConfig.h` → `rfal_analogConfig.h` in the
  two legacy NFC driver files, `uiview.h` → `uiView.h` in the two EM4100/
  H10301 protocol files); the tree now compiles under Linux/WSL ext4, and a
  new `tools/check_case_sensitivity.py` (stdlib-only, `--fix` capable,
  collision-aware, checks quoted and angle-bracket include forms) enforces the
  invariant from the pre-commit hook. Concept source: upstream PR
  Monstatek/M1#23, whose `m1_sub_ghz.c` hunks are not taken
- Fork: small port-stability fixes re-landed from the legacy tree (issue #52
  carry, part 1 of 2) — `dat_filename` widened 64 → 256 bytes to stop SD
  data-file paths truncating at 63 chars (FatFS LFN names alone reach 255;
  the file-loader `fu_path_combine` join scales with `sizeof` automatically,
  no other bound-dependent site exists), the GPIO CTS wait in
  `SI446x_Wait_CTS()` bounded to 0xFFFF iterations so a stuck CTS line
  degrades to a timeout instead of hanging the radio task, and an
  `assert(0)` unreachable-guard added to the `SI446x_Change_Radio_Setting()`
  default case (fork origins 78d6b86 + `legacy/v0.8.11` radio stability
  hunks)
- Fork: non-portable C23 `0b` binary literals replaced with the equivalent
  hex constants in `Sub_Ghz/m1_sub_ghz_api.h` (13 values) and
  `m1_csrc/m1_system.h` (`BUTTON_LOGIC_HIGH`/`LOW`) so the documented
  ARM GNU 14.2 toolchain builds green with the global `-Wpedantic` intact
  under the scoped `-Werror` gate — gcc 14.2 diagnoses `0b` as a C23/GCC
  extension inside the gated app include closure, while the Windows host
  toolchain (gcc 15.2.1) accepts it, which hid the failure; milestone
  Merge 2
