/* Host test for the USB CDC RX diagnostics counters (issue #44, part A).
 *
 * The device RX path (CDC_Receive_FS in USB/Class/CDC/Src/usbd_cdc_if.c) feeds
 * host bytes through UserRxBufferFS -> usbRxBuffer -> h_usb_rx_streambuf. That
 * path can back-pressure: a callback can hand only part of usbRxBuffer to the
 * stream buffer (or nothing at all), and under sustained pressure the
 * intermediate usbRxBuffer overflows. Before this unit none of that was
 * observable over the CLI.
 *
 * The counters are diagnostic only (nothing branches on them), ISR-updated,
 * and their arithmetic is deliberately pure, so this test pins:
 *
 *   1. the arithmetic itself (m1_cdc_rx_stats.h):
 *        - full buffer: everything offered is counted as refused,
 *        - partial fit: only the shortfall is counted,
 *        - exact fit and empty callback: nothing counted,
 *        - intermediate-buffer overflow: the bytes that never fit are counted,
 *        - carry-over: bytes still queued from an earlier callback are NOT
 *          re-counted when the next callback cannot accept them,
 *        - clamping: a send can never make the result underflow,
 *        - the loss-only helper counts intake rejections and nothing else, so
 *          pure back-pressure never moves the loss counter.
 *   2. a replay of the device control flow over a small modelled stream buffer
 *      (same ordering as CDC_Receive_FS: pending snapshot, copy or overflow,
 *      spaces-available, conditional send, memmove-equivalent accounting,
 *      record), asserting the per-arrival identity delivered+refused == offered
 *      for every packet, that no byte counts twice, and that the refusal and
 *      loss counters move independently. The modelled stream buffer declares
 *      xLength = MODEL_STREAM_LENGTH and exposes MODEL_STREAM_LENGTH - 1 usable
 *      bytes, which is FreeRTOS's real arithmetic.
 *   3. high-watermark monotonicity across drain cycles, buffered-now, reset
 *      semantics and the bridge-mode discard accounting.
 *   4. source seams on usbd_cdc_if.c / m1_log_debug.c (that code is HAL/RTOS
 *      code and cannot be linked here): counters are `volatile` and
 *      ISR-updated at the send site, reset is wrapped in
 *      taskENTER_CRITICAL()/taskEXIT_CRITICAL() OUTSIDE the interrupt callback,
 *      the accessors are out-of-line (so the values must be materialised),
 *      buffered-now guards a NULL stream buffer, the mode-switch exit uses the
 *      counting discard helper instead of a bare xStreamBufferReset(), and
 *      CDC_Receive_FS gained no blocking call.
 *
 * Run either from the repository root or from this directory:
 *   ./tools/m1cp_host_test/test_cdc_rx_stats
 *   make -C tools/m1cp_host_test test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m1_cdc_rx_stats.h"

static int count = 0, fails = 0;
static void ok(int cond, const char *what) {
    count++;
    if (cond) { printf("  ok   %s\n", what); }
    else       { printf("  FAIL %s\n", what); fails++; }
}

/* strstr() that ignores `//` comments: the RX callback keeps commented-out
 * debug calls, which must not be mistaken for live code. */
static int has_code_token(const char *src, const char *tok) {
    const char *line = src;
    size_t tok_len = strlen(tok);

    while (*line != '\0') {
        const char *eol = strchr(line, '\n');
        const char *cmt = strstr(line, "//");
        size_t line_len = (eol != NULL) ? (size_t)(eol - line) : strlen(line);
        const char *p;

        if (cmt != NULL && (size_t)(cmt - line) < line_len) line_len = (size_t)(cmt - line);

        for (p = line; (size_t)(p - line) + tok_len <= line_len; p++) {
            if (memcmp(p, tok, tok_len) == 0) return 1;
        }

        if (eol == NULL) break;
        line = eol + 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Model of the device RX path: a small stream buffer + the intermediate     */
/* usbRxBuffer, driven in exactly the order CDC_Receive_FS drives them.      */
/* ------------------------------------------------------------------------- */

#define MODEL_STREAM_LENGTH 129U  /* modelled xLength (FreeRTOS buffer size)   */
#define MODEL_STREAM_CAP    (MODEL_STREAM_LENGTH - 1U) /* usable: xLength - 1 */
#define MODEL_INTERMEDIATE 128U   /* modelled usbRxBuffer (USB_RX_BUF_SIZE)  */

typedef struct {
    size_t used;                  /* bytes pending in the stream buffer     */
    size_t idx;                   /* bytes pending in usbRxBuffer          */
    size_t offered;               /* bytes handed over by the host          */
    size_t delivered_new;         /* arriving bytes accepted (this arrival) */
    size_t delivered_total;       /* bytes accepted out of usbRxBuffer      */
    size_t overflow;              /* *Len that did not fit usbRxBuffer      */
    m1_cdc_rx_stats_t stats;
} model_t;

/* Same arithmetic as xStreamBufferSpacesAvailable(): the usable capacity is
 * xLength - 1, so a full buffer still reports one byte less than xLength. */
static size_t model_spaces(const model_t *m) { return MODEL_STREAM_CAP - m->used; }

/* One CDC OUT callback carrying `len` bytes, mirroring CDC_Receive_FS. */
static void model_callback(model_t *m, size_t len) {
    size_t pending_before = m->idx;      /* carry-over snapshot (pre-copy)     */
    size_t appended = 0U, rejected = 0U, free_space, sent = 0U, refused;

    m->offered += len;

    if ((m->idx + len) < MODEL_INTERMEDIATE) {
        m->idx += len;                   /* memcpy into usbRxBuffer            */
        appended = len;
    } else {
        rejected = len;                  /* usbRxBuffer overflow: lost already */
        m->overflow += len;
    }

    free_space = model_spaces(m);

    if (free_space > 0U && m->idx > 0U) {
        size_t want = (m->idx < free_space) ? m->idx : free_space;
        /* xStreamBufferSendFromISR() accepts up to `want`, then the driver
         * drops it off the front of usbRxBuffer. */
        sent = want;
        m->used += sent;
        m->idx -= sent;
        m->delivered_total += sent;
        if (sent > 0U) {
            /* arriving bytes accepted at this callback (oldest served first) */
            size_t accepted_new = (sent > pending_before) ? (sent - pending_before) : 0U;
            if (accepted_new > appended) accepted_new = appended;
            m->delivered_new += accepted_new;
        }
    }

    refused = m1_cdc_rx_refused_bytes(rejected, appended, sent, pending_before);
    m1_cdc_rx_stats_record(&m->stats, refused,
                           m1_cdc_rx_unrecoverable_bytes(rejected), m->used);
}

static void model_drain(model_t *m, size_t n) {
    if (n > m->used) n = m->used;
    m->used -= n;
}

int main(void) {
    /* ---- 1. arithmetic unit cases ---------------------------------------- */
    printf("arithmetic\n");
    ok(m1_cdc_rx_refused_bytes(0, 0, 0, 0) == 0, "empty callback loses nothing");
    ok(m1_cdc_rx_refused_bytes(0, 64, 0, 0) == 64, "full stream buffer: whole packet refused");
    ok(m1_cdc_rx_refused_bytes(0, 64, 32, 0) == 32, "partial fit: only the shortfall refused");
    ok(m1_cdc_rx_refused_bytes(0, 64, 64, 0) == 0, "exact fit: nothing refused");
    ok(m1_cdc_rx_refused_bytes(64, 0, 0, 0) == 64, "intermediate-buffer overflow counted");
    ok(m1_cdc_rx_refused_bytes(64, 0, 0, 32) == 64, "overflow with carry-over still counts the loss");
    /* Carry-over: 32 bytes were already offered and refused (or queued) by an
     * earlier callback; this callback adds 64 and the stream buffer accepts
     * nothing. Only the 64 new bytes may be counted. */
    ok(m1_cdc_rx_refused_bytes(0, 64, 0, 32) == 64, "queued carry-over is not re-counted");
    /* The send serves the carry-over first; accepting 32 = exactly the old
     * bytes leaves the whole new packet refused. */
    ok(m1_cdc_rx_refused_bytes(0, 64, 32, 32) == 64, "send serving carry-over only still refuses new bytes");
    /* Accepting more than the carry-over means new bytes got through. */
    ok(m1_cdc_rx_refused_bytes(0, 64, 96, 32) == 0, "send covering carry-over and all new bytes loses nothing");
    ok(m1_cdc_rx_refused_bytes(0, 10, 96, 32) == 0, "accepted count clamps at the new bytes (no underflow)");
    ok(m1_cdc_rx_refused_bytes(5, 10, 96, 32) == 5, "clamping keeps intake rejections counted");
    /* The loss-only helper counts just the bytes that never fit usbRxBuffer:
     * nothing is lost when the stream buffer merely refused (retryable), so it
     * is independent of the refusal arithmetic above. */
    ok(m1_cdc_rx_unrecoverable_bytes(0) == 0,
       "no intake rejection: the loss-only helper counts nothing");
    ok(m1_cdc_rx_unrecoverable_bytes(64) == 64,
       "intake rejection is unrecoverable loss");
    ok(m1_cdc_rx_unrecoverable_bytes(10) == 10,
       "loss-only helper is a straight count of the rejected bytes");

    /* ---- 2. replay of the device control flow ---------------------------- */
    printf("replay: stream buffer fills and stays full\n");
    {
        model_t m;
        size_t i;
        memset(&m, 0, sizeof(m));

        /* Two packets fill the modelled stream buffer exactly. */
        model_callback(&m, 64U);
        model_callback(&m, 64U);
        ok(m.idx == 0U && m.used == MODEL_STREAM_CAP,
           "two packets fill the stream buffer, none dropped");
        ok(m.used == MODEL_STREAM_LENGTH - 1U,
           "the model fills to xLength - 1 bytes (FreeRTOS keeps one byte free)");
        ok(m.stats.dropped_bytes == 0U, "no drops while the buffer had room");
        ok(m.stats.high_watermark == MODEL_STREAM_CAP,
           "high watermark reaches usable capacity (xLength - 1)");

        /* Third packet: stream buffer full -> all 64 bytes refused but kept in
         * usbRxBuffer for a retry. */
        model_callback(&m, 64U);
        ok(m.idx == 64U && m.used == MODEL_STREAM_CAP, "refused packet stays queued in usbRxBuffer");
        ok(m.stats.dropped_bytes == 64U, "full-buffer packet counted once");

        /* Two more packets: usbRxBuffer now overflows (64 + 64 == cap). */
        for (i = 0U; i < 2U; i++) model_callback(&m, 64U);
        ok(m.overflow == 128U, "later packets overflow the intermediate buffer");
        ok(m.stats.dropped_bytes == 64U + 128U,
           "overflow bytes added, queued packet not re-counted");
        ok(m.stats.dropped_bytes <= m.offered, "drops never exceed bytes offered");

        /* Per-arrival identity over the whole run: every offered byte is either
         * accepted in its own arrival or counted once as refused then (the
         * bytes still queued in usbRxBuffer were already counted as refused at
         * arrival; they are not counted again when they are retried). */
        ok(m.delivered_new + m.stats.dropped_bytes == m.offered,
           "delivered + refused == offered (no double counting)");

        /* Drain: the queued packet is then accepted and NOT re-counted. */
        {
            uint32_t before = m.stats.dropped_bytes;
            model_drain(&m, MODEL_STREAM_CAP);
            model_callback(&m, 0U);      /* consumer drained, no new host bytes */
            ok(m.stats.dropped_bytes == before, "drain does not change the drop counter");
        }
    }

    printf("replay: partial fit\n");
    {
        model_t m;
        memset(&m, 0, sizeof(m));

        model_drain(&m, 0U);            /* no-op, keeps the flow explicit */
        m.used = MODEL_STREAM_CAP - 36U; /* 36 bytes of room left */
        model_callback(&m, 64U);
        ok(m.stats.dropped_bytes == 28U, "partial fit refuses exactly the 28-byte shortfall");
        ok(m.idx == 28U, "the 28 refused bytes stay queued for retry");
        ok(m.stats.high_watermark == MODEL_STREAM_CAP, "partial fit drives the high watermark to full");

        /* Retry succeeds after a drain: queued bytes delivered, still no
         * additional drop. */
        {
            uint32_t before = m.stats.dropped_bytes;
            model_drain(&m, MODEL_STREAM_CAP);
            model_callback(&m, 64U);
            ok(m.stats.dropped_bytes == before, "successful retry after drain adds no drop");
            ok(m.idx == 0U, "retry clears usbRxBuffer");
            ok(m.delivered_new + m.stats.dropped_bytes == m.offered,
               "all offered bytes accounted for once the queue is empty");
        }
    }

    printf("high watermark, buffered now, reset\n");
    {
        model_t m;
        memset(&m, 0, sizeof(m));

        model_callback(&m, 64U);
        ok(m.stats.high_watermark == 64U, "high watermark tracks pending bytes");
        model_callback(&m, 64U);
        ok(m.stats.high_watermark == MODEL_STREAM_CAP, "high watermark grows to the maximum seen");
        model_drain(&m, MODEL_STREAM_CAP);
        model_callback(&m, 32U);
        ok(m.stats.high_watermark == MODEL_STREAM_CAP,
           "high watermark is monotonic across drain cycles (not lowered)");
        ok(m.used == 32U, "buffered-now is the current pending level, not the watermark");

        /* Reset clears the counters and restarts the window. */
        m1_cdc_rx_stats_reset(&m.stats);
        ok(m.stats.dropped_bytes == 0U && m.stats.lost_bytes == 0U &&
           m.stats.high_watermark == 0U,
           "reset zeroes refusal, loss and watermark counters");
        model_drain(&m, m.used);
        model_callback(&m, 64U);
        ok(m.stats.high_watermark == 64U,
           "after reset the watermark restarts from the current level");
        ok(m.stats.dropped_bytes == 0U, "after reset the drop counter restarts at zero");
    }

    /* ---- 2b. loss counter: only unrecoverable bytes ---------------------- */
    printf("loss counter (unrecoverable only)\n");
    {
        model_t m;
        size_t i;
        memset(&m, 0, sizeof(m));

        /* Pure back-pressure: two packets fill the stream buffer, a third is
         * refused at arrival but stays queued in usbRxBuffer. Nothing is lost. */
        model_callback(&m, 64U);
        model_callback(&m, 64U);
        model_callback(&m, 32U);
        ok(m.stats.dropped_bytes == 32U && m.idx == 32U,
           "refused packet counted and still queued for retry");
        ok(m.stats.lost_bytes == 0U,
           "pure back-pressure loses nothing: loss counter stays 0");

        /* The retry succeeds after a drain: delivered, and neither counter
         * moves -- the refusal already charged at arrival stays charged. */
        model_drain(&m, MODEL_STREAM_CAP);
        model_callback(&m, 0U);
        ok(m.idx == 0U && m.stats.dropped_bytes == 32U && m.stats.lost_bytes == 0U,
           "successful retry leaves both counters unchanged (no loss, no re-count)");

        /* Consumer stalled, stream buffer full: the first packet still fits
         * usbRxBuffer (refused, retryable), the next ones overflow it and are
         * gone for good. */
        m.used = MODEL_STREAM_CAP;
        for (i = 0U; i < 3U; i++) model_callback(&m, 64U);
        ok(m.overflow == MODEL_INTERMEDIATE,
           "the modelled intermediate buffer overflowed by two packets");
        ok(m.stats.lost_bytes == MODEL_INTERMEDIATE,
           "usbRxBuffer overflow increments the loss counter only");
        ok(m.stats.dropped_bytes == 32U + 192U,
           "refusal counter keeps its arithmetic (refused plus overflow)");
        ok(m.stats.lost_bytes < m.stats.dropped_bytes,
           "loss is a proper subset of the refusal counter, never a second copy");
        ok(m.delivered_new + m.stats.dropped_bytes == m.offered,
           "delivered + refused == offered still holds with the loss counter");

        /* Bridge-mode exit: bytes accepted into the stream buffer but never
         * read by the consumer are unrecoverable too, and are counted by the
         * discard helper, not by the RX callback. */
        {
            uint32_t lost_before = m.stats.lost_bytes;
            uint32_t dropped_before = m.stats.dropped_bytes;
            m1_cdc_rx_stats_add_lost(&m.stats, (uint32_t)m.used);
            ok(m.stats.lost_bytes == lost_before + (uint32_t)m.used,
               "bridge-mode discard is added to the loss counter");
            ok(m.stats.dropped_bytes == dropped_before,
               "bridge-mode discard does not touch the refusal counter");
            m1_cdc_rx_stats_add_lost(&m.stats, 0U);
            ok(m.stats.lost_bytes == lost_before + (uint32_t)m.used,
               "a zero-byte discard changes nothing");
        }
    }

    /* ---- 3. source seams on the device file ------------------------------ */
    printf("seams: USB/Class/CDC/Src/usbd_cdc_if.c\n");
    {
        static const char *paths[] = {
            "USB/Class/CDC/Src/usbd_cdc_if.c",
            "../../USB/Class/CDC/Src/usbd_cdc_if.c",
            "USB/Class/CDC/Inc/usbd_cdc_if.h",
            "../../USB/Class/CDC/Inc/usbd_cdc_if.h",
            "m1_csrc/m1_cdc_rx_stats.h",
            "../../m1_csrc/m1_cdc_rx_stats.h",
            "m1_csrc/m1_log_debug.c",
            "../../m1_csrc/m1_log_debug.c"
        };
        const char *body_sig = "static int8_t CDC_Receive_FS(uint8_t *Buf, uint32_t *Len)";
        char *src = NULL;
        char *body = NULL;
        FILE *fp;

        /* usbd_cdc_if.c (cwd-independent: repo root or this directory). */
        for (size_t i = 0U; (i < 2U) && (src == NULL); i++) {
            fp = fopen(paths[i], "rb");
            if (fp != NULL) {
                long n;
                if (fseek(fp, 0, SEEK_END) == 0 && (n = ftell(fp)) >= 0 &&
                    fseek(fp, 0, SEEK_SET) == 0) {
                    src = malloc((size_t)n + 1U);
                    if (src != NULL &&
                        fread(src, 1U, (size_t)n, fp) == (size_t)n) {
                        src[n] = '\0';
                    } else {
                        free(src);
                        src = NULL;
                    }
                }
                fclose(fp);
            }
        }
        ok(src != NULL, "read USB/Class/CDC/Src/usbd_cdc_if.c");
        if (src != NULL) {
            /* Bound the CDC_Receive_FS definition body (skip the prototype). */
            const char *search_from = src;
            for (;;) {
                const char *start = strstr(search_from, body_sig);
                const char *after, *open_brace, *p, *end = NULL;
                int depth = 0;
                if (start == NULL) break;
                after = start + strlen(body_sig);
                while (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n') after++;
                if (*after == ';') { search_from = start + strlen(body_sig); continue; }
                open_brace = strchr(start, '{');
                if (open_brace == NULL) break;
                for (p = open_brace; *p != '\0'; p++) {
                    if (*p == '{') depth++;
                    else if (*p == '}' && --depth == 0) { end = p; break; }
                }
                if (end == NULL) break;
                body = malloc((size_t)(end - start) + 2U);
                if (body != NULL) {
                    memcpy(body, start, (size_t)(end - start) + 1U);
                    body[(size_t)(end - start) + 1U] = '\0';
                }
                break;
            }
            ok(body != NULL, "located the CDC_Receive_FS definition");
            if (body != NULL) {
                const char *record = strstr(body, "m1_cdc_rx_stats_record(&cdc_rx_stats");
                const char *refused = strstr(body, "m1_cdc_rx_refused_bytes(rejectedBytes");
                const char *lost = strstr(body, "m1_cdc_rx_unrecoverable_bytes(rejectedBytes");
                const char *spaces = strstr(body, "freeSpace = xStreamBufferSpacesAvailable(");
                const char *send = strstr(body, "xStreamBufferSendFromISR(");
                ok(record != NULL && refused != NULL && lost != NULL,
                   "ISR site calls the shared accounting helpers (refusal and loss)");
                ok(record != NULL && spaces != NULL && send != NULL &&
                   strstr(body, "pendingBefore") != NULL &&
                   strstr(body, "appendedBytes") != NULL &&
                   strstr(body, "rejectedBytes") != NULL,
                   "accounting inputs are the real send-site counts");
                ok(strstr(body, "m1_usbcdc_mode") != NULL &&
                   strstr(body, "usbRxBufIndex") != NULL,
                   "RX path structure still present (stream-buffer bridge branch)");
                /* The data path must not gain a blocking/unbounded call, and a
                 * critical section must not appear in interrupt context. */
                ok(strstr(body, "vTaskDelay") == NULL,
                   "no vTaskDelay in the RX interrupt callback");
                ok(strstr(body, "xStreamBufferSend(") == NULL &&
                   strstr(body, "xStreamBufferReceive(") == NULL,
                   "no blocking stream-buffer API in the RX callback");
                ok(strstr(body, "taskENTER_CRITICAL") == NULL,
                   "no critical section inside the RX interrupt callback");
                ok(has_code_token(body, "printf(") == 0, "no printf in the RX callback");
            }
        }
        free(body);
        free(src);

        /* Accessors: out-of-line, volatile fields, NULL-guarded buffered-now. */
        src = NULL;
        for (size_t i = 2U; (i < 4U) && (src == NULL); i++) {
            fp = fopen(paths[i], "rb");
            if (fp != NULL) {
                long n;
                if (fseek(fp, 0, SEEK_END) == 0 && (n = ftell(fp)) >= 0 &&
                    fseek(fp, 0, SEEK_SET) == 0) {
                    src = malloc((size_t)n + 1U);
                    if (src != NULL &&
                        fread(src, 1U, (size_t)n, fp) == (size_t)n) {
                        src[n] = '\0';
                    } else {
                        free(src);
                        src = NULL;
                    }
                }
                fclose(fp);
            }
        }
        ok(src != NULL, "read USB/Class/CDC/Inc/usbd_cdc_if.h");
        if (src != NULL) {
            ok(strstr(src, "uint32_t CDC_CLI_RxDroppedBytes(void);") != NULL &&
               strstr(src, "uint32_t CDC_CLI_RxLostBytes(void);") != NULL &&
               strstr(src, "uint32_t CDC_CLI_RxHighWatermark(void);") != NULL &&
               strstr(src, "uint32_t CDC_CLI_RxBufferedBytes(void);") != NULL &&
               strstr(src, "void CDC_CLI_RxResetStats(void);") != NULL,
               "legacy CDC_CLI_Rx* accessor names are declared");
            ok(strstr(src, "void CDC_CLI_RxDiscardPendingBytes(void);") != NULL,
               "bridge-mode discard accounting accessor is declared");
            free(src);
        }

        /* Header: counters are volatile 32-bit, so an optimiser that cannot see
         * the CLI reads cannot lose the ISR updates. */
        src = NULL;
        for (size_t i = 4U; (i < 6U) && (src == NULL); i++) {
            fp = fopen(paths[i], "rb");
            if (fp != NULL) {
                long n;
                if (fseek(fp, 0, SEEK_END) == 0 && (n = ftell(fp)) >= 0 &&
                    fseek(fp, 0, SEEK_SET) == 0) {
                    src = malloc((size_t)n + 1U);
                    if (src != NULL &&
                        fread(src, 1U, (size_t)n, fp) == (size_t)n) {
                        src[n] = '\0';
                    } else {
                        free(src);
                        src = NULL;
                    }
                }
                fclose(fp);
            }
        }
        ok(src != NULL, "read m1_csrc/m1_cdc_rx_stats.h (cwd-independent)");
        if (src != NULL) {
            ok(strstr(src, "volatile uint32_t dropped_bytes;") != NULL,
               "dropped_bytes is a volatile 32-bit counter");
            ok(strstr(src, "volatile uint32_t lost_bytes;") != NULL,
               "lost_bytes is a volatile 32-bit counter");
            ok(strstr(src, "volatile uint32_t high_watermark;") != NULL,
               "high_watermark is a volatile 32-bit counter");
            free(src);
        }

        /* Reset must be atomic w.r.t. the RX interrupt, i.e. in the task-side
         * accessor between taskENTER_CRITICAL() and taskEXIT_CRITICAL(). */
        src = NULL;
        fp = fopen(paths[0], "rb");
        if (fp == NULL) fp = fopen(paths[1], "rb");
        if (fp != NULL) {
            long n;
            if (fseek(fp, 0, SEEK_END) == 0 && (n = ftell(fp)) >= 0 &&
                fseek(fp, 0, SEEK_SET) == 0) {
                src = malloc((size_t)n + 1U);
                if (src != NULL &&
                    fread(src, 1U, (size_t)n, fp) == (size_t)n) {
                    src[n] = '\0';
                } else {
                    free(src);
                    src = NULL;
                }
            }
            fclose(fp);
        }
        ok(src != NULL, "re-read usbd_cdc_if.c for the reset seam");
        if (src != NULL) {
            const char *reset = strstr(src, "void CDC_CLI_RxResetStats(void)");
            ok(reset != NULL, "CDC_CLI_RxResetStats() is defined out-of-line");
            if (reset != NULL) {
                const char *enter = strstr(reset, "taskENTER_CRITICAL();");
                const char *zero = strstr(reset, "m1_cdc_rx_stats_reset(&cdc_rx_stats);");
                const char *leave = strstr(reset, "taskEXIT_CRITICAL();");
                ok(enter != NULL && zero != NULL && leave != NULL &&
                   enter < zero && zero < leave,
                   "reset zeroes all counters inside a critical section");
            }
            ok(strstr(src, "void CDC_CLI_RxDiscardPendingBytes(void)") != NULL &&
               strstr(src, "m1_cdc_rx_stats_add_lost(&cdc_rx_stats") != NULL &&
               strstr(src, "xStreamBufferReset(h_usb_rx_streambuf)") != NULL,
               "bridge-mode discard counts the pending bytes before resetting");
            ok(strstr(src, "static m1_cdc_rx_stats_t cdc_rx_stats;") != NULL,
               "counters have one file-scope owner (no shadow copies)");
            ok(strstr(src, "#pragma GCC optimize(\"O0\")") != NULL,
               "file keeps the O0 pragma (RX callback stays debuggable)");
            free(src);
        }

        /* Mode-switch exit: the bridge stream buffer is emptied through the
         * counting helper, not with a bare reset, so bytes still queued there
         * are visible in the loss counter. */
        src = NULL;
        for (size_t i = 6U; (i < 8U) && (src == NULL); i++) {
            fp = fopen(paths[i], "rb");
            if (fp != NULL) {
                long n;
                if (fseek(fp, 0, SEEK_END) == 0 && (n = ftell(fp)) >= 0 &&
                    fseek(fp, 0, SEEK_SET) == 0) {
                    src = malloc((size_t)n + 1U);
                    if (src != NULL &&
                        fread(src, 1U, (size_t)n, fp) == (size_t)n) {
                        src[n] = '\0';
                    } else {
                        free(src);
                        src = NULL;
                    }
                }
                fclose(fp);
            }
        }
        ok(src != NULL, "read m1_csrc/m1_log_debug.c (cwd-independent)");
        if (src != NULL) {
            ok(strstr(src, "CDC_CLI_RxDiscardPendingBytes();") != NULL,
               "bridge-mode exit uses the counting discard helper");
            ok(strstr(src, "xStreamBufferReset(h_usb_rx_streambuf)") == NULL,
               "no bare reset of the bridge stream buffer discards bytes uncounted");
            free(src);
        }
    }

    printf("\n%s (%d/%d)\n", fails == 0 ? "ALL PASS" : "FAILURES", count - fails, count);
    fflush(stdout);
    return fails ? 1 : 0;
}
