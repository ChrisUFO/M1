# Merge 1 — PR-2 baseline evidence (upstream adoption, #37)

Virgin-baseline evidence for the pinned upstream base, captured **before any
fork-side change** on branch `chore/merge1-upstream-base` at pin
`2af1f8a339dbe629f36a0551de4293b161179b71` (= `upstream/main` at execution
time; re-verified by `git fetch upstream` — pin unchanged, no re-pin needed).

Phase-5 results are appended at the bottom.

## 1. Toolchain reality (Windows host)

| Tool | Version / path | Notes |
|---|---|---|
| arm-none-eabi-gcc | **Arm GNU Toolchain 15.2.Rel1 (Build arm-15.86), gcc 15.2.1 20251203** | Resolved from `PATH` at `C:\Program Files (x86)\Arm\GNU Toolchain mingw-w64-i686-arm-none-eabi\bin\` |
| CMakePresets hardcoded compiler | `C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/...` | **Path does not exist on this host.** Preset name and path are stale labels; `cmake/gcc-arm-none-eabi.cmake` (included from CMakeLists.txt) sets `CMAKE_C_COMPILER=arm-none-eabi-gcc` as a normal variable that shadows the stale preset cache value, so the preset still configures — with the ACTUAL 15.2.Rel1 toolchain from `PATH`. Cache evidence: `CMAKE_C_COMPILER:STRING=...mingw-w64-i686-arm-none-eabi/bin/arm-none-eabi-gcc.exe` |
| CMake / Ninja | 4.1.2 / 1.13.2 | |
| Host C compiler | WinLibs MinGW-w64 **gcc 15.2.0** (x86_64-ucrt) | **Ships without ASan/UBSan runtimes** (`ld: cannot find -lasan`, `-lubsan`) |
| Python | 3.13.7 (C:\Python313) | |
| `srec_cat` (srecord) | **NOT INSTALLED** | Blocks the CRC emission POST_BUILD step — see §3 |
| `make` | **NOT AVAILABLE in git-bash** | Blocks the `Makefile` path — see §4 |

## 2. Windows build (upstream preset `gcc-14_2_build-release`)

```
cmake --preset gcc-14_2_build-release        # configure OK
cmake --build out/build/gcc-14_2_build-release
```

Result: **compile + link green (411/411 steps, 0 errors)**. Artifacts
`MonstaTek_M1_v0800.elf/.bin/.hex` generated; memory: RAM 49.37 %, FLASH 68.52 %
(717,752 B used), `FW_CONFIG_RESERVED` 20 B. Post-build symbol guards
`verify_mfc_raw_hwpass` and `verify_no_forbidden_symbols` **PASS**;
`verify_mfc_arch` fails as-shipped on this clone — see §6 (CRLF artifact,
passes with LF working files).

**Build exit code is 1 due to the Windows emitter issue** (§3): the final
POST_BUILD step `srec_cat ... -STM32_Little_Endian 0x000FFC14 ...` cannot run.

**Reproducibility:** two independent clean full rebuilds of the same preset
config produce a **byte-identical** `MonstaTek_M1_v0800.bin`
(sha256 `ef454925e6332c17f1f3626058c1f329527ffb64d3cd622b2b8df665197d060d`).
The only in-tree `__DATE__/__TIME__` use (`NFC/NFC_drv/legacy/nfc_poller.c:2908`)
does not perturb the release binary (proven empirically by the identical hashes).

## 3. srecord CRC pipeline + the Windows emitter issue

Upstream emission (CMakeLists.txt:127-134):
`srec_cat $BIN -binary -crop 0x0 0x000FFC14 -STM32_Little_Endian 0x000FFC14 -o ${STEM}_wCRC.bin -binary`.

**Windows emitter issue (evidence):** `srec_cat` is not installed on this host,
and POST_BUILD custom commands run through `cmd.exe`:

```
Generating bin file with CRC MonstaTek_M1_v0800_wCRC.bin
'srec_cat' is not recognized as an internal or external command,
operable program or batch file.
ninja: build stopped: subcommand failed.
```

This is the ONLY failing step of both build paths. No system packages may be
installed for the baseline, so the flow was **emulated** (script preserved in
the PR-2 working notes; algorithm below) and verified against independent
reference vectors:

- Algorithm: STM32 hardware CRC = CRC-32/MPEG-2 — poly `0x04C11DB7`, init
  `0xFFFFFFFF`, no reflection, no final xor, fed 32-bit **little-endian** words;
  result appended little-endian at `0x000FFC14`.
- Self-test vectors: `b"\x00\x00\x00\x00"` → `0xC704DD7B`,
  `b"\x04\x03\x02\x01"` → `0x793737CD` (cross-checked against the same layout
  the device rollback gate consumes, fork `tools/append_crc32.py`).
- The linker places `.FW_CONFIG_SECTION` at `0x080FFC00` (size 0x14), so every
  correct `.bin` ends exactly at `0xFFC14` — the crop covers the whole image;
  `_wCRC.bin` = image + 4-byte LE CRC (size `0xFFC18`).

**Reference CRC vectors (virgin pin build):**

| Artifact | Value |
|---|---|
| `MonstaTek_M1_v0800.bin` size | 1,047,572 (`0xFFC14`) |
| `MonstaTek_M1_v0800.bin` sha256 | `ef454925e6332c17f1f3626058c1f329527ffb64d3cd622b2b8df665197d060d` |
| STM32 CRC-32 over crop `[0x0, 0xFFC14)` | **`0x8B9D76E2`** |
| emulated `MonstaTek_M1_v0800_wCRC.bin` sha256 (size `0xFFC18`) | `2597e27c093109d4eb7b30d7389353d237084ac05fdb1c26b296fd8a595884c6` |
| repeat emission | byte-identical (deterministic on identical input) |

For comparison, the `build.sh` configure path (embeds `-DM1_GIT_COMMIT/-DM1_GIT_DIRTY`,
which the preset path does not) yields a different, equally deterministic image:
bin sha256 `25c9830bc8a99ad1b3d6813396f08b8eb3e3cc6350bb36f2021f1e6f4db1d708`,
CRC `0xDA120317`. Full `--release` determinism (SOURCE_DATE_EPOCH + embedded
commit literal check) is a `build.sh` feature that cannot complete on this
host for the same srec_cat reason.

## 4. Bash / Make path (git-bash)

- **`sh build.sh`**: submodules no-op → CMake configure (Ninja, Release,
  `M1_MFC_RAW_EMULATION=ON M1_MFC_DMA_AT=ON`, git metadata embedded) → full
  compile+link **green** → artifacts copied → **fails at the same srec_cat
  emission step** (`rc=1`, same `'srec_cat' is not recognized` error). Everything
  except the CRC emission runs.
- **`make`**: `make: command not found` (not installed in git-bash). Even with
  make, `check_env` would fail: it requires both `arm-none-eabi-gcc` (present)
  and `srec_cat` (absent). Bounded attempts only — no system package installs.

## 5. Upstream host tests (UNTOUCHED) — per-suite results

All suites run from the repository root on the virgin tree. Three Windows-host
adaptations were required, none touching any test file:

1. `CC` env → documented shim `gcc-nosan.exe`: the only host gcc (WinLibs
   15.2.0) **lacks the ASan/UBSan runtimes** that upstream harnesses hardcode
   (`ld: cannot find -lasan`, `-lubsan`), so the shim strips
   `-fsanitize=address,undefined` and demotes exactly three gcc-15-only
   `-Werror=` classes on **test-generated stub code** (`sign-compare`,
   `misleading-indentation`, `pointer-to-int-cast` — the last is a 64-bit-host
   vs 32-bit-target delta on `(uint32_t)&REG` casts). Runs are therefore
   **uninstrumented**: all assertions execute, but ASan/UBSan memory-safety
   coverage is absent on this host.
2. `cc.exe` (same shim) prepended to PATH for the three harnesses that
   hardcode `cc` (no env override exists).
3. `PYTHONUTF8=1` — some harnesses call `Path.read_text()` without encoding;
   Windows default cp1252 cannot decode UTF-8 sources
   (`UnicodeDecodeError ... byte 0x90`).

| Suite | Result | Notes |
|---|---|---|
| `m1_csrc/test/log_usb_nonblocking_test.py` | **PASS** | "Producer no-wait structural checks PASS" |
| `m1_csrc/test/m1_ble_signal_test.py` | **PASS** | BLE parser/RSSI/loss/recovery |
| `m1_csrc/test/m1_file_browser_navigation_test.py` | **PASS** | |
| `m1_csrc/test/m1_gatt_hex_editor_test.py` | **PASS** | |
| `m1_csrc/test/m1_gatt_reissue_test.py` | **PASS** | |
| `m1_csrc/test/m1_pass1_memory_safety_test.py` | **PASS** | uninstrumented (shim) |
| `m1_csrc/test/m1_pass3_helpers_test.py` | **PASS** | |
| `m1_csrc/test/m1_pass4_api_test.py` | **PARTIAL (4/5 subtests)** | filesystem / save_bounds / selection / sd_lifecycle PASS; `uart()` subtest is **revision-pinned** to `a41fdeaaa807fca12f96607b95125e8bcb3a7a1f` via `git show` — that object **does not exist in any public clone** (`git cat-file: bad object`), so the subtest aborts before running. See §7. |
| `m1_csrc/test/m1_pass5_reliability_test.py` | **PASS** | needs the shim's sign-compare demotion; verified the only diagnostic is on the test's own generated assertion (`dma_cleanup.c:52`) — with `-Wno-error=sign-compare` it compiles and all subtests pass |
| `m1_csrc/test/m1_wifi_persistence_hardening_test.py` | **PASS** | |
| `m1_csrc/test/mfc_start_rendezvous_test.py` | **PASS** | |
| `NFC/.../run_mfc_suite.sh` (`CC`=shim) | **12/13 ok; 1 env-artifact** | ce_mfc 37/0, crypto1_recover 47200/0, mfc_session 37/0, rawown_lifecycle 91/0, raw_hw_logic 34/0, mfc_resolver 7/0, emu_image 28/0, harvest 66/0, key_source ok, dict_phase ok, persona_routing 57/0 — all green. `ce_dev_type_test` 24 pass / 3 fail → **CRLF artifact**, proven below 27/27 on an LF tree. |
| `lfrfid/test/result_render_test.py` | **FAIL — genuinely broken at the pin** | Extracted `lfrfid_read_message()` (from `m1_csrc/m1_rfid.c`) calls `m1_lcd_wake_restart_timer()`; the generated test provides **no declaration and no stub** → `implicit declaration` error (gcc≥14/clang≥16) and, decisively, `undefined reference to m1_lcd_wake_restart_timer` **at link on any toolchain**. Upstream added the wake call without updating this test's stub set. Not fixable without touching the test (forbidden in Phase 2). |
| `lfrfid/test/extract_wave_builders.py` | **PASS** | |
| `cmake/m1_01/verify_mfc_raw_hwpass.sh` | **PASS** | "all 9 canonical MFC raw-emulation symbols present" |
| `cmake/m1_01/verify_no_forbidden_symbols.sh` | **PASS** | |
| `cmake/m1_01/verify_mfc_arch.sh` | **PASS on LF tree** (fails as checked out) | see §6 |

## 6. CRLF working-tree artifact (root cause of both env-dependent failures)

This clone has `core.autocrlf=true` and the upstream tree ships **no
`.gitattributes`**, so every text file is checked out with CRLF
(`git ls-files --eol` → `i/lf w/crlf attr/unspecified`). Two consumers break on
CRLF through no fault of their logic:

- `ce_dev_type_test.c` `read_file()` opens **"rb"** and matches multi-line
  needles containing `\n` → the 3 failing pins (all multi-line; the 24 passing
  ones are single-line).
- `verify_mfc_arch.sh` reads `mfc_authoritative_manifest.txt` with
  `while read -r kind arg` → every `arg` carries a trailing `\r`, so
  `REQUIRED_SYM name\r$` can never match.

**Proof both are pure EOL artifacts** (each verified by temporarily converting
the working file to LF, re-running, restoring — repo left untouched):

- `ce_dev_type_test`: **27 passed, 0 failed** on the LF file.
- `verify_mfc_arch.sh`: `OK -- single authoritative MFC implementation
  enforced (manifest clean).` on the LF manifest.

This is direct motivation for re-landing the fork `.gitattributes` + renormalize
(Phase 4a): with an EOL policy in place, these suites become platform-robust.

## 7. Structure/revision-pinned tests — review (not waived)

- `m1_pass4_api_test.py` pins, by extracting production functions into host
  builds: FatFs-API semantics of `m1_file_util.c`/`m1_file_browser.c`
  (`fs_path_kind`, open/read/write variants across all FR_* results), saved-file
  bounds, menu selection, SD lifecycle state machine in `m1_sdcard_man.c`, and a
  **differential pin** of `m1_esp_uart_transport.c` against `m1_wifi.c` at
  revision `a41fdeaa…`. On the virgin baseline as-is: the four non-pinned
  subtests pass; the differential subtest cannot start because the pinned
  object is unreachable (`git cat-file -t a41fdeaa…` → `bad object`) — in any
  public clone, not just this one. Recorded, not waived.
- `m1_pass1_memory_safety_test.py` accepts `--ref HEAD` for before/after
  differential runs (optional; not exercised — no code change to compare).

## 8. How to reproduce

```sh
git fetch upstream && git rev-parse upstream/main        # expect 2af1f8a339db...
git switch -c chore/merge1-upstream-base 2af1f8a
cmake --preset gcc-14_2_build-release                    # note: stale 14.2 path; actual gcc 15.2.Rel1 from PATH
cmake --build out/build/gcc-14_2_build-release           # green until 'srec_cat' POST_BUILD step
sh build.sh                                              # green until the same srec_cat step
# host suites (git-bash): see §5 adaptations, e.g.
PYTHONUTF8=1 CC=<gcc-nosan-shim> python m1_csrc/test/m1_pass3_helpers_test.py
PYTHONUTF8=1 CC=<gcc-nosan-shim> python lfrfid/test/extract_wave_builders.py
CC=<gcc-nosan-shim> sh NFC/NFC_drv/common/test/run_mfc_suite.sh
sh cmake/m1_01/verify_mfc_arch.sh <arm-none-eabi-nm> <elf> <src-root>
```

The `gcc-nosan` shim is a Windows-only run adapter (built once with the host
gcc); it is evidence tooling, not a repo artifact.

---

## 9. Phase 5 — new-base verification results (post re-land)

Measured on the final branch tip (post 4a/4b/4c/4d), same host, same run
adaptations as §5 (`CC`=gcc-nosan shim, PATH `cc`, `PYTHONUTF8=1`).

### 9.1 Windows build (preset `gcc-14_2_build-release`)

Configure: `-- Project name: M1_v0.9.0.0-UFO (version single-sourced from
m1_csrc/m1_fw_update_bl.h)`, compiler identification GNU 15.2.1. Compile +
link green (all steps; upstream's pre-existing warnings unchanged — warning
hygiene is the PR-4 unit). Artifacts: `M1_v0.9.0.0-UFO.elf/.bin/.hex/.list/.map`.
The `srec_cat` POST_BUILD step still fails (srecord absent — the §3 Windows
emitter issue, unchanged); it remains the only failing step.

**Reproducibility:** two independent clean full rebuilds → **byte-identical**
`M1_v0.9.0.0-UFO.bin` (sha256 `f077f06e94f70adaa35bcb126d817238fb23af42566abfd84a10f63924c266e2`).

### 9.2 Bash path

`sh build.sh` (fresh `build/`): derives `ARTIFACT_STEM=M1_v0.9.0.0-UFO` from
the header, configures, compiles, links — `build/M1_v0.9.0.0-UFO.*` produced —
and aborts (set -e) at the same in-`cmake --build` srec_cat emission step
(rc=1, unchanged). Note (erratum vs §4's wording): the failure fires inside
`cmake --build`, i.e. **before** build.sh's artifact-copy loop; the artifacts
nevertheless exist in `build/` from POST_BUILD. `make` remains unavailable in
git-bash; its `check_env` still requires `srec_cat`.

### 9.3 Host tests

| Suite | Result |
|---|---|
| `m1_csrc/test/*.py` (11) | 10 **PASS**; `m1_pass4_api_test.py` PARTIAL as in §5/§7 — 4/5 subtests PASS, `uart()` blocked by the unreachable `a41fdeaa…` pin (unchanged, expected) |
| `tools/test_fw_version_compare.py` (4c port) | **PASS** — all 6 checks (0.9.0.0 > 0.8.11 ≡ 0.8.11.0 > 0.8.1.0; `-UFO` display-only; packing vectors) |
| `NFC/.../run_mfc_suite.sh` | **ALL PASS** (rc=0) — including `ce_dev_type_test` now 27/27 **as checked out**: the 4a `.gitattributes` (`* text=auto eol=lf`) gives an LF working tree on this clone, eliminating the §6 CRLF artifact |
| `lfrfid/test/extract_wave_builders.py` | **PASS** |
| `lfrfid/test/result_render_test.py` | FAIL — unchanged; genuinely broken at the pin (§5: missing `m1_lcd_wake_restart_timer` stub in generated test → undefined reference on any toolchain). Not touched in PR-2; candidate for a follow-up upstream/test fix, out of this unit's scope. |
| `cmake/m1_01/verify_mfc_raw_hwpass.sh` | **PASS** (9/9 symbols, new ELF) |
| `cmake/m1_01/verify_mfc_arch.sh` | **PASS as checked out** (no LF conversion needed anymore) |
| `cmake/m1_01/verify_no_forbidden_symbols.sh` | **PASS** |

### 9.4 CRC pipeline vs Phase-2 vectors

Same emulated srec_cat flow (§3), same crop semantics (bin size 1,047,572 =
`0xFFC14`, CRC appended at `0xFFC14` → `0xFFC18`-byte image):

| Artifact | Value |
|---|---|
| `M1_v0.9.0.0-UFO.bin` sha256 | `f077f06e94f70adaa35bcb126d817238fb23af42566abfd84a10f63924c266e2` |
| STM32 CRC-32 over crop | **`0x9F14BA11`** |
| emulated `_wCRC.bin` sha256 | `1beb1ec53a489ce84c45409bcc62016d07ed0795cb0054a96fc8c2cfeb6fe814` |
| repeat emission | byte-identical |

The value differs from §3 by design (new version macros + renormalized
sources → different image bytes). Pipeline behavior — crop length, algorithm,
word packing, append offset, determinism — is unchanged and reproducible;
emission on this host still requires the documented Python emulation (srecord
absent).

### 9.5 Re-land invariants

- After 4a (and excluding branch-added files): `git diff --ignore-cr-at-eol
  2af1f8a HEAD` → **0 bytes**; full diffstat at that point = `.gitattributes`
  + this evidence doc only (both new files). Zero upstream-tracked files
  modified by EOL or anything else at that commit.
- `git ls-files --eol` after 4a: **0** `i/crlf` entries (was 459 + 17 mixed).
- Graft proofs (Phase 3): `git merge-base --is-ancestor 2af1f8a HEAD` exit 0;
  `git rev-parse legacy/v0.8.11` → `cf48ff01b4e7b9bfaba51b1308126cc52c1688af`
  before and after the graft (unmodified); `git diff c211a9b c20a3fe` empty
  (graft changed no tree content).
