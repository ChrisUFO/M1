<!-- See COPYING.txt for license details. -->

# Release delta audit — upstream/main..main at v0.9.0.0-UFO (issue #45)

The issue #45 final gate requires that `git diff upstream/main..main` show
**exactly the justified carry layer — nothing unexplained**. This document is
that audit, its table cut at the basis recorded below (provenance for the
cut, not the release head), and it is machine-checked:
`python tools/check_delta_audit.py` recomputes the diff at `HEAD` and
fails on any unexplained or stale row (exit 0 = zero unexplained rows).

## Basis

- Upstream side: `upstream/main` = MonstaTek/M1 pin
  `2af1f8a339dbe629f36a0551de4293b161179b71` (upstream 0.8.1.0). The gate
  asserts `git merge-base upstream/main <basis-end>` equals this pin, so the
  audit can never silently widen to include upstream's own later movement.
- Fork side: the basis recorded below — provenance for when the table was
  cut, not the release head. It must be an ancestor of `HEAD`; coverage is
  always evaluated at `HEAD`, so a commit made after the cut cannot add a
  differing file that escapes the audit.
- Enumeration: `git diff --name-only upstream/main HEAD` — every file whose
  blob differs.

## Method

Each differing file lands in exactly one class:

1. **Content** — the file has text hunks under a carriage-return-ignored diff
   (`git diff --ignore-cr-at-eol --numstat`). Only CRs are ignored, never all
   whitespace: a delta that changes nothing but whitespace inside a literal is
   a real content change and is audited, while the merge-1 CRLF→LF
   renormalization stays classifiable as EOL-only.
   Content rows cite the merge ledger
   ([LEDGER.md](LEDGER.md)) where a legacy-fork disposition exists, and
   otherwise carry an explicit merge-era explanation (gloss + landing PRs).
2. **EOL-only** — the blob differs but `--ignore-cr-at-eol` finds zero hunks: the merge-1
   LF renormalization (commit `07a24e8`, `.gitattributes` LF-in-repo policy)
   rewrote upstream's CRLF-stored files to LF with no content change. These
   are enumerated in the appendix and explained as a class; individually they
   carry no fork delta.

Pure case-only renames (`R100`, zero content delta) are listed in the content
table because `git diff --name-only` reports them under their new (lowercase)
names — a Linux-build fix, not a content change.

## Summary

basis-end: `c141d1eb8ac81e81aaf352a906b7e90cbe00e636`

| Content rows (table below) | **202** |
| — cited to a merge-ledger disposition | 73 |
| — explained as a merge-era change (gloss + PR refs) | 129 |
| EOL-only rows (appendix below) | **448** |
| Total differing files | **650** |

<!-- check_delta_audit: content-table begin -->
| File | Change | Ledger disposition | Merge-era explanation (PRs) |
|---|---|---|---|
| `.gitattributes` | A +37 | — (new file added by merge-era work) | LF-in-repo policy re-landed on the adopted base; drives the EOL-only class below (#48, #47) |
| `.github/CONTRIBUTING.md` | M +41 | — (changed by merge-era work (not a legacy-fork delta)) | fork contribution guide: ledger-coordination guidance (merge 1) plus the future-sync instruction added by this release (#51, #48) |
| `.github/workflows/host-tests.yml` | A +32 | — (new file added by merge-era work) | CI entry point running the wired host-test aggregator (PR-1) (#68, #66) |
| `.project` | M +1 | — (changed by merge-era work (not a legacy-fork delta)) | Eclipse project name tracks the derived artifact stem M1_v0.9.0.0-UFO (#48) |
| `.vscode/launch.json` | M +3 | — (changed by merge-era work (not a legacy-fork delta)) | debug config tracks the derived artifact stem (#47) |
| `.vscode/tasks.json` | M +5 | — (changed by merge-era work (not a legacy-fork delta)) | build task config tracks the derived artifact stem (#48) |
| `Battery/golden_image_0420.h` | rename | — (changed by merge-era work (not a legacy-fork delta)) | case-only rename (from Golden_Image_0420.h) so Linux builds resolve the include (issue #46, PR #56) (#56) |
| `CHANGELOG.md` | M +905 | intentionally-dropped (LEDGER.md) | see ledger row; later PRs: #93, #88, #86, #85, #82, #81, #78, #75, #72, #71, #68, #66, #65, #64, #62, #61, #59, #56, #57, #55, #54, #53, #51, #48, #47, #34, #33, #29, #28, #19, #18, #17, #15 |
| `CMakeLists.txt` | M +66 | retained-by-upstream (LEDGER.md) | see ledger row; later PRs: #59, #48, #47, #34, #29, #19, #17, #15 |
| `Core/Inc/stm32h5xx_hal_conf.h` | M +1 | — (changed by merge-era work (not a legacy-fork delta)) | HAL config enables the RNG module for the crypto foundation (PR-8) (#82) |
| `Core/Src/FreeRTOS_CLI.c` | M +13 | carried-by-patch -> #44 (LEDGER.md) | see ledger row; later PRs: #65, #53, #48, #19 |
| `Core/Src/app_freertos.c` | M +1 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #53, #48, #19 |
| `Core/Src/cli_app.c` | M +701 | carried-by-patch -> #44 (LEDGER.md) | see ledger row; later PRs: #65, #64, #48, #34, #29, #19, #17, #15 |
| `Core/Src/main.c` | M +80 | carried-by-patch -> #41 (LEDGER.md) | see ledger row; later PRs: #62, #48, #34 |
| `Core/Src/stm32h5xx_hal_msp.c` | M +68 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #82, #53, #48, #19 |
| `Core/Src/stm32h5xx_it.c` | M +1 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #53, #48, #19 |
| `DEVELOPMENT.md` | M +126 | retained-by-upstream (LEDGER.md) | see ledger row; later PRs: #56, #53, #48, #47, #34 |
| `Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_rng.h` | A +389 | — (new file added by merge-era work) | STM32H5 HAL RNG driver header, absent from the upstream CubeMX generate set, added for HW RNG (PR-8) (#82) |
| `Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_rng_ex.h` | A +263 | — (new file added by merge-era work) | STM32H5 HAL RNG extended driver header (PR-8) (#82) |
| `Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_rng.c` | A +1036 | — (new file added by merge-era work) | STM32H5 HAL RNG driver implementation (PR-8) (#82) |
| `Esp_spi_at/examples/at_spi_master/spi/stm32/main/esp_app_main.c` | M +352 | carried-by-patch -> #42 (LEDGER.md) | see ledger row; later PRs: #88, #48, #15 |
| `Esp_spi_at/examples/at_spi_master/spi/stm32/main/esp_at_list.h` | M +27 | carried-by-patch -> #42 (LEDGER.md) | see ledger row; later PRs: #88, #48, #15 |
| `Infrared/m1_ir_browser.c` | A +1978 | — (new file added by merge-era work) | IR file browser UX over the streaming loader (PR-5; favorites/recent + cycle-all wiring PR-6) (#78, #75) |
| `Infrared/m1_ir_browser.h` | A +87 | — (new file added by merge-era work) | IR file browser header incl. documented scan bounds (PR-5/PR-6) (#78, #75) |
| `Infrared/m1_ir_file.c` | A +1005 | — (new file added by merge-era work) | streaming per-device .ir loader with strict-skip (PR-4) (#72) |
| `Infrared/m1_ir_file.h` | A +159 | — (new file added by merge-era work) | streaming .ir loader API (PR-4) (#72) |
| `Infrared/m1_ir_remotes.c` | M +211 | — (changed by merge-era work (not a legacy-fork delta)) | upstream universal-remotes module: single-frame EOF wrap + favorites hooks (PR-4 carry, fix in PR-6 harden) (#78, #75, #72, #48) |
| `Infrared/m1_ir_remotes.h` | M +20 | — (changed by merge-era work (not a legacy-fork delta)) | upstream universal-remotes module header (PR-4/PR-6) (#78, #72, #48) |
| `NFC/NFC_drv/common/nfc_storage.c` | M +2 | intentionally-dropped (LEDGER.md) | see ledger row; later PRs: #53, #48, #18 |
| `NFC/NFC_drv/common/test/nfc_mfc_dict_ui_test.c` | M +22 | — (changed by merge-era work (not a legacy-fork delta)) | upstream NFC source-matching host test, retargeted to the LF-normalized tree (merge 1) (#48) |
| `NFC/NFC_drv/common/test/nfc_mfc_emu_ui_test.c` | M +4 | — (changed by merge-era work (not a legacy-fork delta)) | upstream NFC source-matching host test, retargeted to the LF-normalized tree (merge 1) (#48) |
| `NFC/NFC_drv/common/test/nfc_mfc_partial_result_ui_test.c` | M +4 | — (changed by merge-era work (not a legacy-fork delta)) | upstream NFC source-matching host test, retargeted to the LF-normalized tree (merge 1) (#48) |
| `NFC/NFC_drv/common/test/nfc_saved_back_navigation_test.c` | M +8 | — (changed by merge-era work (not a legacy-fork delta)) | upstream NFC source-matching host test, retargeted to the LF-normalized tree (merge 1) (#48) |
| `NFC/NFC_drv/common/test/nfc_t2t_unlock_test.c` | M +6 | — (changed by merge-era work (not a legacy-fork delta)) | upstream NFC source-matching host test, retargeted to the LF-normalized tree (merge 1) (#48) |
| `NFC/NFC_drv/legacy/nfc_listener.c` | M +1 | carried-by-patch -> #46 (LEDGER.md) | see ledger row; later PRs: #56, #48, #34 |
| `NFC/NFC_drv/legacy/nfc_poller.c` | M +1 | carried-by-patch -> #46 (LEDGER.md) | see ledger row; later PRs: #56, #48, #34 |
| `README.md` | M +29 | carried-by-patch -> #41 (LEDGER.md) | see ledger row; later PRs: #61, #51, #48, #47, #34, #29, #18, #17, #15 |
| `Sub_Ghz/m1_sub_ghz_api.c` | M +370 | carried-by-patch -> #52 (LEDGER.md) | see ledger row; later PRs: #81, #54, #48, #28, #19 |
| `Sub_Ghz/m1_sub_ghz_api.h` | M +81 | intentionally-dropped (LEDGER.md) | see ledger row; later PRs: #81, #57, #48, #28 |
| `Sub_Ghz/protocols/m1_secplus_v2_decode.c` | M +1 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #53, #48, #19 |
| `Sub_Ghz/radio_config/si4463_revc2_patch.h` | M +1 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #53, #48, #19 |
| `USB/Class/CDC/Inc/usbd_cdc_if.h` | M +47 | carried-by-patch -> #44 (LEDGER.md) | see ledger row; later PRs: #64, #48, #34 |
| `USB/Class/CDC/Src/usbd_cdc_if.c` | M +134 | carried-by-patch -> #44 (LEDGER.md) | see ledger row; later PRs: #64, #48, #34 |
| `build.sh` | M +115 | — (changed by merge-era work (not a legacy-fork delta)) | canonical cross-platform release build: derived stem, clean-tree gate, CRC contract, manifest (issue #40, PR #59) (#59, #48) |
| `cmake/m1_01/CMakeLists.txt` | M +42 | intentionally-dropped (LEDGER.md) | see ledger row; later PRs: #93, #86, #82, #75, #72, #71, #66, #53, #48, #19, #15 |
| `cmake/m1_01/verify_merge_ledger.sh` | A +75 | — (new file added by merge-era work) | merge-ledger completeness gate (issue #38, PR #51) (#51) |
| `docs/README.md` | M +13 | — (changed by merge-era work (not a legacy-fork delta)) | documentation index for the adopted base (merge 1), extended per unit (#75, #48) |
| `docs/architecture.md` | M +466 | — (changed by merge-era work (not a legacy-fork delta)) | new-base architecture overview, updated by every unit that changes subsystems (#93, #88, #86, #85, #82, #81, #78, #75, #71, #68, #66, #62, #61, #48) |
| `docs/build-and-debug.md` | M +142 | — (changed by merge-era work (not a legacy-fork delta)) | canonical build/CRC/manifest documentation (issues #40/#41) (#61, #59, #48) |
| `docs/ir-provisioning.md` | A +159 | — (new file added by merge-era work) | IR database provisioning guide (PR-5, bounds table PR-5 harden; starter-pack pointer added by this release) (#78, #75) |
| `docs/merge/DELTA_AUDIT.md` | A +733 | — (new file added by merge-era work) | this audit: the issue #45 final-gate record (this release) (—) |
| `docs/merge/LEDGER.md` | A +1968 | — (new file added by merge-era work) | merge ledger itself (issue #38, PR #51) (#51) |
| `docs/protocols/README.md` | M +2 | — (changed by merge-era work (not a legacy-fork delta)) | protocol index updated for the CLI and USB-CDC pages (issues #44) (#65, #64, #48) |
| `docs/protocols/cli.md` | A +291 | — (new file added by merge-era work) | console command reference for the ported CLI diagnostics (issue #44, PR #65) (#65) |
| `docs/protocols/firmware-update.md` | M +441 | — (changed by merge-era work (not a legacy-fork delta)) | flash layout, boot failsafe, DFU and rollback documentation (issue #41, PRs #61/#62; release-state sweep by this release) (#62, #61, #48) |
| `docs/protocols/usb-cdc.md` | A +135 | — (new file added by merge-era work) | USB CDC RX diagnostics documentation (issue #44, PRs #64/#65) (#65, #64) |
| `docs/releases.md` | M +60 | — (changed by merge-era work (not a legacy-fork delta)) | release and version policy page, rewritten for the 0.9.0.0-UFO release state by this release (#48) |
| `docs/releases/0.9.0.0-UFO.md` | A +208 | — (new file added by merge-era work) | v0.9.0.0-UFO release notes (this release) (—) |
| `docs/releases/0.9.0.0-ufo-smoke-checklist.md` | A +63 | — (new file added by merge-era work) | archived on-device smoke checklist aggregating the open hardware-acceptance issues (this release) (—) |
| `docs/releases/irdb-attribution.md` | A +166 | — (new file added by merge-era work) | CC0-1.0 text and attribution for the IR starter database (this release) (—) |
| `docs/security.md` | M +46 | — (changed by merge-era work (not a legacy-fork delta)) | security architecture incl. RNG/crypto notes (PR-8/PR-9) (#85, #82, #48) |
| `docs/security/wifi-credential-threat-model.md` | A +152 | — (new file added by merge-era work) | WiFi credential threat model (issue #42, PR-9) (#88, #85) |
| `documentation/0.9.0.0-ufo-verification.md` | A +233 | — (new file added by merge-era work) | release verification record: milestone state, version surfaces, build + parity evidence (this release) (—) |
| `documentation/mbt.md` | M +43 | — (changed by merge-era work (not a legacy-fork delta)) | build-tool walkthrough tracking the derived stem (issues #40/#41; stem sweep merge 1) (#61, #59, #48) |
| `documentation/merge1-baseline-evidence.md` | A +273 | — (new file added by merge-era work) | merge-1 virgin-baseline evidence at the upstream pin (issue #37) (#48) |
| `documentation/stability_phase5_validation.md` | A +132 | carried-by-patch -> #52 (LEDGER.md) | see ledger row; later PRs: #55, #48, #34 |
| `documentation/usingVSCode.md` | M +1 | — (changed by merge-era work (not a legacy-fork delta)) | VS Code walkthrough; build-target name swept to the derived stem by this release (#48) |
| `lfrfid/lfrfid.c` | M +9 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #53, #48, #19 |
| `lfrfid/lfrfid_protocol_em4100.c` | M +25 | carried-by-patch -> #46 (LEDGER.md) | see ledger row; later PRs: #56, #53, #48, #34, #19 |
| `lfrfid/lfrfid_protocol_h10301.c` | M +16 | carried-by-patch -> #46 (LEDGER.md) | see ledger row; later PRs: #56, #53, #48, #34, #19 |
| `lfrfid/t5577.c` | M +1 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #53, #48, #19 |
| `m1_csrc/m1_boot_failsafe.h` | A +170 | — (new file added by merge-era work) | boot failsafe API/contract header (issue #41, PR #61) (#61) |
| `m1_csrc/m1_bq27421.c` | M +44 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #65, #53, #48, #19 |
| `m1_csrc/m1_bq27421.h` | M +1 | — (changed by merge-era work (not a legacy-fork delta)) | fuel-gauge header: console-facing additions (issue #44, PR #65) (#65, #48) |
| `m1_csrc/m1_bt.c` | M +569 | upstream-equivalent (LEDGER.md) | see ledger row; later PRs: #93, #53, #48, #19 |
| `m1_csrc/m1_bt.h` | M +1 | — (changed by merge-era work (not a legacy-fork delta)) | Bluetooth app header: adv-editor menu wiring (PR-12) (#93, #48) |
| `m1_csrc/m1_bt_adv.c` | A +553 | — (new file added by merge-era work) | BLE advertisement preset core + editor logic, host-testable (PR-12) (#93) |
| `m1_csrc/m1_bt_adv.h` | A +213 | — (new file added by merge-era work) | BLE advertisement editor API (PR-12) (#93) |
| `m1_csrc/m1_cdc_rx_stats.h` | A +190 | — (new file added by merge-era work) | USB CDC RX drop/high-watermark counters (issue #44, PR #64) (#64) |
| `m1_csrc/m1_cli.c` | M +174 | carried-by-patch -> #44 (LEDGER.md) | see ledger row; later PRs: #75, #65, #53, #48, #29, #28, #19, #17 |
| `m1_csrc/m1_cli_help.c` | M +51 | carried-by-patch -> #44 (LEDGER.md) | see ledger row; later PRs: #65, #48, #28 |
| `m1_csrc/m1_crypto.c` | A +824 | carried-by-patch -> #42 (LEDGER.md) | see ledger row; later PRs: #82, #48, #15 |
| `m1_csrc/m1_crypto.h` | A +158 | carried-by-patch -> #42 (LEDGER.md) | see ledger row; later PRs: #82, #48, #15 |
| `m1_csrc/m1_dfu_entry.h` | A +285 | — (new file added by merge-era work) | ROM DFU entry/strap/recovery-offer API (issue #41, PR #62) (#62) |
| `m1_csrc/m1_display.c` | M +6 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #53, #48, #19 |
| `m1_csrc/m1_display.h` | M +1 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #53, #48, #19 |
| `m1_csrc/m1_esp32_fw_update.c` | M +11 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #68, #53, #48, #19 |
| `m1_csrc/m1_esp_feature_gate.c` | A +355 | — (new file added by merge-era work) | live ESP32 firmware feature gate, four states (PR-10) (#93, #88, #86) |
| `m1_csrc/m1_esp_feature_gate.h` | A +176 | — (new file added by merge-era work) | feature-gate API (PR-10) (#93, #88, #86) |
| `m1_csrc/m1_feedback_manager.c` | M +22 | — (changed by merge-era work (not a legacy-fork delta)) | feedback manager: LED-notification gate and brightness hooks (PR-2) (#68, #48) |
| `m1_csrc/m1_feedback_manager.h` | M +13 | — (changed by merge-era work (not a legacy-fork delta)) | feedback manager API (PR-2) (#68, #48) |
| `m1_csrc/m1_feedback_orchestration.c` | M +17 | — (changed by merge-era work (not a legacy-fork delta)) | feedback orchestration sequences gated by the LED setting (PR-2) (#68, #48) |
| `m1_csrc/m1_feedback_task.c` | M +9 | — (changed by merge-era work (not a legacy-fork delta)) | feedback task: synchronous awake-baseline seeding for boot-time brightness apply (PR-2) (#66, #48) |
| `m1_csrc/m1_file_browser.c` | M +14 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #53, #48, #33, #19 |
| `m1_csrc/m1_fusb302.c` | M +1 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #53, #48, #19 |
| `m1_csrc/m1_fw_update.c` | M +84 | carried-by-patch -> #41 (LEDGER.md) | see ledger row; later PRs: #62, #61, #53, #48, #34, #29, #19, #17, #15 |
| `m1_csrc/m1_fw_update.h` | M +8 | carried-by-patch -> #41 (LEDGER.md) | see ledger row; later PRs: #62, #48, #34, #17 |
| `m1_csrc/m1_fw_update_bl.c` | M +587 | carried-by-patch -> #41 (LEDGER.md) | see ledger row; later PRs: #62, #61, #53, #48, #34, #29, #19 |
| `m1_csrc/m1_fw_update_bl.h` | M +71 | carried-by-patch -> #41 (LEDGER.md) | see ledger row; later PRs: #62, #61, #48, #47, #34, #33, #29, #28, #19, #18, #15 |
| `m1_csrc/m1_gpio.c` | M +22 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #64, #53, #48, #19 |
| `m1_csrc/m1_infrared.c` | M +58 | carried-by-patch -> #43 (LEDGER.md) | see ledger row; later PRs: #75, #53, #48, #19 |
| `m1_csrc/m1_infrared.h` | M +16 | — (changed by merge-era work (not a legacy-fork delta)) | infrared app header: browser menu wiring (PR-5) (#75, #48) |
| `m1_csrc/m1_int_hdl.c` | M +101 | carried-by-patch -> #52 (LEDGER.md) | see ledger row; later PRs: #55, #48, #34, #19 |
| `m1_csrc/m1_isr_drop_counters.h` | M +7 | — (changed by merge-era work (not a legacy-fork delta)) | ISR drop counters for deferred-context ingestion (issue #52, PR #55) (#55, #48) |
| `m1_csrc/m1_lcd.c` | M +2 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #53, #48, #19 |
| `m1_csrc/m1_lib.c` | M +5 | intentionally-dropped (LEDGER.md) | see ledger row; later PRs: #53, #48, #28, #19 |
| `m1_csrc/m1_log_capture.h` | A +162 | — (new file added by merge-era work) | console log-capture buffer API (issue #44, PR #65) (#65) |
| `m1_csrc/m1_log_debug.c` | M +54 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #65, #64, #53, #48, #29, #19 |
| `m1_csrc/m1_log_debug.h` | M +11 | intentionally-dropped (LEDGER.md) | see ledger row; later PRs: #65, #48, #29 |
| `m1_csrc/m1_low_power.c` | M +2 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #53, #48, #19 |
| `m1_csrc/m1_lp5814.c` | M +1 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #53, #48, #19 |
| `m1_csrc/m1_lp5814.h` | M +2 | — (changed by merge-era work (not a legacy-fork delta)) | LED driver header: pedantic-clean binary literals (issue #46, PR #57) (#57, #48) |
| `m1_csrc/m1_manager_fwupdate.h` | M +6 | — (changed by merge-era work (not a legacy-fork delta)) | manager firmware-update protocol additions for the failsafe (issue #41) (#61, #48) |
| `m1_csrc/m1_menu.c` | M +147 | carried-by-patch -> #41 (LEDGER.md) | see ledger row; later PRs: #93, #88, #81, #78, #75, #71, #68, #53, #48, #19, #18, #17 |
| `m1_csrc/m1_nfc.c` | M +15 | upstream-equivalent (LEDGER.md) | see ledger row; later PRs: #53, #48, #34, #19, #18 |
| `m1_csrc/m1_power_ctl.c` | M +7 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #53, #48, #34, #19 |
| `m1_csrc/m1_rf_spi.c` | M +1 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #53, #48, #19 |
| `m1_csrc/m1_rfid.c` | M +26 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #53, #48, #34, #19 |
| `m1_csrc/m1_rng.c` | A +191 | — (new file added by merge-era work) | STM32H5 HW RNG wrapper: sticky-failure latch, no fallback (PR-8) (#82) |
| `m1_csrc/m1_rng.h` | A +74 | — (new file added by merge-era work) | HW RNG wrapper API (PR-8) (#82) |
| `m1_csrc/m1_rtc.c` | A +119 | — (new file added by merge-era work) | RTC read/write helpers with HAL shadow/init-mode semantics (PR-3) (#71) |
| `m1_csrc/m1_rtc.h` | A +67 | — (new file added by merge-era work) | RTC access API (PR-3) (#71) |
| `m1_csrc/m1_rtc_date.c` | A +209 | — (new file added by merge-era work) | date/time editor with validation and 24-hour stepping (PR-3) (#71) |
| `m1_csrc/m1_rtc_date.h` | A +63 | — (new file added by merge-era work) | date/time editor API (PR-3) (#71) |
| `m1_csrc/m1_sdcard.c` | M +5 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #53, #48, #19 |
| `m1_csrc/m1_sdcard_capacity.h` | A +41 | — (new file added by merge-era work) | SD capacity reporting for the System screen (issue #44 re-home, PR #65) (#65) |
| `m1_csrc/m1_sdcard_man.c` | M +12 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #53, #48, #19 |
| `m1_csrc/m1_sdcard_man.h` | M +1 | carried-by-patch -> #52 (LEDGER.md) | see ledger row; later PRs: #54, #48, #19 |
| `m1_csrc/m1_sdcard_provision.h` | M +5 | — (changed by merge-era work (not a legacy-fork delta)) | SD provisioning constants touched by the settings re-land (PR-1) (#66, #48) |
| `m1_csrc/m1_settings.c` | M +914 | retained-by-upstream (LEDGER.md) | see ledger row; later PRs: #81, #71, #68, #53, #48, #19 |
| `m1_csrc/m1_settings.h` | M +17 | — (changed by merge-era work (not a legacy-fork delta)) | settings menu: new leaves (LCD & Notifications, System, Date & Time) (PR-2/PR-3) (#81, #71, #68, #48) |
| `m1_csrc/m1_settings_store.c` | A +990 | — (new file added by merge-era work) | durable user-settings store on SD (settings.cfg, atomic save, tolerant parse) (PR-1, v2 migration PR-7) (#81, #66) |
| `m1_csrc/m1_settings_store.h` | A +167 | — (new file added by merge-era work) | settings-store API and format contract (PR-1) (#81, #66) |
| `m1_csrc/m1_storage.c` | M +3 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #53, #48, #19 |
| `m1_csrc/m1_sub_ghz.c` | M +948 | carried-by-patch -> #52 (LEDGER.md) | see ledger row; later PRs: #81, #55, #53, #48, #34, #28, #19, #18 |
| `m1_csrc/m1_sub_ghz.h` | M +27 | carried-by-patch -> #52 (LEDGER.md) | see ledger row; later PRs: #81, #55, #48, #34 |
| `m1_csrc/m1_sys_init.c` | M +49 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #82, #68, #66, #53, #48, #19 |
| `m1_csrc/m1_system.c` | M +476 | carried-by-patch -> #41 (LEDGER.md) | see ledger row; later PRs: #68, #62, #61, #53, #48, #34, #29, #19, #17, #15 |
| `m1_csrc/m1_system.h` | M +78 | carried-by-patch -> #41 (LEDGER.md) | see ledger row; later PRs: #68, #62, #61, #57, #48, #29, #17 |
| `m1_csrc/m1_tasks.c` | M +19 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #53, #48, #19 |
| `m1_csrc/m1_usb_cdc_msc.c` | M +0 | carried-by-patch -> #44 (LEDGER.md) | see ledger row; later PRs: #53, #48, #34, #19 |
| `m1_csrc/m1_watchdog.c` | M +3 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #53, #48, #19 |
| `m1_csrc/m1_wifi.c` | M +1325 | carried-by-patch -> #42 (LEDGER.md) | see ledger row; later PRs: #88, #85, #53, #48, #29, #19, #18, #15 |
| `m1_csrc/m1_wifi.h` | M +42 | carried-by-patch -> #42 (LEDGER.md) | see ledger row; later PRs: #88, #85, #48, #15 |
| `m1_csrc/privateprofilestring.c` | M +2 | carried-by-patch -> #39 (LEDGER.md) | see ledger row; later PRs: #53, #48, #19 |
| `m1_csrc/res_string.c` | rename | — (changed by merge-era work (not a legacy-fork delta)) | case-only rename (from Res_String.c; ledger row `m1_csrc/Res_String.c`, carried-by-patch) for case-sensitive filesystems (issue #46, PR #56) (#56) |
| `m1_csrc/res_string.h` | rename | — (changed by merge-era work (not a legacy-fork delta)) | case-only rename (from Res_String.h; ledger row `m1_csrc/Res_String.h`, carried-by-patch) for case-sensitive filesystems (issue #46, PR #56) (#56) |
| `m1_csrc/test/m1_bt_adv_test.py` | A +1483 | — (new file added by merge-era work) | wired host test suite for its unit (see PR refs) (#93) |
| `m1_csrc/test/m1_crypto_test.py` | A +876 | — (new file added by merge-era work) | wired host test suite for its unit (see PR refs) (#82) |
| `m1_csrc/test/m1_esp_feature_gate_test.py` | A +541 | — (new file added by merge-era work) | wired host test suite for its unit (see PR refs) (#93, #88, #86) |
| `m1_csrc/test/m1_ir_browser_test.py` | A +821 | — (new file added by merge-era work) | wired host test suite for its unit (see PR refs) (#78, #75) |
| `m1_csrc/test/m1_ir_favorites_test.py` | A +760 | — (new file added by merge-era work) | wired host test suite for its unit (see PR refs) (#78) |
| `m1_csrc/test/m1_ir_file_test.py` | A +492 | — (new file added by merge-era work) | wired host test suite for its unit (see PR refs) (#72) |
| `m1_csrc/test/m1_ir_golden_corpus_main.c` | A +317 | — (new file added by merge-era work) | wired host test suite for its unit (see PR refs) (#72) |
| `m1_csrc/test/m1_ir_golden_decode_test.py` | A +274 | — (new file added by merge-era work) | wired host test suite for its unit (see PR refs) (#72) |
| `m1_csrc/test/m1_ir_golden_tx_main.c` | A +460 | — (new file added by merge-era work) | wired host test suite for its unit (see PR refs) (#72) |
| `m1_csrc/test/m1_rtc_test.py` | A +476 | — (new file added by merge-era work) | wired host test suite for its unit (see PR refs) (#71) |
| `m1_csrc/test/m1_sdcard_provision_test.c` | M +4 | — (changed by merge-era work (not a legacy-fork delta)) | wired host test suite for its unit (see PR refs) (#48) |
| `m1_csrc/test/m1_settings_screens_test.py` | A +456 | — (new file added by merge-era work) | wired host test suite for its unit (see PR refs) (#71, #68) |
| `m1_csrc/test/m1_settings_store_test.py` | A +1537 | — (new file added by merge-era work) | wired host test suite for its unit (see PR refs) (#93, #81, #66) |
| `m1_csrc/test/m1_storage_menu_cleanup_test.c` | M +3 | — (changed by merge-era work (not a legacy-fork delta)) | wired host test suite for its unit (see PR refs) (#48) |
| `m1_csrc/test/m1_subghz_radio_settings_test.py` | A +727 | — (new file added by merge-era work) | wired host test suite for its unit (see PR refs) (#81) |
| `m1_csrc/test/m1_wifi_join_test.py` | A +956 | — (new file added by merge-era work) | wired host test suite for its unit (see PR refs) (#88) |
| `m1_csrc/test/m1_wifi_persistence_hardening_test.py` | M +798 | — (changed by merge-era work (not a legacy-fork delta)) | wired host test suite for its unit (see PR refs) (#85, #48) |
| `m1_csrc/test/stub_irmp_host/app_freertos.h` | A +6 | — (new file added by merge-era work) | IRMP host-build stub headers for the golden-decode harness (PR-4) (#72) |
| `m1_csrc/test/stub_irmp_host/irmp_host_cfg.h` | A +146 | — (new file added by merge-era work) | IRMP host-build stub headers for the golden-decode harness (PR-4) (#72) |
| `m1_csrc/test/stub_irmp_host/m1_infrared.h` | A +12 | — (new file added by merge-era work) | IRMP host-build stub headers for the golden-decode harness (PR-4) (#72) |
| `m1_csrc/test/stub_irmp_host/m1_ir_signals.h` | A +26 | — (new file added by merge-era work) | IRMP host-build stub headers for the golden-decode harness (PR-4) (#72) |
| `m1_csrc/test/stub_irmp_host/m1_log_debug.h` | A +18 | — (new file added by merge-era work) | IRMP host-build stub headers for the golden-decode harness (PR-4) (#72) |
| `m1_csrc/test/stub_irmp_host/m1_tasks.h` | A +5 | — (new file added by merge-era work) | IRMP host-build stub headers for the golden-decode harness (PR-4) (#72) |
| `m1_csrc/test/stub_irmp_host/main.h` | A +92 | — (new file added by merge-era work) | IRMP host-build stub headers for the golden-decode harness (PR-4) (#72) |
| `m1_csrc/tests/test_backlight_startup.py` | M +114 | — (changed by merge-era work (not a legacy-fork delta)) | pre-existing host test adjusted by its unit (see PR refs) (#68, #48) |
| `m1_csrc/tests/test_feedback_manager.c` | M +96 | — (changed by merge-era work (not a legacy-fork delta)) | pre-existing host test adjusted by its unit (see PR refs) (#68, #48) |
| `scripts/check_no_raw_bkp_writes.sh` | A +74 | — (new file added by merge-era work) | guard asserting no raw backup-register writes (issue #41, PR #61) (#61) |
| `scripts/crc_parity_golden.txt` | A +42 | — (new file added by merge-era work) | golden digests for the CRC emitter parity harness (issue #40, PR #59) (#59) |
| `scripts/fetch_ir_starter_db.sh` | A +163 | — (new file added by merge-era work) | reproducible fetch/pack step for the IR starter-database release asset at the pinned Flipper-IRDB commit (this release) (—) |
| `scripts/portable_hash.sh` | A +33 | — (new file added by merge-era work) | portable md5/sha256 helpers for Git-Bash/Linux/macOS (issue #40, PR #59) (#59) |
| `scripts/program.jlink` | M +5 | — (changed by merge-era work (not a legacy-fork delta)) | SWD programming script tracking the derived stem (#48, #47) |
| `scripts/run_host_tests.sh` | A +100 | — (new file added by merge-era work) | host-test aggregator running the wired suites (PR-1, extended per unit) (#93, #88, #86, #85, #82, #81, #78, #75, #72, #71, #68, #66) |
| `scripts/verify_crc_parity.sh` | A +491 | — (new file added by merge-era work) | CRC emitter parity harness (issue #40, PR #59) (#59) |
| `scripts/verify_esp_interface_contract.sh` | M +21 | — (changed by merge-era work (not a legacy-fork delta)) | ESP SPI interface contract check, updated for the version-derived build (issue #40) (#59, #48) |
| `tools/WINDOWS_VERIFICATION.md` | A +79 | — (new file added by merge-era work) | Windows verification adapter guide (issue #49) (#53) |
| `tools/check_case_sensitivity.py` | A +266 | carried-by-patch -> #46 (LEDGER.md) | see ledger row; later PRs: #56, #48, #34 |
| `tools/check_delta_audit.py` | A +410 | — (new file added by merge-era work) | machine gate behind this audit: regenerates and verifies zero unexplained rows (this release) (—) |
| `tools/crc_test_vectors.py` | A +106 | — (new file added by merge-era work) | deterministic CRC test-vector generator (issue #40, PR #59) (#59) |
| `tools/emulate_srec_cat_crc.py` | A +114 | — (new file added by merge-era work) | pure-Python srec_cat CRC emitter for hosts without SRecord (issue #49, PR #53) (#53) |
| `tools/gcc-nosan/gcc-nosan.c` | A +134 | — (new file added by merge-era work) | gcc shim landing the Windows verification adapters (issue #49, PR #53) (#53) |
| `tools/gen_merge_ledger.py` | A +1790 | — (new file added by merge-era work) | deterministic merge-ledger generator (issue #38, PR #51) (#51) |
| `tools/m1cp_host_test/.gitignore` | M +2 | — (changed by merge-era work (not a legacy-fork delta)) | host-test build-output ignores (issue #41) (#61, #48) |
| `tools/m1cp_host_test/Makefile` | M +62 | — (changed by merge-era work (not a legacy-fork delta)) | host-test build for the m1cp extraction suites (issues #41/#44) (#65, #64, #62, #61, #48) |
| `tools/m1cp_host_test/check_mtest_compile.sh` | A +142 | — (new file added by merge-era work) | mtest compile guard (issue #44, PR #65) (#65) |
| `tools/m1cp_host_test/seam_util.h` | A +140 | — (new file added by merge-era work) | shared seam helpers for the m1cp host tests (issue #44, PR #65) (#65) |
| `tools/m1cp_host_test/test_cdc_rx_stats.c` | A +582 | — (new file added by merge-era work) | CDC RX stats host test (issue #44, PRs #64/#65) (#65, #64) |
| `tools/m1cp_host_test/test_cli_commands.c` | A +861 | — (new file added by merge-era work) | console command host test incl. version grammar (issue #44, PR #65) (#65) |
| `tools/m1cp_host_test/test_log_capture.c` | A +240 | — (new file added by merge-era work) | log-capture host test (issue #44, PR #65) (#65) |
| `tools/m1cp_host_test/test_m1cp_dfu.c` | A +740 | — (new file added by merge-era work) | DFU entry host test (issue #41, PR #62) (#62) |
| `tools/m1cp_host_test/test_m1cp_failsafe.c` | A +339 | — (new file added by merge-era work) | boot failsafe host test (issue #41, PR #61) (#61) |
| `tools/m1cp_host_test/test_m1cp_fs.c` | M +16 | — (changed by merge-era work (not a legacy-fork delta)) | filesystem seam host test (issue #40) (#59, #48) |
| `tools/m1cp_host_test/test_no_raw_bkp_writes.sh` | A +108 | — (new file added by merge-era work) | backup-register write guard as a host test (issue #41, PR #61) (#61) |
| `tools/m1cp_host_test/test_sdcard_usage.c` | A +102 | — (new file added by merge-era work) | SD usage host test (issue #44 re-home, PR #65) (#65) |
| `tools/m1cp_host_test/test_update_intake_size.c` | A +308 | — (new file added by merge-era work) | update intake size host test (issue #41, PR #61) (#61) |
| `tools/test_fw_version_compare.py` | A +203 | — (new file added by merge-era work) | rollback-comparator host test pinning 0.9.0.0 > 0.8.11/0.8.1.0 (issues #36/#37) (#48, #47) |
<!-- check_delta_audit: content-table end -->

<!-- check_delta_audit: eol-list begin -->
```text
.cproject
.mxproject
.settings/com.st.stm32cube.ide.mcu.sfrview.prefs
.settings/org.eclipse.cdt.core.prefs
.settings/stm32cubeide.project.prefs
Changes log.txt
Core/Inc/FreeRTOSConfig.h
Core/Inc/app_freertos.h
Core/Inc/cli_app.h
Core/Inc/main.h
Core/Inc/stm32h5xx_it.h
Core/Src/stm32h5xx_hal_timebase_tim.c
Core/Src/system_stm32h5xx.c
Core/Startup/startup_stm32h573vitx.s
Drivers/CMSIS/Device/ST/STM32H5xx/Include/stm32h573xx.h
Drivers/CMSIS/Device/ST/STM32H5xx/Include/stm32h5xx.h
Drivers/CMSIS/Device/ST/STM32H5xx/Include/system_stm32h5xx.h
Drivers/CMSIS/Device/ST/STM32H5xx/LICENSE.md
Drivers/CMSIS/Device/ST/STM32H5xx/LICENSE.txt
Drivers/CMSIS/Include/cachel1_armv7.h
Drivers/CMSIS/Include/cmsis_armcc.h
Drivers/CMSIS/Include/cmsis_armclang.h
Drivers/CMSIS/Include/cmsis_armclang_ltm.h
Drivers/CMSIS/Include/cmsis_compiler.h
Drivers/CMSIS/Include/cmsis_gcc.h
Drivers/CMSIS/Include/cmsis_iccarm.h
Drivers/CMSIS/Include/cmsis_version.h
Drivers/CMSIS/Include/core_armv81mml.h
Drivers/CMSIS/Include/core_armv8mbl.h
Drivers/CMSIS/Include/core_armv8mml.h
Drivers/CMSIS/Include/core_cm0.h
Drivers/CMSIS/Include/core_cm0plus.h
Drivers/CMSIS/Include/core_cm1.h
Drivers/CMSIS/Include/core_cm23.h
Drivers/CMSIS/Include/core_cm3.h
Drivers/CMSIS/Include/core_cm33.h
Drivers/CMSIS/Include/core_cm35p.h
Drivers/CMSIS/Include/core_cm4.h
Drivers/CMSIS/Include/core_cm55.h
Drivers/CMSIS/Include/core_cm7.h
Drivers/CMSIS/Include/core_cm85.h
Drivers/CMSIS/Include/core_sc000.h
Drivers/CMSIS/Include/core_sc300.h
Drivers/CMSIS/Include/core_starmc1.h
Drivers/CMSIS/Include/mpu_armv7.h
Drivers/CMSIS/Include/mpu_armv8.h
Drivers/CMSIS/Include/pac_armv81.h
Drivers/CMSIS/Include/pmu_armv8.h
Drivers/CMSIS/Include/tz_context.h
Drivers/CMSIS/LICENSE.txt
Drivers/STM32H5xx_HAL_Driver/Inc/Legacy/stm32_hal_legacy.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_cortex.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_def.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_dma.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_dma_ex.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_exti.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_flash.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_flash_ex.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_gpio.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_gpio_ex.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_hcd.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_i2c.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_i2c_ex.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_icache.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_iwdg.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_pwr.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_pwr_ex.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_rcc.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_rcc_ex.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_rtc.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_rtc_ex.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_sd.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_sd_ex.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_spi.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_spi_ex.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_tim.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_tim_ex.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_uart.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_uart_ex.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_ll_bus.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_ll_cortex.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_ll_crs.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_ll_dlyb.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_ll_dma.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_ll_exti.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_ll_gpio.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_ll_i2c.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_ll_icache.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_ll_iwdg.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_ll_lpuart.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_ll_pwr.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_ll_rcc.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_ll_sdmmc.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_ll_spi.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_ll_system.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_ll_usart.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_ll_usb.h
Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_ll_utils.h
Drivers/STM32H5xx_HAL_Driver/LICENSE.md
Drivers/STM32H5xx_HAL_Driver/LICENSE.txt
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_cortex.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_dma.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_dma_ex.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_exti.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_flash.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_flash_ex.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_gpio.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_hcd.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_i2c.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_i2c_ex.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_icache.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_iwdg.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_pwr.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_pwr_ex.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_rcc.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_rcc_ex.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_sd.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_sd_ex.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_spi.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_spi_ex.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_tim.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_tim_ex.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_uart.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_uart_ex.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_ll_dlyb.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_ll_sdmmc.c
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_ll_usb.c
Drivers/u8g2_cppsrc/MUIU8g2.h
Drivers/u8g2_cppsrc/U8g2lib.cpp
Drivers/u8g2_cppsrc/U8g2lib.h
Drivers/u8g2_cppsrc/U8x8lib.cpp
Drivers/u8g2_cppsrc/U8x8lib.h
Drivers/u8g2_csrc/mui.c
Drivers/u8g2_csrc/mui.h
Drivers/u8g2_csrc/mui_u8g2.c
Drivers/u8g2_csrc/mui_u8g2.h
Drivers/u8g2_csrc/u8g2.h
Drivers/u8g2_csrc/u8g2_arc.c
Drivers/u8g2_csrc/u8g2_bitmap.c
Drivers/u8g2_csrc/u8g2_box.c
Drivers/u8g2_csrc/u8g2_buffer.c
Drivers/u8g2_csrc/u8g2_button.c
Drivers/u8g2_csrc/u8g2_circle.c
Drivers/u8g2_csrc/u8g2_cleardisplay.c
Drivers/u8g2_csrc/u8g2_d_memory.c
Drivers/u8g2_csrc/u8g2_d_setup.c
Drivers/u8g2_csrc/u8g2_font.c
Drivers/u8g2_csrc/u8g2_fonts.c
Drivers/u8g2_csrc/u8g2_fonts_selected.c
Drivers/u8g2_csrc/u8g2_hvline.c
Drivers/u8g2_csrc/u8g2_input_value.c
Drivers/u8g2_csrc/u8g2_intersection.c
Drivers/u8g2_csrc/u8g2_kerning.c
Drivers/u8g2_csrc/u8g2_line.c
Drivers/u8g2_csrc/u8g2_ll_hvline.c
Drivers/u8g2_csrc/u8g2_message.c
Drivers/u8g2_csrc/u8g2_polygon.c
Drivers/u8g2_csrc/u8g2_selection_list.c
Drivers/u8g2_csrc/u8g2_setup.c
Drivers/u8g2_csrc/u8log.c
Drivers/u8g2_csrc/u8log_u8g2.c
Drivers/u8g2_csrc/u8log_u8x8.c
Drivers/u8g2_csrc/u8x8.h
Drivers/u8g2_csrc/u8x8_8x8.c
Drivers/u8g2_csrc/u8x8_byte.c
Drivers/u8g2_csrc/u8x8_cad.c
Drivers/u8g2_csrc/u8x8_capture.c
Drivers/u8g2_csrc/u8x8_d_a2printer.c
Drivers/u8g2_csrc/u8x8_d_ch1120.c
Drivers/u8g2_csrc/u8x8_d_gp1247ai.c
Drivers/u8g2_csrc/u8x8_d_gp1287ai.c
Drivers/u8g2_csrc/u8x8_d_gp1294ai.c
Drivers/u8g2_csrc/u8x8_d_gu800.c
Drivers/u8g2_csrc/u8x8_d_hd44102.c
Drivers/u8g2_csrc/u8x8_d_il3820_296x128.c
Drivers/u8g2_csrc/u8x8_d_ist3020.c
Drivers/u8g2_csrc/u8x8_d_ist3088.c
Drivers/u8g2_csrc/u8x8_d_ist7920.c
Drivers/u8g2_csrc/u8x8_d_ks0108.c
Drivers/u8g2_csrc/u8x8_d_lc7981.c
Drivers/u8g2_csrc/u8x8_d_ld7032_60x32.c
Drivers/u8g2_csrc/u8x8_d_ls013b7dh03.c
Drivers/u8g2_csrc/u8x8_d_max7219.c
Drivers/u8g2_csrc/u8x8_d_pcd8544_84x48.c
Drivers/u8g2_csrc/u8x8_d_pcf8812.c
Drivers/u8g2_csrc/u8x8_d_pcf8814_hx1230.c
Drivers/u8g2_csrc/u8x8_d_s1d15300.c
Drivers/u8g2_csrc/u8x8_d_s1d15721.c
Drivers/u8g2_csrc/u8x8_d_s1d15e06.c
Drivers/u8g2_csrc/u8x8_d_sbn1661.c
Drivers/u8g2_csrc/u8x8_d_sed1330.c
Drivers/u8g2_csrc/u8x8_d_sh1106_64x32.c
Drivers/u8g2_csrc/u8x8_d_sh1106_72x40.c
Drivers/u8g2_csrc/u8x8_d_sh1107.c
Drivers/u8g2_csrc/u8x8_d_sh1108.c
Drivers/u8g2_csrc/u8x8_d_sh1122.c
Drivers/u8g2_csrc/u8x8_d_ssd1305.c
Drivers/u8g2_csrc/u8x8_d_ssd1306_128x32.c
Drivers/u8g2_csrc/u8x8_d_ssd1306_128x64_noname.c
Drivers/u8g2_csrc/u8x8_d_ssd1306_2040x16.c
Drivers/u8g2_csrc/u8x8_d_ssd1306_48x64.c
Drivers/u8g2_csrc/u8x8_d_ssd1306_64x32.c
Drivers/u8g2_csrc/u8x8_d_ssd1306_64x48.c
Drivers/u8g2_csrc/u8x8_d_ssd1306_72x40.c
Drivers/u8g2_csrc/u8x8_d_ssd1306_96x16.c
Drivers/u8g2_csrc/u8x8_d_ssd1306_96x40.c
Drivers/u8g2_csrc/u8x8_d_ssd1309.c
Drivers/u8g2_csrc/u8x8_d_ssd1312.c
Drivers/u8g2_csrc/u8x8_d_ssd1316.c
Drivers/u8g2_csrc/u8x8_d_ssd1317.c
Drivers/u8g2_csrc/u8x8_d_ssd1318.c
Drivers/u8g2_csrc/u8x8_d_ssd1320.c
Drivers/u8g2_csrc/u8x8_d_ssd1322.c
Drivers/u8g2_csrc/u8x8_d_ssd1325.c
Drivers/u8g2_csrc/u8x8_d_ssd1326.c
Drivers/u8g2_csrc/u8x8_d_ssd1327.c
Drivers/u8g2_csrc/u8x8_d_ssd1329.c
Drivers/u8g2_csrc/u8x8_d_ssd1362.c
Drivers/u8g2_csrc/u8x8_d_ssd1363.c
Drivers/u8g2_csrc/u8x8_d_ssd1606_172x72.c
Drivers/u8g2_csrc/u8x8_d_ssd1607_200x200.c
Drivers/u8g2_csrc/u8x8_d_st7302.c
Drivers/u8g2_csrc/u8x8_d_st7511.c
Drivers/u8g2_csrc/u8x8_d_st75160.c
Drivers/u8g2_csrc/u8x8_d_st75161.c
Drivers/u8g2_csrc/u8x8_d_st75256.c
Drivers/u8g2_csrc/u8x8_d_st7528.c
Drivers/u8g2_csrc/u8x8_d_st75320.c
Drivers/u8g2_csrc/u8x8_d_st7539.c
Drivers/u8g2_csrc/u8x8_d_st7565.c
Drivers/u8g2_csrc/u8x8_d_st7567.c
Drivers/u8g2_csrc/u8x8_d_st7571.c
Drivers/u8g2_csrc/u8x8_d_st7586s_erc240160.c
Drivers/u8g2_csrc/u8x8_d_st7586s_jlx320160.c
Drivers/u8g2_csrc/u8x8_d_st7586s_jlx384160.c
Drivers/u8g2_csrc/u8x8_d_st7586s_md240128.c
Drivers/u8g2_csrc/u8x8_d_st7586s_s028hn118a.c
Drivers/u8g2_csrc/u8x8_d_st7586s_ymc240160.c
Drivers/u8g2_csrc/u8x8_d_st7588.c
Drivers/u8g2_csrc/u8x8_d_st7920.c
Drivers/u8g2_csrc/u8x8_d_stdio.c
Drivers/u8g2_csrc/u8x8_d_t6963.c
Drivers/u8g2_csrc/u8x8_d_uc1601.c
Drivers/u8g2_csrc/u8x8_d_uc1604.c
Drivers/u8g2_csrc/u8x8_d_uc1608.c
Drivers/u8g2_csrc/u8x8_d_uc1609.c
Drivers/u8g2_csrc/u8x8_d_uc1610.c
Drivers/u8g2_csrc/u8x8_d_uc1611.c
Drivers/u8g2_csrc/u8x8_d_uc1617.c
Drivers/u8g2_csrc/u8x8_d_uc1628.c
Drivers/u8g2_csrc/u8x8_d_uc1638.c
Drivers/u8g2_csrc/u8x8_d_uc1701_dogs102.c
Drivers/u8g2_csrc/u8x8_d_uc1701_mini12864.c
Drivers/u8g2_csrc/u8x8_debounce.c
Drivers/u8g2_csrc/u8x8_display.c
Drivers/u8g2_csrc/u8x8_fonts.c
Drivers/u8g2_csrc/u8x8_gpio.c
Drivers/u8g2_csrc/u8x8_input_value.c
Drivers/u8g2_csrc/u8x8_message.c
Drivers/u8g2_csrc/u8x8_selection_list.c
Drivers/u8g2_csrc/u8x8_setup.c
Drivers/u8g2_csrc/u8x8_string.c
Drivers/u8g2_csrc/u8x8_u16toa.c
Drivers/u8g2_csrc/u8x8_u8toa.c
Infrared/irmp-irsnd/IR-Data/3xNEC3xAPPLE.log.txt
Infrared/irmp-irsnd/IR-Data/DK_Digital.txt
Infrared/irmp-irsnd/IR-Data/Dbox.txt
Infrared/irmp-irsnd/IR-Data/Grundig_TP715.txt
Infrared/irmp-irsnd/IR-Data/Grundig_TP715_SatTV.txt
Infrared/irmp-irsnd/IR-Data/Grundig_TP715_Video.txt
Infrared/irmp-irsnd/IR-Data/Grundig_TP715_lange.txt
Infrared/irmp-irsnd/IR-Data/Kathrein-UFS-912-Remote.txt
Infrared/irmp-irsnd/IR-Data/Matsushita.txt
Infrared/irmp-irsnd/IR-Data/Nokia.txt
Infrared/irmp-irsnd/IR-Data/Panasonic-Blue-Ray.txt
Infrared/irmp-irsnd/IR-Data/RC5-Taste.txt
Infrared/irmp-irsnd/IR-Data/Samsung_DVD_Rec_00062C.txt
Infrared/irmp-irsnd/IR-Data/Samsung_TV.txt
Infrared/irmp-irsnd/IR-Data/Siemens-Gigaset-M740AV-15kHz.txt
Infrared/irmp-irsnd/IR-Data/Sony-RM-S-310.txt
Infrared/irmp-irsnd/IR-Data/Sony-RM-U305C.txt
Infrared/irmp-irsnd/IR-Data/Sony-RM-X151.txt
Infrared/irmp-irsnd/IR-Data/Sony-RMT-D142P-DVD.txt
Infrared/irmp-irsnd/IR-Data/Sony-RMT-V406.txt
Infrared/irmp-irsnd/IR-Data/Sony_Bravia_RM-ED0009_new.txt
Infrared/irmp-irsnd/IR-Data/Sony_RM-S315_lange.txt
Infrared/irmp-irsnd/IR-Data/Yamaha-RAV388.txt
Infrared/irmp-irsnd/IR-Data/a1tvbox-15kHz.txt
Infrared/irmp-irsnd/IR-Data/a1tvbox-20kHz.txt
Infrared/irmp-irsnd/IR-Data/acp24-15kHz.txt
Infrared/irmp-irsnd/IR-Data/apple-15kHz.txt
Infrared/irmp-irsnd/IR-Data/apple-unibody-remote.txt
Infrared/irmp-irsnd/IR-Data/apple.txt
Infrared/irmp-irsnd/IR-Data/bo_beolink1000-10kHz.txt
Infrared/irmp-irsnd/IR-Data/bo_beolink1000-15kHz.txt
Infrared/irmp-irsnd/IR-Data/bose_wave_system_15khz.txt
Infrared/irmp-irsnd/IR-Data/denon-15kHz.txt
Infrared/irmp-irsnd/IR-Data/denon-rc-176-15kHz.txt
Infrared/irmp-irsnd/IR-Data/denon-rc-176-repeat-15kHz.txt
Infrared/irmp-irsnd/IR-Data/denon.txt
Infrared/irmp-irsnd/IR-Data/denon1_kurz_10khz.txt
Infrared/irmp-irsnd/IR-Data/denon3_kurz_10khz.txt
Infrared/irmp-irsnd/IR-Data/elta_radio.txt
Infrared/irmp-irsnd/IR-Data/fdc-20kHz.txt
Infrared/irmp-irsnd/IR-Data/fdc.txt
Infrared/irmp-irsnd/IR-Data/fdc2-20kHz.txt
Infrared/irmp-irsnd/IR-Data/irc-15kHz.txt
Infrared/irmp-irsnd/IR-Data/irmp16-15kHz.txt
Infrared/irmp-irsnd/IR-Data/jvc-nec.txt
Infrared/irmp-irsnd/IR-Data/jvc-rm-rk250-10kHz.txt
Infrared/irmp-irsnd/IR-Data/jvc.txt
Infrared/irmp-irsnd/IR-Data/kaseikyo-15kHz.txt
Infrared/irmp-irsnd/IR-Data/kathrein-15kHz.txt
Infrared/irmp-irsnd/IR-Data/lg-air-15kHz.txt
Infrared/irmp-irsnd/IR-Data/merlin-15kHz.txt
Infrared/irmp-irsnd/IR-Data/nec-non-std-rep.txt
Infrared/irmp-irsnd/IR-Data/nec-repetition.txt
Infrared/irmp-irsnd/IR-Data/nec-skymaster-dt500.txt
Infrared/irmp-irsnd/IR-Data/nec.txt
Infrared/irmp-irsnd/IR-Data/nikon.txt
Infrared/irmp-irsnd/IR-Data/nubert-subwoofer.txt
Infrared/irmp-irsnd/IR-Data/orion_vcr_07660BM070.txt
Infrared/irmp-irsnd/IR-Data/ortek-hama-10kHz.txt
Infrared/irmp-irsnd/IR-Data/panasonic-15kHz.txt
Infrared/irmp-irsnd/IR-Data/panasonic-scan.txt
Infrared/irmp-irsnd/IR-Data/panasonic-vcr-15kHz.txt
Infrared/irmp-irsnd/IR-Data/panasonic_DVD_N2QAYB000333.txt
Infrared/irmp-irsnd/IR-Data/pentax-15kHz.txt
Infrared/irmp-irsnd/IR-Data/rc-car-20kHz.txt
Infrared/irmp-irsnd/IR-Data/rc-car.txt
Infrared/irmp-irsnd/IR-Data/rc5-philipps-15kHz.txt
Infrared/irmp-irsnd/IR-Data/rc5.txt
Infrared/irmp-irsnd/IR-Data/rc5x-79.txt
Infrared/irmp-irsnd/IR-Data/rc5x.txt
Infrared/irmp-irsnd/IR-Data/rc6-hold.txt
Infrared/irmp-irsnd/IR-Data/rc6.txt
Infrared/irmp-irsnd/IR-Data/rc6a-siemens-15kHz.txt
Infrared/irmp-irsnd/IR-Data/rcii-15kHz.txt
Infrared/irmp-irsnd/IR-Data/rcmm-20kHz.txt
Infrared/irmp-irsnd/IR-Data/recs80-15kHz.txt
Infrared/irmp-irsnd/IR-Data/roomba-15kHz.txt
Infrared/irmp-irsnd/IR-Data/s100-56-15kHz.txt
Infrared/irmp-irsnd/IR-Data/saa3004-15kHz.txt
Infrared/irmp-irsnd/IR-Data/saa3004-20kHz.txt
Infrared/irmp-irsnd/IR-Data/samsung-br-15kHz.txt
Infrared/irmp-irsnd/IR-Data/samsung32-15kHz.txt
Infrared/irmp-irsnd/IR-Data/samsung32-tv-15kHz.txt
Infrared/irmp-irsnd/IR-Data/samsung48-15kHz.txt
Infrared/irmp-irsnd/IR-Data/sharp-denon.txt
Infrared/irmp-irsnd/IR-Data/sharp-denon2.txt
Infrared/irmp-irsnd/IR-Data/sharp_15khz.txt
Infrared/irmp-irsnd/IR-Data/sharp_kurz_10khz.txt
Infrared/irmp-irsnd/IR-Data/sharp_lang_10khz.txt
Infrared/irmp-irsnd/IR-Data/sony-rm-s311.txt
Infrared/irmp-irsnd/IR-Data/sony-television-service-commander.txt
Infrared/irmp-irsnd/IR-Data/speaker-15kHz.txt
Infrared/irmp-irsnd/IR-Data/t-home-mediareceiver-15kHz.txt
Infrared/irmp-irsnd/IR-Data/t-home-mediareceiver.txt
Infrared/irmp-irsnd/IR-Data/technics-15kHz.txt
Infrared/irmp-irsnd/IR-Data/telefunken-1560-20kHz.txt
Infrared/irmp-irsnd/IR-Data/thomson-mb100-15kHz.txt
Infrared/irmp-irsnd/IR-Data/tp400vt-15kHz.txt
Infrared/irmp-irsnd/IR-Data/universal-15kHz.txt
Infrared/irmp-irsnd/IR-Data/ventilator-15kHz.txt
Infrared/irmp-irsnd/IR-Data/vincent-15kHz.txt
Infrared/irmp-irsnd/IR-Data/vincent-flash-15kHz.txt
Infrared/irmp-irsnd/IR-Data/xbox360-10kHz.txt
Infrared/irmp-irsnd/IR-Data/xbox360-15kHz.txt
Infrared/irmp-irsnd/README.txt
Infrared/irmp-irsnd/irmp.c
Infrared/irmp-irsnd/irmp.h
Infrared/irmp-irsnd/irmpconfig.h
Infrared/irmp-irsnd/irmpextlog.c
Infrared/irmp-irsnd/irmpextlog.h
Infrared/irmp-irsnd/irmpprotocols.h
Infrared/irmp-irsnd/irmpsystem.h
Infrared/irmp-irsnd/irsnd.c
Infrared/irmp-irsnd/irsnd.h
Infrared/irmp-irsnd/irsndconfig.h
Middlewares/Third_Party/CMSIS/RTOS2/Include/cmsis_os2.h
Middlewares/Third_Party/CMSIS/RTOS2/Include/os_tick.h
Middlewares/Third_Party/FreeRTOS/LICENSE.md
Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/cmsis_os.h
Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/cmsis_os2.c
Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/freertos_mpool.h
Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/freertos_os2.h
Middlewares/Third_Party/FreeRTOS/Source/LICENSE
Middlewares/Third_Party/FreeRTOS/Source/croutine.c
Middlewares/Third_Party/FreeRTOS/Source/event_groups.c
Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h
Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOSConfig_template.h
Middlewares/Third_Party/FreeRTOS/Source/include/StackMacros.h
Middlewares/Third_Party/FreeRTOS/Source/include/atomic.h
Middlewares/Third_Party/FreeRTOS/Source/include/croutine.h
Middlewares/Third_Party/FreeRTOS/Source/include/deprecated_definitions.h
Middlewares/Third_Party/FreeRTOS/Source/include/event_groups.h
Middlewares/Third_Party/FreeRTOS/Source/include/list.h
Middlewares/Third_Party/FreeRTOS/Source/include/message_buffer.h
Middlewares/Third_Party/FreeRTOS/Source/include/mpu_prototypes.h
Middlewares/Third_Party/FreeRTOS/Source/include/mpu_wrappers.h
Middlewares/Third_Party/FreeRTOS/Source/include/portable.h
Middlewares/Third_Party/FreeRTOS/Source/include/projdefs.h
Middlewares/Third_Party/FreeRTOS/Source/include/queue.h
Middlewares/Third_Party/FreeRTOS/Source/include/semphr.h
Middlewares/Third_Party/FreeRTOS/Source/include/stack_macros.h
Middlewares/Third_Party/FreeRTOS/Source/include/stream_buffer.h
Middlewares/Third_Party/FreeRTOS/Source/include/task.h
Middlewares/Third_Party/FreeRTOS/Source/include/timers.h
Middlewares/Third_Party/FreeRTOS/Source/list.c
Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33_NTZ/non_secure/port.c
Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33_NTZ/non_secure/portasm.c
Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33_NTZ/non_secure/portasm.h
Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33_NTZ/non_secure/portmacro.h
Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33_NTZ/non_secure/portmacrocommon.h
Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c
Middlewares/Third_Party/FreeRTOS/Source/queue.c
Middlewares/Third_Party/FreeRTOS/Source/stream_buffer.c
Middlewares/Third_Party/FreeRTOS/Source/tasks.c
Middlewares/Third_Party/FreeRTOS/Source/timers.c
NFC/NFC_drv/common/m1_mfc_dma.c
NFC/NFC_drv/common/test/ce_dev_type_test.c
Project
m1_csrc/m1_bq25896.c
m1_csrc/m1_bq25896.h
m1_csrc/m1_buzzer.c
m1_csrc/m1_buzzer.h
m1_csrc/m1_cli.h
m1_csrc/m1_compile_cfg.h
m1_csrc/m1_esp32_hal.c
m1_csrc/m1_esp32_hal.h
m1_csrc/m1_file_browser.h
m1_csrc/m1_gpio.h
m1_csrc/m1_i2c.c
m1_csrc/m1_i2c.h
m1_csrc/m1_int_hdl.h
m1_csrc/m1_lcd.h
m1_csrc/m1_lp5562.c
m1_csrc/m1_lp5562.h
m1_csrc/m1_menu.h
m1_csrc/m1_nfc.h
m1_csrc/m1_power_ctl.h
m1_csrc/m1_rfid.h
m1_csrc/m1_sdcard.h
m1_csrc/m1_storage.h
m1_csrc/m1_tasks.h
m1_csrc/m1_types_def.h
```
<!-- check_delta_audit: eol-list end -->

## Appendix: EOL-only files

Every file below differs from `upstream/main` **only** by line endings (zero
content hunks under `git diff --ignore-cr-at-eol`); the merge-1 LF renormalization explains
them as a class. The gate holds the list exhaustive: a file may not appear
here unless it is in the recorded diff with no content delta, and no EOL-only
file may be missing.

## Re-running the gate

```sh
python tools/check_delta_audit.py
```

Requires the `upstream` remote fetched. The gate re-derives both classes at
`HEAD`, re-reads the ledger dispositions at `HEAD`, and verifies the tables
above row for row — fail-closed on anything unexplained, missing, or stale,
and on a recorded basis that is not an ancestor of `HEAD`. Regenerate the body after a future release cut with
`python tools/check_delta_audit.py --emit` (see the tool header for details).
