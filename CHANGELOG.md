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
  and put the device into a visible `FW CORRUPT!` recovery state with a seam
  for DFU entry. A validating boot clears the accounting. `S_M1_BK_REGS_t`
  now exposes those fields plus `swap_attempts` inside its existing 3 reserved
  bytes (still exactly 8 bytes = DR0/DR1, statically asserted; DR2/DR3 stay
  ESP-version-owned) via generalized `startup_config_write()`/
  `startup_config_read()` accessors, `SYS_CONFIG_MAGIC_NUMBER` is bumped to
  invalidate the old layout once, and the interactive rollback path counts its
  swaps with a one-swap-per-validated-boot loop guard. Boot-path IWDG option
  bytes are inspected read-only (the legacy per-boot rewrite is deliberately
  not ported). New host tests (`test_m1cp_failsafe`, guard test) and
  `scripts/check_no_raw_bkp_writes.sh` cover the logic and ban raw `TAMP->BKP`
  access outside the accessors
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
