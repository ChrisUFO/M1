/* Host unit test for the boot-time firmware integrity failsafe (issue #41-A).
 *
 * Covers, with no hardware:
 *   - the fixed-span arithmetic the boot check is built on (and that the span
 *     equals the build's 0xFFC14 CRC crop end / append offset),
 *   - the CRC decision helper (valid / corrupt CRC),
 *   - fault classification for every corruption class (magic / header / CRC),
 *   - the saturating boot-attempt and swap counters and the swap loop guard,
 *   - a source seam asserting the device-side running-bank check never uses
 *     the candidate-bank offset (+M1_FLASH_BANK_SIZE) that bl_crc_check() uses
 *     for the OTHER bank -- the one assumption that would silently validate
 *     the wrong bytes if it regressed.
 *
 * The pure helpers are compiled from the real m1_csrc/m1_boot_failsafe.h; the
 * device side is a flash/CRC wrapper that cannot be linked here, so the seam
 * grep above is the automated substitute (same pattern as
 * m1_manager_fwupdate_seam_test.c). */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "m1_boot_failsafe.h"

static int count = 0, fails = 0;
static void ok(int cond, const char *what) {
    count++;
    if (cond) { printf("  ok   %s\n", what); }
    else       { printf("  FAIL %s\n", what); fails++; }
}

/* Flash addresses (m1_fw_update_bl.h) restated as plain numbers: this test
 * must not pull in the FatFs-heavy update header. m1_fw_update_bl.h
 * static-asserts the real macros against M1_BOOT_CRC_CROP_END_BYTES. */
#define BANK1_START        0x08000000U
#define BANK2_START        (BANK1_START + 0x00100000U)
#define BANK1_CRC_ADDR     (BANK1_START + M1_BOOT_CRC_CROP_END_BYTES)
#define BANK2_CRC_ADDR     (BANK2_START + M1_BOOT_CRC_CROP_END_BYTES)
#define METADATA_OFFSET    0x000FFC00U   /* FW_CONFIG_RESERVED_ADDRESS - bank start */
#define METADATA_SIZE      20U           /* sizeof(S_M1_FW_CONFIG_t) */

/* Read a file into buf; returns bytes read or -1. Source-seam helper. */
static long slurp(const char *path, char *buf, size_t cap) {
    FILE *fp = fopen(path, "rb");
    size_t n;
    if (!fp) return -1;
    n = fread(buf, 1, cap - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    return (long)n;
}

int main(void) {
    printf("m1cp boot failsafe test\n");

    /* ---- span arithmetic ---- */
    ok(M1_BOOT_CRC_CROP_END_BYTES == 0x000FFC14U,
       "crop end 0xFFC14 (build.sh CROP_END_BYTES / CMake -crop 0x0 0x000FFC14)");
    ok(m1_boot_crc_span_bytes(BANK1_START, BANK1_CRC_ADDR) == 0x000FFC14U,
       "running bank span = FW_CRC_ADDRESS - FW_START_ADDRESS = 0xFFC14");
    ok(m1_boot_crc_span_bytes(BANK2_START, BANK2_CRC_ADDR) == 0x000FFC14U,
       "candidate bank has the same span (only the base differs)");
    ok(m1_boot_crc_span_bytes(BANK1_CRC_ADDR, BANK1_START) == 0U,
       "inverted range degenerates to 0 (no underflow into a huge read length)");
    ok(m1_boot_crc_span_bytes(BANK1_START, BANK1_START) == 0U,
       "empty range is 0");
    ok((m1_boot_crc_span_bytes(BANK1_START, BANK1_CRC_ADDR) % 4U) == 0U,
       "span is a whole number of 32-bit CRC words");

    /* The metadata block (both magic words) sits inside the signed span, which
     * is what makes the magic/header fault classes meaningful. */
    ok(METADATA_OFFSET + METADATA_SIZE == M1_BOOT_CRC_CROP_END_BYTES,
       "metadata block [0xFFC00,0xFFC14) ends exactly at the CRC word");
    ok(METADATA_OFFSET < M1_BOOT_CRC_CROP_END_BYTES,
       "metadata is inside the validated span (magic faults are diagnosable)");

    /* ---- CRC decision helper ---- */
    ok(m1_boot_crc_matches(0x12345678U, 0x12345678U), "matching CRC accepted");
    ok(!m1_boot_crc_matches(0x12345678U, 0x12345679U), "single-bit-different CRC rejected");
    ok(!m1_boot_crc_matches(0x00000000U, 0xFFFFFFFFU), "erased CRC word rejected");
    ok(m1_boot_crc_matches(0U, 0U), "0 == 0 accepted (helper is pure equality, not a range)");

    /* ---- fault classification ---- */
    {
        const uint32_t M1 = 0x4D493235U, M2 = 0x534A1F41U, CRC = 0xDEADBEEFU;
        ok(m1_boot_classify_fault(M1, M1, M2, M2, CRC, CRC) == M1_BOOT_FAULT_NONE,
           "valid image -> no fault");
        ok(m1_boot_classify_fault(0xFFFFFFFFU, M1, M2, M2, CRC, CRC) == M1_BOOT_FAULT_MAGIC_MISMATCH,
           "erased metadata magic -> magic fault");
        ok(m1_boot_classify_fault(M1, M1, 0x00000000U, M2, CRC, CRC) == M1_BOOT_FAULT_HEADER_MISMATCH,
           "bad metadata header (magic_number_2) -> header fault");
        ok(m1_boot_classify_fault(M1, M1, M2, M2, 0x11111111U, CRC) == M1_BOOT_FAULT_CRC_MISMATCH,
           "intact metadata, wrong image CRC -> CRC fault");
        /* precedence: a wrong magic explains the mismatch, so it wins */
        ok(m1_boot_classify_fault(M1, M1, 0xBAADF00DU, M2, 0x1U, 0x2U) == M1_BOOT_FAULT_HEADER_MISMATCH,
           "header fault wins over a simultaneous CRC mismatch");
        ok(m1_boot_classify_fault(0U, M1, 0U, M2, 0x1U, 0x2U) == M1_BOOT_FAULT_MAGIC_MISMATCH,
           "magic fault wins over header+CRC mismatches");
    }

    /* ---- counters ---- */
    ok(m1_boot_next_counter(0) == 1, "boot attempts 0 -> 1");
    ok(m1_boot_next_counter(7) == 8, "boot attempts increment");
    ok(m1_boot_next_counter(254) == 255, "boot attempts 254 -> 255");
    ok(m1_boot_next_counter(255) == 255, "boot attempts saturate at 255 (never wrap to a healthy-looking 0)");

    /* ---- swap loop guard ---- */
    ok(M1_BOOT_SWAP_ATTEMPTS_MAX == 1U, "one swap allowed between CRC-validated boots");
    ok(m1_boot_swap_allowed(0), "fresh budget allows a swap");
    ok(!m1_boot_swap_allowed(1), "budget exhausted after one unvalidated swap");
    ok(!m1_boot_swap_allowed(255), "saturated counter also refuses");
    /* the rollback path's exact sequence: allowed -> increment -> refused */
    {
        uint8_t swaps = 0;
        ok(m1_boot_swap_allowed(swaps), "rollback request 1 allowed");
        swaps = m1_boot_next_counter(swaps);
        ok(!m1_boot_swap_allowed(swaps), "rollback request 2 (no validated boot since) refused");
    }

    /* ---- device-seam: running bank, not candidate bank ---- */
    {
        static char src[1 << 20];
        long n = slurp("../../m1_csrc/m1_fw_update_bl.c", src, sizeof src);
        ok(n > 0, "read m1_csrc/m1_fw_update_bl.c for the seam check");
        if (n > 0) {
            char *fn = strstr(src, "S_M1_BOOT_FAULT_t bl_crc_validate_active_bank(void)");
            ok(fn != NULL, "device-side bl_crc_validate_active_bank() exists");
            if (fn) {
                char *end = strstr(fn, "// S_M1_BOOT_FAULT_t bl_crc_validate_active_bank(void)");
                ok(end != NULL, "function body is delimited (seam scope)");
                if (end) {
                    char save = *end;
                    *end = '\0';
                    ok(strstr(fn, "M1_FLASH_BANK_SIZE") == NULL,
                       "running-bank check never uses the candidate-bank offset (+M1_FLASH_BANK_SIZE)");
                    ok(strstr(fn, "FW_CRC_ADDRESS") != NULL,
                       "running-bank check compares against the CRC at FW_CRC_ADDRESS");
                    ok(strstr(fn, "FW_START_ADDRESS") != NULL,
                       "running-bank check computes over FW_START_ADDRESS");
                    *end = save;
                }
            }
            ok(strstr(src, "FW_CRC_ADDRESS + M1_FLASH_BANK_SIZE") != NULL,
               "bl_crc_check() still targets the other bank at +M1_FLASH_BANK_SIZE (unchanged)");
        }
    }

    printf("\n%s (%d/%d)\n", fails == 0 ? "ALL PASS" : "FAILURES", count - fails, count);
    fflush(stdout);
    return fails ? 1 : 0;
}
