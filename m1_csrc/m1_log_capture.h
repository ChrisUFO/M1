/* See COPYING.txt for license details. */

/*
*
* m1_log_capture.h
*
* Linear "last N bytes of log output" history window (issue #44, part B).
*
* Pure, HAL-free and RTOS-free: the whole implementation is two small
* memcpy() calls into a caller-owned struct, so
* tools/m1cp_host_test/test_log_capture.c exercises the real code natively with
* no stubs.
*
* This exists because the legacy fork's `log` command captured into a ring that
* saturated at 2047 bytes and then froze (its reader restored the tail it had
* just consumed, so the buffer stopped accepting new data), and it truncated the
* capture length to the TX-ring return value, so a full ring captured nothing.
* This window instead keeps ONE absolute byte counter (`wr`, bytes written since
* boot) and derives everything from it:
*
*   - the content is a linear history: the newest min(wr, SIZE) bytes, always
*     returned oldest-first, so a wrapped window never re-orders the text;
*   - no init is required and `wr == 0` is a valid empty state, so the history
*     survives m1_logdb_init() re-entry across USB mode switches (the struct is
*     a file-scope object that is never memset());
*   - it can never freeze: every put() overwrites the oldest bytes and only
*     advances `wr`.
*
* Overflow: `wr` is a free-running unsigned counter whose arithmetic is defined
* to wrap (C11 6.2.5p9), and M1_LOG_CAPTURE_SIZE is a power of two that divides
* 2^32, so `wr % M1_LOG_CAPTURE_SIZE` stays a valid index across the wrap. Only
* the reported count degrades, conservatively: for the first SIZE bytes after a
* 2^32-byte wrap the window reports only the bytes written since the wrap. The
* CONTENT is still the correct newest bytes and no index leaves [0, SIZE), so a
* 4 GiB wrap can never read or write out of bounds.
*
* M1 Project
*
*/

#ifndef M1_LOG_CAPTURE_H_
#define M1_LOG_CAPTURE_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Window size: the `log` command advertises "the last 2 KB", and 2048 is a
 * power of two, which is what makes the modulo below wrap-safe. */
#define M1_LOG_CAPTURE_SIZE 2048U

typedef struct
{
    uint8_t buf[M1_LOG_CAPTURE_SIZE];
    uint32_t wr;    /* bytes ever written (free-running; see the overflow note) */
} S_M1_LogCapture;

/* Append len bytes (newest wins: old bytes are overwritten, never refused).
 *
 * len is uint16_t: every caller on the device path is bounded by the log TX
 * buffer (2048 bytes), and a 64 KiB argument would be a bug, not a feature.
 * `len` larger than M1_LOG_CAPTURE_SIZE is fine -- only its tail survives. */
static inline void m1_log_capture_put(S_M1_LogCapture *c, const uint8_t *d,
                                      uint16_t len)
{
    uint32_t skip;
    uint32_t kept;
    uint32_t start;
    uint32_t first;

    if ((c == NULL) || (d == NULL) || (len == 0U))
    {
        return;
    }

    /* Only the last SIZE bytes of an oversized write can survive; skipping the
     * head first also bounds both memcpy() calls below by the window size. */
    skip = ((uint32_t)len > (uint32_t)M1_LOG_CAPTURE_SIZE) ?
           ((uint32_t)len - (uint32_t)M1_LOG_CAPTURE_SIZE) : 0U;
    kept = (uint32_t)len - skip;

    d += skip;
    start = (c->wr + skip) % (uint32_t)M1_LOG_CAPTURE_SIZE;
    first = (uint32_t)M1_LOG_CAPTURE_SIZE - start;
    if (first > kept)
    {
        first = kept;
    }

    (void)memcpy(&c->buf[start], d, first);
    if (kept > first)
    {
        /* wraps: the remaining bytes go to the front of the window */
        (void)memcpy(&c->buf[0], d + first, kept - first);
    }

    /* Count every byte, including the skipped head, so `wr` stays the absolute
     * stream position the modulo above expects. */
    c->wr += (uint32_t)len;
}

/* Copy the newest min(total_written, M1_LOG_CAPTURE_SIZE) bytes, oldest first,
 * into out as a NUL-terminated string.
 *
 * out must hold at least max_len bytes; at most max_len - 1 content bytes are
 * copied and out is always NUL-terminated. max_len == 0 copies nothing and
 * returns 0.
 *
 * total_written (optional, may be NULL) receives the free-running byte count
 * (`wr`), NOT the clamped window length, so a caller can tell whether the
 * window has wrapped (total_written > returned length).
 *
 * Returns the number of content bytes copied (0 for an empty window). */
static inline uint16_t m1_log_capture_peek(const S_M1_LogCapture *c, char *out,
                                           uint16_t max_len,
                                           uint32_t *total_written)
{
    uint32_t total;
    uint32_t count;
    uint32_t start;
    uint32_t first;

    if (total_written != NULL)
    {
        *total_written = (c != NULL) ? c->wr : 0U;
    }

    if ((c == NULL) || (out == NULL) || (max_len == 0U))
    {
        return 0U;
    }

    total = (c->wr < (uint32_t)M1_LOG_CAPTURE_SIZE) ?
            c->wr : (uint32_t)M1_LOG_CAPTURE_SIZE;

    count = total;
    if (count > (uint32_t)(max_len - 1U))
    {
        count = (uint32_t)(max_len - 1U);
    }

    /* Oldest byte of the requested span: `count` bytes back from the cursor.
     * Subtracting before the modulo keeps this wrap-safe in unsigned
     * arithmetic (see the overflow note above). */
    start = (c->wr - count) % (uint32_t)M1_LOG_CAPTURE_SIZE;
    first = M1_LOG_CAPTURE_SIZE - start;
    if (first > count)
    {
        first = count;
    }

    (void)memcpy(out, &c->buf[start], first);
    if (count > first)
    {
        (void)memcpy(out + first, &c->buf[0], count - first);
    }
    out[count] = '\0';

    return (uint16_t)count;
}

#endif /* M1_LOG_CAPTURE_H_ */
