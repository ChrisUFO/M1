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
with `RXSTREAMBUF_USB_SIZE` = 2048 bytes (`m1_csrc/m1_usb_cdc_msc.h`). Each USB
OUT callback (`CDC_Receive_FS()`, interrupt context) does three things:

1. copy the received bytes onto the end of the intermediate `usbRxBuffer`
   (`USB_RX_BUF_SIZE`); if it no longer fits, those bytes are lost
   (`DEBUG_ovf_cnt`),
2. send as much of `usbRxBuffer` as the stream buffer has room for
   (`xStreamBufferSpacesAvailable()` / `xStreamBufferSendFromISR()`), keeping
   any remainder queued at the front for the next callback,
3. if the stream buffer was full, set `usbcdc_rx_paused` so the OUT endpoint is
   not re-armed until the `usb2ser` task drains it.

Nothing in the RX path branches on the counters below: they are **diagnostic
only** and adding them does not change buffering, back-pressure decisions,
copies or drop behaviour.

## RX diagnostics counters

Declared in `usbd_cdc_if.h`, defined in `usbd_cdc_if.c`, updated only from the
USB interrupt callback, read from the CLI task.

| Accessor | Meaning |
| --- | --- |
| `CDC_CLI_RxDroppedBytes()` | Cumulative count of host bytes that **never reached the stream buffer**: refused because it was full in that callback (partial fit or no room at all), plus bytes lost when the intermediate `usbRxBuffer` overflowed under the same back-pressure. |
| `CDC_CLI_RxHighWatermark()` | Maximum number of bytes observed waiting in the stream buffer, sampled after each callback's send. Monotonic since the last reset, so it shows the worst back-pressure episode seen (useful capacity is 2048 bytes). |
| `CDC_CLI_RxBufferedBytes()` | Bytes pending in the stream buffer right now (0 if it has not been created). |

Counting semantics, precisely:

- A byte is counted **once, at arrival**, when the callback cannot accept it
  into the stream buffer. Bytes that stay queued in `usbRxBuffer` and are
  retried on later callbacks are **not** re-counted; if a retry later succeeds
  (the task drained the buffer), the byte had already been counted at arrival,
  so the counter measures *host traffic that met a full stream buffer*, not
  proof that the byte was lost forever. Only the `usbRxBuffer` overflow case is
  unrecoverable loss.
- Because the send serves `usbRxBuffer` from the front, carry-over from earlier
  callbacks is served first; only `sent - pendingBefore` bytes of a callback can
  be new arrivals. The shared arithmetic lives in
  `m1_csrc/m1_cdc_rx_stats.h` (`m1_cdc_rx_lost_bytes()`,
  `m1_cdc_rx_stats_record()`), which is host-tested by
  `tools/m1cp_host_test/test_cdc_rx_stats.c`.
- Both counters are `volatile uint32_t` and are written with single 32-bit
  statements, so a task-context read cannot see a torn value.
  `CDC_CLI_RxResetStats()` zeroes them inside
  `taskENTER_CRITICAL()`/`taskEXIT_CRITICAL()`, so the RX interrupt cannot
  interleave a pre-reset read-modify-write and resurrect a stale value.
- The reported window is "since boot or since the last reset".
- Counters are cumulative 32-bit and are not saturated: at ~1 MB/s of
  back-pressure they would wrap after roughly an hour of sustained drops.
  `cdcreset` starts a fresh window.

## Reading the counters

Over the console:

```text
cli> cdcstats
USB CLI RX Diagnostics:
  Dropped bytes: 0
  High watermark: 0 bytes
  Buffered now: 0 bytes
cli> cdcreset
USB CLI RX diagnostics reset.
```

`cdcstats` is read-only; `cdcreset` zeroes the drop counter and the high
watermark and confirms. The same text (and the legacy `CDC_CLI_Rx*` names) is
shared with the fork's original diagnostics, so existing notes and procedures
still apply.

Interpretation:

- `Dropped bytes` growing means the host sends faster than the `usb2ser`
  consumer drains — a real back-pressure/loss signal on the bridge path.
- `High watermark` approaching 2048 means the stream buffer filled up; it stays
  at its maximum until `cdcreset`, so it is the value to quote when reporting a
  stall even after the buffer drained.
- `Buffered now` is a live gauge, useful before/after a drain to confirm the
  consumer is keeping up.

**Verification status:** host arithmetic and source seams are covered by
`test_cdc_rx_stats`; "counters become non-zero during a real CDC stress
transfer on hardware" is hardware-gated (issue #44 hardware gate, milestone
"Post-merge stabilization") and has not been recorded yet.

## Related

- [Firmware image and update](firmware-update.md)
- Console command registration: `Core/Src/cli_app.c` (`cdcstats`, `cdcreset`),
  `m1_csrc/m1_cli.c`.
