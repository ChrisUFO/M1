/* See COPYING.txt for license details. */

/*
*
* m1_cdc_rx_stats.h
*
* USB CDC RX diagnostics accounting (issue #44, part A): pure, host-testable
* helpers.
*
* Everything in this header is plain integer arithmetic on caller-supplied
* counts -- no HAL, stream-buffer or RTOS dependency -- so
* tools/m1cp_host_test/test_cdc_rx_stats.c can exercise the arithmetic natively.
* The device side lives in CDC_Receive_FS() (USB/Class/CDC/Src/usbd_cdc_if.c),
* which owns the counters, calls these helpers from interrupt context and
* exposes the values through CDC_CLI_RxDroppedBytes(),
* CDC_CLI_RxHighWatermark(), CDC_CLI_RxBufferedBytes() and
* CDC_CLI_RxResetStats().
*
* Scope: only upstream's stream-buffer RX path (CDC_MODE_VCP / CDC_MODE_ESP32,
* intermediate usbRxBuffer -> h_usb_rx_streambuf). The legacy CLI input path
* (CDC_MODE_LOG_CLI) has its own small FIFO with a different (drop-oldest)
* policy and is deliberately NOT counted here.
*
* M1 Project
*
*/

#ifndef M1_CDC_RX_STATS_H_
#define M1_CDC_RX_STATS_H_

#include <stdint.h>
#include <stddef.h>

/* Cumulative counters. Both fields are 32-bit, naturally aligned and written
 * only by short, non-nested statements, so a concurrent 32-bit read (the CLI
 * task) can never observe a torn value on Cortex-M; no lock is needed on the
 * read side. The only read-modify-write site is the USB interrupt callback
 * itself (one writer). Reset is the one cross-context writer and runs inside
 * taskENTER_CRITICAL()/taskEXIT_CRITICAL() so an interrupt cannot interleave a
 * stale read-modify-write with it. Keep both fields `volatile`: the CLI reads
 * them purely for diagnostics, so a compiler that cannot see those reads could
 * otherwise lose or hoist the updates. */
typedef struct
{
    volatile uint32_t dropped_bytes;    /* never-recoverable bytes, cumulative   */
    volatile uint32_t high_watermark;   /* max bytes seen pending in the stream
                                         * buffer since the last reset          */
} m1_cdc_rx_stats_t;

/* Bytes offered by the host in one CDC_Receive_FS() callback that will never
 * reach the RX stream buffer.
 *
 *   rejected_at_intake : *Len of this callback that did not even fit the
 *                        intermediate usbRxBuffer (it overflowed) -- already
 *                        lost before any send attempt.
 *   appended           : *Len of this callback that was copied into
 *                        usbRxBuffer (the bytes a send attempt can serve).
 *   accepted_from_buffer: bytes the stream buffer actually accepted from
 *                        usbRxBuffer in this callback (0 when it was full or
 *                        when there was nothing pending).
 *   pending_before     : usbRxBuffer bytes already waiting when the callback
 *                        started, i.e. carry-over from earlier callbacks.
 *
 * Carry-over matters: the stream buffer is drained from the FRONT of
 * usbRxBuffer, so the bytes accepted this callback are the OLDEST ones. Only
 * `accepted_from_buffer - pending_before` (clamped at 0) can have been taken
 * from the bytes that arrived now; everything else of `appended` stays queued
 * in usbRxBuffer and is retried on the next callback. Counting per arrival
 * therefore counts each lost byte exactly once -- a plain
 * "usbRxBufIndex - accepted" reading would re-count the same carry-over on
 * every callback while the buffer stays full. */
static inline uint32_t m1_cdc_rx_lost_bytes(size_t rejected_at_intake,
                                            size_t appended,
                                            size_t accepted_from_buffer,
                                            size_t pending_before)
{
    size_t accepted_of_new;

    if (accepted_from_buffer > pending_before)
    {
        accepted_of_new = accepted_from_buffer - pending_before;
    }
    else
    {
        accepted_of_new = 0U;   /* the send only served earlier carry-over */
    }

    if (accepted_of_new > appended)
    {
        accepted_of_new = appended;
    }

    return (uint32_t)(rejected_at_intake + (appended - accepted_of_new));
}

/* Fold one callback's outcome into the counters.
 *
 *   lost_bytes  : result of m1_cdc_rx_lost_bytes() for this callback.
 *   buffered_now: bytes pending in the stream buffer when checked after the
 *                 send (xStreamBufferBytesAvailable()).
 *
 * The high watermark is monotonic (max-so-far) until m1_cdc_rx_stats_reset(),
 * so it survives drain cycles and shows the worst backpressure episode seen.
 * Interrupt-safe: two independent 32-bit read-modify-writes, no shared
 * temporaries, no loops. */
static inline void m1_cdc_rx_stats_record(m1_cdc_rx_stats_t *stats,
                                          uint32_t lost_bytes,
                                          size_t buffered_now)
{
    if (lost_bytes != 0U)
    {
        stats->dropped_bytes += lost_bytes;
    }

    if ((uint32_t)buffered_now > stats->high_watermark)
    {
        stats->high_watermark = (uint32_t)buffered_now;
    }
}

/* Zero both counters. Callers must make this atomic with respect to the USB
 * interrupt callback (the device side wraps it in taskENTER_CRITICAL()) so a
 * concurrent interrupt cannot write back a pre-reset value. The high-watermark
 * comparison then restarts from 0, i.e. "since last reset" is the reported
 * window. */
static inline void m1_cdc_rx_stats_reset(m1_cdc_rx_stats_t *stats)
{
    stats->dropped_bytes = 0U;
    stats->high_watermark = 0U;
}

#endif /* M1_CDC_RX_STATS_H_ */
