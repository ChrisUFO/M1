/* Host unit test for the boot-time firmware integrity failsafe (issue #41-A).
 *
 * Covers, with no hardware:
 *   - the fixed-span arithmetic the boot check is built on (and that the span
 *     equals the build's 0xFFC14 CRC crop end / append offset),
 *   - the CRC decision helper (valid / corrupt CRC),
 *   - fault classification for every corruption class (magic / header / CRC),
 *   - the full composition against a synthetic bank buffer: a deterministic
 *     0xFFC14-byte image, signed with a C port of the build's CRC emitter, then
 *     injected with each corruption class (valid / flipped byte / zeroed
 *     magic_number_1 / zeroed magic_number_2 / CRC word relocated +-4 bytes),
 *   - the IWDG option-bit decision (OPTSR value mask vs USERType selector),
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

/* Metadata fields used by the synthetic bank (m1_fw_update_bl.h:
 * FW_CONFIG_MAGIC_NUMBER_1/2 and offsetof(magic_number_2) == 0x10). */
#define FW_CFG_MAGIC_1         0x4D493235U
#define FW_CFG_MAGIC_2         0x534A1F41U
#define METADATA_MAGIC1_OFF    0x0U
#define METADATA_MAGIC2_OFF    0x10U

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

/* ---- synthetic bank image ------------------------------------------------ */

/* CRC-32/MPEG-2 exactly as the STM32 CRC unit (and the emitter) computes it:
 * polynomial 0x04C11DB7, init 0xFFFFFFFF, no input/output reflection, no final
 * XOR, fed whole little-endian words. Straight C port of
 * tools/emulate_srec_cat_crc.py:36-48, the tool build.sh substitutes for
 * srec_cat; the two reference vectors below are that tool's own self-test. */
#define CRC32_POLY     0x04C11DB7U
#define CRC32_INIT     0xFFFFFFFFU

static uint32_t stm32_crc(const uint8_t *data, uint32_t len) {
    uint32_t crc = CRC32_INIT;
    uint32_t off;
    int b, bit;

    for (off = 0; off < len; off += 4U) {
        uint32_t word = 0;
        for (b = 0; b < 4; b++) {
            uint32_t idx = off + (uint32_t)b;
            word |= (uint32_t)((idx < len) ? data[idx] : 0U) << (8U * (uint32_t)b);
        }
        crc ^= word;
        for (bit = 0; bit < 32; bit++) {
            crc = (crc & 0x80000000U) ? ((crc << 1) ^ CRC32_POLY) : (crc << 1);
        }
    }
    return crc;
}

static uint32_t rd_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* The bank plus 8 trailing bytes, so a CRC word can be relocated +4 bytes. */
#define BANK_BUF_SIZE (M1_BOOT_CRC_CROP_END_BYTES + 8U)
static uint8_t bank[BANK_BUF_SIZE];

/* Deterministic pseudo-random fill (xorshift32, fixed seed): the "image" is not
 * zeroes, so a one-byte corruption cannot leave the CRC accidentally equal. */
static void bank_fill(uint8_t *b, size_t n) {
    uint32_t s = 0x41A5C0DEU;
    size_t i;
    for (i = 0; i < n; i++) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        b[i] = (uint8_t)(s >> 24);
    }
}

/* Write the metadata and sign the image the way the build does: CRC over
 * [0, span) appended little-endian at span. */
static void bank_sign(uint8_t *b) {
    uint32_t span = m1_boot_crc_span_bytes(BANK1_START, BANK1_CRC_ADDR);
    wr_le32(b + METADATA_OFFSET + METADATA_MAGIC1_OFF, FW_CFG_MAGIC_1);
    wr_le32(b + METADATA_OFFSET + METADATA_MAGIC2_OFF, FW_CFG_MAGIC_2);
    wr_le32(b + span, stm32_crc(b, span));
}

/* bl_crc_validate_active_bank() over a RAM image: span by address arithmetic,
 * CRC over those bytes, metadata words from inside the span, stored CRC word
 * from just past it -- then the shared classification helper. */
static S_M1_BOOT_FAULT_t bank_validate(const uint8_t *b) {
    uint32_t span = m1_boot_crc_span_bytes(BANK1_START, BANK1_CRC_ADDR);
    return m1_boot_classify_fault(
        rd_le32(b + METADATA_OFFSET + METADATA_MAGIC1_OFF), FW_CFG_MAGIC_1,
        rd_le32(b + METADATA_OFFSET + METADATA_MAGIC2_OFF), FW_CFG_MAGIC_2,
        stm32_crc(b, span), rd_le32(b + span));
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

    /* ---- synthetic bank buffer: span -> CRC over bytes -> compare ---- */
    {
        const uint32_t SPAN = m1_boot_crc_span_bytes(BANK1_START, BANK1_CRC_ADDR);
        uint32_t signed_crc;

        ok(stm32_crc((const uint8_t *)"\x00\x00\x00\x00", 4) == 0xC704DD7BU,
           "CRC port matches the emitter's vector 00000000 -> 0xC704DD7B");
        ok(stm32_crc((const uint8_t *)"\x04\x03\x02\x01", 4) == 0x793737CDU,
           "CRC port matches the emitter's vector 04030201 -> 0x793737CD");

        bank_fill(bank, sizeof bank);
        bank_sign(bank);
        signed_crc = rd_le32(bank + SPAN);
        ok(signed_crc == stm32_crc(bank, SPAN),
           "0xFFC14-byte synthetic bank: stored word == CRC over the span");
        ok(bank_validate(bank) == M1_BOOT_FAULT_NONE,
           "valid synthetic bank -> no fault (full span/CRC/compare composition)");

        /* The CRC word sits outside the span: changing it must not perturb the
         * computation, only the comparison. */
        wr_le32(bank + SPAN, signed_crc ^ 0x00000001U);
        ok(stm32_crc(bank, SPAN) == signed_crc,
           "CRC word is excluded from the validated span (its bytes are not hashed)");
        ok(bank_validate(bank) == M1_BOOT_FAULT_CRC_MISMATCH,
           "tampered CRC word -> CRC fault");
        wr_le32(bank + SPAN, signed_crc);

        /* one flipped byte mid-bank */
        bank[0x40000] ^= 0xFFU;
        ok(bank_validate(bank) == M1_BOOT_FAULT_CRC_MISMATCH,
           "one flipped byte at 0x40000 -> CRC fault");
        bank[0x40000] ^= 0xFFU;

        /* magic_number_1 zeroed: erased/foreign metadata */
        wr_le32(bank + METADATA_OFFSET + METADATA_MAGIC1_OFF, 0U);
        ok(bank_validate(bank) == M1_BOOT_FAULT_MAGIC_MISMATCH,
           "magic_number_1 zeroed -> magic fault");
        bank_sign(bank);

        /* magic_number_2 zeroed: intact identity, broken metadata tail */
        wr_le32(bank + METADATA_OFFSET + METADATA_MAGIC2_OFF, 0U);
        ok(bank_validate(bank) == M1_BOOT_FAULT_HEADER_MISMATCH,
           "magic_number_2 zeroed -> header fault");
        bank_sign(bank);

        /* CRC word relocated +4 bytes: the position is a boundary, not a hunt */
        wr_le32(bank + SPAN + 4U, rd_le32(bank + SPAN));
        wr_le32(bank + SPAN, 0xA5A55A5AU);
        ok(bank_validate(bank) == M1_BOOT_FAULT_CRC_MISMATCH,
           "CRC word moved +4 bytes -> CRC fault (word is read at a fixed address)");
        bank_sign(bank);

        /* CRC word relocated -4 bytes: the span ends flush against the metadata
         * block (0xFFC00..0xFFC14), so "4 bytes earlier" lands on
         * magic_number_2 -- still a mismatch, reported as the header fault that
         * explains it rather than as a bare CRC difference. */
        wr_le32(bank + SPAN - 4U, rd_le32(bank + SPAN));
        wr_le32(bank + SPAN, 0xA5A55A5AU);
        ok(bank_validate(bank) != M1_BOOT_FAULT_NONE,
           "CRC word moved -4 bytes -> mismatch (never silently accepted)");
        ok(bank_validate(bank) == M1_BOOT_FAULT_HEADER_MISMATCH,
           "the -4 target is magic_number_2, so the header fault wins");
        bank_sign(bank);
        ok(bank_validate(bank) == M1_BOOT_FAULT_NONE,
           "re-signing restores a validating bank (no residue from earlier cases)");
    }

    /* ---- IWDG mode: OPTSR value mask (bit 3), never the USERType selector ---- */
    {
        /* FLASH_OPTSR_IWDG_SW is bit 3; BORH_EN (bit 2) is a different option. */
        ok(M1_BOOT_OPTSR_IWDG_SW == 0x00000008U,
           "IWDG mask is OPTSR bit 3 (FLASH_OPTSR_IWDG_SW == OB_IWDG_SW)");
        ok(m1_boot_iwdg_is_hardware(0x00000000U),
           "OPTSR bit 3 clear -> hardware-started IWDG");
        ok(!m1_boot_iwdg_is_hardware(M1_BOOT_OPTSR_IWDG_SW),
           "OPTSR bit 3 set -> software-started IWDG (expected configuration)");
        /* the D1 regression: BORH_EN on OPTSR bit 2 says nothing about the IWDG */
        ok(m1_boot_iwdg_is_hardware(0x00000004U),
           "BORH_EN (bit 2) alone does not read as software IWDG");
        ok(!m1_boot_iwdg_is_hardware(0x00000004U | M1_BOOT_OPTSR_IWDG_SW),
           "BORH_EN + IWDG_SW reads as software (bit 2 cannot mask the answer)");
        ok(m1_boot_iwdg_is_hardware(0xFFFFFFF7U) && !m1_boot_iwdg_is_hardware(0xFFFFFFFFU),
           "decision depends on bit 3 only (all other bits irrelevant)");
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
