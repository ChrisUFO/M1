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
| `carried-by-patch` | Content the new base lacks and worth keeping; names the P4 carry issue (#39–#44/#46) and cites fork origin SHAs. |
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
| retained-by-upstream | 0 |
| carried-by-patch | 0 |
| upstream-equivalent | 0 |
| intentionally-dropped | 0 |
| unknown (must be 0 — gate) | 113 |
| Entries tagged `recovered-warning-fix` (feed for #39) | 0 |

*Regenerate deterministically with `python tools/gen_merge_ledger.py`; the DISPOSITIONS table in that script is the single source of truth. Gate: `bash cmake/m1_01/verify_merge_ledger.sh`.*

## Per-file dispositions

### `.github/ISSUE_TEMPLATE/usb_dfu.md`

- Status: A (ours-only; -w delta +79/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -0,0 +1,79 @@`

_pending_

### `.gitignore`

- Status: M (both-changed; -w delta +28/-0; 2 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), d10a6f9 refactor: remove FCC test placeholders and associated dead code (#28), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), 774eab8 temp file update, (+2 more)
- Hunk summary (-w): `@@ -42,6 +42,14 @@ logs/`; `@@ -49,6 +57,26 @@ Thumbs.db`

_pending_

### `ARCHITECTURE.md`

- Status: M (ours-only; -w delta +368/-13; 1 hunks)
- Disposition: **unknown**
- Fork origin: d99e2f8 release: v0.8.11 - DFU visual indicator (LED), e028e9d release: v0.8.10 - DFU stability hardening and safety net, d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), 31b5d13 docs: update architecture and README for v0.8.5 zero-warning release, (+3 more)
- Hunk summary (-w): `@@ -4,24 +4,379 @@`

_pending_

### `CHANGELOG.md`

- Status: M (both-changed; -w delta +176/-5; 1 hunks)
- Disposition: **unknown**
- Fork origin: d99e2f8 release: v0.8.11 - DFU visual indicator (LED), e028e9d release: v0.8.10 - DFU stability hardening and safety net, d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 16402da fix(fw-update): fix empty filename issue when selecting fw via SD card (#33), 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), (+9 more)
- Hunk summary (-w): `@@ -5,12 +5,183 @@`

_pending_

### `CMakeFiles/4.0.2/CMakeSystem.cmake`

- Status: D (both-changed; -w delta +0/-15; 1 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -1,15 +0,0 @@`

_pending_

### `CMakeFiles/4.0.2/VCTargetsPath.vcxproj`

- Status: D (both-changed; -w delta +0/-27; 1 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -1,27 +0,0 @@`

_pending_

### `CMakeFiles/CMakeConfigureLog.yaml`

- Status: D (both-changed; -w delta +0/-24; 1 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -1,24 +0,0 @@`

_pending_

### `CMakeFiles/cmake.check_cache`

- Status: D (both-changed; -w delta +0/-1; 1 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -1 +0,0 @@`

_pending_

### `CMakeLists.txt`

- Status: M (both-changed; -w delta +67/-23; 5 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), 509c68d feat: add and harden USB DFU mode workflow (#17), 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15), (+1 more)
- Hunk summary (-w): `@@ -20,20 +20,31 @@ if(NOT CMAKE_BUILD_TYPE)`; `@@ -49,7 +60,7 @@ target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE`; `@@ -64,8 +75,10 @@ target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE`; *+2 more hunks*

_pending_

### `CMakePresets.json`

- Status: M (ours-only; -w delta +2/-2; 1 hunks)
- Disposition: **unknown**
- Fork origin: ad3b8c6 feat: add Universal Remote feature with Flipper IRDB support
- Hunk summary (-w): `@@ -10,8 +10,8 @@`

_pending_

### `Core/Inc/main.h`

- Status: M (both-changed; -w delta +14/-11; 2 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -31,23 +31,24 @@ extern "C" {`; `@@ -231,10 +232,12 @@ void Error_Handler(void);`

_pending_

### `Core/Src/FreeRTOS_CLI.c`

- Status: M (ours-only; -w delta +4/-2; 3 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -76,7 +76,8 @@ static const CLI_Command_Definition_t xHelpCommand =`; `@@ -159,7 +160,7 @@ BaseType_t FreeRTOS_CLIProcessCommand( const char * const pcCommandInput,`; `@@ -344,6 +345,7 @@ static BaseType_t prvHelpCommand( char * pcWriteBuffer,`

_pending_

### `Core/Src/app_freertos.c`

- Status: M (both-changed; -w delta +1/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -63,6 +63,7 @@ const osThreadAttr_t dummytask_attributes = {`

_pending_

### `Core/Src/cli_app.c`

- Status: M (both-changed; -w delta +559/-95; 9 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), 509c68d feat: add and harden USB DFU mode workflow (#17), 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -25,22 +25,30 @@ CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.`; `@@ -48,34 +56,117 @@ const char * cli_prompt = "\r\ncli> ";`; `@@ -83,37 +174,41 @@ const CLI_Command_Definition_t xCommandList[] = {`; *+6 more hunks*

_pending_

### `Core/Src/main.c`

- Status: M (both-changed; -w delta +224/-118; 34 hunks)
- Disposition: **unknown**
- Fork origin: d99e2f8 release: v0.8.11 - DFU visual indicator (LED), e028e9d release: v0.8.10 - DFU stability hardening and safety net, d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -22,12 +22,14 @@`; `@@ -98,20 +100,116 @@ void assert_failed(uint8_t* file, uint32_t line);`; `@@ -120,8 +218,9 @@ int main(void)`; *+31 more hunks*

_pending_

### `Core/Src/stm32h5xx_hal_msp.c`

- Status: M (both-changed; -w delta +4/-0; 2 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -511,8 +511,10 @@ void HAL_SPI_MspDeInit(SPI_HandleTypeDef* hspi)`; `@@ -548,7 +550,9 @@ void HAL_TIM_Base_MspInit(TIM_HandleTypeDef* htim_base)`

_pending_

### `Core/Src/stm32h5xx_it.c`

- Status: M (both-changed; -w delta +1/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -74,6 +74,7 @@ void NMI_Handler(void)`

_pending_

### `Core/Src/system_stm32h5xx.c`

- Status: M (both-changed; -w delta +79/-57; 12 hunks)
- Disposition: **unknown**
- Fork origin: 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29)
- Hunk summary (-w): `@@ -21,9 +21,9 @@`; `@@ -110,6 +110,7 @@`; `@@ -143,7 +144,8 @@`; *+9 more hunks*

_pending_

### `DEVELOPMENT.md`

- Status: M (ours-only; -w delta +1/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -34,3 +34,4 @@ Follow [Conventional Commits](https://www.conventionalcommits.org/):`

_pending_

### `Esp_spi_at/examples/at_spi_master/spi/stm32/main/esp_app_main.c`

- Status: M (both-changed; -w delta +282/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -652,6 +652,288 @@ uint8_t wifi_ap_scan_list(ctrl_cmd_t *app_req)`

_pending_

### `Esp_spi_at/examples/at_spi_master/spi/stm32/main/esp_app_main.h`

- Status: M (ours-only; -w delta +4/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -14,11 +14,15 @@`

_pending_

### `Esp_spi_at/examples/at_spi_master/spi/stm32/main/esp_at_list.h`

- Status: M (both-changed; -w delta +9/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -31,6 +31,15 @@`

_pending_

### `Infrared/irmp-irsnd/irmpconfig.h`

- Status: M (both-changed; -w delta +150/-75; 10 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), a55968b Infrared Universal Remote: Dashboard, Pagination, Search & Favorites (#18)
- Hunk summary (-w): `@@ -33,9 +33,10 @@`; `@@ -44,71 +45,131 @@`; `@@ -133,9 +194,11 @@`; *+7 more hunks*

_pending_

### `NFC/NFC_drv/common/nfc_file.c`

- Status: M (both-changed; -w delta +1/-1; 1 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -20,7 +20,7 @@`

_pending_

### `NFC/NFC_drv/common/nfc_storage.c`

- Status: M (both-changed; -w delta +102/-111; 23 hunks)
- Disposition: **unknown**
- Fork origin: a55968b Infrared Universal Remote: Dashboard, Pagination, Search & Favorites (#18)
- Hunk summary (-w): `@@ -1,16 +1,16 @@`; `@@ -20,7 +20,6 @@ typedef struct {`; `@@ -28,9 +27,9 @@ typedef struct {`; *+20 more hunks*

_pending_

### `NFC/NFC_drv/legacy/nfc_listener.c`

- Status: M (both-changed; -w delta +1/-1; 1 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -59,7 +59,7 @@`

_pending_

### `NFC/NFC_drv/legacy/nfc_poller.c`

- Status: M (both-changed; -w delta +1/-1; 1 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -51,7 +51,7 @@`

_pending_

### `README.md`

- Status: M (both-changed; -w delta +496/-9; 3 hunks)
- Disposition: **unknown**
- Fork origin: d99e2f8 release: v0.8.11 - DFU visual indicator (LED), e028e9d release: v0.8.10 - DFU stability hardening and safety net, d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), 31b5d13 docs: update architecture and README for v0.8.5 zero-warning release, (+4 more)
- Hunk summary (-w): `@@ -1,6 +1,12 @@`; `@@ -11,9 +17,9 @@ The M1 firmware provides support for:`; `@@ -26,31 +32,512 @@ See [HARDWARE.md](HARDWARE.md) for more details.`

_pending_

### `ROADMAP.md`

- Status: A (ours-only; -w delta +353/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15), ad3b8c6 feat: add Universal Remote feature with Flipper IRDB support
- Hunk summary (-w): `@@ -0,0 +1,353 @@`

_pending_

### `Sub_Ghz/m1_sub_ghz_api.c`

- Status: M (both-changed; -w delta +174/-342; 48 hunks)
- Disposition: **unknown**
- Fork origin: d10a6f9 refactor: remove FCC test placeholders and associated dead code (#28), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -12,30 +12,31 @@`; `@@ -45,7 +46,6 @@`; `@@ -62,48 +62,53 @@ static union si446x_cmd_reply_union si446x_cmd;`; *+45 more hunks*

_pending_

### `Sub_Ghz/m1_sub_ghz_api.h`

- Status: M (both-changed; -w delta +18/-12; 6 hunks)
- Disposition: **unknown**
- Fork origin: d10a6f9 refactor: remove FCC test placeholders and associated dead code (#28)
- Hunk summary (-w): `@@ -13,12 +13,15 @@`; `@@ -39,7 +42,8 @@`; `@@ -57,8 +61,7 @@`; *+3 more hunks*

_pending_

### `Sub_Ghz/protocols/m1_secplus_v2_decode.c`

- Status: M (ours-only; -w delta +1/-1; 1 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -61,7 +61,7 @@ uint8_t m1_secplus_v2_decode_half(uint64_t in_bits, uint8_t *half_code, uint32_t`

_pending_

### `Sub_Ghz/radio_config/si4463_revc2_patch.h`

- Status: M (ours-only; -w delta +1/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -42,6 +42,7 @@`

_pending_

### `USB/Class/CDC/Inc/usbd_cdc_if.h`

- Status: M (both-changed; -w delta +4/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -35,6 +35,10 @@ extern USBD_CDC_ItfTypeDef  USBD_CDC_Interface_fops;`

_pending_

### `USB/Class/CDC/Src/usbd_cdc_if.c`

- Status: M (both-changed; -w delta +91/-75; 16 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -17,17 +17,17 @@`; `@@ -40,18 +40,18 @@`; `@@ -71,7 +71,6 @@ uint8_t UserTxBufferFS[USB_FS_CHUNK_SIZE];`; *+13 more hunks*

_pending_

### `build`

- Status: A (ours-only; -w delta +136/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 5363ad0 fix: force cmake reconfigure on version change in build script, 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15), ad3b8c6 feat: add Universal Remote feature with Flipper IRDB support
- Hunk summary (-w): `@@ -0,0 +1,136 @@`

_pending_

### `cmake/driver_warnings.cmake`

- Status: A (ours-only; -w delta +15/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -0,0 +1,15 @@`

_pending_

### `cmake/gcc-arm-none-eabi.cmake`

- Status: M (ours-only; -w delta +7/-11; 1 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -38,17 +38,13 @@ if(CMAKE_BUILD_TYPE MATCHES Release)`

_pending_

### `cmake/m1_01/CMakeLists.txt`

- Status: M (both-changed; -w delta +56/-28; 6 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15), ad3b8c6 feat: add Universal Remote feature with Flipper IRDB support
- Hunk summary (-w): `@@ -3,19 +3,19 @@`; `@@ -65,20 +65,8 @@ target_include_directories(m1_01 INTERFACE`; `@@ -306,11 +294,6 @@ target_sources(m1_01 INTERFACE`; *+3 more hunks*

_pending_

### `documentation/stability_phase5_validation.md`

- Status: A (ours-only; -w delta +97/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -0,0 +1,97 @@`

_pending_

### `lfrfid/lfrfid.c`

- Status: M (both-changed; -w delta +8/-0; 4 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -201,17 +201,21 @@ void lfrfid_Init(void)`; `@@ -219,11 +223,13 @@ void lfrfid_Init(void)`; `@@ -390,6 +396,7 @@ void bytes_to_u32_array(BitOrder order, const uint8_t in_data[], uint32_t out_da`; *+1 more hunks*

_pending_

### `lfrfid/lfrfid_file.c`

- Status: M (both-changed; -w delta +1/-1; 1 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -18,7 +18,7 @@`

_pending_

### `lfrfid/lfrfid_protocol_em4100.c`

- Status: M (both-changed; -w delta +25/-17; 19 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -15,7 +15,7 @@`; `@@ -77,21 +77,21 @@ static EM4100_Decoder_t g_em4100_16_dec;`; `@@ -372,10 +372,12 @@ static void bit_stream_push(EM4100_Decoder_t* dec, uint8_t bit)`; *+16 more hunks*

_pending_

### `lfrfid/lfrfid_protocol_h10301.c`

- Status: M (both-changed; -w delta +14/-11; 13 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -13,7 +13,7 @@`; `@@ -21,9 +21,7 @@`; `@@ -71,10 +69,10 @@ static fsk_bit_state_t    bit_st;`; *+10 more hunks*

_pending_

### `lfrfid/t5577.c`

- Status: M (both-changed; -w delta +1/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -256,6 +256,7 @@ static void t5577_write_block(uint8_t block, bool lock_bit, uint32_t data)`

_pending_

### `m1_csrc/Res_String.c`

- Status: M (both-changed; -w delta +1/-1; 1 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -4,7 +4,7 @@`

_pending_

### `m1_csrc/logger.c`

- Status: M (both-changed; -w delta +3/-0; 2 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -90,6 +90,7 @@ uint8_t logUsartTx(uint8_t *data, uint16_t dataLen);`; `@@ -118,6 +119,8 @@ void logUsartInit(UART_HandleTypeDef *husart)`

_pending_

### `m1_csrc/m1_bq27421.c`

- Status: M (both-changed; -w delta +24/-6; 15 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -57,16 +57,24 @@ static bool bq27421_itporFlag(void);`; `@@ -305,8 +313,8 @@ bool bq27421_i2c_read_data_block( uint8_t offset, uint8_t *data, uint8_t bytes )`; `@@ -1087,7 +1095,7 @@ bool bq27421_applyConfigIfMatches(uint16_t designCapacity_mAh,`; *+12 more hunks*

_pending_

### `m1_csrc/m1_bt.c`

- Status: M (both-changed; -w delta +6/-1; 1 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -483,8 +483,13 @@ static uint16_t ble_scan_list_print(ctrl_cmd_t *app_resp, bool up_dir)`

_pending_

### `m1_csrc/m1_buzzer.c`

- Status: M (both-changed; -w delta +1/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -149,6 +149,7 @@ void buzzer_sys_init(uint16_t frequency)`

_pending_

### `m1_csrc/m1_buzzer.h`

- Status: M (both-changed; -w delta +13/-5; 3 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -13,12 +13,16 @@`; `@@ -29,9 +33,11 @@`; `@@ -43,7 +49,9 @@`

_pending_

### `m1_csrc/m1_cli.c`

- Status: M (both-changed; -w delta +278/-258; 44 hunks)
- Disposition: **unknown**
- Fork origin: 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), d10a6f9 refactor: remove FCC test placeholders and associated dead code (#28), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), 509c68d feat: add and harden USB DFU mode workflow (#17)
- Hunk summary (-w): `@@ -11,37 +11,39 @@`; `@@ -58,8 +60,7 @@`; `@@ -68,39 +69,54 @@ const osThreadAttr_t cmdLineTask_attributes = {`; *+41 more hunks*

_pending_

### `m1_csrc/m1_cli.h`

- Status: M (both-changed; -w delta +14/-1; 1 hunks)
- Disposition: **unknown**
- Fork origin: d10a6f9 refactor: remove FCC test placeholders and associated dead code (#28), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), 509c68d feat: add and harden USB DFU mode workflow (#17)
- Hunk summary (-w): `@@ -12,7 +12,20 @@`

_pending_

### `m1_csrc/m1_cli_help.c`

- Status: M (both-changed; -w delta +44/-65; 11 hunks)
- Disposition: **unknown**
- Fork origin: d10a6f9 refactor: remove FCC test placeholders and associated dead code (#28)
- Hunk summary (-w): `@@ -11,22 +11,21 @@`; `@@ -46,8 +45,7 @@ void cmd_m1_mtest_help_gpio(void);`; `@@ -68,21 +66,19 @@ BaseType_t cmd_m1_mtest_help(void)`; *+8 more hunks*

_pending_

### `m1_csrc/m1_crypto.c`

- Status: A (ours-only; -w delta +604/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -0,0 +1,604 @@`

_pending_

### `m1_csrc/m1_crypto.h`

- Status: A (ours-only; -w delta +45/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -0,0 +1,45 @@`

_pending_

### `m1_csrc/m1_display.c`

- Status: M (both-changed; -w delta +6/-5; 6 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -115,7 +115,7 @@ uint8_t m1_gui_submenu_update(const char *phmenu[], uint8_t num_items, uint8_t s`; `@@ -249,7 +249,7 @@ uint8_t m1_gui_submenu_update(const char *phmenu[], uint8_t num_items, uint8_t s`; `@@ -295,7 +295,7 @@ uint8_t m1_gui_submenu_update(const char *phmenu[], uint8_t num_items, uint8_t s`; *+3 more hunks*

_pending_

### `m1_csrc/m1_display.h`

- Status: M (both-changed; -w delta +1/-1; 1 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -162,7 +162,7 @@ void m1_gui_scr_animation(void);`

_pending_

### `m1_csrc/m1_esp32_fw_update.c`

- Status: M (both-changed; -w delta +9/-7; 7 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -141,7 +141,7 @@ void setting_esp32_exit(void)`; `@@ -264,7 +264,7 @@ void setting_esp32_image_file(void)`; `@@ -298,7 +298,7 @@ void setting_esp32_image_file(void)`; *+4 more hunks*

_pending_

### `m1_csrc/m1_esp_hosted_config.h`

- Status: M (both-changed; -w delta +21/-14; 1 hunks)
- Disposition: **unknown**
- Fork origin: d10a6f9 refactor: remove FCC test placeholders and associated dead code (#28), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -34,42 +34,49 @@`

_pending_

### `m1_csrc/m1_esp_hosted_dummy.c`

- Status: M (ours-only; -w delta +4/-30; 3 hunks)
- Disposition: **unknown**
- Fork origin: d10a6f9 refactor: remove FCC test placeholders and associated dead code (#28)
- Hunk summary (-w): `@@ -10,9 +10,9 @@`; `@@ -20,14 +20,11 @@`; `@@ -35,32 +32,9 @@ void esp32_app_init(void);`

_pending_

### `m1_csrc/m1_file_browser.c`

- Status: M (both-changed; -w delta +185/-279; 43 hunks)
- Disposition: **unknown**
- Fork origin: 16402da fix(fw-update): fix empty filename issue when selecting fw via SD card (#33), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -12,17 +12,20 @@`; `@@ -68,16 +71,13 @@ uint8_t m1_fb_check_low_freespace(void);`; `@@ -108,24 +108,19 @@ S_M1_file_browser_hdl *m1_fb_init(u8g2_t *lcd_hdl)`; *+40 more hunks*

_pending_

### `m1_csrc/m1_file_browser.h`

- Status: M (both-changed; -w delta +8/-17; 1 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -13,34 +13,25 @@`

_pending_

### `m1_csrc/m1_fusb302.c`

- Status: M (ours-only; -w delta +1/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -207,6 +207,7 @@ void pd_unsetBit(uint8_t *reg, uint8_t mask)`

_pending_

### `m1_csrc/m1_fw_update.c`

- Status: M (both-changed; -w delta +284/-168; 21 hunks)
- Disposition: **unknown**
- Fork origin: e028e9d release: v0.8.10 - DFU stability hardening and safety net, d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), 509c68d feat: add and harden USB DFU mode workflow (#17), (+1 more)
- Hunk summary (-w): `@@ -12,19 +12,26 @@`; `@@ -34,7 +41,6 @@`; `@@ -52,7 +58,9 @@ void firmware_update_exit(void);`; *+18 more hunks*

_pending_

### `m1_csrc/m1_fw_update.h`

- Status: M (ours-only; -w delta +2/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 509c68d feat: add and harden USB DFU mode workflow (#17)
- Hunk summary (-w): `@@ -20,5 +20,7 @@ void firmware_update_exit(void);`

_pending_

### `m1_csrc/m1_fw_update_bl.c`

- Status: M (both-changed; -w delta +376/-152; 40 hunks)
- Disposition: **unknown**
- Fork origin: e028e9d release: v0.8.10 - DFU stability hardening and safety net, d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -12,15 +12,17 @@`; `@@ -37,10 +39,10 @@ FW_CFG_SECTION S_M1_FW_CONFIG_t m1_fw_config = {`; `@@ -50,14 +52,19 @@ FW_CFG_SECTION S_M1_FW_CONFIG_t m1_fw_config = {`; *+37 more hunks*

_pending_

### `m1_csrc/m1_fw_update_bl.h`

- Status: M (both-changed; -w delta +49/-18; 6 hunks)
- Disposition: **unknown**
- Fork origin: d99e2f8 release: v0.8.11 - DFU visual indicator (LED), e028e9d release: v0.8.10 - DFU stability hardening and safety net, d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 16402da fix(fw-update): fix empty filename issue when selecting fw via SD card (#33), 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), (+6 more)
- Hunk summary (-w): `@@ -15,14 +15,22 @@`; `@@ -36,8 +44,9 @@`; `@@ -45,13 +54,33 @@`; *+3 more hunks*

_pending_

### `m1_csrc/m1_gpio.c`

- Status: M (both-changed; -w delta +13/-5; 5 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -165,8 +165,10 @@ void menu_gpio_exit(void)`; `@@ -190,8 +192,10 @@ void gpio_manual_control(void)`; `@@ -221,8 +225,10 @@ void gpio_3_3v_on_gpio(void)`; *+2 more hunks*

_pending_

### `m1_csrc/m1_infrared.c`

- Status: M (both-changed; -w delta +12/-78; 5 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), ad3b8c6 feat: add Universal Remote feature with Flipper IRDB support
- Hunk summary (-w): `@@ -18,6 +18,7 @@`; `@@ -208,76 +209,8 @@ S_M1_IR_Tx_States infrared_transmit(uint8_t init)`; `@@ -334,10 +267,10 @@ void infrared_learn_new_remote(void)`; *+2 more hunks*

_pending_

### `m1_csrc/m1_int_hdl.c`

- Status: M (both-changed; -w delta +99/-10; 6 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -129,7 +129,7 @@ void EXTI15_IRQHandler(void)`; `@@ -271,7 +271,10 @@ void USART1_IRQHandler(void)`; `@@ -323,6 +326,79 @@ void GPDMA1_Channel2_IRQHandler(void)`; *+3 more hunks*

_pending_

### `m1_csrc/m1_ir_universal.c`

- Status: A (ours-only; -w delta +1182/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), a55968b Infrared Universal Remote: Dashboard, Pagination, Search & Favorites (#18), 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15), ad3b8c6 feat: add Universal Remote feature with Flipper IRDB support
- Hunk summary (-w): `@@ -0,0 +1,1182 @@`

_pending_

### `m1_csrc/m1_ir_universal.h`

- Status: A (ours-only; -w delta +100/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), a55968b Infrared Universal Remote: Dashboard, Pagination, Search & Favorites (#18), ad3b8c6 feat: add Universal Remote feature with Flipper IRDB support
- Hunk summary (-w): `@@ -0,0 +1,100 @@`

_pending_

### `m1_csrc/m1_lcd.c`

- Status: M (both-changed; -w delta +2/-0; 2 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -57,6 +57,7 @@ void m1_lcd_cleardisplay(void);`; `@@ -111,6 +112,7 @@ uint8_t u8x8_byte_stm32_4wire_hw_spi(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int,`

_pending_

### `m1_csrc/m1_led_indicator.c`

- Status: M (both-changed; -w delta +5/-0; 5 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -81,6 +81,7 @@ void m1_led_func_recovery(void);`; `@@ -95,6 +96,7 @@ void m1_led_indicator_off(uint8_t *params)`; `@@ -158,6 +160,7 @@ void m1_led_fast_blink(uint8_t r_g_b, uint8_t pwm_rgb, uint8_t on_off_ms)`; *+2 more hunks*

_pending_

### `m1_csrc/m1_lib.c`

- Status: M (both-changed; -w delta +23/-179; 15 hunks)
- Disposition: **unknown**
- Fork origin: d10a6f9 refactor: remove FCC test placeholders and associated dead code (#28), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -12,20 +12,20 @@`; `@@ -34,13 +34,6 @@`; `@@ -54,131 +47,6 @@ void m1_hard_delay(uint32_t x);`; *+12 more hunks*

_pending_

### `m1_csrc/m1_log_debug.c`

- Status: M (both-changed; -w delta +161/-158; 24 hunks)
- Disposition: **unknown**
- Fork origin: 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -13,20 +13,20 @@`; `@@ -37,10 +37,10 @@`; `@@ -57,6 +57,7 @@ typedef struct {`; *+21 more hunks*

_pending_

### `m1_csrc/m1_log_debug.h`

- Status: M (both-changed; -w delta +18/-10; 2 hunks)
- Disposition: **unknown**
- Fork origin: 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29)
- Hunk summary (-w): `@@ -13,20 +13,27 @@`; `@@ -57,4 +64,5 @@ extern TaskHandle_t log_db_task_hdl;`

_pending_

### `m1_csrc/m1_low_power.c`

- Status: M (ours-only; -w delta +2/-0; 2 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -60,6 +60,7 @@ void vPortSetupTimerInterrupt( void );`; `@@ -85,6 +86,7 @@ void PreSleepProcessing(uint32_t ulExpectedIdleTime)`

_pending_

### `m1_csrc/m1_lp5814.c`

- Status: M (both-changed; -w delta +1/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -587,6 +587,7 @@ void lp5814_set_blink_timer(uint8_t r_g_b, uint16_t on_off_ms, uint8_t mode, voi`

_pending_

### `m1_csrc/m1_menu.c`

- Status: M (both-changed; -w delta +438/-362; 6 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), a55968b Infrared Universal Remote: Dashboard, Pagination, Search & Favorites (#18), 509c68d feat: add and harden USB DFU mode workflow (#17)
- Hunk summary (-w): `@@ -11,25 +11,27 @@`; `@@ -39,315 +41,375 @@`; `@@ -369,11 +431,11 @@ void subfunc_handler_task(void *param);`; *+3 more hunks*

_pending_

### `m1_csrc/m1_nfc.c`

- Status: M (both-changed; -w delta +119/-107; 58 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), a55968b Infrared Universal Remote: Dashboard, Pagination, Search & Favorites (#18)
- Hunk summary (-w): `@@ -23,7 +23,7 @@`; `@@ -104,33 +104,33 @@ typedef enum {`; `@@ -176,27 +176,27 @@ static void nfc_rename_gui_update(uint8_t param);`; *+55 more hunks*

_pending_

### `m1_csrc/m1_nfc.h`

- Status: M (both-changed; -w delta +3/-3; 1 hunks)
- Disposition: **unknown**
- Fork origin: a55968b Infrared Universal Remote: Dashboard, Pagination, Search & Favorites (#18)
- Hunk summary (-w): `@@ -13,9 +13,9 @@`

_pending_

### `m1_csrc/m1_power_ctl.c`

- Status: M (both-changed; -w delta +8/-4; 8 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -28,7 +28,7 @@`; `@@ -198,6 +198,7 @@ void power_off(void)`; `@@ -259,7 +260,7 @@ static void battery_info_gui_create(uint8_t param)`; *+5 more hunks*

_pending_

### `m1_csrc/m1_rf_spi.c`

- Status: M (both-changed; -w delta +1/-1; 1 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -235,7 +235,7 @@ int32_t m1_spi_hal_wrapper(const uint8_t * const pTxData, uint8_t * const pRxDat`

_pending_

### `m1_csrc/m1_rfid.c`

- Status: M (both-changed; -w delta +27/-13; 26 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -25,7 +25,7 @@`; `@@ -424,6 +424,7 @@ static void lfrfid_read_create(uint8_t param)`; `@@ -613,7 +614,7 @@ static void lfrfid_read_submenu_create(uint8_t param)`; *+23 more hunks*

_pending_

### `m1_csrc/m1_sdcard.c`

- Status: M (both-changed; -w delta +7/-0; 6 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -79,9 +79,11 @@ uint8_t 			sdcard_status_changed = 0;`; `@@ -798,6 +800,7 @@ static uint8_t m1_sdcard_getcardstate(void)`; `@@ -971,6 +974,7 @@ DSTATUS m1_sdcard_status(uint8_t param)`; *+3 more hunks*

_pending_

### `m1_csrc/m1_sdcard_man.c`

- Status: M (both-changed; -w delta +13/-9; 8 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -122,7 +122,8 @@ static uint8_t m1_sdm_check_low_sdcard(void)`; `@@ -176,7 +177,7 @@ static void m1_sdm_main_task(void *argument)`; `@@ -371,6 +372,7 @@ void m1_sdm_task_init(void)`; *+5 more hunks*

_pending_

### `m1_csrc/m1_sdcard_man.h`

- Status: M (ours-only; -w delta +4/-7; 1 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), 78d6b86 Fix buffer overflow build errors in m1_sub_ghz.c and m1_sdcard_man.h
- Hunk summary (-w): `@@ -42,25 +42,22 @@`

_pending_

### `m1_csrc/m1_settings.c`

- Status: M (both-changed; -w delta +6/-5; 2 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), ad3b8c6 feat: add Universal Remote feature with Flipper IRDB support
- Hunk summary (-w): `@@ -314,7 +314,7 @@ void settings_about(void)`; `@@ -326,10 +326,11 @@ static void settings_about_display_choice(uint8_t choice)`

_pending_

### `m1_csrc/m1_storage.c`

- Status: M (both-changed; -w delta +6/-6; 3 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -147,10 +147,10 @@ void storage_about(void)`; `@@ -738,7 +738,7 @@ void storage_unmount(void)`; `@@ -771,9 +771,9 @@ static void browse_gui_update(uint8_t sel_item, char *file_name)`

_pending_

### `m1_csrc/m1_sub_ghz.c`

- Status: M (both-changed; -w delta +991/-842; 123 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), d10a6f9 refactor: remove FCC test placeholders and associated dead code (#28), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), a55968b Infrared Universal Remote: Dashboard, Pagination, Search & Favorites (#18), 78d6b86 Fix buffer overflow build errors in m1_sub_ghz.c and m1_sdcard_man.h
- Hunk summary (-w): `@@ -12,27 +12,29 @@`; `@@ -42,7 +44,8 @@`; `@@ -57,6 +60,9 @@`; *+120 more hunks*

_pending_

### `m1_csrc/m1_sub_ghz.h`

- Status: M (both-changed; -w delta +12/-0; 2 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -46,6 +46,7 @@`; `@@ -139,12 +140,23 @@ void sub_ghz_replay(void);`

_pending_

### `m1_csrc/m1_sys_init.c`

- Status: M (both-changed; -w delta +3/-0; 2 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -63,9 +63,11 @@ void m1_system_init(void)`; `@@ -150,6 +152,7 @@ void m1_system_GPIO_init(void)`

_pending_

### `m1_csrc/m1_system.c`

- Status: M (both-changed; -w delta +324/-281; 25 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), 509c68d feat: add and harden USB DFU mode workflow (#17), 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15), (+1 more)
- Hunk summary (-w): `@@ -12,26 +12,30 @@`; `@@ -40,18 +44,18 @@`; `@@ -72,17 +76,17 @@ void startup_config_handler(void);`; *+22 more hunks*

_pending_

### `m1_csrc/m1_system.h`

- Status: M (both-changed; -w delta +26/-28; 8 hunks)
- Disposition: **unknown**
- Fork origin: 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), 509c68d feat: add and harden USB DFU mode workflow (#17)
- Hunk summary (-w): `@@ -11,11 +11,11 @@`; `@@ -33,9 +33,11 @@`; `@@ -53,10 +55,13 @@`; *+5 more hunks*

_pending_

### `m1_csrc/m1_tasks.c`

- Status: M (both-changed; -w delta +14/-0; 5 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -78,30 +78,39 @@ void m1_tasks_init(void)`; `@@ -160,6 +169,8 @@ void vApplicationIdleHook(void)`; `@@ -194,6 +205,7 @@ void vApplicationMallocFailedHook(void)`; *+2 more hunks*

_pending_

### `m1_csrc/m1_usb_cdc_msc.c`

- Status: M (both-changed; -w delta +132/-165; 44 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -6,27 +6,25 @@`; `@@ -63,8 +61,7 @@`; `@@ -81,6 +78,7 @@ SemaphoreHandle_t ser2usb_task_semaphore;`; *+41 more hunks*

_pending_

### `m1_csrc/m1_usb_cdc_msc.h`

- Status: M (both-changed; -w delta +1/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -64,6 +64,7 @@ extern volatile uint16_t head_usart1_dma;`

_pending_

### `m1_csrc/m1_virtual_kb.c`

- Status: M (both-changed; -w delta +205/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -1061,3 +1061,208 @@ uint8_t m1_vkbs_get_data(char *description, char *data_buffer)`

_pending_

### `m1_csrc/m1_virtual_kb.h`

- Status: M (both-changed; -w delta +1/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -15,5 +15,6 @@`

_pending_

### `m1_csrc/m1_watchdog.c`

- Status: M (both-changed; -w delta +3/-0; 2 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -82,9 +82,11 @@ void m1_wdt_init(void)`; `@@ -98,6 +100,7 @@ void m1_wdt_init(void)`

_pending_

### `m1_csrc/m1_wifi.c`

- Status: M (both-changed; -w delta +780/-141; 15 hunks)
- Disposition: **unknown**
- Fork origin: 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19), a55968b Infrared Universal Remote: Dashboard, Pagination, Search & Favorites (#18), 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -12,14 +12,17 @@`; `@@ -36,7 +39,6 @@`; `@@ -48,6 +50,8 @@ void wifi_config(void);`; *+12 more hunks*

_pending_

### `m1_csrc/m1_wifi.h`

- Status: M (both-changed; -w delta +6/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -21,4 +21,10 @@ void menu_wifi_init(void);`

_pending_

### `m1_csrc/m1_wifi_cred.c`

- Status: A (ours-only; -w delta +299/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -0,0 +1,299 @@`

_pending_

### `m1_csrc/m1_wifi_cred.h`

- Status: A (ours-only; -w delta +85/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -0,0 +1,85 @@`

_pending_

### `m1_csrc/privateprofilestring.c`

- Status: M (both-changed; -w delta +2/-2; 2 hunks)
- Disposition: **unknown**
- Fork origin: 811b346 build: complete Zero-Warning Core initiative (Phases 1-5) (#19)
- Hunk summary (-w): `@@ -572,7 +572,7 @@ bool isValidHeaderField(ParsedValue *data, const char* filetype, const char* ver`; `@@ -798,7 +798,7 @@ bool isValidHeaderField(ParsedValue *data, const char* filetype, const char* ver`

_pending_

### `tools/__pycache__/append_crc32.cpython-313.pyc`

- Status: A (ours-only; -w delta +-/--; 0 hunks)
- Disposition: **unknown**
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)

_pending_

### `tools/append_crc32.py`

- Status: A (ours-only; -w delta +166/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: 6e73998 feat(boot): implement boot-time CRC verification and failsafe fallback (#29), 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15), ad3b8c6 feat: add Universal Remote feature with Flipper IRDB support
- Hunk summary (-w): `@@ -0,0 +1,166 @@`

_pending_

### `tools/check_case_sensitivity.py`

- Status: A (ours-only; -w delta +262/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34)
- Hunk summary (-w): `@@ -0,0 +1,262 @@`

_pending_

### `tools/crc_debug.py`

- Status: A (ours-only; -w delta +105/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -0,0 +1,105 @@`

_pending_

### `tools/test_crc.py`

- Status: A (ours-only; -w delta +112/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -0,0 +1,112 @@`

_pending_

### `tools/test_crc_fix.py`

- Status: A (ours-only; -w delta +124/-0; 1 hunks)
- Disposition: **unknown**
- Fork origin: d3a0208 fix: harden stability paths for issues #22, #26, #27 (#34), 3b6c252 feat(wifi): implement WiFi Config with credential encryption (#15)
- Hunk summary (-w): `@@ -0,0 +1,124 @@`

_pending_

