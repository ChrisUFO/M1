<!-- See COPYING.txt for license details. -->

# Changelog

All notable changes to the M1 project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

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
  `CMakeLists.txt`/`.cmake` included, in the forms this tree uses: `#define` and
  `-D` in C, and the bare macro name that `target_compile_definitions()` /
  `add_compile_definitions()` takes) and `test_sdcard_usage` (the
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
