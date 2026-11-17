# Legacy CLI console

Status: **Code-defined.** The implementation is the source of truth:
`Core/Src/cli_app.c` (command table, handlers, console task),
`Core/Src/FreeRTOS_CLI.c` (dispatch and the built-in `help`),
`m1_csrc/m1_cli.c` and `m1_csrc/m1_cli_help.c` (`mtest` dispatch and its ID
map), `m1_csrc/m1_log_debug.c` with `m1_csrc/m1_log_capture.h` (the `log`
snapshot) and `m1_csrc/m1_cdc_rx_stats.h` (the counters `cdcstats` prints).

## Scope

One task, `vCommandConsoleTask()`, serves both transports:

| Transport | Input path | Active when |
| --- | --- | --- |
| USB CDC | OUT endpoint → M1CP sniff → 256-byte FIFO (`LOGCLI_RX_FIFO_SIZE`, drop-oldest when full) | `m1_usbcdc_mode == CDC_MODE_LOG_CLI`, the boot default |
| USART1 debug UART | 1-byte RX interrupt → task notification | `M1_DEBUG_CLI_ENABLE` is defined (`m1_compile_cfg.h`) |

Output leaves through `cliWrite()`: directly to the CDC endpoint in
`CDC_MODE_LOG_CLI`, otherwise through the log TX path. While an M1CP manager
session is active the byte stream belongs to the protocol — console output is
gated off (`m1_logdb_usbcdc_tx_ready()`) and input is not replayed to the CLI —
so the commands below are only usable outside a session.

Line editing is local to the console: `MAX_INPUT_LENGTH` is 64 bytes per line,
CR or LF submits it, 0x08 and 0x7F delete, every received byte is echoed, and
the prompt is `\r\ncli> `. The backspace echo has two compile-time variants
(`USING_VS_CODE_TERMINAL` / `USING_OTHER_TERMINAL` at the top of `cli_app.c`);
the tree is built for the latter (PuTTY/Tera Term), and `cls` emits ANSI
escapes, so a host terminal has to interpret both.

Response size: `configCOMMAND_INT_MAX_OUTPUT_SIZE` is 200 bytes
(`Core/Inc/FreeRTOSConfig.h`) and every handler is written to fit. A handler
that needs more uses FreeRTOS+CLI's multi-call protocol — return `pdTRUE` and
be called again for the next chunk — which `log` does for its 2 KB snapshot.
`mtest`'s decade pages instead go through the log path (`M1_LOG_N`, i.e. the
same text as the log stream), because one page is far longer than 200 bytes.

Dispatch is table-driven: `xCommandList[]` is registered by
`vRegisterCLICommands()` at task start, and `help` (built into
`FreeRTOS_CLI.c`, not an entry in the table) walks that list and prints each
`pcHelpString`. A help string is bounded by the same 200-byte slot:
`prvHelpCommand()` copies it and then forces a terminator at the last byte of
the buffer, so an over-long entry cannot leave the output buffer unterminated
(which the caller's `strlen()` would run past). No entry is exempt from the
slot: the host suite asserts that every `pcHelpString` in the table is shorter
than `configCOMMAND_INT_MAX_OUTPUT_SIZE`, so a command's help line has to say
its piece in under 200 bytes and leave the detail to this page and
[USB CDC](usb-cdc.md). An unknown command answers "Command not recognized."
Every table command but `mtest` declares `cExpectedNumberOfParameters = 0`, so a
command typed with extra parameters fails the count check and the framework
calls its registered `cmd_*_help()` helper instead of the handler. For these
zero-parameter commands the helper only returns `pdFALSE` and writes nothing,
so the visible result is an empty response line followed by the prompt, not an
error message.

## Commands

| Command | Purpose | Parameters | Notes |
| --- | --- | --- | --- |
| `cls` | Clear the terminal | none | Sends the ANSI sequence on the log path, not in the response buffer |
| `mtest` | Multi-purpose hardware test entry point | `<id> [p1..p5]` | ID-decoded; see [mtest and its ID map](#mtest-and-its-id-map) |
| `log` | Snapshot of recent log output | none | Multi-call; 2 KB window; see below |
| `status` | Version, active bank, USB CDC RX state | none | Bridge-path counters, not console input |
| `version` | Numeric version plus fork tag, active bank | none | Same version fields as `status`, no counters |
| `sdcard` | Mount status and capacity | none | Probes the card only when the cached status is `SD_access_OK` |
| `wifi` | ESP32 co-processor init status | none | "Ready" / "Not ready" only |
| `battery` | Gauge level, voltage, capacity | none | Reports a failed gauge read instead of zeros |
| `reboot` | Reset the unit | none | Acknowledges, waits, then resets |
| `memory` | Heap, task count, CDC RX high watermark | none | |
| `cdcstats` | USB CDC bridge RX diagnostics | none | 197 bytes worst case; counter semantics in [USB CDC](usb-cdc.md) |
| `cdcreset` | Zero the RX counters and the high watermark | none | |
| `dfu` | Enter the ROM USB DFU bootloader | none | Acknowledges, waits, then jumps (does not return) |

## Command notes

### `log`

The handler copies the newest bytes of a 2048-byte linear history window
(`M1_LOG_CAPTURE_SIZE`) into a static snapshot at the moment the command runs
(`m1_logdb_get_recent()`), then streams that snapshot 199 bytes per response.
The snapshot is taken once per invocation and copied under the log write mutex;
nothing is printed while the mutex is held.

It is best effort in two directions:

- producers that cannot take the log write mutex immediately drop their message
  from the history (`m1_logdb_enqueue()` tries with a zero timeout), and
  interrupt-context or pre-scheduler output is never captured, so a message can
  be missing;
- if the reader cannot take the mutex within 20 ms, the command answers one
  line: "Recent log messages: none captured yet (best effort: empty or busy)" —
  the same line as for a genuinely empty window.

Output produced before the log transport became ready (before the TX ring,
mutex and log queue exist) is not in the window either. When more than 2048
bytes have been captured the header names both numbers — "(N bytes, window
2048 B)" — where N is the free-running captured total since boot, and the body
is the newest 2048 bytes, oldest first. Bytes written after the snapshot is
copied are not part of it.

### `status`

Prints the version quad with `FW_VERSION_FORK_TAG`, the active bank (1 or 2),
then the CDC RX figures — `RX refused: N B (retryable)` on one line and
`RX lost: N B, buffered: N B` on the next. These are the VCP/ESP32 bridge
counters, not measurements of console input; what they count, their 32-bit
wrap behaviour and their `cdcreset` window are defined once in
[USB CDC](usb-cdc.md) and not repeated here.

### `sdcard`

Reads `m1_sdcard_get_status()` first and calls `m1_sdcard_get_info()` (which
runs `f_getfree()`) only when the cached status is `SD_access_OK`, so an absent
or unmounted card reports the status text from `m1_sd_error_msg()` instead of
generating a probe error. A probe that fails anyway — the card is pulled, or an
I/O error hits between the cached read and the probe — returns NULL without
updating that cached status, so the two cases are reported separately: the
command prints `Status: capacity probe failed (FatFs error N)` rather than
reusing the stale `OK` text. MB figures are derived from the KB the accessor
returns; the used percentage comes from `m1_sdcard_used_percent()`
(`m1_csrc/m1_sdcard_capacity.h`), which is 64-bit so a card more than ~41 GiB
full cannot wrap it, and is 0 when the total is 0.

### `battery`

Performs four checked gauge reads — voltage, remaining capacity, full charge
capacity and state of charge (the last through
`bq27421_readStateOfCharge_percent()`) — and requires all four to succeed. Any
failure prints exactly one line and nothing else:

    Battery: gauge read failed

so a partial success is not reported at all: valid voltage and capacity are
withheld rather than shown beside a level that did not come off the gauge, and
no field is ever printed as 0 for a read that failed. On success the output is
`Battery status:` followed by `Level: N %`, `Voltage: N mV` and
`Capacity: remaining/full mAh`. The CLI does not use `bq27421_soc()`: that
accessor returns 0 when the I2C transfer fails, so it cannot tell a failed read
from an empty battery — it still serves the periodic gauge update, where the
value is refreshed on the next call.

### `reboot` and `dfu`

FreeRTOS+CLI only sends a handler's response buffer after the handler returns,
so a reset inside the handler would race that write and the acknowledgement
would never reach the host. Both handlers therefore write their line through
`cliWrite()` themselves, clear the buffer so it is not echoed twice, wait 50 ms
for the transfer, and only then act. `reboot` calls `NVIC_SystemReset()`;
`dfu` calls `firmware_update_enter_usb_dfu()`, which persists
`DEV_OP_STATUS_USB_DFU_REQUEST`, drains the log, resets, and is declared
noreturn — no code follows it.

### `memory`

Reports FreeRTOS heap free / minimum-ever-free / total / used (clamped so it
cannot underflow), the task count, and the CDC RX high watermark labelled for
what it measures. The watermark is sticky until `cdcreset`; see
[USB CDC](usb-cdc.md).

## mtest and its ID map

Syntax: `mtest` alone prints the ID map; `mtest <id>` runs that test;
`mtest <decade>` with no further parameter prints that decade's page, which
carries the per-ID parameter forms and is the authoritative syntax for every
ID listed there. Up to five parameters are parsed (`INPUT_PARAMS_MAX`), and
`mtest` is refused with "Error: some task is being executed!" while a
sub-function is running (`M1_OPERATION_MODE_SUB_FUNC_RUNNING`). Numeric
parameters go through `strtol`; range checking is per ID.

| Decade | IDs |
| --- | --- |
| 0-9 basic system | 0 virtual keyboard (not enabled), 1 buzzer play, 2 set font (fixed at build time), 3 write text, 4 clear display, 5 heap/stack telemetry, 6 reset file-browser perf counters, 9 firmware CRC corruption test (DEV-ONLY) |
| 10-19 SD card | 10 create file, 11 close file, 12 open file, 13 write file, 14 read file, 17 delete file/folder, 18 format drive, 19 list |
| 20-29 LED | 20 fast blink RGB, 21 RGB off, 22/23/24 direct R/G/B (legacy-equivalent) |
| 30-39 LCD | 30 backlight, 31 power save, 32 clear, 33 contrast, 34 regulation ratio, 35 init retry |
| 40-49 infrared | 40 transmit |
| 50-59 power | 50 off, 51 reboot, 52 `bc\|pd [reg]` read/status, 59 battery info |
| 60-69 Sub-GHz | 60 init, 61 CW TX (DEV-ONLY), 62 TX power, 63 RX, 64 frontend network, 68 RSSI, 69 radio state |
| 70-79 ESP32 | 70 init, 71 AP list, 72 AT command, 78 reset, 79 deinit |
| 80-89 GPIO | 80 external 3V3 on/off, 81 external 5V on/off |
| 90-99 NFC | 90 placeholder (does nothing), 91 mock `.m1h` capture to SD (debug), 92 live nonce harvest (debug) |

An ID that is not listed is not implemented and answers "CLI mtest: command
not defined yet!". Two details of the map are deliberate rather than
oversights: the 0-9 decade page documents IDs 0-5 only, so 6 and 9 appear in
the top-level map but not on that page; and 53/54 (fuel gauge, USB-PD) are no
longer advertised anywhere, because no handler implements them — both are
`mtest 52`.

IDs 5, 6, 22-24, 61, 64, 68, 69, 78 and 79 already exist on the adopted
upstream base (0.8.1.0) and keep their meaning; 5, 6, 61, 64, 68, 69, 78 and
79 are upstream-owned and must never be renumbered or re-used here. Only `9`
was free in the basic-system decade (7 and 8 stay unassigned), which is where
this branch re-homed the fork's destructive corruption test. 22/23/24 needed
no change at all: the base already implements direct R/G/B PWM under those IDs
with the same `lp5814_led_on_*` calls and the same missing-parameter error the
fork used, so the collision the issue listed applied to `9` only. New test IDs
must come from the gaps above.

## Dev-only build flags

Two `mtest` paths are compiled out of shipped images:

- `M1_TEST_CMDS` — `mtest 9`. Destructive by design: it erases sector 127 of
  the active bank (8 KiB, shared by the firmware configuration and the padding
  after the image), writes the metadata quadwords back with only the stored
  CRC word at `FW_CRC_ADDRESS` (`0x080FFC14` in bank 1) inverted, and makes the
  next boot fail its image-CRC check. No code or rodata is lost by the erase —
  the loaded image ends far below the sector (`0x080B2590` in the 0.9.0.0
  build) and the sector holds only the 20-byte configuration section plus the
  CRC word, the rest being `0x00` padding that the erase turns into `0xFF` —
  but the image no longer validates, so **the device will not boot until it is
  re-flashed**. Recovery is the shipped failsafe path:
  `FW CORRUPT!` plus the bounded DFU offer (OK within 5 s), or a power cycle
  while holding UP for the DFU strap followed by re-flashing
  `<artifact>_wCRC.bin`. The CRC word is the target because the failsafe
  compares the computed image CRC against it; the config magic is untouched
  because `m1_boot_classify_fault()` prefers metadata faults over a CRC
  mismatch and would mask the stage under test. Erase and program address the
  same physical bank while "active bank" means "the bank mapped at
  `0x08000000`", which is the invariant this image runs under: the erase
  selects `FLASH_BANK_1/2` from `bl_get_active_bank()` (a physical bank select,
  `FLASH_CR_BKSEL`), while `HAL_FLASH_Program()` writes through the
  CPU-visible `FW_CRC_ADDRESS`, which the option-byte bank swap maps to
  whichever bank is active — the same address
  `bl_crc_validate_active_bank()` reads the stored word from at boot. Run the
  test on both banks deliberately as hardware validation rather than inferring
  it from that reasoning alone.
- `M1_SUBGHZ_CW_TEST_TX` — `mtest 61`, raw continuous-wave transmit. It keys
  the transmitter directly and does not pass the regional TX-permission gate,
  so it is dev-only for a different reason (regulatory, not destructive).

Neither macro is defined by any build file or source file in this tree; a
build that defines one is a bench build. Without them `mtest 9` answers
"Firmware corruption test not available in this build" and `mtest 61` answers
"CW test TX not available in this build".

## Verification status

Registration, the help strings, the bound on every handler write, the
terminator in the built-in `help` path and the absence of both dev-only guards
are covered by the host suite (`tools/m1cp_host_test/test_cli_commands.c`):
every `pcHelpString` must be shorter than the
`configCOMMAND_INT_MAX_OUTPUT_SIZE` it is sent through (no exceptions), and the
guard check walks the project source trees (`Core/`, `m1_csrc/`, `USB/` and the
other project directories — nested `CMakeLists.txt` and `.cmake` files
included) plus the root build files, asserting that none of them defines
`M1_TEST_CMDS` or `M1_SUBGHZ_CW_TEST_TX`. It knows the forms this project
actually uses: `#define` and `-D` in C sources, and in build/config files the
bare macro name that `target_compile_definitions()` /
`add_compile_definitions()` takes (comments stripped first, so a mention in a
comment does not trip it). That is a source and build-configuration statement,
**not** an image check: no built ELF is inspected by the suite. Confirming a
shipped image is free of both paths is a manual step — `arm-none-eabi-strings`
on the ELF must show the two `#else` notices ("Firmware corruption test not
available in this build", "CW test TX not available in this build") and none of
the guarded strings. The log window itself is covered by `test_log_capture`,
the `sdcard` percentage arithmetic by `test_sdcard_usage`, and `make test` also
compiles the guarded `mtest 9` path (`-DM1_TEST_CMDS -fsyntax-only` on
`m1_csrc/m1_cli.c`, with the include set taken from the firmware's own compile
line) so it cannot rot — that step needs `arm-none-eabi-gcc` on `PATH` and
prints `SKIP` with the reason on a host without it. No on-device verification
is claimed here: "CDC counters become non-zero during a real stress transfer"
is hardware-gated by issue #63 (milestone "Post-merge stabilization"), and the
`mtest 9` boot recovery path — failsafe banner, bounded DFU offer, strap
recovery — is hardware-gated with the rest of the boot-recovery milestone
(issue #41).

## Related

- [USB CDC](usb-cdc.md) — RX counter arithmetic, 2047-byte capacity, #63 gate
- [Firmware image and update](firmware-update.md) — CRC word, boot failsafe,
  DFU entry
- [Build and debug](../build-and-debug.md) — toolchain, build command and
  artifact sizes

## Source files

`Core/Src/cli_app.c`, `Core/Src/FreeRTOS_CLI.c`, `m1_csrc/m1_cli.c`,
`m1_csrc/m1_cli_help.c`, `m1_csrc/m1_log_debug.c`, `m1_csrc/m1_log_capture.h`,
`m1_csrc/m1_sdcard_capacity.h`, `m1_csrc/m1_bq27421.c` (the checked
state-of-charge accessor `battery` uses), `m1_csrc/m1_cdc_rx_stats.h`.
Behaviour is code-defined: the handlers are the
specification, and this page only describes what they do at this commit. The
`help` listing and the `mtest` pages are generated from the same table and
functions, so where this page and the console disagree, the console is right.
