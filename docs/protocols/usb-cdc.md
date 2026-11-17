# USB CDC

Status: **Code-defined.** The implementation is the source of truth:
`USB/Class/CDC/Src/usbd_cdc_if.c`, `USB/Class/CDC/Inc/usbd_cdc_if.h`,
`m1_csrc/m1_usb_cdc_msc.c`.

## Scope

The device exposes one USB CDC function, used in two roles selected by
`m1_usbcdc_mode`:

| Mode | Role | RX path |
| --- | --- | --- |
| `CDC_MODE_LOG_CLI` | Legacy console (`cli_app.c`, `m1_cli.c`) over the CDC endpoint | OUT endpoint → `m1cp_rx_from_isr()` sniff → 256-byte CLI FIFO → CLI task |
| `CDC_MODE_VCP`, `CDC_MODE_ESP32` | Byte bridge to the ESP32 host link | OUT endpoint → `UserRxBufferFS` → `usbRxBuffer` → `h_usb_rx_streambuf` → `usb2ser` task |

This page documents the RX diagnostics counters for the stream-buffer
(bridge) path. The console path has its own small FIFO with a different
drop policy (it discards the oldest byte when full) and is not covered by
these counters.

## Stream-buffer RX path and back-pressure

`h_usb_rx_streambuf` is created by `xStreamBufferCreate(RXSTREAMBUF_USB_SIZE, 1)`
with `RXSTREAMBUF_USB_SIZE` = 2048 bytes (`m1_csrc/m1_usb_cdc_msc.h`), so the
usable capacity is **2047 bytes**: FreeRTOS keeps one byte free so that
head == tail can only mean "empty" (`xStreamBufferSpacesAvailable()` subtracts
one). Each USB OUT callback (`CDC_Receive_FS()`, interrupt context) does three
things:

1. copy the received bytes onto the end of the intermediate `usbRxBuffer`
   (`USB_RX_BUF_SIZE`); if it no longer fits, those bytes are lost
   (`DEBUG_ovf_cnt`, and counted in `CDC_CLI_RxLostBytes()`),
2. send as much of `usbRxBuffer` as the stream buffer has room for
   (`xStreamBufferSpacesAvailable()` / `xStreamBufferSendFromISR()`), keeping
   any remainder queued at the front for the next callback,
3. if the stream buffer was full, set `usbcdc_rx_paused` so the OUT endpoint is
   not re-armed until the `usb2ser` task drains it.

Nothing in the RX path branches on the counters below: they are **diagnostic
only** and adding them does not change buffering, back-pressure decisions,
copies or drop behaviour.

## RX diagnostics counters

Declared in `usbd_cdc_if.h`, defined in `usbd_cdc_if.c`, updated from the USB
interrupt callback (plus the bridge-mode exit path), read from the CLI task.

The names are the legacy fork names (API/CLI parity), but they do **not** measure
what the fork's counters measured: the fork maintained `CDC_CLI_Rx*` on the
`CDC_MODE_LOG_CLI` branch and counted true drops of console input, whereas these
observe the bridge path and separate retryable refusals from real loss. Notes
and procedures written for the legacy counters do not carry over.

| Accessor | Meaning |
| --- | --- |
| `CDC_CLI_RxDroppedBytes()` | Cumulative host bytes **refused at arrival** on the bridge path: the callback could not hand them to the stream buffer because it was full (partial fit or no room at all), plus the bytes that overflowed the intermediate `usbRxBuffer`. **Retryable** back-pressure, not proof of loss: bytes that stay queued in `usbRxBuffer` are delivered on a later drain. |
| `CDC_CLI_RxLostBytes()` | Cumulative host bytes that **can never be delivered**: the `usbRxBuffer` overflow component above, plus bytes still pending in the stream buffer when a bridge-mode exit empties it (`CDC_CLI_RxDiscardPendingBytes()`). Loss-only counter: it stays at 0 while the bridge merely back-pressures. |
| `CDC_CLI_RxHighWatermark()` | Maximum number of bytes observed waiting in the stream buffer, sampled after each callback's send. Sticky since the last reset (a drain does not lower it), so it shows the worst back-pressure episode seen. Useful capacity is `RXSTREAMBUF_USB_SIZE - 1` = **2047 bytes**. |
| `CDC_CLI_RxBufferedBytes()` | Bytes pending in the stream buffer right now (0 before USB init). A **sampled gauge, not an exact value**: it reads the buffer's volatile head/tail pair without a critical section, so a concurrent drain can make it off by one burst. Never use it as a synchronisation signal. |

Counting semantics, precisely:

- A byte is counted **once, at arrival**, when the callback cannot accept it
  into the stream buffer. Bytes that stay queued in `usbRxBuffer` and are
  retried on later callbacks are **not** re-counted; if a retry later succeeds
  (the task drained the buffer), the byte had already been counted at arrival.
  `CDC_CLI_RxDroppedBytes()` therefore measures *host traffic that met a full
  stream buffer*, not loss. The loss-only part is `CDC_CLI_RxLostBytes()`:
  everything that never fit `usbRxBuffer` (never retried) plus whatever a
  bridge-mode exit throws away. Under pure back-pressure the refusal counter
  climbs while the loss counter stays at 0.
- Because the send serves `usbRxBuffer` from the front, carry-over from earlier
  callbacks is served first; only `sent - pendingBefore` bytes of a callback can
  be new arrivals. The shared arithmetic lives in
  `m1_csrc/m1_cdc_rx_stats.h` (`m1_cdc_rx_refused_bytes()`,
  `m1_cdc_rx_unrecoverable_bytes()`, `m1_cdc_rx_stats_record()`), which is
  host-tested by `tools/m1cp_host_test/test_cdc_rx_stats.c`.
- All counters are `volatile uint32_t` and are written with single 32-bit
  statements, so a task-context read cannot see a torn value.
  `CDC_CLI_RxResetStats()` zeroes them inside
  `taskENTER_CRITICAL()`/`taskEXIT_CRITICAL()`, so the RX interrupt cannot
  interleave a pre-reset read-modify-write and resurrect a stale value.
- The reported window is "since boot or since the last reset".
- Counters are cumulative 32-bit and are not saturated: at ~1 MB/s they would
  wrap after roughly an hour of sustained load. `cdcreset` starts a fresh
  window, which is the only way to bound a long run.

## Reading the counters

Over the console:

```text
cli> cdcstats
USB CDC bridge RX diagnostics:
  Refused at arrival: 0 (retryable)
  Lost (unrecoverable): 0
  High watermark: 0 bytes (sticky)
  Buffered now: 0 (sampled)
cli> cdcreset
USB CDC bridge RX diagnostics reset.
```

`cdcstats` is read-only; `cdcreset` zeroes both cumulative counters and the high
watermark and confirms. The commands themselves, with the rest of the console
table, are documented in [the CLI console reference](cli.md). The output is
qualified on purpose: the legacy names (`CDC_CLI_RxDroppedBytes()`, "dropped
bytes") belonged to a different path and a different metric, so reading the new
values as the legacy ones would be wrong.

Interpretation:

- `Refused at arrival` growing while `Lost (unrecoverable)` stays at 0 is normal
  back-pressure: the host sends faster than the `usb2ser` consumer drains, bytes
  were delayed, none were lost. Do not read it as data loss.
- `Lost (unrecoverable)` growing is real loss: the intermediate `usbRxBuffer`
  overflowed, or a mode switch discarded bytes that were still pending.
- `High watermark` approaching 2047 bytes means the stream buffer filled up; it
  stays at its maximum until `cdcreset`, so it is the value to quote when
  reporting a stall even after the buffer drained.
- `Buffered now` is a sampled gauge, useful before/after a drain to confirm the
  consumer is keeping up; it can be off by one burst while the consumer drains.

**Verification status:** host arithmetic and source seams are covered by
`test_cdc_rx_stats`; "counters become non-zero during a real CDC stress
transfer on hardware" is hardware-gated by issue #63 (milestone "Post-merge
stabilization") and has not been recorded yet. When it runs, record
`CDC_CLI_RxLostBytes()` alongside the refusal counter: a non-zero refusal
counter alone proves back-pressure, not loss.

## Related

- [Firmware image and update](firmware-update.md)
- Console command registration: `Core/Src/cli_app.c` (`cdcstats`, `cdcreset`),
  `m1_csrc/m1_cli.c`.
