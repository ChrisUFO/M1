<!-- See COPYING.txt for license details. -->

# Changelog

All notable changes to the M1 project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

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
