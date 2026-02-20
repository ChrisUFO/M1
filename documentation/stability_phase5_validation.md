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
