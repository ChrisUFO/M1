#!/usr/bin/env python3
"""Release delta-audit gate and generator (issue #45).

Proves the issue #45 final gate -- ``git diff upstream/main..main`` shows
exactly the justified carry layer -- against the release-cut snapshot
recorded in ``docs/merge/DELTA_AUDIT.md``:

* every file whose blob differs from ``upstream/main`` (the adopted pin,
  verified to be the merge base) is accounted for either as a **content**
  row in the audit table (citing the merge-ledger disposition where one
  exists, or the merge-era change otherwise) or as an **EOL-only** entry
  (blob differs solely by the merge-1 LF renormalization; zero content
  hunks under ``git diff --ignore-cr-at-eol``);
* no table row is stale (every row corresponds to a real diff entry), and
  every Change cell matches the recomputed status/add-count -- except the
  audit's own row, which is rendered ``M (self)`` (or ``A (self)``) because
  rewriting the table changes that file's own add-count.

Content classification ignores carriage returns only (``git diff
--ignore-cr-at-eol``), never all whitespace: a delta that changes
nothing but whitespace *inside* a literal is a real content change and
must be audited, while the merge-1 CRLF->LF renormalization stays
classifiable as EOL-only.

Coverage is always evaluated at ``HEAD``. The ``basis-end`` recorded in
the doc is provenance for when the table was cut: it must resolve and be
an ancestor of ``HEAD``, so a commit made after the cut cannot add a
differing file that escapes the audit.

Fail-closed: any unexplained or stale row, any diff-side file missing
from the doc, an unresolvable recorded basis, or a basis that is not an
ancestor of ``HEAD`` exits non-zero.

Usage (from the repository root):

    python tools/check_delta_audit.py            # verify the doc (gate)
    python tools/check_delta_audit.py --emit     # print a regenerated doc
                                                # body for DELTA_AUDIT.md
"""

import argparse
import re
import subprocess
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")


REPO_FILES = {
    "DOC": "docs/merge/DELTA_AUDIT.md",
    "LEDGER": "docs/merge/LEDGER.md",
}

# Merge-3 series commits landed without a PR reference in their subject;
# the series index in the release plan is the authority for their PR numbers.
MERGE3_PR_BY_SHA = {
    "cc921a5": "#66",  # PR-1 settings store
    "009d67b": "#68",  # PR-2 LCD/Notifications + System screens
    "75d9217": "#71",  # PR-3 RTC date/time
    "087b478": "#72",  # PR-4 streaming .ir loader
    "e8ae587": "#75",  # PR-5 IR browser
    "b8e06bd": "#78",  # PR-6 favorites/recent + cycle-all
    "a90ae07": "#81",  # PR-7 Sub-GHz radio settings
    "cfdb84d": "#82",  # PR-8 crypto + HW RNG
    "50c4a47": "#85",  # PR-9 encrypted credential column
    "34da870": "#86",  # PR-10 ESP feature gate
    "d970c8e": "#88",  # PR-11 WiFi join flow
    "685a60c": "#93",  # PR-12 BLE adv editor
}

# Explanations for content deltas the merge ledger does not cover (the
# ledger dispositions the LEGACY fork delta; these are merge-era changes
# that exist only on top of the adopted base). Exact paths first, then
# prefix rules (longest match wins). A diff file matching neither the
# ledger nor this map fails the gate -- that is the point.
GLOSS_EXACT = {
    ".gitattributes": "LF-in-repo policy re-landed on the adopted base; drives the EOL-only class below",
    ".github/CONTRIBUTING.md": "fork contribution guide: ledger-coordination guidance (merge 1) plus the future-sync instruction added by this release",
    ".github/workflows/host-tests.yml": "CI entry point running the wired host-test aggregator (PR-1)",
    ".project": "Eclipse project name tracks the derived artifact stem M1_v0.9.0.0-UFO",
    ".vscode/launch.json": "debug config tracks the derived artifact stem",
    ".vscode/tasks.json": "build task config tracks the derived artifact stem",
    "Core/Inc/stm32h5xx_hal_conf.h": "HAL config enables the RNG module for the crypto foundation (PR-8)",
    "Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_rng.h": "STM32H5 HAL RNG driver header, absent from the upstream CubeMX generate set, added for HW RNG (PR-8)",
    "Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_rng_ex.h": "STM32H5 HAL RNG extended driver header (PR-8)",
    "Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_rng.c": "STM32H5 HAL RNG driver implementation (PR-8)",
    "Infrared/m1_ir_browser.c": "IR file browser UX over the streaming loader (PR-5; favorites/recent + cycle-all wiring PR-6)",
    "Infrared/m1_ir_browser.h": "IR file browser header incl. documented scan bounds (PR-5/PR-6)",
    "Infrared/m1_ir_file.c": "streaming per-device .ir loader with strict-skip (PR-4)",
    "Infrared/m1_ir_file.h": "streaming .ir loader API (PR-4)",
    "Infrared/m1_ir_remotes.c": "upstream universal-remotes module: single-frame EOF wrap + favorites hooks (PR-4 carry, fix in PR-6 harden)",
    "Infrared/m1_ir_remotes.h": "upstream universal-remotes module header (PR-4/PR-6)",
    "build.sh": "canonical cross-platform release build: derived stem, clean-tree gate, CRC contract, manifest (issue #40, PR #59)",
    "cmake/m1_01/verify_merge_ledger.sh": "merge-ledger completeness gate (issue #38, PR #51)",
    "docs/README.md": "documentation index for the adopted base (merge 1), extended per unit",
    "docs/architecture.md": "new-base architecture overview, updated by every unit that changes subsystems",
    "docs/build-and-debug.md": "canonical build/CRC/manifest documentation (issues #40/#41)",
    "docs/ir-provisioning.md": "IR database provisioning guide (PR-5, bounds table PR-5 harden; starter-pack pointer added by this release)",
    "docs/merge/LEDGER.md": "merge ledger itself (issue #38, PR #51)",
    "docs/protocols/README.md": "protocol index updated for the CLI and USB-CDC pages (issues #44)",
    "docs/protocols/cli.md": "console command reference for the ported CLI diagnostics (issue #44, PR #65)",
    "docs/protocols/firmware-update.md": "flash layout, boot failsafe, DFU and rollback documentation (issue #41, PRs #61/#62; release-state sweep by this release)",
    "docs/protocols/usb-cdc.md": "USB CDC RX diagnostics documentation (issue #44, PRs #64/#65)",
    "docs/security.md": "security architecture incl. RNG/crypto notes (PR-8/PR-9)",
    "docs/security/wifi-credential-threat-model.md": "WiFi credential threat model (issue #42, PR-9)",
    "documentation/mbt.md": "build-tool walkthrough tracking the derived stem (issues #40/#41; stem sweep merge 1)",
    "documentation/merge1-baseline-evidence.md": "merge-1 virgin-baseline evidence at the upstream pin (issue #37)",
    "documentation/usingVSCode.md": "VS Code walkthrough; build-target name swept to the derived stem by this release",
    "m1_csrc/m1_boot_failsafe.h": "boot failsafe API/contract header (issue #41, PR #61)",
    "m1_csrc/m1_bq27421.h": "fuel-gauge header: console-facing additions (issue #44, PR #65)",
    "m1_csrc/m1_bt.h": "Bluetooth app header: adv-editor menu wiring (PR-12)",
    "m1_csrc/m1_bt_adv.c": "BLE advertisement preset core + editor logic, host-testable (PR-12)",
    "m1_csrc/m1_bt_adv.h": "BLE advertisement editor API (PR-12)",
    "m1_csrc/m1_cdc_rx_stats.h": "USB CDC RX drop/high-watermark counters (issue #44, PR #64)",
    "m1_csrc/m1_dfu_entry.h": "ROM DFU entry/strap/recovery-offer API (issue #41, PR #62)",
    "m1_csrc/m1_esp_feature_gate.c": "live ESP32 firmware feature gate, four states (PR-10)",
    "m1_csrc/m1_esp_feature_gate.h": "feature-gate API (PR-10)",
    "m1_csrc/m1_feedback_manager.c": "feedback manager: LED-notification gate and brightness hooks (PR-2)",
    "m1_csrc/m1_feedback_manager.h": "feedback manager API (PR-2)",
    "m1_csrc/m1_feedback_orchestration.c": "feedback orchestration sequences gated by the LED setting (PR-2)",
    "m1_csrc/m1_feedback_task.c": "feedback task: synchronous awake-baseline seeding for boot-time brightness apply (PR-2)",
    "m1_csrc/m1_infrared.h": "infrared app header: browser menu wiring (PR-5)",
    "m1_csrc/m1_isr_drop_counters.h": "ISR drop counters for deferred-context ingestion (issue #52, PR #55)",
    "m1_csrc/m1_log_capture.h": "console log-capture buffer API (issue #44, PR #65)",
    "m1_csrc/m1_lp5814.h": "LED driver header: pedantic-clean binary literals (issue #46, PR #57)",
    "m1_csrc/m1_manager_fwupdate.h": "manager firmware-update protocol additions for the failsafe (issue #41)",
    "m1_csrc/m1_rng.c": "STM32H5 HW RNG wrapper: sticky-failure latch, no fallback (PR-8)",
    "m1_csrc/m1_rng.h": "HW RNG wrapper API (PR-8)",
    "m1_csrc/m1_rtc.c": "RTC read/write helpers with HAL shadow/init-mode semantics (PR-3)",
    "m1_csrc/m1_rtc.h": "RTC access API (PR-3)",
    "m1_csrc/m1_rtc_date.c": "date/time editor with validation and 24-hour stepping (PR-3)",
    "m1_csrc/m1_rtc_date.h": "date/time editor API (PR-3)",
    "m1_csrc/m1_sdcard_capacity.h": "SD capacity reporting for the System screen (issue #44 re-home, PR #65)",
    "m1_csrc/m1_sdcard_provision.h": "SD provisioning constants touched by the settings re-land (PR-1)",
    "m1_csrc/m1_settings.h": "settings menu: new leaves (LCD & Notifications, System, Date & Time) (PR-2/PR-3)",
    "m1_csrc/m1_settings_store.c": "durable user-settings store on SD (settings.cfg, atomic save, tolerant parse) (PR-1, v2 migration PR-7)",
    "m1_csrc/m1_settings_store.h": "settings-store API and format contract (PR-1)",
    "scripts/check_no_raw_bkp_writes.sh": "guard asserting no raw backup-register writes (issue #41, PR #61)",
    "scripts/crc_parity_golden.txt": "golden digests for the CRC emitter parity harness (issue #40, PR #59)",
    "scripts/portable_hash.sh": "portable md5/sha256 helpers for Git-Bash/Linux/macOS (issue #40, PR #59)",
    "scripts/program.jlink": "SWD programming script tracking the derived stem",
    "scripts/run_host_tests.sh": "host-test aggregator running the wired suites (PR-1, extended per unit)",
    "scripts/verify_crc_parity.sh": "CRC emitter parity harness (issue #40, PR #59)",
    "scripts/verify_esp_interface_contract.sh": "ESP SPI interface contract check, updated for the version-derived build (issue #40)",
    "tools/WINDOWS_VERIFICATION.md": "Windows verification adapter guide (issue #49)",
    "tools/crc_test_vectors.py": "deterministic CRC test-vector generator (issue #40, PR #59)",
    "tools/emulate_srec_cat_crc.py": "pure-Python srec_cat CRC emitter for hosts without SRecord (issue #49, PR #53)",
    "tools/gcc-nosan/gcc-nosan.c": "gcc shim landing the Windows verification adapters (issue #49, PR #53)",
    "tools/gen_merge_ledger.py": "deterministic merge-ledger generator (issue #38, PR #51)",
    "tools/m1cp_host_test/.gitignore": "host-test build-output ignores (issue #41)",
    "tools/m1cp_host_test/Makefile": "host-test build for the m1cp extraction suites (issues #41/#44)",
    "tools/m1cp_host_test/check_mtest_compile.sh": "mtest compile guard (issue #44, PR #65)",
    "tools/m1cp_host_test/seam_util.h": "shared seam helpers for the m1cp host tests (issue #44, PR #65)",
    "tools/m1cp_host_test/test_cdc_rx_stats.c": "CDC RX stats host test (issue #44, PRs #64/#65)",
    "tools/m1cp_host_test/test_cli_commands.c": "console command host test incl. version grammar (issue #44, PR #65)",
    "tools/m1cp_host_test/test_log_capture.c": "log-capture host test (issue #44, PR #65)",
    "tools/m1cp_host_test/test_m1cp_dfu.c": "DFU entry host test (issue #41, PR #62)",
    "tools/m1cp_host_test/test_m1cp_failsafe.c": "boot failsafe host test (issue #41, PR #61)",
    "tools/m1cp_host_test/test_m1cp_fs.c": "filesystem seam host test (issue #40)",
    "tools/m1cp_host_test/test_no_raw_bkp_writes.sh": "backup-register write guard as a host test (issue #41, PR #61)",
    "tools/m1cp_host_test/test_sdcard_usage.c": "SD usage host test (issue #44 re-home, PR #65)",
    "tools/m1cp_host_test/test_update_intake_size.c": "update intake size host test (issue #41, PR #61)",
    "tools/test_fw_version_compare.py": "rollback-comparator host test pinning 0.9.0.0 > 0.8.11/0.8.1.0 (issues #36/#37)",
    "Battery/golden_image_0420.h": "case-only rename (from Golden_Image_0420.h) so Linux builds resolve the include (issue #46, PR #56)",
    "m1_csrc/res_string.c": "case-only rename (from Res_String.c; ledger row `m1_csrc/Res_String.c`, carried-by-patch) for case-sensitive filesystems (issue #46, PR #56)",
    "m1_csrc/res_string.h": "case-only rename (from Res_String.h; ledger row `m1_csrc/Res_String.h`, carried-by-patch) for case-sensitive filesystems (issue #46, PR #56)",
    "docs/releases.md": "release and version policy page, rewritten for the 0.9.0.0-UFO release state by this release",
    "docs/merge/DELTA_AUDIT.md": "this audit: the issue #45 final-gate record (this release)",
    "tools/check_delta_audit.py": "machine gate behind this audit: regenerates and verifies zero unexplained rows (this release)",
    "documentation/0.9.0.0-ufo-verification.md": "release verification record: milestone state, version surfaces, build + parity evidence (this release)",
    "docs/releases/0.9.0.0-UFO.md": "v0.9.0.0-UFO release notes (this release)",
    "docs/releases/irdb-attribution.md": "CC0-1.0 text and attribution for the IR starter database (this release)",
    "docs/releases/0.9.0.0-ufo-smoke-checklist.md": "archived on-device smoke checklist aggregating the open hardware-acceptance issues (this release)",
    "scripts/fetch_ir_starter_db.sh": "reproducible fetch/pack step for the IR starter-database release asset at the pinned Flipper-IRDB commit (this release)",
}

GLOSS_PREFIX = [
    (
        "m1_csrc/test/stub_irmp_host/",
        "IRMP host-build stub headers for the golden-decode harness (PR-4)",
    ),
    ("m1_csrc/test/", "wired host test suite for its unit (see PR refs)"),
    ("m1_csrc/tests/", "pre-existing host test adjusted by its unit (see PR refs)"),
    (
        "NFC/NFC_drv/common/test/",
        "upstream NFC source-matching host test, retargeted to the LF-normalized tree (merge 1)",
    ),
]

BASIS_RE = re.compile(r"^basis-end:\s*`([0-9a-f]{40})`", re.M)
CONTENT_MARK = (
    "<!-- check_delta_audit: content-table begin -->",
    "<!-- check_delta_audit: content-table end -->",
)
EOL_MARK = (
    "<!-- check_delta_audit: eol-list begin -->",
    "<!-- check_delta_audit: eol-list end -->",
)


def git(*args):
    return subprocess.run(
        ["git", "-c", "core.quotePath=false", *args],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=True,
    ).stdout


def load_ledger(ref):
    """path -> 'disposition -> carry-issue' from the ledger's headings."""
    entries = {}
    path = None
    for line in git("show", f"{ref}:{REPO_FILES['LEDGER']}").splitlines():
        m = re.match(r"^### `([^`]+)`", line)
        if m:
            path = m.group(1)
            entries[path] = ""
            continue
        if path:
            m = re.match(r"^- Disposition: \*\*([^*]+)\*\*", line)
            if m:
                entries[path] = m.group(1)
            m = re.match(r"^- Carry patch: (#[0-9]+)", line)
            if m and entries[path]:
                entries[path] = f"{entries[path]} -> {m.group(1)}"
    return entries


def classify(up, end):
    """Return (files_all, content) where content maps path -> (status, add)."""
    files_all = [f for f in git("diff", "--name-only", up, end).splitlines() if f]
    content = {}
    for line in git("diff", "--ignore-cr-at-eol", "--numstat", up, end).splitlines():
        if not line:
            continue
        add, dele, path = line.split("\t")
        m = re.match(r"^(.*)\{.+ => (.+)\}$", path)
        if m:
            content[m.group(1) + m.group(2)] = ("R100", add)
            continue
        status = git("diff", "--name-status", up, end, "--", path).split("\t")[0]
        content[path] = (status, add)
    return files_all, content


def pr_refs(up, end, path):
    refs = []
    for line in git("log", "--format=%h|%s", up, end, "--", path).splitlines():
        sha, subject = line.split("|", 1)
        m = re.search(r"\(#(\d+)\)\s*$", subject)
        pr = f"#{m.group(1)}" if m else MERGE3_PR_BY_SHA.get(sha)
        if pr and pr not in refs:
            refs.append(pr)
    return refs


def gloss_for(path):
    if path in GLOSS_EXACT:
        return GLOSS_EXACT[path]
    for prefix, gloss in GLOSS_PREFIX:
        if path.startswith(prefix):
            return gloss
    return None


def emit(up, end, ledger):
    files_all, content = classify(up, end)
    eol_only = [f for f in files_all if f not in content]
    lines = []
    lines.append(f"basis-end: `{git('rev-parse', end).strip()}`")
    lines.append("")
    lines.append(f"| Content rows (table below) | **{len(content)}** |")
    lines.append(
        f"| — cited to a merge-ledger disposition | {sum(1 for p in content if p in ledger)} |"
    )
    lines.append(
        f"| — explained as a merge-era change (gloss + PR refs) | {sum(1 for p in content if p not in ledger)} |"
    )
    lines.append(f"| EOL-only rows (appendix below) | **{len(eol_only)}** |")
    lines.append(f"| Total differing files | **{len(files_all)}** |")
    lines.append("")
    lines.append(CONTENT_MARK[0])
    lines.append("| File | Change | Ledger disposition | Merge-era explanation (PRs) |")
    lines.append("|---|---|---|---|")
    missing = []
    for path in sorted(content):
        status, add = content[path]
        if path == REPO_FILES["DOC"]:
            # Self-reference: rewriting this table changes this file's own
            # add-count, so a numeric cell could never be stable. The marker
            # is what the gate expects for this one row.
            delta = f"{status[:1]} (self)"
        else:
            delta = "rename" if status == "R100" else f"{status} +{add}"
        if path in ledger:
            refs = ", ".join(pr_refs(up, end, path))
            extra = f"; later PRs: {refs}" if refs else ""
            lines.append(
                f"| `{path}` | {delta} | {ledger[path]} (LEDGER.md) | see ledger row{extra} |"
            )
        else:
            g = gloss_for(path)
            if g is None:
                missing.append(path)
                g = "**UNEXPLAINED — gate failure**"
            refs = ", ".join(pr_refs(up, end, path)) or "—"
            klass = (
                "new file added by merge-era work"
                if status == "A"
                else "changed by merge-era work (not a legacy-fork delta)"
            )
            lines.append(f"| `{path}` | {delta} | — ({klass}) | {g} ({refs}) |")
    lines.append(CONTENT_MARK[1])
    lines.append("")
    lines.append(EOL_MARK[0])
    lines.append("```text")
    lines.extend(sorted(eol_only))
    lines.append("```")
    lines.append(EOL_MARK[1])
    return "\n".join(lines), missing, files_all, content, eol_only


def doc_basis(doc):
    m = BASIS_RE.search(doc)
    if not m:
        sys.exit("FAIL: DELTA_AUDIT.md has no resolvable basis-end commit")
    return m.group(1)


def check(up):
    doc = open(REPO_FILES["DOC"], encoding="utf-8").read()
    basis = doc_basis(doc)
    # Provenance: the recorded basis must resolve and upstream must still be
    # its merge base (the audit may never silently widen to include upstream's
    # own later movement)...
    git("rev-parse", f"{basis}^{{commit}}")
    base = git("merge-base", up, basis).strip()
    pin = git("rev-parse", up).strip()
    if base != pin:
        sys.exit(f"FAIL: merge-base({up}, basis) = {base} != pin {pin}")
    # ...and it must be an ancestor of HEAD, so coverage below (evaluated at
    # HEAD, not at the recorded basis) still describes this tree.
    if git("merge-base", basis, "HEAD").strip() != basis:
        sys.exit("FAIL: recorded basis is not an ancestor of HEAD")
    ledger = load_ledger("HEAD")
    _, missing, _, content, eol_only = emit(up, "HEAD", ledger)

    def between(mark):
        i = doc.index(mark[0]) + len(mark[0])
        return doc[i : doc.index(mark[1])]

    doc_cells = {}
    for line in between(CONTENT_MARK).splitlines():
        m = re.match(r"^\| `([^`]+)` \| ([^|]*?)\s*\|", line)
        if m:
            doc_cells[m.group(1)] = m.group(2).strip()
    doc_rows = set(doc_cells)
    doc_eol = set(
        l.strip()
        for l in between(EOL_MARK).splitlines()
        if l.strip() and not l.strip().startswith("```")
    )
    want_rows, want_eol = set(content), set(eol_only)

    fails = []
    for p in missing:
        fails.append(f"no gloss and no ledger row for content file: {p}")
    for p in sorted(want_rows - doc_rows):
        fails.append(f"diff file missing from the audit table: {p}")
    for p in sorted(doc_rows - want_rows):
        fails.append(f"stale audit row (not in the recorded diff): {p}")
    for p in sorted(want_rows & doc_rows):
        status, add = content[p]
        if p == REPO_FILES["DOC"]:
            expected = f"{status[:1]} (self)"
        else:
            expected = "rename" if status == "R100" else f"{status} +{add}"
        got = doc_cells[p]
        if got != expected:
            fails.append(
                f"stale Change cell for {p}: doc has {got!r}, diff says {expected!r}"
            )
    for p in sorted(want_eol - doc_eol):
        fails.append(f"EOL-only file missing from the appendix: {p}")
    for p in sorted(doc_eol - want_eol):
        fails.append(f"stale appendix entry (not in the recorded diff): {p}")

    print(f"delta-audit gate: basis {basis[:12]} (evaluated at HEAD)")
    print(
        f"  content rows: {len(want_rows)} "
        f"({sum(1 for p in want_rows if p in ledger)} ledger-cited, "
        f"{sum(1 for p in want_rows if p not in ledger)} merge-era)"
    )
    print(f"  EOL-only rows: {len(want_eol)}")
    print(f"  total differing files: {len(want_rows) + len(want_eol)}")
    if fails:
        for f in fails:
            print(f"FAIL: {f}")
        sys.exit(1)
    print("PASS: zero unexplained rows, zero stale rows")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--emit",
        action="store_true",
        help="print a regenerated body for DELTA_AUDIT.md",
    )
    ap.add_argument(
        "--end", default="HEAD", help="basis end ref (emit mode only; default HEAD)"
    )
    args = ap.parse_args()
    up = "upstream/main"
    if args.emit:
        ledger = load_ledger(args.end)
        body, missing, _, _, _ = emit(up, args.end, ledger)
        print(body)
        if missing:
            for p in missing:
                print(
                    f"NOTE: no gloss for {p} — row emitted as UNEXPLAINED",
                    file=sys.stderr,
                )
    else:
        check(up)


if __name__ == "__main__":
    main()
