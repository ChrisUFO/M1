<!-- See COPYING.txt for license details. -->

# Merge ledger — fork-changed file dispositions (issue #38)

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
| `carried-by-patch` | Content the new base lacks and worth keeping; names the P4 carry issue (#39–#44/#46/#52) and cites fork origin SHAs. |
| `upstream-equivalent` | The file differs, but upstream achieves the fork's intent differently/better on the new base; evidence cited on both sides. |
| `intentionally-dropped` | Not carried; written reason below against the maintainer's evidentiary bar. |

Hunk classification uses whitespace-ignored diffs throughout
(`git diff -w 93acd751 legacy/v0.8.11`) so EOL/restyle noise cannot hide real
deltas; per-file numstat and hunk headers above are `-w` values. The fork's
"Zero-Warning Core" restyle (811b346) embedded genuine warning fixes; every
genuine fix found is tagged `recovered-warning-fix` below — issue #39 (PR-4)
mines exactly this tag.

## Summary

| Metric | Count |
|---|---|
| Fork-changed files (hard invariant) | **113** |
| Both-changed (also touched upstream since fork point) | 83 |
| Ours-only (upstream untouched since fork point) | 30 |
| retained-by-upstream | 10 |
| carried-by-patch | 84 |
| upstream-equivalent | 4 |
| intentionally-dropped | 15 |
| unknown (must be 0 — gate) | 0 |
| Entries tagged `recovered-warning-fix` (feed for #39) | 34 |

*Regenerate deterministically with `python tools/gen_merge_ledger.py`; the DISPOSITIONS table in that script is the single source of truth. Gate: `bash cmake/m1_01/verify_merge_ledger.sh`.*

## Per-file dispositions

### `.github/ISSUE_TEMPLATE/usb_dfu.md`

- Status: A (ours-only; -w delta +79/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #41
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -0,0 +1,79 @@`

Fork's bug-report template for DFU recovery issues. Restored with the DFU port
(#41) — trivially re-landed then, when the feature it documents exists again on
the base (CHANGELOG `### Removed` lists it among the recoverable paths).


### `.gitignore`

- Status: M (both-changed; -w delta +28/-0; 2 hunks)
- Disposition: **retained-by-upstream**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), d10a6f9 refactor: remove FCC test placeholders and associated dead code (#28), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), 774eab8 temp file update, (+2 more)
- Hunk summary (-w): `@@ -42,6 +42,14 @@ logs/`; `@@ -49,6 +57,26 @@ Thumbs.db`

Fork-relevant hygiene (ignore build outputs/artifacts) is on the new base via the
PR-1 re-land: `main:.gitignore` covers `out/ build/ build_*/ artifacts/ CMakeFiles/`
(lines 66-70). The fork-only remainder (PLAN.md, `.claude/*`, `review_*.json`,
`*log.txt`, `*.diff`) is session-local tooling residue, intentionally not carried
(PR-1's merged set is the reviewed baseline; extend in a follow-up if the shared
tree needs it). Evidence: `git diff -w legacy/v0.8.11 main -- .gitignore` shows only
those personal patterns missing.


### `ARCHITECTURE.md`

- Status: M (ours-only; -w delta +368/-13; 1 hunks)
- Disposition: **intentionally-dropped**
- Fork origin: d99e2f8 release: v0.8.11 - DFU visual indicator (LED), e028e9d release: v0.8.10 - DFU stability hardening and safety net, d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), 31b5d13 docs: update architecture and README for v0.8.5 zero-warning release, (+3 more)
- Hunk summary (-w): `@@ -4,24 +4,379 @@`

Fork's 368-line architecture overview (feature-status matrix, protocol notes,
recommended implementation order) documents the LEGACY tree; re-landing it verbatim
would misdocument the new base (e.g. it describes m1_ir_universal and the CDC
rewrite that #41-#44 re-home differently). Its durable content is preserved: the
recommended implementation order table lives on in this ledger's Phase 3 verdicts,
and the feature status is tracked by issues #3/#4/#7/#9/#11. A fresh ARCHITECTURE.md
against the new base is follow-up work when the P4 carries land.


### `CHANGELOG.md`

- Status: M (both-changed; -w delta +176/-5; 1 hunks)
- Disposition: **intentionally-dropped**
- Fork origin: d99e2f8 release: v0.8.11 - DFU visual indicator (LED), e028e9d release: v0.8.10 - DFU stability hardening and safety net, d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 16402da fix(fw-update): fix empty filename issue when selecting fw via SD card (#33), 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), (+9 more)
- Hunk summary (-w): `@@ -5,12 +5,183 @@`

Fork delta = the fork's own v0.8.2-v0.8.11 release entries. The release history
is preserved verbatim in the immutable archive (`git show legacy/v0.8.11:CHANGELOG.md`)
and GitHub releases; the new base's CHANGELOG (PR-1/PR-2 `Unreleased` entries)
already records the fork lineage and points at the ledger for the merge-era story
(the `### Removed` section resolves via this file). Re-landing 176 lines of
superseded release notes would duplicate the archive.


### `CMakeFiles/4.0.2/CMakeSystem.cmake`

- Status: D (both-changed; -w delta +0/-15; 1 hunks)
- Disposition: **retained-by-upstream**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -1,15 +0,0 @@`

Tracked-by-upstream build artifact (existed at the fork point); the fork deleted it and
so did upstream later. Both trees clean: `git ls-tree upstream/main CMakeFiles/` is
empty and `git ls-files main -- CMakeFiles/` is empty. PR-1's `.gitignore` keeps
`CMakeFiles/` ignored (main:.gitignore:70). Nothing to carry.


### `CMakeFiles/4.0.2/VCTargetsPath.vcxproj`

- Status: D (both-changed; -w delta +0/-27; 1 hunks)
- Disposition: **retained-by-upstream**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -1,27 +0,0 @@`

Tracked-by-upstream build artifact (existed at the fork point); the fork deleted it and
so did upstream later. Both trees clean: `git ls-tree upstream/main CMakeFiles/` is
empty and `git ls-files main -- CMakeFiles/` is empty. PR-1's `.gitignore` keeps
`CMakeFiles/` ignored (main:.gitignore:70). Nothing to carry.


### `CMakeFiles/CMakeConfigureLog.yaml`

- Status: D (both-changed; -w delta +0/-24; 1 hunks)
- Disposition: **retained-by-upstream**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -1,24 +0,0 @@`

Tracked-by-upstream build artifact (existed at the fork point); the fork deleted it and
so did upstream later. Both trees clean: `git ls-tree upstream/main CMakeFiles/` is
empty and `git ls-files main -- CMakeFiles/` is empty. PR-1's `.gitignore` keeps
`CMakeFiles/` ignored (main:.gitignore:70). Nothing to carry.


### `CMakeFiles/cmake.check_cache`

- Status: D (both-changed; -w delta +0/-1; 1 hunks)
- Disposition: **retained-by-upstream**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -1 +0,0 @@`

Tracked-by-upstream build artifact (existed at the fork point); the fork deleted it and
so did upstream later. Both trees clean: `git ls-tree upstream/main CMakeFiles/` is
empty and `git ls-files main -- CMakeFiles/` is empty. PR-1's `.gitignore` keeps
`CMakeFiles/` ignored (main:.gitignore:70). Nothing to carry.


### `CMakeLists.txt`

- Status: M (both-changed; -w delta +67/-23; 5 hunks)
- Disposition: **retained-by-upstream**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), 509c68d feat: add and harden USB DFU mode workflow (#17), 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15), (+1 more)
- Hunk summary (-w): `@@ -20,20 +20,31 @@ if(NOT CMAKE_BUILD_TYPE)`; `@@ -49,7 +60,7 @@ target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE`; `@@ -64,8 +75,10 @@ target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE`; *+2 more hunks*

The fork-relevant content — version-derived project name — is on the new base in
stronger form via the PR-2 re-land: `main:CMakeLists.txt:27-41` single-sources
`M1_v{MAJOR}.{MINOR}.{BUILD}.{RC}-{FORK_TAG}` from `m1_csrc/m1_fw_update_bl.h`
(the fork's own derivation dropped the RC field). Fork post-build CRC steps
(append_crc32.py invocations) are NOT on main — carried under #40 with the tooling
(see `tools/append_crc32.py`); noted here so the hunk is not lost.


### `CMakePresets.json`

- Status: M (ours-only; -w delta +2/-2; 1 hunks)
- Disposition: **retained-by-upstream**
- Fork origin: ad3b8c6 feat: add Universal Remote feature with Flipper IRDB support
- Hunk summary (-w): `@@ -10,8 +10,8 @@`

Fork delta = toolchain path update to `Arm GNU Toolchain arm-none-eabi/14.2 rel1`;
already on the new base: `main:CMakePresets.json:13` carries the identical path.
`git diff -w legacy/v0.8.11 main -- CMakePresets.json` is whitespace-only apart from
binaryDir naming. Nothing to carry.


### `Core/Inc/main.h`

- Status: M (both-changed; -w delta +14/-11; 2 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #41
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -31,23 +31,24 @@ extern "C" {`; `@@ -231,10 +232,12 @@ void Error_Handler(void);`

Carried hunk: `IWDG_RELOAD 4000 -> 1250` (watchdog window 4 s -> 10 s, origin
d3a0208 — stability hardening #22/#26/#27): the boot-time CRC failsafe (#41) walks
both banks before the watchdog bites; with the 4 s window the failsafe itself can
trip IWDG. `main:Core/Inc/main.h:237` still has 4000. Include-order and
comment-rewrap hunks are restyle (not carried). #41's motivation already covers the
adjacent IWDG option-byte defect (main.c:194-212) — the port fixes both together.


### `Core/Src/FreeRTOS_CLI.c`

- Status: M (ours-only; -w delta +4/-2; 3 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #44
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -76,7 +76,8 @@ static const CLI_Command_Definition_t xHelpCommand =`; `@@ -159,7 +160,7 @@ BaseType_t FreeRTOS_CLIProcessCommand( const char * const pcCommandInput,`; `@@ -344,6 +345,7 @@ static BaseType_t prvHelpCommand( char * pcWriteBuffer,`

Fork extends the FreeRTOS CLI command struct with the `pxCommandHelper` field
(help registry consumed by cli_app/m1_cli_help) and initializes
`uint8_t num_of_params = 0;` in FreeRTOS_CLIProcessCommand (uninitialized-read
warning fix — tagged). File exists unchanged on main otherwise (ours-only class =
upstream untouched since fork point). #44 re-homes the helper extension and the
mtest ID space (ours: 9, 22-24; upstream's: 5/6/61/64/68/69/78/79 — collisions
guaranteed post-merge per #44).


### `Core/Src/app_freertos.c`

- Status: M (both-changed; -w delta +1/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -63,6 +63,7 @@ const osThreadAttr_t dummytask_attributes = {`

Fork adds `(void)xExpectedIdleTime;` in the weak vPortSuppressTicksAndSleep stub
(unused-parameter). Main's stub still lacks it (grep on main = 0). Class:
-Wunused-parameter. Fix hunk: one line at the function head.


### `Core/Src/cli_app.c`

- Status: M (both-changed; -w delta +559/-95; 9 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #44
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), 509c68d feat: add and harden USB DFU mode workflow (#17), 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -25,22 +25,30 @@ CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.`; `@@ -48,34 +56,117 @@ const char * cli_prompt = "\r\ncli> ";`; `@@ -83,37 +174,41 @@ const CLI_Command_Definition_t xCommandList[] = {`; *+6 more hunks*

Fork adds 11 CLI commands with help plumbing: log, status, version, sdcard, wifi,
battery, reboot, memory, cdcstats, cdcreset (+dfu -> #41). Upstream's cli_app on
main has cls/mtest only (#44 motivation: fork CLI additions upstream lacks).
#44 ports them individually with help text + regression tests, re-homing
`cdcstats` onto the re-implemented CDC_CLI_Rx* counters.


### `Core/Src/main.c`

- Status: M (both-changed; -w delta +224/-118; 34 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #41
- Fork origin: d99e2f8 release: v0.8.11 - DFU visual indicator (LED), e028e9d release: v0.8.10 - DFU stability hardening and safety net, d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -22,12 +22,14 @@`; `@@ -98,20 +100,116 @@ void assert_failed(uint8_t* file, uint32_t line);`; `@@ -120,8 +218,9 @@ int main(void)`; *+31 more hunks*

Carried hunks: DFU boot entry — read `TAMP->BKP1R` op-status before `MX_IWDG_Init()`,
skip normal IWDG init when USB DFU requested, program a 32 s IWDG safety window and
drive PD12/PD13 as DFU visual indicator (legacy main.c:228-262; origins 509c68d,
e028e9d, d99e2f8). All absent on main (`git grep -c "DEV_OP_STATUS_USB_DFU_REQUEST" main`
= 0). Port must resolve the BKP1R collision against upstream's bu_regs layout —
see Phase 4(b) evidence — and stop rewriting IWDG option bytes silently (#41 AC).


### `Core/Src/stm32h5xx_hal_msp.c`

- Status: M (both-changed; -w delta +4/-0; 2 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -511,8 +511,10 @@ void HAL_SPI_MspDeInit(SPI_HandleTypeDef* hspi)`; `@@ -548,7 +550,9 @@ void HAL_TIM_Base_MspInit(TIM_HandleTypeDef* htim_base)`

Fork `#if 0`-guards two unused local declarations in HAL_TIM_Base_MspInit /
HAL_TIM_MspPostInit (GPIO_InitTypeDef/NodeConfig + GPIO_InitStruct). Main has one
`#if 0` in the file but not these two guards (grep 'TODO: Enable TIM DMA' on
main = 0). Class: -Wunused-variable. Two small guard hunks.


### `Core/Src/stm32h5xx_it.c`

- Status: M (both-changed; -w delta +1/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -74,6 +74,7 @@ void NMI_Handler(void)`

Fork annotates the ECCDR read-to-clear in NMI_Handler with `(void)eccdr_value;`
+ rationale comment (the read is required for the hardware flag clear; the value is
deliberately unused). Main lacks it. Class: -Wunused-variable (value-used-for-side-
effect pattern). One hunk.


### `Core/Src/system_stm32h5xx.c`

- Status: M (both-changed; -w delta +79/-57; 12 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #41
- Fork origin: 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29)
- Hunk summary (-w): `@@ -21,9 +21,9 @@`; `@@ -110,6 +110,7 @@`; `@@ -143,7 +144,8 @@`; *+9 more hunks*

Carried hunk: `extern void boot_recovery_check(void);` + call as the first action
of `SystemInit()` (legacy system_stm32h5xx.c; origin 6e73998, boot-time CRC #29) —
runs the failsafe before C runtime/FreeRTOS start. Absent on main
(`git grep -c boot_recovery_check main` = 0; upstream has no pre-main hook).
Remainder of the fork delta is clang-format rewrap of ST's boilerplate (dropped).


### `DEVELOPMENT.md`

- Status: M (ours-only; -w delta +1/-0; 1 hunks)
- Disposition: **retained-by-upstream**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -34,3 +34,4 @@ Follow [Conventional Commits](https://www.conventionalcommits.org/):`

Fork delta vs fork point is a single line (an NFC/RFID/Sub-GHz verification bullet).
The new base carries the comprehensive PR-1/PR-2 `DEVELOPMENT.md` (tag policy,
renormalize workflow, merge policy) which subsumes it. Nothing to carry.


### `Esp_spi_at/examples/at_spi_master/spi/stm32/main/esp_app_main.c`

- Status: M (both-changed; -w delta +282/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #42
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -652,6 +652,288 @@ uint8_t wifi_ap_scan_list(ctrl_cmd_t *app_req)`

Carried hunk: `wifi_connect_ap()` — the ESP-side join handler (legacy
esp_app_main.c:655-728 per #42 motivation; AT+CWJAP with retry/tick budgeting,
origin 3b6c252). The CTRL_REQ_CONNECT_AP control API exists on both sides but the
handler is fork-only. #42 ships it versioned with the STM32 image. The rest of the
fork delta is clang-format (dropped).


### `Esp_spi_at/examples/at_spi_master/spi/stm32/main/esp_app_main.h`

- Status: M (ours-only; -w delta +4/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #42
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -14,11 +14,15 @@`

Fork adds the join/scan protos consumed by the ESP-side handler — carried with
esp_app_main.c under #42.


### `Esp_spi_at/examples/at_spi_master/spi/stm32/main/esp_at_list.h`

- Status: M (both-changed; -w delta +9/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #42
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -31,6 +31,15 @@`

Fork adds the AT command vocabulary for join: `AT+CWJAP=`, `AT+CWJAP?`,
`AT+CIPSTA?`, `AT+CWQAP` defines (+CWJAP/+CIPSTA result keys). Absent on main
(grep CWJAP on main = 0). Carried with the #42 join flow.


### `Infrared/irmp-irsnd/irmpconfig.h`

- Status: M (both-changed; -w delta +150/-75; 10 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #43
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), a55968b Infrared Universal Remote: Dashboard, Pagination, Search & Favorites (#18)
- Hunk summary (-w): `@@ -33,9 +33,10 @@`; `@@ -44,71 +45,131 @@`; `@@ -133,9 +194,11 @@`; *+7 more hunks*

Fork rebalanced the IRMP protocol enable set for the Flipper-IRDB vocabulary
(+150/-75 -w: enabled protocols the `.ir` corpus needs). Upstream's IRMP on main
already covers the full vocabulary incl. IRMP_NEC8/PIONEER/RC5X/SIRCS15/SIRCS20/RAW
(#43 motivation, verified 5/5). #43 re-derives the enable set during consolidation
instead of carrying the fork's stale balance.


### `NFC/NFC_drv/common/nfc_file.c`

- Status: M (both-changed; -w delta +1/-1; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #46
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -20,7 +20,7 @@`

One-line case fix `res_string.h` -> `Res_String.h`. Main:25 still lowercase —
resolves via the #46 rename.


### `NFC/NFC_drv/common/nfc_storage.c`

- Status: M (both-changed; -w delta +102/-111; 23 hunks)
- Disposition: **intentionally-dropped**
- Tags: recovered-warning-fix
- Fork origin: a55968b Infrared Universal Remote: Dashboard, Pagination, Search & Favorites (#18)
- Hunk summary (-w): `@@ -1,16 +1,16 @@`; `@@ -20,7 +20,6 @@ typedef struct {`; `@@ -28,9 +27,9 @@ typedef struct {`; *+20 more hunks*

Fork delta = clang-format pass (include sort + brace/indent reformat of
unchanged logic) PLUS two genuine pointer-sign casts the restyle swept in:
`parse_device_type((char *)data.buf, faminfo)` and
`platformLog(..., (char *)data.buf)` (legacy nfc_storage.c:275-276; the
fork point passes the raw `uint8_t data.buf` at :280-281). Both still
apply on the new base: main:NFC/NFC_drv/common/nfc_storage.c:519-520 has
the uncast calls — tagged `recovered-warning-fix` for #39 to re-apply.
The restyle residue itself is dropped: upstream evolved the file on the
new base (+846/-122 vs legacy) and that is the maintained shape.


### `NFC/NFC_drv/legacy/nfc_listener.c`

- Status: M (both-changed; -w delta +1/-1; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #46
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -59,7 +59,7 @@`

One-line case fix `rfal_AnalogConfig.h` -> `rfal_analogConfig.h` (on-disk:
NFC/Middlewares/ST/rfal/Inc/rfal_analogConfig.h). Main:63 still wrong-case —
#46 AC names this exact site. NOTE: upstream also rewrote the listener on main
(+652/-76 vs legacy); the fix lands against the new body.


### `NFC/NFC_drv/legacy/nfc_poller.c`

- Status: M (both-changed; -w delta +1/-1; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #46
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -51,7 +51,7 @@`

Same one-line `rfal_AnalogConfig.h` case fix at legacy nfc_poller.c:54;
main:54 still wrong-case. #46 AC names this site too.


### `README.md`

- Status: M (both-changed; -w delta +496/-9; 3 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #41 + #42 + #43 + #44
- Fork origin: d99e2f8 release: v0.8.11 - DFU visual indicator (LED), e028e9d release: v0.8.10 - DFU stability hardening and safety net, d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), 31b5d13 docs: update architecture and README for v0.8.5 zero-warning release, (+4 more)
- Hunk summary (-w): `@@ -1,6 +1,12 @@`; `@@ -11,9 +17,9 @@ The M1 firmware provides support for:`; `@@ -26,31 +32,512 @@ See [HARDWARE.md](HARDWARE.md) for more details.`

Fork rewrote the README around fork features: DFU flashing workflow, serial
console commands, Universal Remote usage + IR database provisioning, menu/feature
status matrix. Main's README is upstream's + the PR-2 "based on upstream 0.8.1.0"
note. The fork sections return with their features (#41 DFU docs, #43 IR docs,
#42/#44 console/command docs) rather than landing stale documentation for absent
features now.


### `ROADMAP.md`

- Status: A (ours-only; -w delta +353/-0; 1 hunks)
- Disposition: **intentionally-dropped**
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15), ad3b8c6 feat: add Universal Remote feature with Flipper IRDB support
- Hunk summary (-w): `@@ -0,0 +1,353 @@`

Fork's phased roadmap tied to the legacy tree and its v0.8.x release cadence.
Superseded by the issue tracker (P4 issues #40-#44, #46, #49 and milestone
planning). Note: open issues #3/#4/#7/#11 link to ROADMAP anchors — those links
404 on the new base; harmless and superseded by the ledger verdicts. Priority
ranking preserved in Phase 3 below.


### `Sub_Ghz/m1_sub_ghz_api.c`

- Status: M (both-changed; -w delta +174/-342; 48 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #52
- Fork origin: d10a6f9 refactor: remove FCC test placeholders and associated dead code (#28), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -12,30 +12,31 @@`; `@@ -45,7 +46,6 @@`; `@@ -62,48 +62,53 @@ static union si446x_cmd_reply_union si446x_cmd;`; *+45 more hunks*

Fork delta is NOT pure restyle (hunk-verified). Two genuine functional
hunks, both still applicable on main: (a) `SI446x_Wait_CTS()` bounds the
CTS busy-wait — `for (uint32_t timeout = 0xFFFF; timeout > 0 && ...; timeout--)`
replacing the unbounded `while (HAL_GPIO_ReadPin(...))` (legacy :179-182;
main:Sub_Ghz/m1_sub_ghz_api.c:164-170 still unbounded); (b) an `assert(0)`
unreachable-guard in `SI446x_Change_Radio_Setting()`'s `default:` case
(legacy :791; main:...:860 has a bare `break`). Both hang-proof the radio
SPI path — carried under #52 (port stability hardening carry). Dropped
hunks: the FCC-test-helper removal family (origin d10a6f9 #28) —
`SI446x_Start_Tx_CW`, `SI446x_GPIO_Config`, the `Radio_Test_Configuration_
Data_Array`/`RadioConfiguration_Test` defines and the
`m1_sub_ghz_xxx_tx_test.h` include; main retains them all (main api.c:138,
:148, :673) and the same dead-code-cleanup-not-worth-churn reasoning as
m1_lib.c applies. Non-finding recorded for fidelity: the `(uint8_t *)
Radio_IRcal_Sequence_*` and `(uint16_t)si446x_cmd_buffer[6/7]` cast lines
are inherited from the fork point (93acd751:667-668/:404-405), rewrapped
only — NOT fork additions, hence no `recovered-warning-fix` tag here. The
RadioConfigList pointer table itself keeps the same 9 band entries on both
sides (reformat only; the adjacent Test-config defines are the dropped
d10a6f9 hunks above).


### `Sub_Ghz/m1_sub_ghz_api.h`

- Status: M (both-changed; -w delta +18/-12; 6 hunks)
- Disposition: **intentionally-dropped**
- Fork origin: d10a6f9 refactor: remove FCC test placeholders and associated dead code (#28)
- Hunk summary (-w): `@@ -13,12 +13,15 @@`; `@@ -39,7 +42,8 @@`; `@@ -57,8 +61,7 @@`; *+3 more hunks*

Companion header: clang-format rewrap, added `#include <stdbool.h>` /
`<stdint.h>`, and the `SI446x_Start_Tx_CW` proto commented out with a v0.8.6
hygiene note. Upstream keeps the proto (main:Sub_Ghz/m1_sub_ghz_api.h). The
.c's two carried hunks (bounded Wait_CTS, assert(0) guard under #52) change
no signature — the header needs nothing. Not carried.


### `Sub_Ghz/protocols/m1_secplus_v2_decode.c`

- Status: M (ours-only; -w delta +1/-1; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -61,7 +61,7 @@ uint8_t m1_secplus_v2_decode_half(uint64_t in_bits, uint8_t *half_code, uint32_t`

Fork initializes `uint64_t code = 0;` in subghz_decode_security_plus_20
(set-only-on-match previously read on the not-found path). Main:64 still
uninitialized. Class: -Wmaybe-uninitialized — a genuine correctness fix for the
decode-failure path, not just hygiene. Phase 4c: upstream carries the module
unchanged since fork point.


### `Sub_Ghz/radio_config/si4463_revc2_patch.h`

- Status: M (ours-only; -w delta +1/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -42,6 +42,7 @@`

Fork appends `0, 0, 0, 0, {0}` to RADIO_PATCH_CONFIGURATION_DATA — the
si4463_patch struct gained fields upstream of the fork point and this initializer
left them unspecified (-Wmissing-field-initializers; also zeroes the trailing
array). Main:43-45 still has the short initializer. Class:
missing-field-initializers.


### `USB/Class/CDC/Inc/usbd_cdc_if.h`

- Status: M (both-changed; -w delta +4/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #44
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -35,6 +35,10 @@ extern USBD_CDC_ItfTypeDef  USBD_CDC_Interface_fops;`

Fork adds the CDC_CLI_Rx* counter API protos — carried with usbd_cdc_if.c
under #44.


### `USB/Class/CDC/Src/usbd_cdc_if.c`

- Status: M (both-changed; -w delta +91/-75; 16 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #44
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -17,17 +17,17 @@`; `@@ -40,18 +40,18 @@`; `@@ -71,7 +71,6 @@ uint8_t UserTxBufferFS[USB_FS_CHUNK_SIZE];`; *+13 more hunks*

Carried hunks: `usb_cli_rx_dropped_bytes` / `usb_cli_rx_high_watermark` counters
(CDC_CLI_RxDroppedBytes/HighWatermark accessors) — overflow observability for the
CLI rx path (origin d3a0208 #27). #44 AC re-implements the three CDC_CLI_Rx*
counters on upstream's stream-buffer CDC path on main (not the fork's rewrite).


### `build`

- Status: A (ours-only; -w delta +136/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #40
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 5363ad0 fix: force cmake reconfigure on version change in build script, 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15), ad3b8c6 feat: add Universal Remote feature with Flipper IRDB support
- Hunk summary (-w): `@@ -0,0 +1,136 @@`

Fork's bash build wrapper (136 lines, origin 97e80ce + 5363ad0 force-reconfigure):
toolchain discovery, clean/rebuild, post-build steps. The new base ships upstream's
`build.sh`; #40 re-derives the version-stamped artifact pipeline (incl. the Windows
CRC emitter) onto it rather than carrying the fork's parallel entry point.


### `cmake/driver_warnings.cmake`

- Status: A (ours-only; -w delta +15/-0; 1 hunks)
- Disposition: **intentionally-dropped**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -0,0 +1,15 @@`

Fork's vendor-code warning-relaxation include file, consumed by the dropped
m1_drivers/m1_core restructuring (see cmake/m1_01/CMakeLists.txt entry). The new
base achieves scoped warnings via #39: source-file properties with
TARGET_DIRECTORY on app sources only, vendor trees untouched — no separate
relaxation file needed.


### `cmake/gcc-arm-none-eabi.cmake`

- Status: M (ours-only; -w delta +7/-11; 1 hunks)
- Disposition: **intentionally-dropped**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -38,17 +38,13 @@ if(CMAKE_BUILD_TYPE MATCHES Release)`

Fork delta = removal of `-x assembler-with-cpp` from ASM flags and link-flag
simplifications tied to the dropped build restructuring. The upstream toolchain
file on the new base builds green on both documented paths (Windows preset +
bash/Make — PR-2 virgin-baseline evidence, documentation/merge1-baseline-evidence.md).
Blindly carrying flag removals against upstream's evolved file risks breaking the
verified baseline for zero measured gain.


### `cmake/m1_01/CMakeLists.txt`

- Status: M (both-changed; -w delta +56/-28; 6 hunks)
- Disposition: **intentionally-dropped**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15), ad3b8c6 feat: add Universal Remote feature with Flipper IRDB support
- Hunk summary (-w): `@@ -3,19 +3,19 @@`; `@@ -65,20 +65,8 @@ target_include_directories(m1_01 INTERFACE`; `@@ -306,11 +294,6 @@ target_sources(m1_01 INTERFACE`; *+3 more hunks*

Fork delta = build restructuring: INTERFACE `m1_01` split into STATIC `m1_drivers`
(+`m1_core`) with relaxed vendor warnings (`cmake/driver_warnings.cmake`) and ESP32
binary-resource generation. Superseded: upstream's `m1_01` INTERFACE layout is the
maintained base (PR-4/#39 scopes -Werror via source-file properties + TARGET_DIRECTORY
instead of lib separation), and the ESP32 asset generation returns with the #41/#42
ESP-versioned carries. Carrying the restructure would churn every future upstream
merge of this file for no behavioral gain.


### `documentation/stability_phase5_validation.md`

- Status: A (ours-only; -w delta +97/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #52
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -0,0 +1,97 @@`

Fork's validation record for the d3a0208 stability hardening (97 lines): test
matrix and results for #22/#26/#27. Evidence companion of the stability carry —
re-landed (re-run) when the #52 carry lands on the new base.


### `lfrfid/lfrfid.c`

- Status: M (both-changed; -w delta +8/-0; 4 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -201,17 +201,21 @@ void lfrfid_Init(void)`; `@@ -219,11 +223,13 @@ void lfrfid_Init(void)`; `@@ -390,6 +396,7 @@ void bytes_to_u32_array(BitOrder order, const uint8_t in_data[], uint32_t out_da`; *+1 more hunks*

Fork adds `(void)ret;`/`(void)free_heap;` (xTaskCreate results checked via
assert — release builds compile asserts out) and two `(void)param;` in task
entries. Main lacks them (grep on main = 0 for this pattern in lfrfid.c). Class:
-Wunused-variable/set-but-unused. Three hunks.


### `lfrfid/lfrfid_file.c`

- Status: M (both-changed; -w delta +1/-1; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #46
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -18,7 +18,7 @@`

One-line case fix `res_string.h` -> `Res_String.h` (legacy lfrfid_file.c:21).
Main:23 still lowercase — resolves via the #46 rename.


### `lfrfid/lfrfid_protocol_em4100.c`

- Status: M (both-changed; -w delta +25/-17; 19 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #46 + #39
- Tags: recovered-warning-fix
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -15,7 +15,7 @@`; `@@ -77,21 +77,21 @@ static EM4100_Decoder_t g_em4100_16_dec;`; `@@ -372,10 +372,12 @@ static void bit_stream_push(EM4100_Decoder_t* dec, uint8_t bit)`; *+16 more hunks*

Mixed entry. #46 hunk: include case `uiview.h` -> `uiView.h` (on-disk m1_csrc/
uiView.h). #39-tagged hunks: decoder signature type fixes (`uint16_t size` ->
`int size` matching the protocol vtable, + `(uint8_t)size` narrowing at the
manchester feed), `const` fixes (GetBitFromFrame/em4100_is_valid frames),
`(void)proto;` x3, `#if 0` around unused bit_stream_get. Classes: sign-compare,
pointer-sign, unused-parameter/function. Main:33 still has wrong-case include; the
type fixes absent (main signatures still uint16_t). Both carries re-check
applicability against upstream's lfrfid evolution.


### `lfrfid/lfrfid_protocol_h10301.c`

- Status: M (both-changed; -w delta +14/-11; 13 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #46 + #39
- Tags: recovered-warning-fix
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -13,7 +13,7 @@`; `@@ -21,9 +21,7 @@`; `@@ -71,10 +69,10 @@ static fsk_bit_state_t    bit_st;`; *+10 more hunks*

Same pattern as em4100: `uiview.h`->`uiView.h` case fix (#46; main:32 wrong-case)
plus `uint16_t`->`int` decoder signatures, `(void)proto;` (#39-tagged). Classes:
sign-compare, unused-parameter.


### `lfrfid/t5577.c`

- Status: M (both-changed; -w delta +1/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -256,6 +256,7 @@ static void t5577_write_block(uint8_t block, bool lock_bit, uint32_t data)`

Fork adds `(void)block;` in t5577_execute_write (parameter intentionally unused —
the API takes block for future use). Main lacks it. Class: -Wunused-parameter.


### `m1_csrc/Res_String.c`

- Status: M (both-changed; -w delta +1/-1; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #46
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -4,7 +4,7 @@`

Fork fixes the self-include case: `res_string.h` -> `Res_String.h` (on-disk file
is Res_String.h). Main still has the wrong case (main:m1_csrc/Res_String.c:7);
#46 renames the file pair to `res_string.{c,h}` so all 9 include sites resolve
untouched — the rename subsumes this fix.


### `m1_csrc/logger.c`

- Status: M (both-changed; -w delta +3/-0; 2 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -90,6 +90,7 @@ uint8_t logUsartTx(uint8_t *data, uint16_t dataLen);`; `@@ -118,6 +119,8 @@ void logUsartInit(UART_HandleTypeDef *husart)`

Fork adds `(void)husart;`, `(void)data;`, `(void)dataLen;` in the stubbed
logUsart functions (bodies are commented-out placeholders). Main lacks all three.
Class: -Wunused-parameter. Two hunks.


### `m1_csrc/m1_bq27421.c`

- Status: M (both-changed; -w delta +24/-6; 15 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -57,16 +57,24 @@ static bool bq27421_itporFlag(void);`; `@@ -305,8 +313,8 @@ bool bq27421_i2c_read_data_block( uint8_t offset, uint8_t *data, uint8_t bytes )`; `@@ -1087,7 +1095,7 @@ bool bq27421_applyConfigIfMatches(uint16_t designCapacity_mAh,`; *+12 more hunks*

MUST-ADJUDICATE (see also the dedicated Phase 2 subsection): the fork's entire -w
delta is warning hygiene — (a) `#if 0` guards around the four unused static helpers
(enterConfig/exitConfig/readBlockData/readExtendedData) killing -Wunused-function;
(b) initializer fixes `uint16_t flags/status/csum = 0;` and the designEnergy/taper/
checksum block in bq27421_init() killing -Wmaybe-uninitialized; (c) `(void)oldCsum;`
+ split declaration. Upstream on main resolved the SAME unused-function set by
deleting the functions outright and now *uses* readBlockData
(main:m1_bq27421.c:1435,1576); its `bq27421_flags` still has the uninitialized
`uint16_t flags;` (main:1132) so the (b) class still applies. Boot-hang
adjudication: upstream's `m1_bq27421_boot_guard.{c,h}`
(main:m1_csrc/m1_bq27421_boot_guard.h:12-23) fixes the ITPOR `while(timeout--)`
wraparound + unbounded post-update `while(1)` — defects the fork NEVER fixed (fork
kept the inline loops from the fork point). On the new base upstream's boot guard
wins; the fork loses nothing (it had no equivalent). Carry = the (b)-class
initializer fixes only, tagged here for #39 to re-apply where upstream's code
still needs them.


### `m1_csrc/m1_bt.c`

- Status: M (both-changed; -w delta +6/-1; 1 hunks)
- Disposition: **upstream-equivalent**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -483,8 +483,13 @@ static uint16_t ble_scan_list_print(ctrl_cmd_t *app_resp, bool up_dir)`

Fork delta vs fork point is 6 lines: BSSID hex-formatting in the BLE scan list
(legacy m1_bt.c:483-493). Upstream rewrote the whole BT subsystem on the new base
(2474 lines: `bluetooth_scan_devices`/`bluetooth_signal_meter`/
`bluetooth_gatt_explorer`/`bluetooth_saved`, main:m1_bt.c:132-137) with native
BSSID-string handling (`char bssid[BSSID_STR_SIZE]`, main:m1_bt.c:86,99,118) —
the fork's formatting intent is achieved better upstream. `bluetooth_config()` (#11
stub) exists nowhere on main — verdict in Phase 3.


### `m1_csrc/m1_buzzer.c`

- Status: M (both-changed; -w delta +1/-0; 1 hunks)
- Disposition: **retained-by-upstream**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -149,6 +149,7 @@ void buzzer_sys_init(uint16_t frequency)`

The fork's entire -w delta is one line — `(void)xTimer;` in `buzzer_sys_deinit()`
(unused-parameter warning fix). Upstream made the same fix independently:
`main:m1_csrc/m1_buzzer.c:191`. Nothing to carry.


### `m1_csrc/m1_buzzer.h`

- Status: M (both-changed; -w delta +13/-5; 3 hunks)
- Disposition: **intentionally-dropped**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -13,12 +13,16 @@`; `@@ -29,9 +33,11 @@`; `@@ -43,7 +49,9 @@`

Fork delta = clang-format rewrap of macro definitions plus one added
`#include "m1_compile_cfg.h"`. The compile switch it serves already reaches the
build on the new base: `main:m1_csrc/m1_compile_cfg.h:24-27` defines
M1_APP_BUZZER_USE_TIMER8 (guarded) and the base builds green without the header
change (PR-2 baseline evidence, documentation/merge1-baseline-evidence.md).
Restyle residue; not carried.


### `m1_csrc/m1_cli.c`

- Status: M (both-changed; -w delta +278/-258; 44 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #44
- Fork origin: 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), d10a6f9 refactor: remove FCC test placeholders and associated dead code (#28), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), 509c68d feat: add and harden USB DFU mode workflow (#17)
- Hunk summary (-w): `@@ -11,37 +11,39 @@`; `@@ -58,8 +60,7 @@`; `@@ -68,39 +69,54 @@ const osThreadAttr_t cmdLineTask_attributes = {`; *+41 more hunks*

Fork adds the mtest subcommand suite (basic_system/sdcard/led/lcd/infrared/power/
subghz/esp32/gpio/nfc with per-test help) and `cmd_dfu` (-> #41). Upstream rewrote
m1_cli.c on main with its own mtest IDs incl. a guarded FCC hook (main:m1_cli.c:998)
that the fork's unguarded `mtest 9` lacks (#44 motivation). #44 re-homes IDs
collision-free and adds guards.


### `m1_csrc/m1_cli.h`

- Status: M (both-changed; -w delta +14/-1; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #44 + #41
- Fork origin: d10a6f9 refactor: remove FCC test placeholders and associated dead code (#28), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), 509c68d feat: add and harden USB DFU mode workflow (#17)
- Hunk summary (-w): `@@ -12,7 +12,20 @@`

Fork makes the header self-contained (FreeRTOS/cmsis includes), defines
MAX_INPUT_LENGTH, adds `cmd_dfu` protos. Carried with the #44 CLI port (dfu proto
with #41).


### `m1_csrc/m1_cli_help.c`

- Status: M (both-changed; -w delta +44/-65; 11 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #44
- Fork origin: d10a6f9 refactor: remove FCC test placeholders and associated dead code (#28)
- Hunk summary (-w): `@@ -11,22 +11,21 @@`; `@@ -46,8 +45,7 @@ void cmd_m1_mtest_help_gpio(void);`; `@@ -68,21 +66,19 @@ BaseType_t cmd_m1_mtest_help(void)`; *+8 more hunks*

Fork's mtest help texts (per-subcommand syntax strings). Carried with the #44
re-home so help and IDs stay consistent.


### `m1_csrc/m1_crypto.c`

- Status: A (ours-only; -w delta +604/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #42
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -0,0 +1,604 @@`

Fork's crypto helpers (604 lines, origin 3b6c252): AES/IV plumbing for credential
encryption. Upstream has no equivalent (grep `m1_crypto` on main = 0). #42 carries
with the documented hardening (hardware RNG for IVs was fork #16, closed).


### `m1_csrc/m1_crypto.h`

- Status: A (ours-only; -w delta +45/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #42
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -0,0 +1,45 @@`

Crypto helper API header — carried with m1_crypto.c under #42.


### `m1_csrc/m1_display.c`

- Status: M (both-changed; -w delta +6/-5; 6 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -115,7 +115,7 @@ uint8_t m1_gui_submenu_update(const char *phmenu[], uint8_t num_items, uint8_t s`; `@@ -249,7 +249,7 @@ uint8_t m1_gui_submenu_update(const char *phmenu[], uint8_t num_items, uint8_t s`; `@@ -295,7 +295,7 @@ uint8_t m1_gui_submenu_update(const char *phmenu[], uint8_t num_items, uint8_t s`; *+3 more hunks*

Fork: `m1_info_box_display_draw(const uint8_t*)` -> `(const char*)` (pointer-sign
vs u8g2 API), two `// No break here!` -> `/* fallthrough */` annotations, `(void)u8g2;`
in the u8x8 byte callback. Main's display code still has the uint8_t signature
(matching header entry below) and plain comments. Classes: pointer-sign,
-Wimplicit-fallthrough, unused-parameter.


### `m1_csrc/m1_display.h`

- Status: M (both-changed; -w delta +1/-1; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -162,7 +162,7 @@ void m1_gui_scr_animation(void);`

Companion proto hunk: `m1_info_box_display_draw(uint8_t box_row, const char *ptext);`
— carried with m1_display.c.


### `m1_csrc/m1_esp32_fw_update.c`

- Status: M (both-changed; -w delta +9/-7; 7 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -141,7 +141,7 @@ void setting_esp32_exit(void)`; `@@ -264,7 +264,7 @@ void setting_esp32_image_file(void)`; `@@ -298,7 +298,7 @@ void setting_esp32_image_file(void)`; *+4 more hunks*

Fork: `uint8_t prn_name` -> `char` x2, `(char*)` casts on the three
m1_fb_read_from_file call sites (pointer-sign), `#if 0` guard on one unused
prn_name, and a bounded-copy hardening `strncpy -> snprintf("%.*s")` for the
ESP boot-info line (truncation-safe). Main lacks all. Classes: pointer-sign,
unused-variable; plus one genuine overflow-safe copy fix.


### `m1_csrc/m1_esp_hosted_config.h`

- Status: M (both-changed; -w delta +21/-14; 1 hunks)
- Disposition: **intentionally-dropped**
- Fork origin: d10a6f9 refactor: remove FCC test placeholders and associated dead code (#28), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -34,42 +34,49 @@`

Fork delta = clang-format rewrap of the esp-hosted Kconfig defines PLUS two
genuine hunks (hunk-verified): `#ifndef/#endif` guards around both
`CONFIG_WIFI_PROV_SCAN_MAX_ENTRIES` and `CONFIG_WIFI_PROV_AUTOSTOP_TIMEOUT`
(legacy :71-73, :77-79), and deletion of the `void test_get_available_wifi(void);`
prototype (present at fork point :72). Disposition: the guards are genuine
include hygiene still absent on main (main:m1_csrc/m1_esp_hosted_config.h:74-77
unguarded) — but nothing else in-tree defines `CONFIG_WIFI_PROV_*`, so no
redefinition warning can fire today; not tagged `recovered-warning-fix` (no
warning class is actually recovered). Re-add the guards only if real
esp-hosted SDK headers ever land. The proto deletion is not applicable: main
still declares it (main:...:80) with a live definition and call site
(main:m1_esp_hosted_dummy.c:51, main:m1_cli.c:1107). Dropped.


### `m1_csrc/m1_esp_hosted_dummy.c`

- Status: M (ours-only; -w delta +4/-30; 3 hunks)
- Disposition: **intentionally-dropped**
- Fork origin: d10a6f9 refactor: remove FCC test placeholders and associated dead code (#28)
- Hunk summary (-w): `@@ -10,9 +10,9 @@`; `@@ -20,14 +20,11 @@`; `@@ -35,32 +32,9 @@ void esp32_app_init(void);`

Fork delta (`git diff -w 93acd751 legacy/v0.8.11 -- m1_csrc/m1_esp_hosted_dummy.c`,
hunk-verified) = include/whitespace churn PLUS deletions: the
`test_get_available_wifi()` stub definition (fork point :51) with its
prototype (:29), one of the two duplicate `esp32_app_init` protos (fork
point :30), and `esp32_app_init()` reduced to a `{ ; }` stub (legacy :40).
Drop stands on the new base, honestly stated: main actively defines and
calls the removed function (main:m1_esp_hosted_dummy.c:51 definition,
main:m1_cli.c:1107 caller) and keeps the protos (main:...:28-30) — the
fork's deletion was its own dead-code choice, not a fix the base lacks.


### `m1_csrc/m1_file_browser.c`

- Status: M (both-changed; -w delta +185/-279; 43 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 16402da fix(fw-update): fix empty filename issue when selecting fw via SD card (#33), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -12,17 +12,20 @@`; `@@ -68,16 +71,13 @@ uint8_t m1_fb_check_low_freespace(void);`; `@@ -108,24 +108,19 @@ S_M1_file_browser_hdl *m1_fb_init(u8g2_t *lcd_hdl)`; *+40 more hunks*

Fork: initialize-on-declaration fixes inside m1_fb_display (`uint8_t k = 0;`,
`ext_len = 0;`) killing -Wmaybe-uninitialized on the extension-scan path.
Main:m1_file_browser.c:817 still declares `ext_len` uninitialized (used at :1222+).
Applicability re-checked at carry time — upstream evolved the file (+924/-255 vs
legacy). Restyle hunks dropped.


### `m1_csrc/m1_file_browser.h`

- Status: M (both-changed; -w delta +8/-17; 1 hunks)
- Disposition: **intentionally-dropped**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -13,34 +13,25 @@`

Fork delta = clang-format compaction of the enums/protos plus an include swap
(`u8g2.h` -> `m1_system.h`, which reaches u8g2 through the display include chain).
No signature changes; the base's header compiles green against the base's .c
(PR-2 virgin-baseline builds). Restyle residue; the .c counterpart carries its
genuine init fixes under #39.


### `m1_csrc/m1_fusb302.c`

- Status: M (ours-only; -w delta +1/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -207,6 +207,7 @@ void pd_unsetBit(uint8_t *reg, uint8_t mask)`

Fork adds `(void)reg;` in pd_fusb302_init (the ID read is diagnostics-only).
Main lacks it (grep on main = 0). Class: -Wunused-variable.
Ours-only module note (Phase 4c): upstream carries the identical module unchanged
since the fork point; no different-name equivalent needed.


### `m1_csrc/m1_fw_update.c`

- Status: M (both-changed; -w delta +284/-168; 21 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #41
- Fork origin: e028e9d release: v0.8.10 - DFU stability hardening and safety net, d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), 509c68d feat: add and harden USB DFU mode workflow (#17), (+1 more)
- Hunk summary (-w): `@@ -12,19 +12,26 @@`; `@@ -34,7 +41,6 @@`; `@@ -52,7 +58,9 @@ void firmware_update_exit(void);`; *+18 more hunks*

Carried hunks: `firmware_update_usb_dfu_mode()`, `firmware_update_usb_dfu_mode_cli()`,
`firmware_update_enter_usb_dfu()` (USB ROM DFU entry, origins 509c68d/e028e9d) and
the SD-image empty-filename fix (origin 16402da, fork issue #33). Absent on main
(`git grep -c "usb_dfu" main -- m1_csrc/` = 0). #41 ports the workflow onto
upstream's recovery stack.


### `m1_csrc/m1_fw_update.h`

- Status: M (ours-only; -w delta +2/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #41
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 509c68d feat: add and harden USB DFU mode workflow (#17)
- Hunk summary (-w): `@@ -20,5 +20,7 @@ void firmware_update_exit(void);`

Fork adds the DFU entry protos `firmware_update_usb_dfu_mode()` /
`firmware_update_usb_dfu_mode_cli()` (absent on main: m1_fw_update.h ends at
firmware_update_gui_update). Returns with the #41 port.


### `m1_csrc/m1_fw_update_bl.c`

- Status: M (both-changed; -w delta +376/-152; 40 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #41
- Fork origin: e028e9d release: v0.8.10 - DFU stability hardening and safety net, d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -12,15 +12,17 @@`; `@@ -37,10 +39,10 @@ FW_CFG_SECTION S_M1_FW_CONFIG_t m1_fw_config = {`; `@@ -50,14 +52,19 @@ FW_CFG_SECTION S_M1_FW_CONFIG_t m1_fw_config = {`; *+37 more hunks*

Carried hunks: BOOT_FAIL_SIGNATURE failsafe state machine (raw `TAMP->BKP1R`
writes at legacy m1_fw_update_bl.c:731-772 — boot-fail escalation, boot-CRC verdict
propagation; origins 6e73998/e028e9d). Upstream on main has only the basic CRC
pieces (`bl_crc_check` main:m1_fw_update_bl.c:68, `bl_get_crc_chunk` :113) with
interactive rollback (#41 motivation: upstream rollback is interactive only). Port
composes the failsafe with upstream's pieces and replaces raw BKP1R writes with the
bu_regs struct API (Phase 4(b)). Restyle hunks dropped.


### `m1_csrc/m1_fw_update_bl.h`

- Status: M (both-changed; -w delta +49/-18; 6 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #41
- Fork origin: d99e2f8 release: v0.8.11 - DFU visual indicator (LED), e028e9d release: v0.8.10 - DFU stability hardening and safety net, d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 16402da fix(fw-update): fix empty filename issue when selecting fw via SD card (#33), 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), (+6 more)
- Hunk summary (-w): `@@ -15,14 +15,22 @@`; `@@ -36,8 +44,9 @@`; `@@ -45,13 +54,33 @@`; *+3 more hunks*

Mixed entry. Retained part: version macros — PR-2 froze 0.9.0.0-UFO on the new
base (main:m1_fw_update_bl.h:37-43 + FW_BUILD_LABEL :50-52), superseding the fork's
0.8.11 values. Carried part (#41): `BOOT_FAIL_SIGNATURE` (absent on main), the
`FW_CONFiG` -> `FW_CONFIG` spelling fix (main:19-20 still has the typo — fork issue
#31 lineage), and the parenthesized `FW_CRC_ADDRESS` macro (main:25 is unparenthesized,
an operator-precedence hazard in compound expressions). Slider-macro rewraps dropped.


### `m1_csrc/m1_gpio.c`

- Status: M (both-changed; -w delta +13/-5; 5 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -165,8 +165,10 @@ void menu_gpio_exit(void)`; `@@ -190,8 +192,10 @@ void gpio_manual_control(void)`; `@@ -221,8 +225,10 @@ void gpio_3_3v_on_gpio(void)`; *+2 more hunks*

Fork: `uint8_t prn_name[...]` -> `char prn_name[...]` x5 across gpio screens
(pointer-sign vs u8g2/sprintf) and `#if 0` guards on unused locals
(GPIO_InitStruct, msg_len/msg_id). Main's gpio screens still use uint8_t buffers
(grep 'char prn_name' on main:m1_gpio.c = 0). Classes: pointer-sign,
unused-variable.


### `m1_csrc/m1_infrared.c`

- Status: M (both-changed; -w delta +12/-78; 5 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #43
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), ad3b8c6 feat: add Universal Remote feature with Flipper IRDB support
- Hunk summary (-w): `@@ -18,6 +18,7 @@`; `@@ -208,76 +209,8 @@ S_M1_IR_Tx_States infrared_transmit(uint8_t init)`; `@@ -334,10 +267,10 @@ void infrared_learn_new_remote(void)`; *+2 more hunks*

Fork replaces the `m1_gui_let_update_fw()` placeholder bodies with routing into
the Universal Remote (`#include "m1_ir_universal.h"`, origin ad3b8c6). Upstream's
infrared menu on main is the per-device-class browser (main:m1_menu.c:180-195).
#43 re-wires the fork's Universal Remotes entry alongside upstream's.


### `m1_csrc/m1_int_hdl.c`

- Status: M (both-changed; -w delta +99/-10; 6 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #52
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -129,7 +129,7 @@ void EXTI15_IRQHandler(void)`; `@@ -271,7 +271,10 @@ void USART1_IRQHandler(void)`; `@@ -323,6 +326,79 @@ void GPDMA1_Channel2_IRQHandler(void)`; *+3 more hunks*

Carried hunks: (a) `GPDMA1_Channel3_IRQHandler` — the Sub-GHz Rx DMA ISR
(79 new lines, origin d3a0208 #22: deferred-RTOS-context-switching under heavy
Sub-GHz load); (b) USART1 ISR notify-value packing fix (task id in high byte).
Upstream's int_hdl on main has neither (grep GPDMA1_Channel3 on main = no Ch3
handler). Belongs with the Sub-GHz Rx DMA carry (m1_sub_ghz.c entry) — carried
under #52 (port stability hardening + small fixes).


### `m1_csrc/m1_ir_universal.c`

- Status: A (ours-only; -w delta +1182/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #43
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), a55968b Infrared Universal Remote: Dashboard, Pagination, Search & Favorites (#18), 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15), ad3b8c6 feat: add Universal Remote feature with Flipper IRDB support
- Hunk summary (-w): `@@ -0,0 +1,1182 @@`

Fork's Universal Remote app (1182 lines): Flipper-IRDB `.ir` parser, browser UX,
search/favorites/pagination (origins 547178a, ad3b8c6, a55968b #18, review fixes
471b2e0/5a6818c/f68366d/9d010d9). Absent on main (upstream ships ZERO `.ir`
database files, #43 motivation). #43 consolidates the browser UX onto upstream's
IRMP stack and protocol table — the fork's aliasing map
(legacy m1_ir_universal.c:84-96: NECext->NEC etc.) is NOT carried as-is (it
transmits wrong codes); see Phase 4(a) for the single-source map evidence.


### `m1_csrc/m1_ir_universal.h`

- Status: A (ours-only; -w delta +100/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #43
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), a55968b Infrared Universal Remote: Dashboard, Pagination, Search & Favorites (#18), ad3b8c6 feat: add Universal Remote feature with Flipper IRDB support
- Hunk summary (-w): `@@ -0,0 +1,100 @@`

Universal Remote API header (100 lines) — carried with m1_ir_universal.c under #43.


### `m1_csrc/m1_lcd.c`

- Status: M (both-changed; -w delta +2/-0; 2 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -57,6 +57,7 @@ void m1_lcd_cleardisplay(void);`; `@@ -111,6 +112,7 @@ uint8_t u8x8_byte_stm32_4wire_hw_spi(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int,`

Fork adds `(void)u8x8;` and `(void)arg_ptr;` in the two u8x8 callback wrappers.
Main lacks them. Class: -Wunused-parameter. Two one-line hunks.


### `m1_csrc/m1_led_indicator.c`

- Status: M (both-changed; -w delta +5/-0; 5 hunks)
- Disposition: **upstream-equivalent**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -81,6 +81,7 @@ void m1_led_func_recovery(void);`; `@@ -95,6 +96,7 @@ void m1_led_indicator_off(uint8_t *params)`; `@@ -158,6 +160,7 @@ void m1_led_fast_blink(uint8_t r_g_b, uint8_t pwm_rgb, uint8_t on_off_ms)`; *+2 more hunks*

Fork delta = five `(void)params;` unused-parameter fixes in the LED indicator
callbacks (legacy m1_led_indicator.c:84,99,163,191,206). Upstream performed the
same class of fix in its own evolution of the file: `main:m1_csrc/m1_led_indicator.c`
has `(void)params;` at :144,159,229,261,280,404. Same intent, already achieved on
the new base.


### `m1_csrc/m1_lib.c`

- Status: M (both-changed; -w delta +23/-179; 15 hunks)
- Disposition: **intentionally-dropped**
- Fork origin: d10a6f9 refactor: remove FCC test placeholders and associated dead code (#28), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -12,20 +12,20 @@`; `@@ -34,13 +34,6 @@`; `@@ -54,131 +47,6 @@ void m1_hard_delay(uint32_t x);`; *+12 more hunks*

Fork delta = removal of the dead FCC test GPIO helpers (`m1_test_gpio_*`,
`m1_gpio_toggle`, M1_GPIO_TEST* macros — origin d10a6f9, fork issue #28) plus
clang-format. Upstream retains the helpers on the new base with zero callers
(decls main:m1_lib.h:18-22, defs in m1_lib.c, `git grep m1_test_gpio main` shows no
other sites). Removing upstream's (dead but harmless) code churns a shared file for
no behavioral gain; non-static unused functions produce no warning, so #39 does not
need the removal either.


### `m1_csrc/m1_log_debug.c`

- Status: M (both-changed; -w delta +161/-158; 24 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -13,20 +13,20 @@`; `@@ -37,10 +37,10 @@`; `@@ -57,6 +57,7 @@ typedef struct {`; *+21 more hunks*

Genuine fixes inside a mostly-restyle delta: NULL-guard macro for the TX buffer
predicate and `#if 0` around the unused volatile DMA TX state. Classes:
null-dereference hardening, unused-variable. Restyle hunks (clang-format indent
conversion) dropped. Main's log_debug.c is upstream's version — #39 re-applies the
two fixes where its code still warrants them.


### `m1_csrc/m1_log_debug.h`

- Status: M (both-changed; -w delta +18/-10; 2 hunks)
- Disposition: **intentionally-dropped**
- Fork origin: 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29)
- Hunk summary (-w): `@@ -13,20 +13,27 @@`; `@@ -57,4 +64,5 @@ extern TaskHandle_t log_db_task_hdl;`

Fork delta = include reorder and line-wrapping of the M1_LOG_* macros; the format
attribute annotation is preserved verbatim on both sides. Zero semantic change.
Restyle residue; not carried (the .c counterpart carries two genuine fixes under #39).


### `m1_csrc/m1_low_power.c`

- Status: M (ours-only; -w delta +2/-0; 2 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -60,6 +60,7 @@ void vPortSetupTimerInterrupt( void );`; `@@ -85,6 +86,7 @@ void PreSleepProcessing(uint32_t ulExpectedIdleTime)`

Fork adds `(void)ulExpectedIdleTime;` in PreSleepProcessing/PostSleepProcessing.
Main lacks them. Class: -Wunused-parameter. Phase 4c: upstream carries the module
unchanged (PreSleepProcessing present on upstream/main); fork delta is exactly
these two lines.


### `m1_csrc/m1_lp5814.c`

- Status: M (both-changed; -w delta +1/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -587,6 +587,7 @@ void lp5814_set_blink_timer(uint8_t r_g_b, uint16_t on_off_ms, uint8_t mode, voi`

Fork adds `(void)xTimer;` in lp5814_stop_blink_timer (TimerHandle_t callback API
forces the parameter; body doesn't use it). Main lacks it. Class:
-Wunused-parameter.


### `m1_csrc/m1_menu.c`

- Status: M (both-changed; -w delta +438/-362; 6 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #41 + #43
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), a55968b Infrared Universal Remote: Dashboard, Pagination, Search & Favorites (#18), 509c68d feat: add and harden USB DFU mode workflow (#17)
- Hunk summary (-w): `@@ -11,25 +11,27 @@`; `@@ -39,315 +41,375 @@`; `@@ -369,11 +431,11 @@ void subfunc_handler_task(void *param);`; *+3 more hunks*

Carried hunks: (a) `menu_Setting_Firmware_Update_USB_DFU` record + wiring in the
Firmware-update children list (legacy m1_menu.c:252-253, :330) -> #41;
(b) Infrared submenu for the Universal Remote — `menu_Infrared_Universal_Remotes`,
`Learn_New_Remote`, `Saved_Remotes` (legacy m1_menu.c:141-167) -> #43, to sit
alongside upstream's per-device-class entries (main:m1_menu.c:180-195).
Intentionally dropped hunks: the stub records `menu_Settings_System` (:316) and
`menu_Settings_LCD_and_Notifications` (:280) with their commented-out parent links
(:356,:358), and the orphan `menu_Sub_GHz_Radio_Settings` record (:68-73, unlinked)
— Phase 3 verdicts. NFC-rename hunks moot (see m1_nfc.c). clang-format hunks dropped.


### `m1_csrc/m1_nfc.c`

- Status: M (both-changed; -w delta +119/-107; 58 hunks)
- Disposition: **upstream-equivalent**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), a55968b Infrared Universal Remote: Dashboard, Pagination, Search & Favorites (#18)
- Hunk summary (-w): `@@ -23,7 +23,7 @@`; `@@ -104,33 +104,33 @@ typedef enum {`; `@@ -176,27 +176,27 @@ static void nfc_rename_gui_update(uint8_t param);`; *+55 more hunks*

Fork delta = `nfc_*` -> `m1_nfc_*` symbol renames + warning hygiene
((void)param, %p casts) on the pre-rewrite code. Upstream rewrote the file on the
new base (`git diff -w --numstat legacy/v0.8.11 main -- m1_csrc/m1_nfc.c` =
+6652/-692 — MFC raw sessions, dict attack, T2T, DESFire) and kept the `nfc_*`
naming (main:m1_nfc.h:16-37). Renames are churn against the rewrite; the warning
hygiene applies to code that no longer exists. Project-wide warning hygiene on the
NEW code is restored by #39, not by carrying these hunks.


### `m1_csrc/m1_nfc.h`

- Status: M (both-changed; -w delta +3/-3; 1 hunks)
- Disposition: **upstream-equivalent**
- Fork origin: a55968b Infrared Universal Remote: Dashboard, Pagination, Search & Favorites (#18)
- Hunk summary (-w): `@@ -13,9 +13,9 @@`

Fork delta = the same `nfc_*` -> `m1_nfc_*` renames only. New base kept the
`nfc_*` API and extended it (main:m1_csrc/m1_nfc.h:16-37: nfc_read/nfc_saved/
nfc_detect_reader/nfc_mfc_emulate/nfc_harvest/...). Nothing fork-functional to
carry.


### `m1_csrc/m1_power_ctl.c`

- Status: M (both-changed; -w delta +8/-4; 8 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39 + #46
- Tags: recovered-warning-fix
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -28,7 +28,7 @@`; `@@ -198,6 +198,7 @@ void power_off(void)`; `@@ -259,7 +260,7 @@ static void battery_info_gui_create(uint8_t param)`; *+5 more hunks*

Mixed entry. #46 hunk: `res_string.h` -> `Res_String.h` include case. #39-tagged
hunks: `(void)xTimer;`, three empty-destroy `(void)param;`, two `(void)param;` in
gui_update functions. Main lacks all (grep (void)xTimer on main:m1_power_ctl.c = 0).
Classes: unused-parameter.


### `m1_csrc/m1_rf_spi.c`

- Status: M (both-changed; -w delta +1/-1; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -235,7 +235,7 @@ int32_t m1_spi_hal_wrapper(const uint8_t * const pTxData, uint8_t * const pRxDat`

Fork adds the `(uint8_t *)pTxData` cast initializing `.pdata_tx` in
m1_spi_hal_wrapper (const-correctness at the struct-field boundary — the SPI
transaction struct owns a non-const tx pointer). Main:266 still passes the const
pointer raw. Class: discarded-qualifiers. One hunk.


### `m1_csrc/m1_rfid.c`

- Status: M (both-changed; -w delta +27/-13; 26 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39 + #46
- Tags: recovered-warning-fix
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -25,7 +25,7 @@`; `@@ -424,6 +424,7 @@ static void lfrfid_read_create(uint8_t param)`; `@@ -613,7 +614,7 @@ static void lfrfid_read_submenu_create(uint8_t param)`; *+23 more hunks*

Mixed entry. #46 hunk: `Res_String.h` include case. #39-tagged hunks: twelve
`(void)param;` across the gui lifecycle stubs. Main lacks them. Class:
-Wunused-parameter.


### `m1_csrc/m1_sdcard.c`

- Status: M (both-changed; -w delta +7/-0; 6 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -79,9 +79,11 @@ uint8_t 			sdcard_status_changed = 0;`; `@@ -798,6 +800,7 @@ static uint8_t m1_sdcard_getcardstate(void)`; `@@ -971,6 +974,7 @@ DSTATUS m1_sdcard_status(uint8_t param)`; *+3 more hunks*

Fork: `#if 0` around the four unused static FIL/FILINFO/DIR objects and six
`(void)param;` in the FatFs diskio entry points. Main's sdcard.c already has SOME
(void) annotations (upstream's own pass — verify per-hunk at carry) but not the
storage-object guards (grep '#if 0' main:m1_sdcard.c = 0). Classes:
unused-variable, unused-parameter.


### `m1_csrc/m1_sdcard_man.c`

- Status: M (both-changed; -w delta +13/-9; 8 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -122,7 +122,8 @@ static uint8_t m1_sdm_check_low_sdcard(void)`; `@@ -176,7 +177,7 @@ static void m1_sdm_main_task(void *argument)`; `@@ -371,6 +372,7 @@ void m1_sdm_task_init(void)`; *+5 more hunks*

Fork: `(void)` annotations in the two timer/task entries (grep on main = partial).
Class: unused-parameter. (The companion buffer-width fix lives in m1_sdcard_man.h —
separate carry, see above.) Applicability re-checked at carry time.


### `m1_csrc/m1_sdcard_man.h`

- Status: M (ours-only; -w delta +4/-7; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #52
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), 78d6b86 Fix buffer overflow build errors in m1_sub_ghz.c and m1_sdcard_man.h
- Hunk summary (-w): `@@ -42,25 +42,22 @@`

Fork widens `dat_filename[64] -> [256]` in S_M1_SDM_DatFileInfo_t (origin 78d6b86:
buffer-overflow fix in the SD log-manager filename buffer). Main's struct still has
64 (main:m1_csrc/m1_sdcard_man.h:52) — genuine bug fix still broken on the new
base; carried under #52 (port stability hardening + small fixes).
Typedef reformat hunks dropped.


### `m1_csrc/m1_settings.c`

- Status: M (both-changed; -w delta +6/-5; 2 hunks)
- Disposition: **retained-by-upstream**
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), ad3b8c6 feat: add Universal Remote feature with Flipper IRDB support
- Hunk summary (-w): `@@ -314,7 +314,7 @@ void settings_about(void)`; `@@ -326,10 +326,11 @@ static void settings_about_display_choice(uint8_t choice)`

Fork-relevant content = about-box fork-tag display. Retained on the new base via
PR-2: `main:m1_csrc/m1_settings.c:204` draws `FW_BUILD_LABEL` which expands to
`FW 0.9.0.0-UFO` (`main:m1_fw_update_bl.h:50-52`) — a cleaner re-land of the fork's
hardcoded `FW_VERSION_FORK_TAG` row (legacy m1_settings.c about screen).
Carried hunks (tagged): the `uint8_t prn_name[20]` -> `char` + `(char *)` cast
pointer-sign fixes at the same function — `main:m1_settings.c:187-200` still has the
unfixed `uint8_t` buffer passed to `sprintf`/`u8g2_DrawStr(char*)`. #39 re-applies.
Feature-stub hunks (settings_system/settings_lcd_and_notifications): see Phase 3.


### `m1_csrc/m1_storage.c`

- Status: M (both-changed; -w delta +6/-6; 3 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -147,10 +147,10 @@ void storage_about(void)`; `@@ -738,7 +738,7 @@ void storage_unmount(void)`; `@@ -771,9 +771,9 @@ static void browse_gui_update(uint8_t sel_item, char *file_name)`

Fork: the libc-compat format fixes `"%uGB" -> "%luGB"` + `(unsigned long)` casts
x2, `uint8_t -> char prn_name` + casts in browse_gui_update (pointer-sign). The
sprintf args are the uint32_t capacity fields of S_M1_SDCard_Info
(m1_sdcard.h:65-66, identical in all three trees); uint32_t is unsigned long on
arm-none-eabi (verified __UINT32_TYPE__ = long unsigned int), so Main's %u at
m1_storage.c:161,:164 is a genuine -Wformat mismatch; Main:843 uint8_t prn_name
into char *print_ptr still unfixed (pointer-sign). Classes: -Wformat,
pointer-sign.


### `m1_csrc/m1_sub_ghz.c`

- Status: M (both-changed; -w delta +991/-842; 123 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #52
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), d10a6f9 refactor: remove FCC test placeholders and associated dead code (#28), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), a55968b Infrared Universal Remote: Dashboard, Pagination, Search & Favorites (#18), 78d6b86 Fix buffer overflow build errors in m1_sub_ghz.c and m1_sdcard_man.h
- Hunk summary (-w): `@@ -12,27 +12,29 @@`; `@@ -42,7 +44,8 @@`; `@@ -57,6 +60,9 @@`; *+120 more hunks*

Carried hunks: Sub-GHz Rx DMA double-buffering — SUBGHZ_RX_DMA_BLOCK_SAMPLES/
BUFFER_SAMPLES, subghz_rx_dma_buffer, restart-from-ISR plumbing, throttle/drop/
half/total counters (origin d3a0208 #22). All absent on main. This is the ISR-load
stability work behind closed fork issues #22/#26/#27 — carried under #52
(port stability hardening + small fixes). Dropped hunks: the `sub_ghz_radio_settings` stub
deletion is moot (Phase 3 — the stub itself is dropped), clang-format hunks not
carried.


### `m1_csrc/m1_sub_ghz.h`

- Status: M (both-changed; -w delta +12/-0; 2 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #52
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -46,6 +46,7 @@`; `@@ -139,12 +140,23 @@ void sub_ghz_replay(void);`

Fork adds the Rx DMA surface: SUBGHZ_RX_DMA_BLOCK_QUEUE_LEN,
`subghz_rx_dma_restart_from_isr()`, hdma_subghz_rx + counter externs. Carried with
m1_sub_ghz.c under #52 (port stability hardening carry).


### `m1_csrc/m1_sys_init.c`

- Status: M (both-changed; -w delta +3/-0; 2 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -63,9 +63,11 @@ void m1_system_init(void)`; `@@ -150,6 +152,7 @@ void m1_system_GPIO_init(void)`

Fork: `(void)ret;`/`(void)free_heap;` (assert-checked xTaskCreate results) and
`(void)param;` in the init task. Main lacks them. Class: set-but-unused.
Three hunks.


### `m1_csrc/m1_system.c`

- Status: M (both-changed; -w delta +324/-281; 25 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #41
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), 509c68d feat: add and harden USB DFU mode workflow (#17), 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15), (+1 more)
- Hunk summary (-w): `@@ -12,26 +12,30 @@`; `@@ -40,18 +44,18 @@`; `@@ -72,17 +76,17 @@ void startup_config_handler(void);`; *+22 more hunks*

Carried hunks: device-op-status handlers — `handle_op_status_fw_corruption()`
(dual-bank corrupt -> USB DFU fallback screen), fw-update/rollback-complete and
reboot handlers, `startup_error_screen_display()` and its proto (origins 6e73998
#29, d3a0208). All absent on main (`git grep -c "handle_op_status" main` = 0;
upstream's startup_config_handler has no corruption path). (void)-param hunks tagged
below for #39. #41 ports the handlers to coexist with upstream's update state
machine (main:m1_system.c:750-804 interactive rollback).


### `m1_csrc/m1_system.h`

- Status: M (both-changed; -w delta +26/-28; 8 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #41
- Fork origin: 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), 509c68d feat: add and harden USB DFU mode workflow (#17)
- Hunk summary (-w): `@@ -11,11 +11,11 @@`; `@@ -33,9 +33,11 @@`; `@@ -53,10 +55,13 @@`; *+5 more hunks*

Carried hunk: `void startup_error_screen_display(const char *l1, const char *l2);`
proto (absent on main). Not carried: the SYS_CONFIG_MAGIC_NUMBER relocation to
m1_fw_update_bl.h — main keeps it in m1_system.h (main:m1_system.h:85), single
source retained; macro rewraps are restyle.


### `m1_csrc/m1_tasks.c`

- Status: M (both-changed; -w delta +14/-0; 5 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -78,30 +78,39 @@ void m1_tasks_init(void)`; `@@ -160,6 +169,8 @@ void vApplicationIdleHook(void)`; `@@ -194,6 +205,7 @@ void vApplicationMallocFailedHook(void)`; *+2 more hunks*

Fork: ten `(void)ret;`/`(void)free_heap;` across m1_tasks_init (all assert-checked)
and `(void)xTask;`/`(void)pcTaskName;` in the stack-overflow hook (the log macro
reads pcTaskName only when logging enabled). Main lacks them. Classes:
set-but-unused, unused-parameter.


### `m1_csrc/m1_usb_cdc_msc.c`

- Status: M (both-changed; -w delta +132/-165; 44 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #44
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -6,27 +6,25 @@`; `@@ -63,8 +61,7 @@`; `@@ -81,6 +78,7 @@ SemaphoreHandle_t ser2usb_task_semaphore;`; *+41 more hunks*

Carried hunks: the CLI rx stream-buffer consumer side (`h_usb_cli_rx_streambuf`
drain feeding the CLI task) — part of the CDC race fix (origin d3a0208 #27).
Per #44 AC the fork's CDC rewrite is NOT resurrected wholesale: only the pieces
that give `cdcstats` its counters are re-homed onto upstream's usbd_cdc_if.c path.
Restyle hunks dropped.


### `m1_csrc/m1_usb_cdc_msc.h`

- Status: M (both-changed; -w delta +1/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #44
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -64,6 +64,7 @@ extern volatile uint16_t head_usart1_dma;`

Fork adds `extern StreamBufferHandle_t h_usb_cli_rx_streambuf;` — the handle the
#44 counter re-home consumes.


### `m1_csrc/m1_virtual_kb.c`

- Status: M (both-changed; -w delta +205/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #42
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -1061,3 +1061,208 @@ uint8_t m1_vkbs_get_data(char *description, char *data_buffer)`

Fork adds `m1_vkb_get_password()` (masked on-screen keyboard for PSK entry,
+205 lines, origin 3b6c252). Returns with the #42 join flow (upstream's keyboard
lacks a masked mode). Restyle hunks dropped.


### `m1_csrc/m1_virtual_kb.h`

- Status: M (both-changed; -w delta +1/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #42
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -15,5 +15,6 @@`

Fork adds the `m1_vkb_get_password()` proto — carried with m1_virtual_kb.c.


### `m1_csrc/m1_watchdog.c`

- Status: M (both-changed; -w delta +3/-0; 2 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -82,9 +82,11 @@ void m1_wdt_init(void)`; `@@ -98,6 +100,7 @@ void m1_wdt_init(void)`

Fork: `(void)ret;`/`(void)free_heap;` in m1_wdt_init, `(void)param;` in the
handler task. Main lacks them. Class: set-but-unused.


### `m1_csrc/m1_wifi.c`

- Status: M (both-changed; -w delta +780/-141; 15 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #42
- Fork origin: 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), a55968b Infrared Universal Remote: Dashboard, Pagination, Search & Favorites (#18), 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -12,14 +12,17 @@`; `@@ -36,7 +39,6 @@`; `@@ -48,6 +50,8 @@ void wifi_config(void);`; *+12 more hunks*

Carried hunks: the WiFi join flow — `wifi_config()` rework, `wifi_join_network()`,
`wifi_auto_connect()`, `wifi_ensure_initialized()` (origin 3b6c252, fork #15) on top
of the scan list plumbing. Upstream's 5.4k-line `m1_wifi.c` rework on main has NO
join/credential code (#42 motivation, grep-verified). #42 re-homes the join flow
onto upstream's versioned `saved_networks.csv` store; ESP-side handler carried at
Esp_spi_at/.../esp_app_main.c. Scan-list restyle hunks dropped.


### `m1_csrc/m1_wifi.h`

- Status: M (both-changed; -w delta +6/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #42
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -21,4 +21,10 @@ void menu_wifi_init(void);`

Fork adds `wifi_cred_init()`, `wifi_join_network()`, `wifi_show_saved_networks()`,
`wifi_show_connection_status()` protos — the join/credential API surface #42 ports.


### `m1_csrc/m1_wifi_cred.c`

- Status: A (ours-only; -w delta +299/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #42
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -0,0 +1,299 @@`

Fork's credential store (299 lines, origin 3b6c252): encrypted PSK persistence.
#42 carries it re-homed onto upstream's versioned store with the hardened crypto
(council-ratified; threat model documented in the issue).


### `m1_csrc/m1_wifi_cred.h`

- Status: A (ours-only; -w delta +85/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #42
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -0,0 +1,85 @@`

Credential-store API header (85 lines, origin 3b6c252) — carried with
m1_wifi_cred.c under #42.


### `m1_csrc/privateprofilestring.c`

- Status: M (both-changed; -w delta +2/-2; 2 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #39
- Tags: recovered-warning-fix
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -572,7 +572,7 @@ bool isValidHeaderField(ParsedValue *data, const char* filetype, const char* ver`; `@@ -798,7 +798,7 @@ bool isValidHeaderField(ParsedValue *data, const char* filetype, const char* ver`

Fork: `(int)strlen(current_key)` cast x2 in the header-field validators —
strlen returns size_t, `len` is int (sign-compare). Main:524 and :881 still have
the unguarded comparison. Class: -Wsign-compare. THE libc-compat representative:
same size_t/int mismatch family as the m1_storage.c format fixes.


### `tools/__pycache__/append_crc32.cpython-313.pyc`

- Status: A (ours-only; -w delta +-/--; 0 hunks)
- Disposition: **intentionally-dropped**
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)

CPython bytecode artifact accidentally committed by the fork (name-status A,
binary). Never source; excluded by PR-1 hygiene. The generator source
(`tools/append_crc32.py`) is carried under #40.


### `tools/append_crc32.py`

- Status: A (ours-only; -w delta +166/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #40
- Fork origin: 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15), ad3b8c6 feat: add Universal Remote feature with Flipper IRDB support
- Hunk summary (-w): `@@ -0,0 +1,166 @@`

Fork's CRC32 post-build tool (166 lines; origins 49af568, 4693ea2, 7c0139d,
f220529 — word-wise CRC matching the STM32 HAL CRC unit). Upstream has no CRC
append pipeline. #40 carries it with the Windows-path emitter fix; regression-tested
by test_crc*.py below. Referenced by the fork CMakeLists post-build steps (see the
CMakeLists.txt entry).


### `tools/check_case_sensitivity.py`

- Status: A (ours-only; -w delta +262/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #46
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -0,0 +1,262 @@`

Pre-commit case-collision checker (262 lines, origin d3a0208) — the tool that
caught the very mismatches #46 fixes. Already integrated: the repo's pre-commit
hook invokes it (observed live during this PR's commits). Carried with #46 so the
class of defect stays blocked.


### `tools/crc_debug.py`

- Status: A (ours-only; -w delta +105/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #40
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -0,0 +1,105 @@`

CRC debugging aid (105 lines) used to derive the word-wise layout — carried with
the #40 CRC pipeline.


### `tools/test_crc.py`

- Status: A (ours-only; -w delta +112/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #40
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -0,0 +1,112 @@`

CRC regression tests (112 lines) locking the tool against the device layout —
carried with #40 and wired into its verification.


### `tools/test_crc_fix.py`

- Status: A (ours-only; -w delta +124/-0; 1 hunks)
- Disposition: **carried-by-patch**
- Carry patch: #40
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -0,0 +1,124 @@`

Second CRC regression suite (124 lines, trailing-zeros case, origin 4693ea2) —
carried with #40.



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
  restyle + two `(char *)` pointer-sign casts, tagged in its entry below) ->
  `upstream-equivalent`; the case fixes in nfc_file.c /
  nfc_listener.c / nfc_poller.c -> carried under #46. **Nothing functional
  dies**: upstream's NFC stack supersedes, and the genuine small fixes have
  carry vehicles.

### 3. libc-compat changes

The fork's libc-compat work is the newlib/arm-none-eabi type-width family, all
verified still-open on the new base and tagged for #39:
`%u`->`%lu` + `(unsigned long)` casts (m1_storage.c:161,:164 on main: the uint32_t
S_M1_SDCard_Info args are unsigned long on arm-none-eabi — genuine -Wformat
mismatch),
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
above (34 file entries; per-entry hunks cite the warning class and the fix
summary). PR-4 (#39) mines exactly this tag and re-applies the fixes as isolated
commits against the new base, re-verifying applicability per file since upstream
evolved several of them. The restyle itself is `intentionally-dropped` everywhere
(see the nfc_storage.c / m1_esp_hosted_config.h entries for representative
reasoning); the non-restyle hunks found in the restyle family are either tagged
(nfc_storage.c) or explicitly carried (m1_sub_ghz_api.c -> #52).

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

## Evidence re-capture (issue #38 AC — council artifacts were never committed; this ledger is the evidence home)

### (a) IR protocol map — single-source claim

**Claim:** on each tree, the protocol-name -> IRMP-enum mapping is defined exactly
once at app level (no duplicated map).

Commands and verbatim findings:

```
$ git grep -n "ir_proto_map\[\]" legacy/v0.8.11
legacy/v0.8.11:m1_csrc/m1_ir_universal.c:83:static const S_IR_ProtoMap_t ir_proto_map[] = {
$ git grep -c "IRMP_NEC" legacy/v0.8.11 -- m1_csrc/m1_infrared.c
1                      # a comment line only — no second table on the fork
$ git grep -n "ir_protocols_mapping_table\[" main
main:Infrared/m1_ir_remotes.c:398:const S_M1_IR_Protocol_Mapping_t ir_protocols_mapping_table[IR_PROTOCOLS_MAPPING_MAX] =
main:Infrared/m1_ir_remotes.c:898:               (consumers — lookup in m1_ir_remotes.c itself)
$ git grep -c "IRMP_NEC" main -- m1_csrc/m1_infrared.c
1                      # comment only — no second table on the new base either
```

**Verdict: single-source HOLDS on both trees** — fork: `m1_ir_universal.c:83`
(`ir_proto_map[]`, 21 entries); new base: `m1_ir_remotes.c:398`
(`ir_protocols_mapping_table[]`). No duplicate map exists in either tree.

**Substantive difference the audit surfaced** (feeds #43, not a new source): the
two maps disagree on semantics — the fork aliases `"NECext" -> IRMP_NEC_PROTOCOL`
(legacy m1_ir_universal.c:85) while the new base maps `"NEC" -> IRMP_NEC8_PROTOCOL`
and `"NECext" -> IRMP_NEC_PROTOCOL` (main:Infrared/m1_ir_remotes.c:416-421). The
fork's aliasing truncates 16-bit NECext addresses/commands — the verified
wrong-codes defect #43 exists to fix. The consolidation keeps ONE map: upstream's.

### (b) BKP (backup-register) collision claim

**Claim (from #41's motivation):** the fork's failsafe writes raw `TAMP->BKP1R`,
colliding with upstream's `bu_regs.device_op_status` slot; DR2/DR3 are held by the
ESP version records.

Commands and verbatim findings:

```
$ git grep -n "TAMP->BKP1R" legacy/v0.8.11        # the fork's raw writes
legacy/v0.8.11:Core/Src/main.c:236:  uint8_t op_status = (uint8_t)(TAMP->BKP1R & 0xFF);
legacy/v0.8.11:m1_csrc/m1_fw_update_bl.c:731:    if (TAMP->BKP1R == BOOT_FAIL_SIGNATURE) {
legacy/v0.8.11:m1_csrc/m1_fw_update_bl.c:732:      TAMP->BKP1R = DEV_OP_STATUS_NO_OP;
legacy/v0.8.11:m1_csrc/m1_fw_update_bl.c:754/759/764/767/772: (BOOT_FAIL_SIGNATURE state machine)

$ git show main:m1_csrc/m1_system.h | sed -n '87,94p'   # upstream's struct
typedef struct {
        uint32_t magic_number;      // word 0 -> restored from RTC_BKP_DR0
        uint8_t device_op_status;   // word 1 -> restored from RTC_BKP_DR1
        uint8_t reserved_1..3;
} S_M1_BK_REGS_t;

$ git show main:m1_csrc/m1_system.c | sed -n '669,676p' # the restore loop
        k = (sizeof(S_M1_BK_REGS_t) + 3)/4;
        bu_reg_read = RTC_BKP_DR0; ... for (i=0; i<k; i++)
        { *bu_reg_write = HAL_RTCEx_BKUPRead(&hrtc, bu_reg_read++); ... }

$ git grep -n "DR0/DR1\|RTC_BKP_DR2\|RTC_BKP_DR3" main -- m1_csrc/m1_esp_version.c
main:m1_csrc/m1_esp_version.c:40:/* Dedicated backup registers (not DR0/DR1, which startup_bu_registers_init uses,
main:m1_csrc/m1_esp_version.c:42:#define ESP_VER_REG        RTC_BKP_DR2
main:m1_csrc/m1_esp_version.c:43:#define ESP_VER_MAGIC_REG  RTC_BKP_DR3
```

**Verdict: collision CONFIRMED.** On STM32H5, HAL's `RTC_BKP_DRx` indexes map
1:1 onto `TAMP->BKPxR` (stm32h5xx_hal_rtc_ex.h:674-675: DR0=0x00, DR1=0x01).
Upstream's restore loop reads the struct word-by-word starting at DR0, so
`device_op_status` lives precisely in **DR1 == TAMP->BKP1R** — the very register
the fork's failsafe overwrites with raw 32-bit values. `BOOT_FAIL_SIGNATURE`
(0xDEADBEEF) lands in the struct as status byte 0xEF (not any
`S_M1_DEV_OP_STATUS_t` value) plus clobbered reserved bytes; conversely
upstream's `startup_bu_registers_init()` (main:m1_system.c:670-679) rewrites
DR0/DR1 on every boot and would erase a fork failsafe verdict. Upstream's own
esp_version.c:40 documents the DR0/DR1 ownership. **#41's port must use the
`S_M1_BK_REGS_t` struct API (as #41 already specifies) instead of raw writes.**

### (c) Ours-only modules — upstream-equivalent-under-any-name search

```
$ git grep -lni "fusb302\|usb.pd\|power.delivery" upstream/main
  -> only unrelated hits (usbd_desc.c string, LL/PWR headers, CMSIS device header)
$ git ls-tree upstream/main m1_csrc/ | grep -i "fusb\|power"
  m1_csrc/m1_fusb302.c / m1_fusb302.h / m1_low_power.c / m1_low_power.h present
```

- **`m1_fusb302.c` (USB-PD)**: no different-name equivalent exists upstream, and
  none is needed — upstream carries the *identical* module (unchanged since the
  fork point; ours-only class = upstream untouched). The fork's delta is one
  warning fix -> #39.
- **`m1_low_power.c`**: same — module present on upstream unchanged
  (`PreSleepProcessing`/`PostSleepProcessing` at the same sites); fork delta is
  two `(void)` lines -> #39. (Power-management *feature* gap — STOP/STANDBY in
  the idle hook — is open issue #24, not a carry.)
- **`Core/Src/FreeRTOS_CLI.c`**: file shared and upstream-unchanged; the fork's
  delta is the `pxCommandHelper` CLI extension + one init fix -> #44 (+ #39 tag).
- **`Sub_Ghz/protocols/m1_secplus_v2_decode.c`**: upstream-unchanged module
  (pre-fix blob b7c54e2 at the fork point == upstream's copy); fork's
  `uint64_t code = 0;` correctness fix is absent on main -> #39.
- **`m1_crypto.c/h`**: searched — no crypto helper exists anywhere upstream
  (`git grep -ln "m1_crypto\|aes_" upstream/main -- m1_csrc/` empty); truly
  fork-new, carried under #42.

## Upstream vendor deltas — expected base-content changes (PR-1 Phase 6 evidence)

The fork kept `Drivers/` and `Middlewares/` pristine —
`git diff --name-only 93acd751 legacy/v0.8.11 -- Drivers Middlewares` is empty
(fork vendor purity, verified in PR-1). Upstream, however, changed **5 vendor
files** between the fork point and the adopted pin; these are *upstream-side
additions the fork never had*, now part of the adopted base. They are expected
base-content changes, NOT fork content — recorded here so nobody hunts them as
fork losses. Counts reproduce today (whitespace-ignored):

```
$ git diff -w --numstat 93acd751 upstream/main -- Drivers Middlewares
23      0       Drivers/BSP/Components/ST25R3916/rfal_rfst25r3916.c
12      0       Drivers/BSP/Components/ST25R3916/st25r3916_com.c
32      0       Drivers/BSP/Components/ST25R3916/st25r3916_irq.c
250     0       Drivers/u8g2_csrc/tools/extract_selected_fonts.py
826     0       Drivers/u8g2_csrc/u8g2_fonts_selected.c
```

Direction note: `git diff -w --numstat 2af1f8a legacy/v0.8.11 -- Drivers
Middlewares` shows the same five as all-zero-addition rows (0/23, 0/12, 0/32,
0/250, 0/826) — i.e. the fork *lacks* upstream's additions relative to the pin;
diffing the other way (as above) shows them as upstream additions. Either
direction yields exactly this quintet and nothing else.

Interpretation:

| File | Upstream-side change | Nature |
|---|---|---|
| `rfal_rfst25r3916.c` (+23) | ST25R3916 RF-NFC driver additions | vendor bugfix/feature layer the fork never consumed |
| `st25r3916_com.c` (+12) | com-layer additions | same |
| `st25r3916_irq.c` (+32) | IRQ handling additions | same |
| `Drivers/u8g2_csrc/tools/extract_selected_fonts.py` (+250) | font-selection tooling growth | build-time tool, no firmware impact |
| `Drivers/u8g2_csrc/u8g2_fonts_selected.c` (+826) | selected-font set expanded | display font data; consumed by the new base's UI |

All five arrive wholesale with the adopted base (`main` == pin `2af1f8a` content)
— nothing for the merge to carry, nothing fork-side to restore.

