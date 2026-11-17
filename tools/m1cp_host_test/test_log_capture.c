/* Host test for the log history window (issue #44, part B).
 *
 * m1_csrc/m1_log_capture.h is the pure, HAL-free window behind the CLI `log`
 * command: the device side feeds every log byte through m1_log_capture_put()
 * from m1_logdb_enqueue() and the `log` handler reads it back with
 * m1_log_capture_peek(). Because the header has no HAL/RTOS/FatFs dependency it
 * links here unmodified, so this test pins the properties the legacy fork
 * implementation got wrong:
 *
 *   1. content shorter than the window comes back verbatim;
 *   2. wrapped content returns the NEWEST bytes in their original order (the
 *      fork's ring froze at 2047 bytes because its reader rewound the tail, and
 *      its capture length came from the TX-ring return value);
 *   3. a single put() larger than the window keeps its tail, and the length is
 *      clamped to the window, not truncated to the TX-ring result;
 *   4. the absolute `wr` counter wraps safely: no index leaves [0, SIZE), the
 *      newest bytes survive and the report degrades conservatively;
 *   5. the empty state needs no init (wr == 0) and peeks as 0 + "";
 *   6. peek() is non-destructive (the CLI can re-read, and re-entry after
 *      m1_logdb_init() must not see a half-consumed window);
 *   7. max_len is honoured with a NUL terminator.
 *
 * Run either from the repository root or from this directory:
 *   ./tools/m1cp_host_test/test_log_capture
 *   make -C tools/m1cp_host_test test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m1_log_capture.h"

static int count = 0, fails = 0;
static void ok(int cond, const char *what) {
    count++;
    if (cond) { printf("  ok   %s\n", what); }
    else       { printf("  FAIL %s\n", what); fails++; }
}

/* Deterministic byte pattern so "which bytes survived" is checkable. */
static uint8_t pattern_byte(size_t i) {
    return (uint8_t)((i * 37U + 11U) & 0xFFU);
}

int main(void) {
    char out[M1_LOG_CAPTURE_SIZE + 1];

    /* ---- 1. empty state: no init needed, wr == 0 -------------------------- */
    printf("empty state\n");
    {
        static S_M1_LogCapture c;   /* zero-initialised == the required state */
        uint32_t total = 12345U;

        memset(out, 'x', sizeof(out));
        ok(m1_log_capture_peek(&c, out, (uint16_t)sizeof(out), &total) == 0U,
           "peek on a zeroed window returns 0 bytes");
        ok(out[0] == '\0', "peek on a zeroed window produces a terminated empty string");
        ok(total == 0U, "total_written is 0 for the empty window");
        ok(c.wr == 0U, "the empty state is wr == 0 (no init call required)");
    }

    /* ---- 2. shorter than the window --------------------------------------- */
    printf("content shorter than the window\n");
    {
        static S_M1_LogCapture c;
        const char *msg = "hello log\r\n";
        uint32_t total = 0;
        uint16_t n;

        memset(&c, 0, sizeof(c));
        m1_log_capture_put(&c, (const uint8_t *)msg, (uint16_t)strlen(msg));
        n = m1_log_capture_peek(&c, out, (uint16_t)sizeof(out), &total);
        ok(n == (uint16_t)strlen(msg), "returns exactly what was written");
        ok(strcmp(out, msg) == 0, "content is the written text, in order");
        ok(total == (uint32_t)strlen(msg), "total_written tracks the bytes written");
    }

    /* ---- 3. wrapped content keeps the newest bytes in order --------------- */
    printf("wrapped content\n");
    {
        static S_M1_LogCapture c;
        uint8_t in[3000];
        uint32_t total = 0;
        uint16_t n;
        size_t i;
        int order_ok = 1;

        memset(&c, 0, sizeof(c));
        for (i = 0U; i < sizeof(in); i++) in[i] = pattern_byte(i);
        m1_log_capture_put(&c, in, (uint16_t)sizeof(in));

        n = m1_log_capture_peek(&c, out, (uint16_t)sizeof(out), &total);
        ok(n == (uint16_t)M1_LOG_CAPTURE_SIZE, "a wrapped window returns the full window");
        ok(total == (uint32_t)sizeof(in), "total_written is the free-running byte count");
        ok(total > (uint32_t)n, "a wrapped window reports total > window length");
        for (i = 0U; i < (size_t)n; i++) {
            if ((uint8_t)out[i] != pattern_byte(sizeof(in) - (size_t)n + i)) {
                order_ok = 0;
                break;
            }
        }
        ok(order_ok, "wrapped content is the newest bytes, oldest-first, in order");
        ok((uint8_t)out[0] == pattern_byte(sizeof(in) - (size_t)n),
           "the first byte returned is the oldest byte still in the window");
        ok((uint8_t)out[n - 1U] == pattern_byte(sizeof(in) - 1U),
           "the last byte returned is the newest byte written");
    }

    /* ---- 4. one put() larger than the window: tail kept, length clamped --- */
    printf("put larger than the window\n");
    {
        static S_M1_LogCapture c;
        uint8_t in[5000];
        uint32_t total = 0;
        uint16_t n;
        size_t i;
        int tail_ok = 1;

        memset(&c, 0, sizeof(c));
        for (i = 0U; i < sizeof(in); i++) in[i] = pattern_byte(i);
        m1_log_capture_put(&c, in, (uint16_t)sizeof(in));

        n = m1_log_capture_peek(&c, out, (uint16_t)sizeof(out), &total);
        ok(n == (uint16_t)M1_LOG_CAPTURE_SIZE, "the returned length clamps to the window");
        for (i = 0U; i < (size_t)n; i++) {
            if ((uint8_t)out[i] != pattern_byte(sizeof(in) - (size_t)n + i)) {
                tail_ok = 0;
                break;
            }
        }
        ok(tail_ok, "only the tail of an oversized put survives, in order");

        /* Repeated full-window puts must never freeze the window. */
        m1_log_capture_put(&c, in, (uint16_t)sizeof(in));
        m1_log_capture_put(&c, in, (uint16_t)sizeof(in));
        n = m1_log_capture_peek(&c, out, (uint16_t)sizeof(out), &total);
        ok(n == (uint16_t)M1_LOG_CAPTURE_SIZE,
           "the window keeps accepting data after many full-window writes");
        ok((uint8_t)out[n - 1U] == pattern_byte(sizeof(in) - 1U),
           "the newest byte is still the one just written");
    }

    /* ---- 5. wr wrap-around arithmetic ------------------------------------ */
    printf("wr wrap-around\n");
    {
        static S_M1_LogCapture c;
        uint32_t total = 0;
        uint16_t n;

        memset(&c, 0, sizeof(c));
        /* Simulate a cursor that is about to cross 2^32. UINT32_MAX - 3 + 5
         * wraps to 1; the 5 bytes land at 2044..2047 then 0. */
        c.wr = 0xFFFFFFFCU;
        m1_log_capture_put(&c, (const uint8_t *)"abcde", 5U);
        ok(c.wr == 1U, "wr wraps around instead of overflowing (unsigned wrap is defined)");
        n = m1_log_capture_peek(&c, out, (uint16_t)sizeof(out), &total);
        ok(total == 1U, "after the wrap total_written is the post-wrap count");
        ok(n == 1U && out[0] == 'e',
           "peek stays in bounds and returns the newest byte after a wrap");

        /* Simulate sitting right on the wrap boundary. */
        memset(&c, 0, sizeof(c));
        c.wr = 0xFFFFFFFFU;
        m1_log_capture_put(&c, (const uint8_t *)"0123456789", 10U);
        ok(c.wr == 9U, "a 10-byte put on the boundary wraps to 9");
        n = m1_log_capture_peek(&c, out, (uint16_t)sizeof(out), &total);
        ok(n == 9U && strcmp(out, "123456789") == 0,
           "the newest 9 bytes come back in order across the wrap");

        /* And a real (non-artificial) run: many small puts totalling > SIZE. */
        memset(&c, 0, sizeof(c));
        for (uint32_t i = 0U; i < 100U; i++) {
            m1_log_capture_put(&c, (const uint8_t *)"0123456789", 10U);
        }
        ok(c.wr == 1000U, "wr is a plain running byte count");
        n = m1_log_capture_peek(&c, out, (uint16_t)sizeof(out), &total);
        ok(n == 1000U, "below the window size the whole history is returned");
    }

    /* ---- 6. peek is non-destructive -------------------------------------- */
    printf("peek is non-destructive\n");
    {
        static S_M1_LogCapture c;
        char again[64];
        uint32_t total_a = 0, total_b = 0;
        uint16_t a, b;

        memset(&c, 0, sizeof(c));
        m1_log_capture_put(&c, (const uint8_t *)"repeatable", 10U);
        a = m1_log_capture_peek(&c, out, (uint16_t)sizeof(out), &total_a);
        b = m1_log_capture_peek(&c, again, (uint16_t)sizeof(again), &total_b);
        ok(a == 10U && b == 10U && strcmp(out, "repeatable") == 0 &&
           strcmp(again, "repeatable") == 0,
           "a second peek returns the same history (the reader never rewinds)");
        ok(total_a == total_b, "total_written is unchanged by peeking");

        /* New data after a peek is still appended to the same history. */
        m1_log_capture_put(&c, (const uint8_t *)"!", 1U);
        (void)m1_log_capture_peek(&c, again, (uint16_t)sizeof(again), &total_b);
        ok(strcmp(again, "repeatable!") == 0,
           "the window keeps growing after a read (no frozen ring)");
    }

    /* ---- 7. max_len handling --------------------------------------------- */
    printf("max_len handling\n");
    {
        static S_M1_LogCapture c;
        uint8_t in[100];
        uint32_t total = 0;
        uint16_t n;
        size_t i;

        memset(&c, 0, sizeof(c));
        for (i = 0U; i < sizeof(in); i++) in[i] = pattern_byte(i);
        m1_log_capture_put(&c, in, (uint16_t)sizeof(in));

        n = m1_log_capture_peek(&c, out, 11U, &total);
        ok(n == 10U, "max_len leaves room for the terminator");
        ok(out[10] == '\0', "the result is always NUL-terminated");
        ok((uint8_t)out[9] == pattern_byte(sizeof(in) - 1U),
           "a truncated peek still ends with the newest byte");
        ok(total == (uint32_t)sizeof(in),
           "total_written reports the whole capture even when truncated");

        n = m1_log_capture_peek(&c, out, 1U, &total);
        ok(n == 0U && out[0] == '\0', "max_len == 1 yields an empty terminated string");
        n = m1_log_capture_peek(&c, out, 0U, &total);
        ok(n == 0U, "max_len == 0 copies nothing");
        n = m1_log_capture_peek(NULL, out, (uint16_t)sizeof(out), &total);
        ok(n == 0U && total == 0U, "a NULL window reports nothing");
        n = m1_log_capture_peek(&c, NULL, (uint16_t)sizeof(out), &total);
        ok(n == 0U, "a NULL output buffer reports nothing");
        m1_log_capture_put(NULL, in, 10U);
        m1_log_capture_put(&c, NULL, 10U);
        ok(1, "NULL put() arguments are ignored, not dereferenced");
    }

    printf("\n%s (%d/%d)\n", fails == 0 ? "ALL PASS" : "FAILURES", count - fails, count);
    fflush(stdout);
    return fails ? 1 : 0;
}
