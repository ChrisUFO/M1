/* Host test for the `sdcard` used-percentage arithmetic (issue #44, part B).
 *
 * Core/Src/cli_app.c cannot be linked into a host binary (HAL/FreeRTOS/FatFs),
 * so the handler's only real arithmetic lives in a pure, HAL-free header --
 * m1_csrc/m1_sdcard_capacity.h -- and is executed here directly, the same
 * pattern test_log_capture.c uses for the log window.
 *
 * What this pins (PR #65 review, defect D1): `used_kb * 100` in 32-bit
 * arithmetic overflows once a card holds more than 2^32 / 100 KB (~41 GiB)
 * used, which printed a single-digit percentage for half-full 64/128 GB cards
 * while the MB figures beside it stayed correct.
 *
 * Run either from the repository root or from this directory:
 *   ./tools/m1cp_host_test/test_sdcard_usage
 *   make -C tools/m1cp_host_test test
 */
#include <stdio.h>
#include <stdint.h>
#include "m1_sdcard_capacity.h"

static int count = 0, fails = 0;
static void ok(int cond, const char *what) {
    count++;
    if (cond) { printf("  ok   %s\n", what); }
    else       { printf("  FAIL %s\n", what); fails++; }
}

/* Capacities are KB in the firmware (m1_sdcard.c: (sectors/1024)*sector_size). */
#define KB_PER_GIB 1048576UL
#define GB(n) ((uint32_t)((uint64_t)(n) * 1000000000ULL / 1024ULL)) /* decimal GB in KB */

static void expect(uint32_t total_kb, uint32_t free_kb, uint32_t want, const char *what) {
    uint32_t got = m1_sdcard_used_percent(total_kb, free_kb);
    count++;
    if (got == want) {
        printf("  ok   %s (%lu%%)\n", what, (unsigned long)got);
    } else {
        printf("  FAIL %s: got %lu%%, want %lu%%\n",
               what, (unsigned long)got, (unsigned long)want);
        fails++;
    }
}

/* The 32-bit expression this replaces, kept only to prove the cases below are
 * in the range that actually overflowed. */
static uint32_t old_used_percent(uint32_t total_kb, uint32_t free_kb) {
    return (total_kb != 0U) ? ((uint32_t)(((total_kb - free_kb) * 100U) / total_kb)) : 0U;
}

int main(void) {
    printf("sdcard used-percent arithmetic\n");

    /* Degenerate inputs: no capacity -> no division, no percentage. */
    expect(0U, 0U, 0U, "empty volume reports 0%");
    expect(0U, 12345U, 0U, "zero total with a bogus free figure reports 0%");
    expect(1000U, 1000U, 0U, "nothing used reports 0%");
    expect(1000U, 0U, 100U, "completely full reports 100%");

    /* A free figure above the total (never produced by FatFs) must not
     * underflow into a huge used value. */
    expect(1000U, 2000U, 0U, "free > total clamps to 0%, never underflows");

    /* The ranges the brief names: 32 / 64 / 128 GB cards at a realistic fill,
     * plus the 0-byte-used case, all in KB as the firmware computes them. */
    expect((uint32_t)(32UL * KB_PER_GIB), (uint32_t)(32UL * KB_PER_GIB), 0U,
           "32 GiB card, 0 GiB used -> 0%");
    expect((uint32_t)(64UL * KB_PER_GIB), (uint32_t)(32UL * KB_PER_GIB), 50U,
           "64 GiB card, 32 GiB used -> 50%");
    expect((uint32_t)(64UL * KB_PER_GIB), (uint32_t)(14UL * KB_PER_GIB), 78U,
           "64 GiB card, 50 GiB used -> 78%");
    expect((uint32_t)(128UL * KB_PER_GIB), (uint32_t)(64UL * KB_PER_GIB), 50U,
           "128 GiB card, 64 GiB used -> 50%");
    expect((uint32_t)(128UL * KB_PER_GIB), (uint32_t)(68UL * KB_PER_GIB), 46U,
           "128 GiB card, 60 GiB used -> 46%");
    expect((uint32_t)(128UL * KB_PER_GIB), 0U, 100U,
           "128 GiB card, nothing free -> 100%");

    /* The overflow boundary itself: 2^32 / 100 = 42,949,672 KB (~41 GiB).
     * The old 32-bit expression wraps here; the helper does not. */
    {
        uint32_t total = (uint32_t)(64UL * KB_PER_GIB);
        uint32_t free_kb = total - 42949673U; /* just past 2^32/100 used */

        ok(old_used_percent(total, free_kb) != m1_sdcard_used_percent(total, free_kb),
           "the replaced 32-bit expression really does differ above ~41 GiB used");
        expect(total, free_kb, 64U, "64 GiB card, 41 GiB used -> 64%");
    }

    /* The review's worked example: total 62,000,000 KB, used 50,000,000 KB
     * (80%), which the 32-bit expression printed as 11%. */
    expect(GB(63U), GB(13U), 79U, "63 GB card, 50 GB used -> 79%");
    {
        uint32_t total = 62000000U, used = 50000000U;
        ok(old_used_percent(total, total - used) == 11U,
           "the replaced 32-bit expression prints 11% for the same volume");
        expect(total, total - used, 80U, "62,000,000 KB / 50,000,000 KB used -> 80%");
    }

    printf("\n%s (%d/%d)\n", fails == 0 ? "ALL PASS" : "FAILURES", count - fails, count);
    fflush(stdout);
    return fails ? 1 : 0;
}
