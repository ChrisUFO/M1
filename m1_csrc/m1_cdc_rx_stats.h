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
* CDC_CLI_RxLostBytes(), CDC_CLI_RxHighWatermark(),
* CDC_CLI_RxBufferedBytes() and CDC_CLI_RxResetStats().
*
* Scope: only upstream's stream-buffer RX path (CDC_MODE_VCP / CDC_MODE_ESP32,
* intermediate usbRxBuffer -> h_usb_rx_streambuf). The legacy CLI input path
* (CDC_MODE_LOG_CLI) has its own small FIFO with a different (drop-oldest)
* policy and is deliberately NOT counted here.
*
* Two different things are counted, and they must not be conflated:
*
*   dropped_bytes -- host bytes REFUSED AT ARRIVAL because the stream buffer
*                    was full. Retryable: those bytes normally stay queued in
*                    usbRxBuffer and are delivered on the next drain, so this
*                    is back-pressure, NOT proof that a byte was lost.
*   lost_bytes    -- host bytes that can never be delivered: they did not fit
*                    the intermediate usbRxBuffer, or they were still inside
*                    the stream buffer when a bridge-mode exit emptied it.
*
* The legacy fork API/CLI names are kept for parity, but the legacy meaning is
* not this one: the fork's counters ran on the CDC_MODE_LOG_CLI branch and
* counted true drops there, while these observe the bridge path.
*
* M1 Project
*
*/

#ifndef M1_CDC_RX_STATS_H_
#define M1_CDC_RX_STATS_H_

#include <stdint.h>
#include <stddef.h>

/* Cumulative counters. Every field is 32-bit, naturally aligned and written
 * only by short, non-nested statements, so a concurrent 32-bit read (the CLI
 * task) can never observe a torn value on Cortex-M; no lock is needed on the
 * read side. The only read-modify-write sites are the USB interrupt callback
 * (one writer) and the task-context discard accounting that runs before a
 * mode switch empties the stream buffer. Reset is the one cross-context writer
 * and runs inside taskENTER_CRITICAL()/taskEXIT_CRITICAL() so an interrupt
 * cannot interleave a stale read-modify-write with it. Keep every field
 * `volatile`: the CLI reads them purely for diagnostics, so a compiler that
 * cannot see those reads could otherwise lose or hoist the updates. */
typedef struct
{
    volatile uint32_t dropped_bytes;    /* refused at arrival (retryable),
                                         * cumulative -- NOT proven loss      */
    volatile uint32_t lost_bytes;       /* unrecoverable bytes, cumulative:
                                         * usbRxBuffer overflow + bytes still
                                         * in the stream buffer when a bridge
                                         * mode exited and emptied it          */
    volatile uint32_t high_watermark;   /* max bytes seen pending in the stream
                                         * buffer since the last reset          */
} m1_cdc_rx_stats_t;

/* Bytes offered by the host in one CDC_Receive_FS() callback that the stream
 * buffer refused at arrival.
 *
 *   rejected_at_intake : *Len of this callback that did not even fit the
 *                        intermediate usbRxBuffer (it overflowed).
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
 * therefore counts each refused byte exactly once -- a plain
 * "usbRxBufIndex - accepted" reading would re-count the same carry-over on
 * every callback while the buffer stays full.
 *
 * This is the RETRYABLE metric: some of the bytes it counts are delivered
 * later, so it must be reported as "refused at arrival / may be retried", never
 * as bytes lost. */
static inline uint32_t m1_cdc_rx_refused_bytes(size_t rejected_at_intake,
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

/* Bytes from this callback that are provably unrecoverable: the ones that did
 * not fit the intermediate usbRxBuffer. They are never retried, so unlike the
 * refusal metric above they can never be delivered. (The same bytes are also
 * part of m1_cdc_rx_refused_bytes()'s result, which keeps the legacy
 * arithmetic; this helper is the loss-only subset.) */
static inline uint32_t m1_cdc_rx_unrecoverable_bytes(size_t rejected_at_intake)
{
    return (uint32_t)rejected_at_intake;
}

/* Fold one callback's outcome into the counters.
 *
 *   refused_bytes: result of m1_cdc_rx_refused_bytes() for this callback.
 *   lost_bytes   : result of m1_cdc_rx_unrecoverable_bytes() for this callback.
 *   buffered_now : bytes pending in the stream buffer when checked after the
 *                  send (xStreamBufferBytesAvailable()).
 *
 * The two cumulative counters are independent: lost_bytes only moves for bytes
 * that can never be delivered, so a bridge under pure back-pressure can show
 * refused climbing while lost stays at 0.
 *
 * The high watermark is monotonic (max-so-far) until m1_cdc_rx_stats_reset(),
 * so it survives drain cycles and shows the worst backpressure episode seen.
 * Interrupt-safe: three independent 32-bit read-modify-writes, no shared
 * temporaries, no loops. */
static inline void m1_cdc_rx_stats_record(m1_cdc_rx_stats_t *stats,
                                          uint32_t refused_bytes,
                                          uint32_t lost_bytes,
                                          size_t buffered_now)
{
    if (refused_bytes != 0U)
    {
        stats->dropped_bytes += refused_bytes;
    }

    if (lost_bytes != 0U)
    {
        stats->lost_bytes += lost_bytes;
    }

    if ((uint32_t)buffered_now > stats->high_watermark)
    {
        stats->high_watermark = (uint32_t)buffered_now;
    }
}

/* Count bytes that reached the bridge stream buffer but are thrown away before
 * any consumer can read them (a bridge-mode exit empties the buffer). They are
 * unrecoverable by definition, so they are added to lost_bytes only. Called
 * from task context; the caller samples the occupancy. */
static inline void m1_cdc_rx_stats_add_lost(m1_cdc_rx_stats_t *stats,
                                            uint32_t bytes)
{
    if (bytes != 0U)
    {
        stats->lost_bytes += bytes;
    }
}

/* Zero all counters. Callers must make this atomic with respect to the USB
 * interrupt callback (the device side wraps it in taskENTER_CRITICAL()) so a
 * concurrent interrupt cannot write back a pre-reset value. The high-watermark
 * comparison then restarts from 0, i.e. "since last reset" is the reported
 * window. */
static inline void m1_cdc_rx_stats_reset(m1_cdc_rx_stats_t *stats)
{
    stats->dropped_bytes = 0U;
    stats->lost_bytes = 0U;
    stats->high_watermark = 0U;
}

#endif /* M1_CDC_RX_STATS_H_ */
