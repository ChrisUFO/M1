<!-- See COPYING.txt for license details. -->

# Changelog

All notable changes to the M1 project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

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
