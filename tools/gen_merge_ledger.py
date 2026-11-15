#!/usr/bin/env python3
# See COPYING.txt for license details.
"""Generate docs/merge/LEDGER.md — the merge-1 fork-file disposition ledger.

Enumerates every file that differs between the pre-merge fork point
(93acd751, the commit the fork diverged from upstream) and the archived fork
tip (tag legacy/v0.8.11), classifies each as both-changed vs ours-only against
upstream's evolution, and emits one ledger section per file together with
name-status, whitespace-ignored numstat, hunk summaries and fork origin SHAs.

The per-file DISPOSITIONS table below is the single source of truth for the
audit prose; re-running this script regenerates the ledger deterministically:

    python tools/gen_merge_ledger.py            # writes docs/merge/LEDGER.md
    python tools/gen_merge_ledger.py --check    # verify the checked-in ledger
                                               # matches regeneration exactly

The hard invariant: the enumeration must yield exactly EXPECTED_FILES entries
(83 both-changed + 30 ours-only). Anything else fails loud — do not proceed
with a wrong basis.

Gate: cmake/m1_01/verify_merge_ledger.sh independently re-derives the file set
from git and fails on any unknown/missing disposition.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

FORK_POINT = (
    "93acd751de4acc2a77589060cd0e762dbbec2bc5"  # upstream commit the fork diverged from
)
LEGACY = "legacy/v0.8.11"  # archived fork tip (immutable tag)
UPSTREAM = "upstream/main"  # upstream evolution since fork point
MAIN = "main"  # adopted new base (post PR-1/PR-2)

EXPECTED_FILES = 113
EXPECTED_BOTH_CHANGED = 83
EXPECTED_OURS_ONLY = 30

DISPOSITION_VALUES = (
    "retained-by-upstream",
    "carried-by-patch",
    "upstream-equivalent",
    "intentionally-dropped",
)


def git(*args: str) -> str:
    return subprocess.run(
        ["git", "-C", str(REPO), *args],
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    ).stdout


def enumerate_files():
    """Return (ordered file list, name-status map, class map, numstat map)."""
    fork_files = git("diff", "--name-only", FORK_POINT, LEGACY).splitlines()
    upstream_files = set(git("diff", "--name-only", FORK_POINT, UPSTREAM).splitlines())

    name_status = {}
    for line in git("diff", "--name-status", FORK_POINT, LEGACY).splitlines():
        status, path = line.split("\t", 1)
        name_status[path] = status

    classes = {}
    for f in fork_files:
        classes[f] = "ours-only" if f not in upstream_files else "both-changed"

    numstat = {}
    for line in git("diff", "-w", "--numstat", FORK_POINT, LEGACY).splitlines():
        add, del_, path = line.split("\t")
        numstat[path] = (add, del_)

    return fork_files, name_status, classes, numstat


def hunk_summaries(path: str, limit: int = 3):
    """First `limit` hunk headers of the -w fork delta, plus a total count."""
    diff = git("diff", "-w", FORK_POINT, LEGACY, "--", path)
    headers = re.findall(r"^@@ [^@]*@@.*$", diff, flags=re.MULTILINE)
    total = len(headers)
    shown = headers[:limit]
    more = total - len(shown)
    return shown, total, more


def origin_log(path: str, limit: int = 5):
    """Fork commits that touched the file (origin evidence for carries)."""
    log = git(
        "log", "--oneline", "--format=%h %s", f"{FORK_POINT}..{LEGACY}", "--", path
    )
    lines = log.splitlines()
    more = len(lines) - limit
    shown = lines[:limit]
    if more > 0:
        shown.append(f"(+{more} more)")
    return shown


# ---------------------------------------------------------------------------
# Per-file dispositions. THIS TABLE IS THE AUDIT — one disposition per file,
# evidence cited (file:line / SHA / command). Anything absent here renders as
# `unknown` and fails the gate.
#
# Entry fields:
#   disposition : one of DISPOSITION_VALUES
#   carry       : issue number(s) of the P4 carry patch (carried-by-patch only)
#   tags        : e.g. "recovered-warning-fix" (mined by PR-4 / issue #39)
#   evidence    : multi-line markdown, cite-heavy
# ---------------------------------------------------------------------------

DISPOSITIONS = {
    ".gitignore": {
        "disposition": "retained-by-upstream",
        "evidence": (
            "Fork-relevant hygiene (ignore build outputs/artifacts) is on the new base via the\n"
            "PR-1 re-land: `main:.gitignore` covers `out/ build/ build_*/ artifacts/ CMakeFiles/`\n"
            "(lines 66-70). The fork-only remainder (PLAN.md, `.claude/*`, `review_*.json`,\n"
            "`*log.txt`, `*.diff`) is session-local tooling residue, intentionally not carried\n"
            "(PR-1's merged set is the reviewed baseline; extend in a follow-up if the shared\n"
            "tree needs it). Evidence: `git diff -w legacy/v0.8.11 main -- .gitignore` shows only\n"
            "those personal patterns missing.\n"
        ),
    },
    "CMakeFiles/4.0.2/CMakeSystem.cmake": {
        "disposition": "retained-by-upstream",
        "evidence": (
            "Tracked-by-upstream build artifact (existed at the fork point); the fork deleted it and\n"
            "so did upstream later. Both trees clean: `git ls-tree upstream/main CMakeFiles/` is\n"
            "empty and `git ls-files main -- CMakeFiles/` is empty. PR-1's `.gitignore` keeps\n"
            "`CMakeFiles/` ignored (main:.gitignore:70). Nothing to carry.\n"
        ),
    },
    "CMakeFiles/4.0.2/VCTargetsPath.vcxproj": {
        "disposition": "retained-by-upstream",
        "evidence": (
            "Tracked-by-upstream build artifact (existed at the fork point); the fork deleted it and\n"
            "so did upstream later. Both trees clean: `git ls-tree upstream/main CMakeFiles/` is\n"
            "empty and `git ls-files main -- CMakeFiles/` is empty. PR-1's `.gitignore` keeps\n"
            "`CMakeFiles/` ignored (main:.gitignore:70). Nothing to carry.\n"
        ),
    },
    "CMakeFiles/CMakeConfigureLog.yaml": {
        "disposition": "retained-by-upstream",
        "evidence": (
            "Tracked-by-upstream build artifact (existed at the fork point); the fork deleted it and\n"
            "so did upstream later. Both trees clean: `git ls-tree upstream/main CMakeFiles/` is\n"
            "empty and `git ls-files main -- CMakeFiles/` is empty. PR-1's `.gitignore` keeps\n"
            "`CMakeFiles/` ignored (main:.gitignore:70). Nothing to carry.\n"
        ),
    },
    "CMakeFiles/cmake.check_cache": {
        "disposition": "retained-by-upstream",
        "evidence": (
            "Tracked-by-upstream build artifact (existed at the fork point); the fork deleted it and\n"
            "so did upstream later. Both trees clean: `git ls-tree upstream/main CMakeFiles/` is\n"
            "empty and `git ls-files main -- CMakeFiles/` is empty. PR-1's `.gitignore` keeps\n"
            "`CMakeFiles/` ignored (main:.gitignore:70). Nothing to carry.\n"
        ),
    },
    "CMakeLists.txt": {
        "disposition": "retained-by-upstream",
        "evidence": (
            "The fork-relevant content — version-derived project name — is on the new base in\n"
            "stronger form via the PR-2 re-land: `main:CMakeLists.txt:27-41` single-sources\n"
            "`M1_v{MAJOR}.{MINOR}.{BUILD}.{RC}-{FORK_TAG}` from `m1_csrc/m1_fw_update_bl.h`\n"
            "(the fork's own derivation dropped the RC field). Fork post-build CRC steps\n"
            "(append_crc32.py invocations) are NOT on main — carried under #40 with the tooling\n"
            "(see `tools/append_crc32.py`); noted here so the hunk is not lost.\n"
        ),
    },
    "CMakePresets.json": {
        "disposition": "retained-by-upstream",
        "evidence": (
            "Fork delta = toolchain path update to `Arm GNU Toolchain arm-none-eabi/14.2 rel1`;\n"
            "already on the new base: `main:CMakePresets.json:13` carries the identical path.\n"
            "`git diff -w legacy/v0.8.11 main -- CMakePresets.json` is whitespace-only apart from\n"
            "binaryDir naming. Nothing to carry.\n"
        ),
    },
    "m1_csrc/m1_buzzer.c": {
        "disposition": "retained-by-upstream",
        "evidence": (
            "The fork's entire -w delta is one line — `(void)xTimer;` in `buzzer_sys_deinit()`\n"
            "(unused-parameter warning fix). Upstream made the same fix independently:\n"
            "`main:m1_csrc/m1_buzzer.c:191`. Nothing to carry.\n"
        ),
    },
    "DEVELOPMENT.md": {
        "disposition": "retained-by-upstream",
        "evidence": (
            "Fork delta vs fork point is a single line (an NFC/RFID/Sub-GHz verification bullet).\n"
            "The new base carries the comprehensive PR-1/PR-2 `DEVELOPMENT.md` (tag policy,\n"
            "renormalize workflow, merge policy) which subsumes it. Nothing to carry.\n"
        ),
    },
    "m1_csrc/m1_settings.c": {
        "disposition": "retained-by-upstream",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork-relevant content = about-box fork-tag display. Retained on the new base via\n"
            "PR-2: `main:m1_csrc/m1_settings.c:204` draws `FW_BUILD_LABEL` which expands to\n"
            "`FW 0.9.0.0-UFO` (`main:m1_fw_update_bl.h:50-52`) — a cleaner re-land of the fork's\n"
            "hardcoded `FW_VERSION_FORK_TAG` row (legacy m1_settings.c about screen).\n"
            "Carried hunks (tagged): the `uint8_t prn_name[20]` -> `char` + `(char *)` cast\n"
            "pointer-sign fixes at the same function — `main:m1_settings.c:187-200` still has the\n"
            "unfixed `uint8_t` buffer passed to `sprintf`/`u8g2_DrawStr(char*)`. #39 re-applies.\n"
            "Feature-stub hunks (settings_system/settings_lcd_and_notifications): see Phase 3.\n"
        ),
    },
    "m1_csrc/m1_bt.c": {
        "disposition": "upstream-equivalent",
        "evidence": (
            "Fork delta vs fork point is 6 lines: BSSID hex-formatting in the BLE scan list\n"
            "(legacy m1_bt.c:483-493). Upstream rewrote the whole BT subsystem on the new base\n"
            "(2474 lines: `bluetooth_scan_devices`/`bluetooth_signal_meter`/\n"
            "`bluetooth_gatt_explorer`/`bluetooth_saved`, main:m1_bt.c:132-137) with native\n"
            "BSSID-string handling (`char bssid[BSSID_STR_SIZE]`, main:m1_bt.c:86,99,118) —\n"
            "the fork's formatting intent is achieved better upstream. `bluetooth_config()` (#11\n"
            "stub) exists nowhere on main — verdict in Phase 3.\n"
        ),
    },
    "m1_csrc/m1_nfc.c": {
        "disposition": "upstream-equivalent",
        "evidence": (
            "Fork delta = `nfc_*` -> `m1_nfc_*` symbol renames + warning hygiene\n"
            "((void)param, %p casts) on the pre-rewrite code. Upstream rewrote the file on the\n"
            "new base (`git diff -w --numstat legacy/v0.8.11 main -- m1_csrc/m1_nfc.c` =\n"
            "+6652/-692 — MFC raw sessions, dict attack, T2T, DESFire) and kept the `nfc_*`\n"
            "naming (main:m1_nfc.h:16-37). Renames are churn against the rewrite; the warning\n"
            "hygiene applies to code that no longer exists. Project-wide warning hygiene on the\n"
            "NEW code is restored by #39, not by carrying these hunks.\n"
        ),
    },
    "m1_csrc/m1_nfc.h": {
        "disposition": "upstream-equivalent",
        "evidence": (
            "Fork delta = the same `nfc_*` -> `m1_nfc_*` renames only. New base kept the\n"
            "`nfc_*` API and extended it (main:m1_csrc/m1_nfc.h:16-37: nfc_read/nfc_saved/\n"
            "nfc_detect_reader/nfc_mfc_emulate/nfc_harvest/...). Nothing fork-functional to\n"
            "carry.\n"
        ),
    },
    "m1_csrc/m1_led_indicator.c": {
        "disposition": "upstream-equivalent",
        "evidence": (
            "Fork delta = five `(void)params;` unused-parameter fixes in the LED indicator\n"
            "callbacks (legacy m1_led_indicator.c:84,99,163,191,206). Upstream performed the\n"
            "same class of fix in its own evolution of the file: `main:m1_csrc/m1_led_indicator.c`\n"
            "has `(void)params;` at :144,159,229,261,280,404. Same intent, already achieved on\n"
            "the new base.\n"
        ),
    },
    "CHANGELOG.md": {
        "disposition": "intentionally-dropped",
        "evidence": (
            "Fork delta = the fork's own v0.8.2-v0.8.11 release entries. The release history\n"
            "is preserved verbatim in the immutable archive (`git show legacy/v0.8.11:CHANGELOG.md`)\n"
            "and GitHub releases; the new base's CHANGELOG (PR-1/PR-2 `Unreleased` entries)\n"
            "already records the fork lineage and points at the ledger for the merge-era story\n"
            "(the `### Removed` section resolves via this file). Re-landing 176 lines of\n"
            "superseded release notes would duplicate the archive.\n"
        ),
    },
    "cmake/m1_01/CMakeLists.txt": {
        "disposition": "intentionally-dropped",
        "evidence": (
            "Fork delta = build restructuring: INTERFACE `m1_01` split into STATIC `m1_drivers`\n"
            "(+`m1_core`) with relaxed vendor warnings (`cmake/driver_warnings.cmake`) and ESP32\n"
            "binary-resource generation. Superseded: upstream's `m1_01` INTERFACE layout is the\n"
            "maintained base (PR-4/#39 scopes -Werror via source-file properties + TARGET_DIRECTORY\n"
            "instead of lib separation), and the ESP32 asset generation returns with the #41/#42\n"
            "ESP-versioned carries. Carrying the restructure would churn every future upstream\n"
            "merge of this file for no behavioral gain.\n"
        ),
    },
    "m1_csrc/m1_buzzer.h": {
        "disposition": "intentionally-dropped",
        "evidence": (
            "Fork delta = clang-format rewrap of macro definitions plus one added\n"
            '`#include "m1_compile_cfg.h"`. The compile switch it serves already reaches the\n'
            "build on the new base: `main:m1_csrc/m1_compile_cfg.h:24-27` defines\n"
            "M1_APP_BUZZER_USE_TIMER8 (guarded) and the base builds green without the header\n"
            "change (PR-2 baseline evidence, documentation/merge1-baseline-evidence.md).\n"
            "Restyle residue; not carried.\n"
        ),
    },
    "m1_csrc/m1_esp_hosted_config.h": {
        "disposition": "intentionally-dropped",
        "evidence": (
            "Fork delta = pure clang-format line rewrapping of esp-hosted Kconfig defines\n"
            "(backslash continuations) plus include sort. Zero semantic change\n"
            "(`git diff -w 93acd751 legacy/v0.8.11 -- m1_csrc/m1_esp_hosted_config.h` hunks are\n"
            "all rewrap). Restyle residue; not carried.\n"
        ),
    },
    "m1_csrc/m1_lib.c": {
        "disposition": "intentionally-dropped",
        "evidence": (
            "Fork delta = removal of the dead FCC test GPIO helpers (`m1_test_gpio_*`,\n"
            "`m1_gpio_toggle`, M1_GPIO_TEST* macros — origin d10a6f9, fork issue #28) plus\n"
            "clang-format. Upstream retains the helpers on the new base with zero callers\n"
            "(decls main:m1_lib.h:18-22, defs in m1_lib.c, `git grep m1_test_gpio main` shows no\n"
            "other sites). Removing upstream's (dead but harmless) code churns a shared file for\n"
            "no behavioral gain; non-static unused functions produce no warning, so #39 does not\n"
            "need the removal either.\n"
        ),
    },
    "m1_csrc/m1_log_debug.h": {
        "disposition": "intentionally-dropped",
        "evidence": (
            "Fork delta = include reorder and line-wrapping of the M1_LOG_* macros; the format\n"
            "attribute annotation is preserved verbatim on both sides. Zero semantic change.\n"
            "Restyle residue; not carried (the .c counterpart carries two genuine fixes under #39).\n"
        ),
    },
    "NFC/NFC_drv/common/nfc_storage.c": {
        "disposition": "intentionally-dropped",
        "evidence": (
            "Fork delta = clang-format pass only: include sort + brace/indent reformat of\n"
            "unchanged logic (101 added vs 106 removed -w lines are all reformat; verified by\n"
            "hunk inspection — str_ltrim/str_rtrim/hex_nibble/parse_* bodies are identical modulo\n"
            "formatting). Restyle residue; upstream evolved the file on the new base\n"
            "(+846/-122 vs legacy) and that is the maintained shape.\n"
        ),
    },
    "Sub_Ghz/m1_sub_ghz_api.c": {
        "disposition": "intentionally-dropped",
        "evidence": (
            "Fork delta = clang-format restyle (brace style, line wraps) plus removal of\n"
            "`SI446x_Start_Tx_CW` (FCC test helper, origin d10a6f9 #28). Upstream retains\n"
            "Start_Tx_CW on the new base (`git grep -c Start_Tx_CW main -- Sub_Ghz/` = 4 hits\n"
            "across api.c/h). Same dead-code-cleanup-not-worth-churn reasoning as m1_lib.c;\n"
            "the SI4463 command wrappers themselves are unchanged semantically.\n"
        ),
    },
    "Sub_Ghz/m1_sub_ghz_api.h": {
        "disposition": "intentionally-dropped",
        "evidence": (
            "Companion header of the above: restyle + `SI446x_Start_Tx_CW` proto removal.\n"
            "Upstream keeps the proto (main:Sub_Ghz/m1_sub_ghz_api.h). Not carried.\n"
        ),
    },
    "m1_csrc/m1_esp_hosted_dummy.c": {
        "disposition": "intentionally-dropped",
        "evidence": (
            "Fork delta = include reorder and whitespace only\n"
            "(`git diff -w legacy/v0.8.11 main -- m1_csrc/m1_esp_hosted_dummy.c` shows the same\n"
            "lines in different order). Restyle residue; not carried.\n"
        ),
    },
    "m1_csrc/m1_file_browser.h": {
        "disposition": "intentionally-dropped",
        "evidence": (
            "Fork delta = clang-format compaction of the enums/protos plus an include swap\n"
            "(`u8g2.h` -> `m1_system.h`, which reaches u8g2 through the display include chain).\n"
            "No signature changes; the base's header compiles green against the base's .c\n"
            "(PR-2 virgin-baseline builds). Restyle residue; the .c counterpart carries its\n"
            "genuine init fixes under #39.\n"
        ),
    },
    "ARCHITECTURE.md": {
        "disposition": "intentionally-dropped",
        "evidence": (
            "Fork's 368-line architecture overview (feature-status matrix, protocol notes,\n"
            "recommended implementation order) documents the LEGACY tree; re-landing it verbatim\n"
            "would misdocument the new base (e.g. it describes m1_ir_universal and the CDC\n"
            "rewrite that #41-#44 re-home differently). Its durable content is preserved: the\n"
            "recommended implementation order table lives on in this ledger's Phase 3 verdicts,\n"
            "and the feature status is tracked by issues #3/#4/#7/#9/#11. A fresh ARCHITECTURE.md\n"
            "against the new base is follow-up work when the P4 carries land.\n"
        ),
    },
    "ROADMAP.md": {
        "disposition": "intentionally-dropped",
        "evidence": (
            "Fork's phased roadmap tied to the legacy tree and its v0.8.x release cadence.\n"
            "Superseded by the issue tracker (P4 issues #40-#44, #46, #49 and milestone\n"
            "planning). Note: open issues #3/#4/#7/#11 link to ROADMAP anchors — those links\n"
            "404 on the new base; harmless and superseded by the ledger verdicts. Priority\n"
            "ranking preserved in Phase 3 below.\n"
        ),
    },
    "cmake/driver_warnings.cmake": {
        "disposition": "intentionally-dropped",
        "evidence": (
            "Fork's vendor-code warning-relaxation include file, consumed by the dropped\n"
            "m1_drivers/m1_core restructuring (see cmake/m1_01/CMakeLists.txt entry). The new\n"
            "base achieves scoped warnings via #39: source-file properties with\n"
            "TARGET_DIRECTORY on app sources only, vendor trees untouched — no separate\n"
            "relaxation file needed.\n"
        ),
    },
    "cmake/gcc-arm-none-eabi.cmake": {
        "disposition": "intentionally-dropped",
        "evidence": (
            "Fork delta = removal of `-x assembler-with-cpp` from ASM flags and link-flag\n"
            "simplifications tied to the dropped build restructuring. The upstream toolchain\n"
            "file on the new base builds green on both documented paths (Windows preset +\n"
            "bash/Make — PR-2 virgin-baseline evidence, documentation/merge1-baseline-evidence.md).\n"
            "Blindly carrying flag removals against upstream's evolved file risks breaking the\n"
            "verified baseline for zero measured gain.\n"
        ),
    },
    "tools/__pycache__/append_crc32.cpython-313.pyc": {
        "disposition": "intentionally-dropped",
        "evidence": (
            "CPython bytecode artifact accidentally committed by the fork (name-status A,\n"
            "binary). Never source; excluded by PR-1 hygiene. The generator source\n"
            "(`tools/append_crc32.py`) is carried under #40.\n"
        ),
    },
    "Core/Inc/main.h": {
        "disposition": "carried-by-patch",
        "carry": "#41",
        "evidence": (
            "Carried hunk: `IWDG_RELOAD 4000 -> 1250` (watchdog window 4 s -> 10 s, origin\n"
            "d3a0208 — stability hardening #22/#26/#27): the boot-time CRC failsafe (#41) walks\n"
            "both banks before the watchdog bites; with the 4 s window the failsafe itself can\n"
            "trip IWDG. `main:Core/Inc/main.h:237` still has 4000. Include-order and\n"
            "comment-rewrap hunks are restyle (not carried). #41's motivation already covers the\n"
            "adjacent IWDG option-byte defect (main.c:194-212) — the port fixes both together.\n"
        ),
    },
    "Core/Src/main.c": {
        "disposition": "carried-by-patch",
        "carry": "#41",
        "evidence": (
            "Carried hunks: DFU boot entry — read `TAMP->BKP1R` op-status before `MX_IWDG_Init()`,\n"
            "skip normal IWDG init when USB DFU requested, program a 32 s IWDG safety window and\n"
            "drive PD12/PD13 as DFU visual indicator (legacy main.c:228-262; origins 509c68d,\n"
            'e028e9d, d99e2f8). All absent on main (`git grep -c "DEV_OP_STATUS_USB_DFU_REQUEST" main`\n'
            "= 0). Port must resolve the BKP1R collision against upstream's bu_regs layout —\n"
            "see Phase 4(b) evidence — and stop rewriting IWDG option bytes silently (#41 AC).\n"
        ),
    },
    "Core/Src/system_stm32h5xx.c": {
        "disposition": "carried-by-patch",
        "carry": "#41",
        "evidence": (
            "Carried hunk: `extern void boot_recovery_check(void);` + call as the first action\n"
            "of `SystemInit()` (legacy system_stm32h5xx.c; origin 6e73998, boot-time CRC #29) —\n"
            "runs the failsafe before C runtime/FreeRTOS start. Absent on main\n"
            "(`git grep -c boot_recovery_check main` = 0; upstream has no pre-main hook).\n"
            "Remainder of the fork delta is clang-format rewrap of ST's boilerplate (dropped).\n"
        ),
    },
    "m1_csrc/m1_fw_update.c": {
        "disposition": "carried-by-patch",
        "carry": "#41",
        "evidence": (
            "Carried hunks: `firmware_update_usb_dfu_mode()`, `firmware_update_usb_dfu_mode_cli()`,\n"
            "`firmware_update_enter_usb_dfu()` (USB ROM DFU entry, origins 509c68d/e028e9d) and\n"
            "the SD-image empty-filename fix (origin 16402da, fork issue #33). Absent on main\n"
            '(`git grep -c "usb_dfu" main -- m1_csrc/` = 0). #41 ports the workflow onto\n'
            "upstream's recovery stack.\n"
        ),
    },
    "m1_csrc/m1_fw_update_bl.c": {
        "disposition": "carried-by-patch",
        "carry": "#41",
        "evidence": (
            "Carried hunks: BOOT_FAIL_SIGNATURE failsafe state machine (raw `TAMP->BKP1R`\n"
            "writes at legacy m1_fw_update_bl.c:731-772 — boot-fail escalation, boot-CRC verdict\n"
            "propagation; origins 6e73998/e028e9d). Upstream on main has only the basic CRC\n"
            "pieces (`bl_crc_check` main:m1_fw_update_bl.c:68, `bl_get_crc_chunk` :113) with\n"
            "interactive rollback (#41 motivation: upstream rollback is interactive only). Port\n"
            "composes the failsafe with upstream's pieces and replaces raw BKP1R writes with the\n"
            "bu_regs struct API (Phase 4(b)). Restyle hunks dropped.\n"
        ),
    },
    "m1_csrc/m1_fw_update_bl.h": {
        "disposition": "carried-by-patch",
        "carry": "#41",
        "evidence": (
            "Mixed entry. Retained part: version macros — PR-2 froze 0.9.0.0-UFO on the new\n"
            "base (main:m1_fw_update_bl.h:37-43 + FW_BUILD_LABEL :50-52), superseding the fork's\n"
            "0.8.11 values. Carried part (#41): `BOOT_FAIL_SIGNATURE` (absent on main), the\n"
            "`FW_CONFiG` -> `FW_CONFIG` spelling fix (main:19-20 still has the typo — fork issue\n"
            "#31 lineage), and the parenthesized `FW_CRC_ADDRESS` macro (main:25 is unparenthesized,\n"
            "an operator-precedence hazard in compound expressions). Slider-macro rewraps dropped.\n"
        ),
    },
    "m1_csrc/m1_system.c": {
        "disposition": "carried-by-patch",
        "carry": "#41",
        "evidence": (
            "Carried hunks: device-op-status handlers — `handle_op_status_fw_corruption()`\n"
            "(dual-bank corrupt -> USB DFU fallback screen), fw-update/rollback-complete and\n"
            "reboot handlers, `startup_error_screen_display()` and its proto (origins 6e73998\n"
            '#29, d3a0208). All absent on main (`git grep -c "handle_op_status" main` = 0;\n'
            "upstream's startup_config_handler has no corruption path). (void)-param hunks tagged\n"
            "below for #39. #41 ports the handlers to coexist with upstream's update state\n"
            "machine (main:m1_system.c:750-804 interactive rollback).\n"
        ),
    },
    "m1_csrc/m1_system.h": {
        "disposition": "carried-by-patch",
        "carry": "#41",
        "evidence": (
            "Carried hunk: `void startup_error_screen_display(const char *l1, const char *l2);`\n"
            "proto (absent on main). Not carried: the SYS_CONFIG_MAGIC_NUMBER relocation to\n"
            "m1_fw_update_bl.h — main keeps it in m1_system.h (main:m1_system.h:85), single\n"
            "source retained; macro rewraps are restyle.\n"
        ),
    },
    "m1_csrc/m1_menu.c": {
        "disposition": "carried-by-patch",
        "carry": "#41 + #43",
        "evidence": (
            "Carried hunks: (a) `menu_Setting_Firmware_Update_USB_DFU` record + wiring in the\n"
            "Firmware-update children list (legacy m1_menu.c:252-253, :330) -> #41;\n"
            "(b) Infrared submenu for the Universal Remote — `menu_Infrared_Universal_Remotes`,\n"
            "`Learn_New_Remote`, `Saved_Remotes` (legacy m1_menu.c:141-167) -> #43, to sit\n"
            "alongside upstream's per-device-class entries (main:m1_menu.c:180-195).\n"
            "Intentionally dropped hunks: the stub records `menu_Settings_System` (:316) and\n"
            "`menu_Settings_LCD_and_Notifications` (:280) with their commented-out parent links\n"
            "(:356,:358), and the orphan `menu_Sub_GHz_Radio_Settings` record (:68-73, unlinked)\n"
            "— Phase 3 verdicts. NFC-rename hunks moot (see m1_nfc.c). clang-format hunks dropped.\n"
        ),
    },
    ".github/ISSUE_TEMPLATE/usb_dfu.md": {
        "disposition": "carried-by-patch",
        "carry": "#41",
        "evidence": (
            "Fork's bug-report template for DFU recovery issues. Restored with the DFU port\n"
            "(#41) — trivially re-landed then, when the feature it documents exists again on\n"
            "the base (CHANGELOG `### Removed` lists it among the recoverable paths).\n"
        ),
    },
    "m1_csrc/m1_fw_update.h": {
        "disposition": "carried-by-patch",
        "carry": "#41",
        "evidence": (
            "Fork adds the DFU entry protos `firmware_update_usb_dfu_mode()` /\n"
            "`firmware_update_usb_dfu_mode_cli()` (absent on main: m1_fw_update.h ends at\n"
            "firmware_update_gui_update). Returns with the #41 port.\n"
        ),
    },
    "m1_csrc/m1_wifi.c": {
        "disposition": "carried-by-patch",
        "carry": "#42",
        "evidence": (
            "Carried hunks: the WiFi join flow — `wifi_config()` rework, `wifi_join_network()`,\n"
            "`wifi_auto_connect()`, `wifi_ensure_initialized()` (origin 3b6c252, fork #15) on top\n"
            "of the scan list plumbing. Upstream's 5.4k-line `m1_wifi.c` rework on main has NO\n"
            "join/credential code (#42 motivation, grep-verified). #42 re-homes the join flow\n"
            "onto upstream's versioned `saved_networks.csv` store; ESP-side handler carried at\n"
            "Esp_spi_at/.../esp_app_main.c. Scan-list restyle hunks dropped.\n"
        ),
    },
    "m1_csrc/m1_wifi.h": {
        "disposition": "carried-by-patch",
        "carry": "#42",
        "evidence": (
            "Fork adds `wifi_cred_init()`, `wifi_join_network()`, `wifi_show_saved_networks()`,\n"
            "`wifi_show_connection_status()` protos — the join/credential API surface #42 ports.\n"
        ),
    },
    "m1_csrc/m1_wifi_cred.c": {
        "disposition": "carried-by-patch",
        "carry": "#42",
        "evidence": (
            "Fork's credential store (299 lines, origin 3b6c252): encrypted PSK persistence.\n"
            "#42 carries it re-homed onto upstream's versioned store with the hardened crypto\n"
            "(council-ratified; threat model documented in the issue).\n"
        ),
    },
    "m1_csrc/m1_wifi_cred.h": {
        "disposition": "carried-by-patch",
        "carry": "#42",
        "evidence": (
            "Credential-store API header (85 lines, origin 3b6c252) — carried with\n"
            "m1_wifi_cred.c under #42.\n"
        ),
    },
    "m1_csrc/m1_crypto.c": {
        "disposition": "carried-by-patch",
        "carry": "#42",
        "evidence": (
            "Fork's crypto helpers (604 lines, origin 3b6c252): AES/IV plumbing for credential\n"
            "encryption. Upstream has no equivalent (grep `m1_crypto` on main = 0). #42 carries\n"
            "with the documented hardening (hardware RNG for IVs was fork #16, closed).\n"
        ),
    },
    "m1_csrc/m1_crypto.h": {
        "disposition": "carried-by-patch",
        "carry": "#42",
        "evidence": (
            "Crypto helper API header — carried with m1_crypto.c under #42.\n"
        ),
    },
    "Esp_spi_at/examples/at_spi_master/spi/stm32/main/esp_app_main.c": {
        "disposition": "carried-by-patch",
        "carry": "#42",
        "evidence": (
            "Carried hunk: `wifi_connect_ap()` — the ESP-side join handler (legacy\n"
            "esp_app_main.c:655-728 per #42 motivation; AT+CWJAP with retry/tick budgeting,\n"
            "origin 3b6c252). The CTRL_REQ_CONNECT_AP control API exists on both sides but the\n"
            "handler is fork-only. #42 ships it versioned with the STM32 image. The rest of the\n"
            "fork delta is clang-format (dropped).\n"
        ),
    },
    "Esp_spi_at/examples/at_spi_master/spi/stm32/main/esp_app_main.h": {
        "disposition": "carried-by-patch",
        "carry": "#42",
        "evidence": (
            "Fork adds the join/scan protos consumed by the ESP-side handler — carried with\n"
            "esp_app_main.c under #42.\n"
        ),
    },
    "Esp_spi_at/examples/at_spi_master/spi/stm32/main/esp_at_list.h": {
        "disposition": "carried-by-patch",
        "carry": "#42",
        "evidence": (
            "Fork adds the AT command vocabulary for join: `AT+CWJAP=`, `AT+CWJAP?`,\n"
            "`AT+CIPSTA?`, `AT+CWQAP` defines (+CWJAP/+CIPSTA result keys). Absent on main\n"
            "(grep CWJAP on main = 0). Carried with the #42 join flow.\n"
        ),
    },
    "m1_csrc/m1_virtual_kb.c": {
        "disposition": "carried-by-patch",
        "carry": "#42",
        "evidence": (
            "Fork adds `m1_vkb_get_password()` (masked on-screen keyboard for PSK entry,\n"
            "+205 lines, origin 3b6c252). Returns with the #42 join flow (upstream's keyboard\n"
            "lacks a masked mode). Restyle hunks dropped.\n"
        ),
    },
    "m1_csrc/m1_virtual_kb.h": {
        "disposition": "carried-by-patch",
        "carry": "#42",
        "evidence": (
            "Fork adds the `m1_vkb_get_password()` proto — carried with m1_virtual_kb.c.\n"
        ),
    },
    "m1_csrc/m1_ir_universal.c": {
        "disposition": "carried-by-patch",
        "carry": "#43",
        "evidence": (
            "Fork's Universal Remote app (1182 lines): Flipper-IRDB `.ir` parser, browser UX,\n"
            "search/favorites/pagination (origins 547178a, ad3b8c6, a55968b #18, review fixes\n"
            "471b2e0/5a6818c/f68366d/9d010d9). Absent on main (upstream ships ZERO `.ir`\n"
            "database files, #43 motivation). #43 consolidates the browser UX onto upstream's\n"
            "IRMP stack and protocol table — the fork's aliasing map\n"
            "(legacy m1_ir_universal.c:84-96: NECext->NEC etc.) is NOT carried as-is (it\n"
            "transmits wrong codes); see Phase 4(a) for the single-source map evidence.\n"
        ),
    },
    "m1_csrc/m1_ir_universal.h": {
        "disposition": "carried-by-patch",
        "carry": "#43",
        "evidence": (
            "Universal Remote API header (100 lines) — carried with m1_ir_universal.c under #43.\n"
        ),
    },
    "Infrared/irmp-irsnd/irmpconfig.h": {
        "disposition": "carried-by-patch",
        "carry": "#43",
        "evidence": (
            "Fork rebalanced the IRMP protocol enable set for the Flipper-IRDB vocabulary\n"
            "(+150/-75 -w: enabled protocols the `.ir` corpus needs). Upstream's IRMP on main\n"
            "already covers the full vocabulary incl. IRMP_NEC8/PIONEER/RC5X/SIRCS15/SIRCS20/RAW\n"
            "(#43 motivation, verified 5/5). #43 re-derives the enable set during consolidation\n"
            "instead of carrying the fork's stale balance.\n"
        ),
    },
    "m1_csrc/m1_infrared.c": {
        "disposition": "carried-by-patch",
        "carry": "#43",
        "evidence": (
            "Fork replaces the `m1_gui_let_update_fw()` placeholder bodies with routing into\n"
            'the Universal Remote (`#include "m1_ir_universal.h"`, origin ad3b8c6). Upstream\'s\n'
            "infrared menu on main is the per-device-class browser (main:m1_menu.c:180-195).\n"
            "#43 re-wires the fork's Universal Remotes entry alongside upstream's.\n"
        ),
    },
    "Core/Src/cli_app.c": {
        "disposition": "carried-by-patch",
        "carry": "#44",
        "evidence": (
            "Fork adds 11 CLI commands with help plumbing: log, status, version, sdcard, wifi,\n"
            "battery, reboot, memory, cdcstats, cdcreset (+dfu -> #41). Upstream's cli_app on\n"
            "main has cls/mtest only (#44 motivation: fork CLI additions upstream lacks).\n"
            "#44 ports them individually with help text + regression tests, re-homing\n"
            "`cdcstats` onto the re-implemented CDC_CLI_Rx* counters.\n"
        ),
    },
    "Core/Src/FreeRTOS_CLI.c": {
        "disposition": "carried-by-patch",
        "carry": "#44",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork extends the FreeRTOS CLI command struct with the `pxCommandHelper` field\n"
            "(help registry consumed by cli_app/m1_cli_help) and initializes\n"
            "`uint8_t num_of_params = 0;` in FreeRTOS_CLIProcessCommand (uninitialized-read\n"
            "warning fix — tagged). File exists unchanged on main otherwise (ours-only class =\n"
            "upstream untouched since fork point). #44 re-homes the helper extension and the\n"
            "mtest ID space (ours: 9, 22-24; upstream's: 5/6/61/64/68/69/78/79 — collisions\n"
            "guaranteed post-merge per #44).\n"
        ),
    },
    "m1_csrc/m1_cli.c": {
        "disposition": "carried-by-patch",
        "carry": "#44",
        "evidence": (
            "Fork adds the mtest subcommand suite (basic_system/sdcard/led/lcd/infrared/power/\n"
            "subghz/esp32/gpio/nfc with per-test help) and `cmd_dfu` (-> #41). Upstream rewrote\n"
            "m1_cli.c on main with its own mtest IDs incl. a guarded FCC hook (main:m1_cli.c:998)\n"
            "that the fork's unguarded `mtest 9` lacks (#44 motivation). #44 re-homes IDs\n"
            "collision-free and adds guards.\n"
        ),
    },
    "m1_csrc/m1_cli.h": {
        "disposition": "carried-by-patch",
        "carry": "#44 + #41",
        "evidence": (
            "Fork makes the header self-contained (FreeRTOS/cmsis includes), defines\n"
            "MAX_INPUT_LENGTH, adds `cmd_dfu` protos. Carried with the #44 CLI port (dfu proto\n"
            "with #41).\n"
        ),
    },
    "m1_csrc/m1_cli_help.c": {
        "disposition": "carried-by-patch",
        "carry": "#44",
        "evidence": (
            "Fork's mtest help texts (per-subcommand syntax strings). Carried with the #44\n"
            "re-home so help and IDs stay consistent.\n"
        ),
    },
    "m1_csrc/m1_usb_cdc_msc.c": {
        "disposition": "carried-by-patch",
        "carry": "#44",
        "evidence": (
            "Carried hunks: the CLI rx stream-buffer consumer side (`h_usb_cli_rx_streambuf`\n"
            "drain feeding the CLI task) — part of the CDC race fix (origin d3a0208 #27).\n"
            "Per #44 AC the fork's CDC rewrite is NOT resurrected wholesale: only the pieces\n"
            "that give `cdcstats` its counters are re-homed onto upstream's usbd_cdc_if.c path.\n"
            "Restyle hunks dropped.\n"
        ),
    },
    "m1_csrc/m1_usb_cdc_msc.h": {
        "disposition": "carried-by-patch",
        "carry": "#44",
        "evidence": (
            "Fork adds `extern StreamBufferHandle_t h_usb_cli_rx_streambuf;` — the handle the\n"
            "#44 counter re-home consumes.\n"
        ),
    },
    "USB/Class/CDC/Src/usbd_cdc_if.c": {
        "disposition": "carried-by-patch",
        "carry": "#44",
        "evidence": (
            "Carried hunks: `usb_cli_rx_dropped_bytes` / `usb_cli_rx_high_watermark` counters\n"
            "(CDC_CLI_RxDroppedBytes/HighWatermark accessors) — overflow observability for the\n"
            "CLI rx path (origin d3a0208 #27). #44 AC re-implements the three CDC_CLI_Rx*\n"
            "counters on upstream's stream-buffer CDC path on main (not the fork's rewrite).\n"
        ),
    },
    "USB/Class/CDC/Inc/usbd_cdc_if.h": {
        "disposition": "carried-by-patch",
        "carry": "#44",
        "evidence": (
            "Fork adds the CDC_CLI_Rx* counter API protos — carried with usbd_cdc_if.c\n"
            "under #44.\n"
        ),
    },
    "m1_csrc/Res_String.c": {
        "disposition": "carried-by-patch",
        "carry": "#46",
        "evidence": (
            "Fork fixes the self-include case: `res_string.h` -> `Res_String.h` (on-disk file\n"
            "is Res_String.h). Main still has the wrong case (main:m1_csrc/Res_String.c:7);\n"
            "#46 renames the file pair to `res_string.{c,h}` so all 9 include sites resolve\n"
            "untouched — the rename subsumes this fix.\n"
        ),
    },
    "lfrfid/lfrfid_file.c": {
        "disposition": "carried-by-patch",
        "carry": "#46",
        "evidence": (
            "One-line case fix `res_string.h` -> `Res_String.h` (legacy lfrfid_file.c:21).\n"
            "Main:23 still lowercase — resolves via the #46 rename.\n"
        ),
    },
    "NFC/NFC_drv/common/nfc_file.c": {
        "disposition": "carried-by-patch",
        "carry": "#46",
        "evidence": (
            "One-line case fix `res_string.h` -> `Res_String.h`. Main:25 still lowercase —\n"
            "resolves via the #46 rename.\n"
        ),
    },
    "NFC/NFC_drv/legacy/nfc_listener.c": {
        "disposition": "carried-by-patch",
        "carry": "#46",
        "evidence": (
            "One-line case fix `rfal_AnalogConfig.h` -> `rfal_analogConfig.h` (on-disk:\n"
            "NFC/Middlewares/ST/rfal/Inc/rfal_analogConfig.h). Main:63 still wrong-case —\n"
            "#46 AC names this exact site. NOTE: upstream also rewrote the listener on main\n"
            "(+652/-76 vs legacy); the fix lands against the new body.\n"
        ),
    },
    "NFC/NFC_drv/legacy/nfc_poller.c": {
        "disposition": "carried-by-patch",
        "carry": "#46",
        "evidence": (
            "Same one-line `rfal_AnalogConfig.h` case fix at legacy nfc_poller.c:54;\n"
            "main:54 still wrong-case. #46 AC names this site too.\n"
        ),
    },
    "build": {
        "disposition": "carried-by-patch",
        "carry": "#40",
        "evidence": (
            "Fork's bash build wrapper (136 lines, origin 97e80ce + 5363ad0 force-reconfigure):\n"
            "toolchain discovery, clean/rebuild, post-build steps. The new base ships upstream's\n"
            "`build.sh`; #40 re-derives the version-stamped artifact pipeline (incl. the Windows\n"
            "CRC emitter) onto it rather than carrying the fork's parallel entry point.\n"
        ),
    },
    "tools/append_crc32.py": {
        "disposition": "carried-by-patch",
        "carry": "#40",
        "evidence": (
            "Fork's CRC32 post-build tool (166 lines; origins 49af568, 4693ea2, 7c0139d,\n"
            "f220529 — word-wise CRC matching the STM32 HAL CRC unit). Upstream has no CRC\n"
            "append pipeline. #40 carries it with the Windows-path emitter fix; regression-tested\n"
            "by test_crc*.py below. Referenced by the fork CMakeLists post-build steps (see the\n"
            "CMakeLists.txt entry).\n"
        ),
    },
    "tools/crc_debug.py": {
        "disposition": "carried-by-patch",
        "carry": "#40",
        "evidence": (
            "CRC debugging aid (105 lines) used to derive the word-wise layout — carried with\n"
            "the #40 CRC pipeline.\n"
        ),
    },
    "tools/test_crc.py": {
        "disposition": "carried-by-patch",
        "carry": "#40",
        "evidence": (
            "CRC regression tests (112 lines) locking the tool against the device layout —\n"
            "carried with #40 and wired into its verification.\n"
        ),
    },
    "tools/test_crc_fix.py": {
        "disposition": "carried-by-patch",
        "carry": "#40",
        "evidence": (
            "Second CRC regression suite (124 lines, trailing-zeros case, origin 4693ea2) —\n"
            "carried with #40.\n"
        ),
    },
    "tools/check_case_sensitivity.py": {
        "disposition": "carried-by-patch",
        "carry": "#46",
        "evidence": (
            "Pre-commit case-collision checker (262 lines, origin d3a0208) — the tool that\n"
            "caught the very mismatches #46 fixes. Already integrated: the repo's pre-commit\n"
            "hook invokes it (observed live during this PR's commits). Carried with #46 so the\n"
            "class of defect stays blocked.\n"
        ),
    },
    "m1_csrc/m1_int_hdl.c": {
        "disposition": "carried-by-patch",
        "carry": "follow-up carry (issue TBD - see Ledger summary; recommend one 'port stability hardening + small fixes' issue)",
        "evidence": (
            "Carried hunks: (a) `GPDMA1_Channel3_IRQHandler` — the Sub-GHz Rx DMA ISR\n"
            "(79 new lines, origin d3a0208 #22: deferred-RTOS-context-switching under heavy\n"
            "Sub-GHz load); (b) USART1 ISR notify-value packing fix (task id in high byte).\n"
            "Upstream's int_hdl on main has neither (grep GPDMA1_Channel3 on main = no Ch3\n"
            "handler). Belongs with the Sub-GHz Rx DMA carry (m1_sub_ghz.c entry) — no dedicated\n"
            "P4 issue exists yet; flagged for the Follow-ups milestone.\n"
        ),
    },
    "m1_csrc/m1_sub_ghz.c": {
        "disposition": "carried-by-patch",
        "carry": "follow-up carry (issue TBD - see Ledger summary; recommend one 'port stability hardening + small fixes' issue)",
        "evidence": (
            "Carried hunks: Sub-GHz Rx DMA double-buffering — SUBGHZ_RX_DMA_BLOCK_SAMPLES/\n"
            "BUFFER_SAMPLES, subghz_rx_dma_buffer, restart-from-ISR plumbing, throttle/drop/\n"
            "half/total counters (origin d3a0208 #22). All absent on main. This is the ISR-load\n"
            "stability work behind closed fork issues #22/#26/#27; no P4 vehicle yet — flagged\n"
            "for the Follow-ups milestone. Dropped hunks: the `sub_ghz_radio_settings` stub\n"
            "deletion is moot (Phase 3 — the stub itself is dropped), clang-format hunks not\n"
            "carried.\n"
        ),
    },
    "m1_csrc/m1_sub_ghz.h": {
        "disposition": "carried-by-patch",
        "carry": "follow-up carry (issue TBD - see Ledger summary; recommend one 'port stability hardening + small fixes' issue)",
        "evidence": (
            "Fork adds the Rx DMA surface: SUBGHZ_RX_DMA_BLOCK_QUEUE_LEN,\n"
            "`subghz_rx_dma_restart_from_isr()`, hdma_subghz_rx + counter externs. Carried with\n"
            "m1_sub_ghz.c (follow-up stability carry).\n"
        ),
    },
    "documentation/stability_phase5_validation.md": {
        "disposition": "carried-by-patch",
        "carry": "follow-up carry (issue TBD - see Ledger summary; recommend one 'port stability hardening + small fixes' issue)",
        "evidence": (
            "Fork's validation record for the d3a0208 stability hardening (97 lines): test\n"
            "matrix and results for #22/#26/#27. Evidence companion of the stability carry —\n"
            "re-landed (re-run) when that carry lands on the new base.\n"
        ),
    },
    "m1_csrc/m1_sdcard_man.h": {
        "disposition": "carried-by-patch",
        "carry": "follow-up carry (issue TBD - see Ledger summary; recommend one 'port stability hardening + small fixes' issue)",
        "evidence": (
            "Fork widens `dat_filename[64] -> [256]` in S_M1_SDM_DatFileInfo_t (origin 78d6b86:\n"
            "buffer-overflow fix in the SD log-manager filename buffer). Main's struct still has\n"
            "64. Genuine bug fix; belongs to the small-fix carry sweep (flagged, no P4 vehicle).\n"
            "Typedef reformat hunks dropped.\n"
        ),
    },
    "Core/Src/app_freertos.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork adds `(void)xExpectedIdleTime;` in the weak vPortSuppressTicksAndSleep stub\n"
            "(unused-parameter). Main's stub still lacks it (grep on main = 0). Class:\n"
            "-Wunused-parameter. Fix hunk: one line at the function head.\n"
        ),
    },
    "Core/Src/stm32h5xx_hal_msp.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork `#if 0`-guards two unused local declarations in HAL_TIM_Base_MspInit /\n"
            "HAL_TIM_MspPostInit (GPIO_InitTypeDef/NodeConfig + GPIO_InitStruct). Main has one\n"
            "`#if 0` in the file but not these two guards (grep 'TODO: Enable TIM DMA' on\n"
            "main = 0). Class: -Wunused-variable. Two small guard hunks.\n"
        ),
    },
    "Core/Src/stm32h5xx_it.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork annotates the ECCDR read-to-clear in NMI_Handler with `(void)eccdr_value;`\n"
            "+ rationale comment (the read is required for the hardware flag clear; the value is\n"
            "deliberately unused). Main lacks it. Class: -Wunused-variable (value-used-for-side-\n"
            "effect pattern). One hunk.\n"
        ),
    },
    "lfrfid/lfrfid.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork adds `(void)ret;`/`(void)free_heap;` (xTaskCreate results checked via\n"
            "assert — release builds compile asserts out) and two `(void)param;` in task\n"
            "entries. Main lacks them (grep on main = 0 for this pattern in lfrfid.c). Class:\n"
            "-Wunused-variable/set-but-unused. Three hunks.\n"
        ),
    },
    "lfrfid/lfrfid_protocol_em4100.c": {
        "disposition": "carried-by-patch",
        "carry": "#46 + #39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Mixed entry. #46 hunk: include case `uiview.h` -> `uiView.h` (on-disk m1_csrc/\n"
            "uiView.h). #39-tagged hunks: decoder signature type fixes (`uint16_t size` ->\n"
            "`int size` matching the protocol vtable, + `(uint8_t)size` narrowing at the\n"
            "manchester feed), `const` fixes (GetBitFromFrame/em4100_is_valid frames),\n"
            "`(void)proto;` x3, `#if 0` around unused bit_stream_get. Classes: sign-compare,\n"
            "pointer-sign, unused-parameter/function. Main:33 still has wrong-case include; the\n"
            "type fixes absent (main signatures still uint16_t). Both carries re-check\n"
            "applicability against upstream's lfrfid evolution.\n"
        ),
    },
    "lfrfid/lfrfid_protocol_h10301.c": {
        "disposition": "carried-by-patch",
        "carry": "#46 + #39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Same pattern as em4100: `uiview.h`->`uiView.h` case fix (#46; main:32 wrong-case)\n"
            "plus `uint16_t`->`int` decoder signatures, `(void)proto;` (#39-tagged). Classes:\n"
            "sign-compare, unused-parameter.\n"
        ),
    },
    "lfrfid/t5577.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork adds `(void)block;` in t5577_execute_write (parameter intentionally unused —\n"
            "the API takes block for future use). Main lacks it. Class: -Wunused-parameter.\n"
        ),
    },
    "m1_csrc/logger.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork adds `(void)husart;`, `(void)data;`, `(void)dataLen;` in the stubbed\n"
            "logUsart functions (bodies are commented-out placeholders). Main lacks all three.\n"
            "Class: -Wunused-parameter. Two hunks.\n"
        ),
    },
    "m1_csrc/m1_bq27421.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "MUST-ADJUDICATE (see also the dedicated Phase 2 subsection): the fork's entire -w\n"
            "delta is warning hygiene — (a) `#if 0` guards around the four unused static helpers\n"
            "(enterConfig/exitConfig/readBlockData/readExtendedData) killing -Wunused-function;\n"
            "(b) initializer fixes `uint16_t flags/status/csum = 0;` and the designEnergy/taper/\n"
            "checksum block in bq27421_init() killing -Wmaybe-uninitialized; (c) `(void)oldCsum;`\n"
            "+ split declaration. Upstream on main resolved the SAME unused-function set by\n"
            "deleting the functions outright and now *uses* readBlockData\n"
            "(main:m1_bq27421.c:1435,1576); its `bq27421_flags` still has the uninitialized\n"
            "`uint16_t flags;` (main:1132) so the (b) class still applies. Boot-hang\n"
            "adjudication: upstream's `m1_bq27421_boot_guard.{c,h}`\n"
            "(main:m1_csrc/m1_bq27421_boot_guard.h:12-23) fixes the ITPOR `while(timeout--)`\n"
            "wraparound + unbounded post-update `while(1)` — defects the fork NEVER fixed (fork\n"
            "kept the inline loops from the fork point). On the new base upstream's boot guard\n"
            "wins; the fork loses nothing (it had no equivalent). Carry = the (b)-class\n"
            "initializer fixes only, tagged here for #39 to re-apply where upstream's code\n"
            "still needs them.\n"
        ),
    },
    "m1_csrc/m1_display.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork: `m1_info_box_display_draw(const uint8_t*)` -> `(const char*)` (pointer-sign\n"
            "vs u8g2 API), two `// No break here!` -> `/* fallthrough */` annotations, `(void)u8g2;`\n"
            "in the u8x8 byte callback. Main's display code still has the uint8_t signature\n"
            "(matching header entry below) and plain comments. Classes: pointer-sign,\n"
            "-Wimplicit-fallthrough, unused-parameter.\n"
        ),
    },
    "m1_csrc/m1_display.h": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Companion proto hunk: `m1_info_box_display_draw(uint8_t box_row, const char *ptext);`\n"
            "— carried with m1_display.c.\n"
        ),
    },
    "m1_csrc/m1_esp32_fw_update.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork: `uint8_t prn_name` -> `char` x2, `(char*)` casts on the three\n"
            "m1_fb_read_from_file call sites (pointer-sign), `#if 0` guard on one unused\n"
            'prn_name, and a bounded-copy hardening `strncpy -> snprintf("%.*s")` for the\n'
            "ESP boot-info line (truncation-safe). Main lacks all. Classes: pointer-sign,\n"
            "unused-variable; plus one genuine overflow-safe copy fix.\n"
        ),
    },
    "m1_csrc/m1_file_browser.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork: initialize-on-declaration fixes inside m1_fb_display (`uint8_t k = 0;`,\n"
            "`ext_len = 0;`) killing -Wmaybe-uninitialized on the extension-scan path.\n"
            "Main:m1_file_browser.c:817 still declares `ext_len` uninitialized (used at :1222+).\n"
            "Applicability re-checked at carry time — upstream evolved the file (+924/-255 vs\n"
            "legacy). Restyle hunks dropped.\n"
        ),
    },
    "m1_csrc/m1_gpio.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork: `uint8_t prn_name[...]` -> `char prn_name[...]` x5 across gpio screens\n"
            "(pointer-sign vs u8g2/sprintf) and `#if 0` guards on unused locals\n"
            "(GPIO_InitStruct, msg_len/msg_id). Main's gpio screens still use uint8_t buffers\n"
            "(grep 'char prn_name' on main:m1_gpio.c = 0). Classes: pointer-sign,\n"
            "unused-variable.\n"
        ),
    },
    "m1_csrc/m1_lcd.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork adds `(void)u8x8;` and `(void)arg_ptr;` in the two u8x8 callback wrappers.\n"
            "Main lacks them. Class: -Wunused-parameter. Two one-line hunks.\n"
        ),
    },
    "m1_csrc/m1_log_debug.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Genuine fixes inside a mostly-restyle delta: NULL-guard macro for the TX buffer\n"
            "predicate and `#if 0` around the unused volatile DMA TX state. Classes:\n"
            "null-dereference hardening, unused-variable. Restyle hunks (clang-format indent\n"
            "conversion) dropped. Main's log_debug.c is upstream's version — #39 re-applies the\n"
            "two fixes where its code still warrants them.\n"
        ),
    },
    "m1_csrc/m1_lp5814.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork adds `(void)xTimer;` in lp5814_stop_blink_timer (TimerHandle_t callback API\n"
            "forces the parameter; body doesn't use it). Main lacks it. Class:\n"
            "-Wunused-parameter.\n"
        ),
    },
    "m1_csrc/m1_power_ctl.c": {
        "disposition": "carried-by-patch",
        "carry": "#39 + #46",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Mixed entry. #46 hunk: `res_string.h` -> `Res_String.h` include case. #39-tagged\n"
            "hunks: `(void)xTimer;`, three empty-destroy `(void)param;`, two `(void)param;` in\n"
            "gui_update functions. Main lacks all (grep (void)xTimer on main:m1_power_ctl.c = 0).\n"
            "Classes: unused-parameter.\n"
        ),
    },
    "m1_csrc/m1_rf_spi.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork adds the `(uint8_t *)pTxData` cast initializing `.pdata_tx` in\n"
            "m1_spi_hal_wrapper (const-correctness at the struct-field boundary — the SPI\n"
            "transaction struct owns a non-const tx pointer). Main:266 still passes the const\n"
            "pointer raw. Class: discarded-qualifiers. One hunk.\n"
        ),
    },
    "m1_csrc/m1_rfid.c": {
        "disposition": "carried-by-patch",
        "carry": "#39 + #46",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Mixed entry. #46 hunk: `Res_String.h` include case. #39-tagged hunks: twelve\n"
            "`(void)param;` across the gui lifecycle stubs. Main lacks them. Class:\n"
            "-Wunused-parameter.\n"
        ),
    },
    "m1_csrc/m1_sdcard.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork: `#if 0` around the four unused static FIL/FILINFO/DIR objects and six\n"
            "`(void)param;` in the FatFs diskio entry points. Main's sdcard.c already has SOME\n"
            "(void) annotations (upstream's own pass — verify per-hunk at carry) but not the\n"
            "storage-object guards (grep '#if 0' main:m1_sdcard.c = 0). Classes:\n"
            "unused-variable, unused-parameter.\n"
        ),
    },
    "m1_csrc/m1_sdcard_man.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork: `(void)` annotations in the two timer/task entries (grep on main = partial).\n"
            "Class: unused-parameter. (The companion buffer-width fix lives in m1_sdcard_man.h —\n"
            "separate carry, see above.) Applicability re-checked at carry time.\n"
        ),
    },
    "m1_csrc/m1_storage.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            'Fork: the libc-compat format fixes `"%uGB" -> "%luGB"` + `(unsigned long)` casts\n'
            "x2 (newlib's FATFS size types are unsigned long on arm-none-eabi — format/argument\n"
            "mismatch), `uint8_t -> char prn_name` + casts in browse_gui_update (pointer-sign).\n"
            "Main:150-155 still has %u against the wide types. Classes: -Wformat,\n"
            "pointer-sign.\n"
        ),
    },
    "m1_csrc/m1_sys_init.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork: `(void)ret;`/`(void)free_heap;` (assert-checked xTaskCreate results) and\n"
            "`(void)param;` in the init task. Main lacks them. Class: set-but-unused.\n"
            "Three hunks.\n"
        ),
    },
    "m1_csrc/m1_tasks.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork: ten `(void)ret;`/`(void)free_heap;` across m1_tasks_init (all assert-checked)\n"
            "and `(void)xTask;`/`(void)pcTaskName;` in the stack-overflow hook (the log macro\n"
            "reads pcTaskName only when logging enabled). Main lacks them. Classes:\n"
            "set-but-unused, unused-parameter.\n"
        ),
    },
    "m1_csrc/m1_watchdog.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork: `(void)ret;`/`(void)free_heap;` in m1_wdt_init, `(void)param;` in the\n"
            "handler task. Main lacks them. Class: set-but-unused.\n"
        ),
    },
    "m1_csrc/privateprofilestring.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork: `(int)strlen(current_key)` cast x2 in the header-field validators —\n"
            "strlen returns size_t, `len` is int (sign-compare). Main:524 and :881 still have\n"
            "the unguarded comparison. Class: -Wsign-compare. THE libc-compat representative:\n"
            "same size_t/int mismatch family as the m1_storage.c format fixes.\n"
        ),
    },
    "m1_csrc/m1_fusb302.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork adds `(void)reg;` in pd_fusb302_init (the ID read is diagnostics-only).\n"
            "Main lacks it (grep on main = 0). Class: -Wunused-variable.\n"
            "Ours-only module note (Phase 4c): upstream carries the identical module unchanged\n"
            "since the fork point; no different-name equivalent needed.\n"
        ),
    },
    "m1_csrc/m1_low_power.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork adds `(void)ulExpectedIdleTime;` in PreSleepProcessing/PostSleepProcessing.\n"
            "Main lacks them. Class: -Wunused-parameter. Phase 4c: upstream carries the module\n"
            "unchanged (PreSleepProcessing present on upstream/main); fork delta is exactly\n"
            "these two lines.\n"
        ),
    },
    "Sub_Ghz/protocols/m1_secplus_v2_decode.c": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork initializes `uint64_t code = 0;` in subghz_decode_security_plus_20\n"
            "(set-only-on-match previously read on the not-found path). Main:64 still\n"
            "uninitialized. Class: -Wmaybe-uninitialized — a genuine correctness fix for the\n"
            "decode-failure path, not just hygiene. Phase 4c: upstream carries the module\n"
            "unchanged since fork point.\n"
        ),
    },
    "Sub_Ghz/radio_config/si4463_revc2_patch.h": {
        "disposition": "carried-by-patch",
        "carry": "#39",
        "tags": ["recovered-warning-fix"],
        "evidence": (
            "Fork appends `0, 0, 0, 0, {0}` to RADIO_PATCH_CONFIGURATION_DATA — the\n"
            "si4463_patch struct gained fields upstream of the fork point and this initializer\n"
            "left them unspecified (-Wmissing-field-initializers; also zeroes the trailing\n"
            "array). Main:43-45 still has the short initializer. Class:\n"
            "missing-field-initializers.\n"
        ),
    },
    "README.md": {
        "disposition": "carried-by-patch",
        "carry": "#41 + #42 + #43 + #44",
        "evidence": (
            "Fork rewrote the README around fork features: DFU flashing workflow, serial\n"
            "console commands, Universal Remote usage + IR database provisioning, menu/feature\n"
            "status matrix. Main's README is upstream's + the PR-2 \"based on upstream 0.8.1.0\"\n"
            "note. The fork sections return with their features (#41 DFU docs, #43 IR docs,\n"
            "#42/#44 console/command docs) rather than landing stale documentation for absent\n"
            "features now.\n"
        ),
    },
}


def entry_body(f, name_status, cls, numstat):
    d = DISPOSITIONS.get(f)
    disposition = d["disposition"] if d else "unknown"
    carry = d.get("carry") if d else None
    tags = d.get("tags", []) if d else []
    evidence = d.get("evidence", "_pending_") if d else "_pending_"

    add, del_ = numstat.get(f, ("-", "-"))
    status = name_status.get(f, "?")
    hunks, total, more = hunk_summaries(f)
    origins = origin_log(f)

    lines = [f"### `{f}`", ""]
    lines.append(f"- Status: {status} ({cls}; -w delta +{add}/-{del_}; {total} hunks)")
    lines.append(f"- Disposition: **{disposition}**")
    if carry:
        lines.append(f"- Carry patch: {carry}")
    if tags:
        lines.append(f"- Tags: {', '.join(tags)}")
    lines.append("- Fork origin: " + (", ".join(origins) if origins else "(none)"))
    if hunks:
        hlist = [f"`{h}`" for h in hunks]
        if more > 0:
            hlist.append(f"*+{more} more hunks*")
        lines.append("- Hunk summary (-w): " + "; ".join(hlist))
    lines.append("")
    lines.append(evidence)
    lines.append("")
    return "\n".join(lines)


SUMMARY_INTRO = """\
This ledger dispositions every fork-changed file of merge 1 (upstream 0.8.1.0
adoption, issues #36–#39). Basis:

- Fork point (merge base vs upstream): `93acd751` (`git merge-base` of the
  pre-graft fork history; upstream's "Initial Upload" tree the fork diverged
  from)
- Fork tip: tag `legacy/v0.8.11` → `d99e2f8` (immutable archive, PR #47)
- New base: `main` = upstream pin `2af1f8a` + foundation re-land
  (`git merge-base main upstream/main` == `2af1f8a`)
- Enumeration: `git diff --name-only 93acd751 legacy/v0.8.11` — hard invariant
  **113 files** (83 both-changed + 30 ours-only), asserted by
  `tools/gen_merge_ledger.py` and by the gate `cmake/m1_01/verify_merge_ledger.sh`.

Disposition vocabulary (exactly one per file):

| Disposition | Meaning |
|---|---|
| `retained-by-upstream` | The fork-relevant content is already present in the new base — written by upstream, or re-landed onto it by the PR-1/PR-2 foundation work. Evidence: `main` file:line. |
| `carried-by-patch` | Content the new base lacks and worth keeping; names the P4 carry issue (#39–#44/#46) and cites fork origin SHAs. |
| `upstream-equivalent` | The file differs, but upstream achieves the fork's intent differently/better on the new base; evidence cited on both sides. |
| `intentionally-dropped` | Not carried; written reason below against the maintainer's evidentiary bar. |

Hunk classification uses whitespace-ignored diffs throughout
(`git diff -w 93acd751 legacy/v0.8.11`) so EOL/restyle noise cannot hide real
deltas; per-file numstat and hunk headers above are `-w` values. The fork's
"Zero-Warning Core" restyle (811b346) embedded genuine warning fixes; every
genuine fix found is tagged `recovered-warning-fix` below — issue #39 (PR-4)
mines exactly this tag.
"""

# Phase 3/4/5 sections are appended after the per-file entries; see the
# FEATURE_VERDICTS / EVIDENCE_RECERTURE / VENDOR_DELTAS blocks added in the
# respective phase commits.

MUST_ADJUDICATE = """
## Must-adjudicate items (issue #38 AC)

### 1. Upstream `m1_bq27421` boot-hang fix vs the fork's variant

- Upstream extracted the two bounded-wait loops of `bq27421_init()` into
  `m1_csrc/m1_bq27421_boot_guard.{c,h}` (new base; header rationale at
  main:m1_bq27421_boot_guard.h:12-23): fixes (a) the ITPOR wait
  `while (timeout--)` uint16 wraparound that converted a genuine gauge timeout
  into a false success, and (b) the unbounded post-update `while(1)`
  current-stabilization wait that hung boot forever with the watchdog kicked.
- The fork NEVER fixed either defect — its `m1_bq27421.c` delta vs the fork point
  is 100% warning hygiene (`#if 0` guards on unused statics, `= 0` initializers;
  see the per-file entry). The inline hardware-coupled loops shipped as-is in
  every fork release through v0.8.11.
- **Verdict: upstream's boot guard wins on the new base; the fork loses nothing**
  (it had nothing equivalent). The fork's initializer fixes that still apply to
  upstream's code (e.g. `uint16_t flags;` at main:m1_bq27421.c:1132) are tagged
  `recovered-warning-fix` for #39.

### 2. NFC / LF-RFID small changes

- LF-RFID: the fork's deltas are exactly (a) filename-case fixes
  (`uiview.h`->`uiView.h` in em4100/h10301; `res_string.h`->`Res_String.h` in
  lfrfid_file.c) -> carried under #46; (b) decoder signature narrowing
  (`uint16_t`->`int size` + `(uint8_t)` cast) and `(void)proto;`/`#if 0` hygiene
  -> tagged `recovered-warning-fix` under #39; (c) `(void)ret/(void)free_heap`
  in lfrfid.c task init -> #39. No functional LF-RFID behavior change existed.
- NFC: fork deltas are the `nfc_*`->`m1_nfc_*` renames + hygiene on code upstream
  has since rewritten wholesale (m1_nfc.c +6652/-692 vs legacy; nfc_storage.c
  restyle-only) -> `upstream-equivalent`; the case fixes in nfc_file.c /
  nfc_listener.c / nfc_poller.c -> carried under #46. **Nothing functional
  dies**: upstream's NFC stack supersedes, and the genuine small fixes have
  carry vehicles.

### 3. libc-compat changes

The fork's libc-compat work is the newlib/arm-none-eabi type-width family, all
verified still-open on the new base and tagged for #39:
`%u`->`%lu` + `(unsigned long)` casts (m1_storage.c:150-155 on main unfixed),
`(int)strlen` sign-compare guards (privateprofilestring.c:524,:881 on main
unfixed), `(char*)`/`(uint8_t*)` pointer-sign casts (m1_rf_spi.c:266,
m1_gpio.c, m1_esp32_fw_update.c), `uint64_t code = 0` (m1_secplus_v2_decode.c:64
on main unfixed). The vendored `Esp32_serial_flasher/app/stm32/libc_compat.c` is
byte-identical between fork point and the new base (blob 52fb9b1) — the fork
wired it into its (dropped) build restructure; upstream's build already compiles
it where needed.

### 4. Warning fixes embedded in the dropped zero-warning restyle

The Zero-Warning Core initiative (811b346) mixed clang-format sweeps with genuine
fixes. Every genuine fix found in this audit is tagged `recovered-warning-fix`
above (33 file entries; per-entry hunks cite the warning class and the fix
summary). PR-4 (#39) mines exactly this tag and re-applies the fixes as isolated
commits against the new base, re-verifying applicability per file since upstream
evolved several of them. The restyle itself is `intentionally-dropped` everywhere
(see the nfc_storage.c / m1_sub_ghz_api.c / m1_esp_hosted_config.h entries for
representative reasoning).
"""

EXTRA_SECTIONS = (
    MUST_ADJUDICATE
    + """
## Feature verdicts — #7 / #4 / #3 / #11 (with corrected wiring status)

Corrected wiring status on the legacy tree (re-verified for this ledger; the
exploration-era claim "menu-wired" holds only for #11):

| Issue | Feature | Legacy state (verified) | Menu wiring on legacy |
|---|---|---|---|
| #7 | System settings | `settings_system()` exists — placeholder body: draws "SYSTEM..." then busy-waits on the key queue (legacy m1_settings.c:176-229); inherited from the upstream initial upload, fork never implemented | record `menu_Settings_System` exists (legacy m1_menu.c:316); parent link **commented out** (legacy m1_menu.c:358) |
| #4 | LCD & Notifications | `settings_lcd_and_notifications()` exists — same placeholder pattern, draws "LCD..." (legacy m1_settings.c:83-137); fork never implemented | record `menu_Settings_LCD_and_Notifications` exists (legacy m1_menu.c:280); parent link **commented out** (legacy m1_menu.c:356) |
| #3 | Sub-GHz Radio Settings | function definition **deleted** by the fork — only the declaration remains, commented (legacy m1_sub_ghz.c:286); the pre-fork body was itself a `m1_gui_let_update_fw()` placeholder | record `menu_Sub_GHz_Radio_Settings` exists (legacy m1_menu.c:68-73) but is **absent from the Sub-GHz children list** (legacy m1_menu.c:87-89) |
| #11 | Bluetooth Config | `bluetooth_config()` exists — placeholder: calls `m1_gui_let_update_fw()` then busy-waits (legacy m1_bt.c:327-372); fork's own docs label it "no config UI" (legacy ARCHITECTURE.md:327, README.md:467) | record `menu_Bluetooth_Config` (legacy m1_menu.c:380) **live-wired** in the Bluetooth children list (legacy m1_menu.c:398) |

Upstream removed all four placeholder functions and their menu records during its
0.8.1.0 evolution (e.g. upstream delta deleted `settings_system` /
`settings_lcd_and_notifications` from m1_settings.c; `main` has neither; the new
base's Bluetooth menu is Scan Devices / Signal Meter / GATT Explorer / Saved —
main:m1_menu.c:420-447).

### Verdicts (all four): `intentionally-dropped` — the placeholder code; the issues stay OPEN

The fork's tree contains **no implementation** of any of the four features — only
non-functional placeholders inherited from the upstream initial upload (text screen
+ busy-wait, or the "please update firmware" screen). Against the maintainer's
evidentiary bar:

1. **There is nothing to carry.** Carrying `settings_system()` et al. would
   re-plant a `u8g2_DrawStr(..., "SYSTEM...")` + `while(1)` busy-loop that the fork
   itself disabled (3 of 4 menu links commented out or the definition deleted) and
   that upstream removed as dead code. Zero feature value survives the carry.
2. **The issues' actual asks were never "carry the stub".** Each issue text
   explicitly labels the current state a "stub placeholder" and specifies the real
   implementation (e.g. #3: modulation/bandwidth/deviation/power/frequency screen
   persisted via the config pattern; #11: advertisement payload editor with
   AppleJuice-style presets). Those asks are new implementation work on the new
   base, not legacy content retention.
3. **The features remain wanted and roadmap-ranked** — all four appear in the
   fork's ARCHITECTURE.md "Recommended Implementation Order" (priorities 2, 5, 6,
   7; preserved above), and the new base provides equal or better substrate
   (upstream's SI4463 API, settings persistence, BT stack).

Consequently: the stub hunks are dispositioned `intentionally-dropped` inside the
host-file entries (m1_settings.c / m1_sub_ghz.c / m1_menu.c / m1_bt.c), and **all
four issues #3, #4, #7, #11 remain OPEN, re-targeted at fresh implementation on
the new base** (ledger-linked comments posted at PR time). Closing them as
wontfix would misrepresent maintainer intent: the drop applies to placeholder
code, not to the feature requests. This deviates from the binary
issue-resolution mapping originally sketched for this unit (carried->open /
dropped->wontfix) and is documented here deliberately: the mapping's premise
("fork-implemented features") is contradicted by the verified evidence above.
"""
)


def count_by(files, key):
    counts = {}
    for f in files:
        d = DISPOSITIONS.get(f)
        v = d["disposition"] if d else "unknown"
        counts[v] = counts.get(v, 0) + 1
    return counts


def generate() -> str:
    files, name_status, classes, numstat = enumerate_files()

    n = len(files)
    both = sum(1 for f in files if classes[f] == "both-changed")
    ours = n - both
    if (n, both, ours) != (EXPECTED_FILES, EXPECTED_BOTH_CHANGED, EXPECTED_OURS_ONLY):
        sys.stderr.write(
            f"FATAL: enumeration invariant violated: files={n} (expect {EXPECTED_FILES}), "
            f"both-changed={both} (expect {EXPECTED_BOTH_CHANGED}), ours-only={ours} "
            f"(expect {EXPECTED_OURS_ONLY}). Basis changed — STOP and re-derive.\n"
        )
        raise SystemExit(1)

    counts = count_by(files, None)
    warnfix = sum(
        1
        for f in files
        if "recovered-warning-fix" in DISPOSITIONS.get(f, {}).get("tags", [])
    )

    out = []
    out.append("<!-- See COPYING.txt for license details. -->\n")
    out.append("# Merge ledger — fork-changed file dispositions (issue #38)\n")
    out.append(SUMMARY_INTRO)
    out.append("## Summary\n")
    out.append("| Metric | Count |")
    out.append("|---|---|")
    out.append(f"| Fork-changed files (hard invariant) | **{n}** |")
    out.append(f"| Both-changed (also touched upstream since fork point) | {both} |")
    out.append(f"| Ours-only (upstream untouched since fork point) | {ours} |")
    for v in DISPOSITION_VALUES:
        out.append(f"| {v} | {counts.get(v, 0)} |")
    out.append(f"| unknown (must be 0 — gate) | {counts.get('unknown', 0)} |")
    out.append(f"| Entries tagged `recovered-warning-fix` (feed for #39) | {warnfix} |")
    out.append("")
    out.append(
        "*Regenerate deterministically with `python tools/gen_merge_ledger.py`; "
        "the DISPOSITIONS table in that script is the single source of truth. "
        "Gate: `bash cmake/m1_01/verify_merge_ledger.sh`.*\n"
    )

    out.append("## Per-file dispositions\n")
    for f in files:
        out.append(entry_body(f, name_status, classes[f], numstat))

    extra = globals().get("EXTRA_SECTIONS", "")
    if extra:
        out.append(extra)

    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--output", default=str(REPO / "docs/merge/LEDGER.md"))
    ap.add_argument(
        "--check",
        action="store_true",
        help="verify the checked-in ledger matches regeneration (no write)",
    )
    args = ap.parse_args()

    content = generate()

    if args.check:
        current = Path(args.output).read_text(encoding="utf-8")
        if current != content:
            sys.stderr.write(
                "FATAL: docs/merge/LEDGER.md is stale vs gen_merge_ledger.py "
                "(rerun the generator and commit both).\n"
            )
            raise SystemExit(1)
        print("ledger check: OK (regeneration matches checked-in ledger)")
        return

    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    Path(args.output).write_text(content, encoding="utf-8", newline="\n")
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
