> **⚠ FORK-BASE (0.8.11) VALIDATION RECORD — CARRIED FOR PROVENANCE.**
> The matrix below was executed against fork firmware `legacy/v0.8.11`
> (origin commit d3a0208, PR #34). Hardware re-validation on the current
> upstream base is **PENDING** — the Sub-GHz Rx DMA ingestion was re-landed
> as a *port* (adapted to this base's EXTI/nIRQ + TIM1_CC architecture) and
> must be re-run on hardware before these results can be claimed for it.
> See "Port notes" at the bottom for the carried / adapted / superseded
> hunk accounting of the port.

# Stability Validation Matrix (Issues #22, #26, #27)

## Scope
This guide defines reproducible validation for:
- Issue #27: USB CDC CLI RX race/overwrite prevention
- Issue #26: IR Universal long-term heap stability
- Issue #22: Sub-GHz ISR queue pressure hardening

## Test Environment Template
- Firmware binary: `distribution/M1_v*.bin`
- Board: M1 (STM32H573VIT6)
- Toolchain: ARM GNU Toolchain 14.2+
- Build host: Windows + CMake preset `gcc-14_2_build-release`
- USB terminal: 9600 baud, 8N1, no flow control
- SD card: FAT32, IR database present at `0:/IR`
- RF setup: Sub-GHz source capable of high edge-rate traffic/noise

## Build and Script Validation
1. Build firmware:
   - `cmake --preset gcc-14_2_build-release`
   - `cmake --build out/build/gcc-14_2_build-release`
2. Run CRC script checks:
   - `python tools/test_crc.py`
   - `python tools/test_crc_fix.py`

Expected:
- Build completes with no `m1_core` warnings/errors.
- `test_crc.py` reports CRC trailer matches STM32 word-based CRC.
- `test_crc_fix.py` passes deterministic/word-vs-byte/end-to-end checks.

## Issue #27 Validation (USB CLI)
### Happy Path
1. Connect USB CDC CLI.
2. Send bursts of commands rapidly (paste multi-line command batch repeatedly).
3. Run `cdcstats` and `status`.

Expected:
- Commands are not truncated/reordered.
- No missing keystrokes under normal bursts.
- `cdcstats` shows bounded buffering and stable counters.

### Failure/Stress Path
1. Intentionally overrun input with sustained high-rate paste bursts.
2. Run `cdcstats` before/after.
3. Run `cdcreset`, repeat a smaller burst, re-check `cdcstats`.

Expected:
- System remains responsive.
- Any drops are visible in `Dropped bytes` and do not corrupt parser state.
- Counter reset works and subsequent measurement is isolated.

## Issue #26 Validation (IR Universal)
### Happy Path
1. Open `Infrared -> Universal Remote`.
2. Run Search with common terms; open and transmit commands.
3. Toggle favorites, open Recents/Favorites repeatedly.

Expected:
- Consistent feedback overlays for success/failure states.
- Search/list/browse remains responsive.
- No crashes or corruption during repeated navigation.

### Soak/Failure Path
1. Repeat Search/Browse/Favorite operations for 15+ minutes.
2. Remove SD card while inside browser/search.

Expected:
- Graceful `SD Removed` handling and clean return path.
- No progressive memory degradation symptoms.

## Issue #22 Validation (Sub-GHz)
### Happy Path
1. Enter Sub-GHz Record and start capture.
2. Verify UI shows capture state and queue-pressure counters.
3. Record, stop, save/replay.

Expected:
- Capture operates continuously.
- UI indicates live/flush state and throttle/overrun counters.
- Replay/save flow remains functional.

### Stress Path
1. Expose receiver to high edge-rate/noisy source.
2. Observe `Thr`/`Ovr` counters during sustained load.
3. Ensure capture can still stop/save cleanly.

Expected:
- ISR does not flood queue uncontrollably.
- Overrun behavior is observable without lockups.
- Device remains stable and interactive.

## PR Reporting Checklist
- Hardware used (board rev, SD card, RF source)
- Exact command/script outputs for build + CRC tests
- USB CLI burst scenario and `cdcstats` snapshots
- IR soak duration and outcomes
- Sub-GHz stress duration and counter observations

## Port notes (upstream-base re-land, issue #52)

The #22 Sub-GHz work above was re-landed from `legacy/v0.8.11` (origin
d3a0208) as a **port**, because the adopted upstream base rewrote the
Sub-GHz flow around an EXTI/nIRQ packet path plus a per-edge `TIM1_CC`
capture ISR, while the fork's capture ran entirely over GPDMA1 Channel 3.
Per-hunk accounting (durable copy of the PR-2 body table):

| Fork hunk (d3a0208) | Outcome on this base |
|---|---|
| `m1_sub_ghz.h`: `SUBGHZ_RX_DMA_BLOCK_QUEUE_LEN`, `hdma_subghz_rx` extern, counter/block-q externs, `subghz_rx_dma_restart_from_isr()` proto | **Re-landed verbatim** (legacy :49, :158) |
| `m1_int_hdl.c`: `GPDMA1_Channel3_IRQHandler` (79 lines, deferred RTOS context switching) | **Re-landed** (legacy :326ff) after `GPDMA1_Channel2_IRQHandler`, fork placement. Adaptation: queue-send failure additionally calls `m1_isr_drop_note_subghz_rx()` (this base's ISR-drop diagnostics convention). Deferred-context-switch property preserved (xQueueSendFromISR + portYIELD_FROM_ISR only; no blocking RTOS calls in the hot path). |
| `m1_int_hdl.c`: USART1 ISR notify-value packing fix | **Re-landed** (pre-adjudicated as likely-superseded; research found the pre-fix site still present on this base at `m1_int_hdl.c` USART1_IRQHandler, so the fix landed rather than being superseded). Note `TASK_NOTIFY_USART1 == 0x0` on this base, so the packed value is numerically unchanged today; the packing is now correct by construction if notify ids diverge. |
| `m1_int_hdl.c`: TIM1_CC early-return under record flag | **Re-landed** as a defensive guard (this base arms CC1 interrupts for the scanner; HAL_TIM_IC_Start_DMA does not enable CC1IE, so the guard protects against a stale enable from a prior scan session). |
| `m1_sub_ghz.c`: DMA channel init + NVIC in `sub_ghz_rx_init` | **Re-landed in fork form** (legacy :1854ff) — GPDMA1 Ch3 on `GPDMA1_REQUEST_TIM1_CH1`, LINKDMA to `TIM_DMA_ID_CC1`, NPRIV attributes, IRQ priority `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`/sub 5. |
| `m1_sub_ghz.c`: `subghz_rx_dma_restart_from_isr` | **Re-landed verbatim** (legacy :392) |
| `m1_sub_ghz.c`: `subghz_rx_dma_process_blocks` + `_invalidate_block` | **Re-landed** (legacy :759-851) with two adaptations: ring-full latches `subghz_capture_overflow` (this base freezes + aborts the recording on overflow; the fork dropped samples and continued) and the SD drain accumulates into `subghz_record_flushed_samples` (this base's GUI counter). |
| `m1_sub_ghz.c`: `sub_ghz_rx_start`/`_pause` switch to Start_DMA/Stop_DMA | **Adapted**: this base shares `sub_ghz_rx_start` with the scanner (per-edge TIM1_CC path, "route2"), so record RAW arms the DMA engine while scan keeps `HAL_TIM_IC_Start_IT`; a `subghz_rx_dma_armed` latch pairs pause with the armed engine, and record start sets `subghz_record_mode_flag` *before* arming (fork order was reversed because its rx_start was unconditionally DMA-driven). |
| `m1_sub_ghz.c`: record GUI `H/T/D` counter lines | **Adapted**: counters surface in the existing `RECDBG` telemetry log (`ht=/tc=/drop=/thr=`) instead of new u8g2 lines — this base's ACTIVE screen is the RSSI graph render (different layout); no host-verifiable visual check exists for new on-screen lines. |
| `m1_sub_ghz.c`: counter resets at record start + ring-buffers deinit | **Re-landed** (legacy :973ff, :2182ff) |
| `m1_int_hdl.c`: USART1 `HAL_UART_TxCpltCallback` semaphore variant | **Superseded**: not part of the #52 ledger carry for this base (this base's callback already resets `tx_cptl_usartx` unconditionally); no target for the fork's `#if 0` semaphore path. |

Hardware re-validation on this base (the matrix above) is the recorded
follow-up; no host-runnable ISR/DMA test exists, so build + code review +
the static symbol checks recorded in the PR are the interim gates.
