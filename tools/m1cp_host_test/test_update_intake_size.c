/* Host test for the firmware-update intake size gate (PR #61 fix-now F1).
 *
 * Before this gate the update path and the boot failsafe disagreed about what a
 * valid image is: the SD intake accepted any nonzero 4-aligned size and verified
 * a span derived from the file's own length, while the failsafe validates the
 * FIXED span [FW_START_ADDRESS, FW_CRC_ADDRESS) on every boot. A shorter,
 * self-consistent file could therefore be flashed and then never validate.
 *
 * Covers, with no hardware:
 *   - the pure decision (m1_boot_image_size_is_canonical): canonical accepted;
 *     zero, plain-image-without-CRC, short, long, unaligned and 1 MiB rejected,
 *     and exactly one size in a window around the canonical value accepted,
 *   - the constant itself: 0xFFC18 == 1047576 == crop end 0xFFC14 + 4, and the
 *     same number in build.sh's release contract,
 *   - source seams on the two intakes: m1_fw_update.c (SD path) rejects with
 *     M1_FW_IMAGE_SIZE_INVALID before any CRC/flash work, and
 *     bl_flash_stream_begin() (streamed M1CP path) rejects with
 *     BL_CODE_SIZE_ERROR before it erases the target bank,
 *   - the compile-time tie (m1_fw_update_bl.h static assert) between the intake
 *     size and FW_CRC_ADDRESS - FW_START_ADDRESS + FW_IMAGE_CRC_SIZE.
 *
 * The device-side intake functions are FatFs/HAL code that cannot be linked
 * here, so those are checked as bounded source seams (same pattern as
 * test_m1cp_failsafe.c and m1_manager_fwupdate_seam_test.c).
 *
 * Run either from the repository root or from this directory:
 *   ./tools/m1cp_host_test/test_update_intake_size
 *   make -C tools/m1cp_host_test test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m1_boot_failsafe.h"

static int count = 0, fails = 0;
static void ok(int cond, const char *what) {
    count++;
    if (cond) { printf("  ok   %s\n", what); }
    else       { printf("  FAIL %s\n", what); fails++; }
}

/* Canonical image size restated as a plain number so the test also pins the
 * literal the build/release docs use (0xFFC18 == 1047576 bytes). */
#define CANONICAL_FILE_BYTES  1047576U

/* Read a file into a NUL-terminated heap buffer; NULL when unreadable. */
static char *read_all(const char *path) {
    FILE *fp = fopen(path, "rb");
    long n;
    char *s;

    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    n = ftell(fp);
    if (n < 0 || fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }
    s = malloc((size_t)n + 1U);
    if (s == NULL) { fclose(fp); return NULL; }
    if (fread(s, 1U, (size_t)n, fp) != (size_t)n) { free(s); fclose(fp); return NULL; }
    s[n] = '\0';
    fclose(fp);
    return s;
}

/* Resolve a repo-root-relative path from either cwd: the repository root
 * (documented standalone usage) or tools/m1cp_host_test/ (this directory's own
 * `make test`, which runs with that directory as its working directory). */
static char *read_repo(const char *path) {
    char alt[512];
    char *s = read_all(path);
    if (s != NULL) return s;
    snprintf(alt, sizeof(alt), "../../%s", path);
    return read_all(alt);
}

/* Brace-matched body bounding: locate `sig`, then bound the block starting at
 * the NEXT '{' after it through its matching '}'. Skips forward past any
 * occurrence immediately followed by ';' (a prototype, not a definition).
 * Returns a malloc'd copy of the body, or NULL. */
static char *bound_block(const char *src, const char *sig) {
    const char *search_from = src;
    for (;;) {
        const char *start = strstr(search_from, sig);
        const char *after, *open_brace, *p, *end = NULL;
        int depth = 0;
        size_t len;
        char *body;

        if (start == NULL) return NULL;
        after = start + strlen(sig);
        while (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n') after++;
        if (*after == ';') { search_from = start + strlen(sig); continue; }
        open_brace = strchr(start, '{');
        if (open_brace == NULL) return NULL;
        for (p = open_brace; *p != '\0'; p++) {
            if (*p == '{') depth++;
            else if (*p == '}') { depth--; if (depth == 0) { end = p; break; } }
        }
        if (end == NULL) return NULL;
        len = (size_t)(end - start) + 1U;
        body = malloc(len + 1U);
        if (body == NULL) return NULL;
        memcpy(body, start, len);
        body[len] = '\0';
        return body;
    }
}

/* Parse the first unsigned decimal integer that follows `key`, so a build/release
 * contract like `WCRC_EXPECTED_BYTES=1047576` can be pinned without depending on
 * the surrounding formatting. Returns 0 when the key is absent. */
static int uint_after(const char *src, const char *key, unsigned long *out) {
    const char *p = strstr(src, key);
    char *end;
    unsigned long v;

    if (p == NULL) return 0;
    p += strlen(key);
    while (*p == ' ' || *p == '\t' || *p == '"') p++;
    v = strtoul(p, &end, 10);
    if (end == p) return 0;
    *out = v;
    return 1;
}

int main(void) {
    printf("Update intake size gate (canonical image only)\n");

    /* ---- the canonical size itself ---------------------------------------- */
    ok(M1_BOOT_IMAGE_FILE_SIZE_CANONICAL == CANONICAL_FILE_BYTES,
       "canonical image is 1047576 bytes (0xFFC18)");
    ok(M1_BOOT_IMAGE_FILE_SIZE_CANONICAL == 0x000FFC18U,
       "canonical image is 0xFFC18 == crop end 0xFFC14 + 4-byte CRC word");
    ok(M1_BOOT_IMAGE_FILE_SIZE_CANONICAL == (M1_BOOT_CRC_CROP_END_BYTES + M1_BOOT_CRC_WORD_BYTES),
       "canonical image is derived from the signed span, not restated");
    ok(M1_BOOT_CRC_WORD_BYTES == 4U, "trailing CRC word is 4 bytes");

    /* ---- the decision ----------------------------------------------------- */
    ok(m1_boot_image_size_is_canonical(CANONICAL_FILE_BYTES),
       "canonical-size image accepted");
    ok(!m1_boot_image_size_is_canonical(0U),
       "zero-length image rejected");
    ok(!m1_boot_image_size_is_canonical(M1_BOOT_CRC_CROP_END_BYTES),
       "plain image without the trailing CRC word (0xFFC14) rejected");
    ok(!m1_boot_image_size_is_canonical(CANONICAL_FILE_BYTES - 4U),
       "image 4 bytes short of canonical rejected");
    ok(!m1_boot_image_size_is_canonical(0x00000400U),
       "small foreign .bin rejected");
    ok(!m1_boot_image_size_is_canonical(CANONICAL_FILE_BYTES + 4U),
       "image 4 bytes longer than canonical rejected");
    ok(!m1_boot_image_size_is_canonical(0x00100000U),
       "1 MiB image rejected (old upper bound was permissive)");
    ok(!m1_boot_image_size_is_canonical(0x00100004U),
       "1 MiB + CRC word rejected");
    ok(!m1_boot_image_size_is_canonical(0x000FFC16U),
       "4-aligned-but-not-canonical size rejected (alignment is not the contract)");
    {
        /* exactly one accepted value in a window around the canonical size */
        uint32_t sz;
        int accepted = 0;
        for (sz = CANONICAL_FILE_BYTES - 0x20U; sz <= CANONICAL_FILE_BYTES + 0x20U; sz += 4U) {
            if (m1_boot_image_size_is_canonical(sz)) accepted++;
        }
        ok(accepted == 1, "exactly one 4-aligned size near canonical is accepted");
    }

    /* ---- seam: SD intake (m1_fw_update.c) --------------------------------- */
    {
        char *src = read_repo("m1_csrc/m1_fw_update.c");
        ok(src != NULL, "read m1_csrc/m1_fw_update.c for the intake seam");
        if (src != NULL) {
            const char *gate = strstr(src, "m1_boot_image_size_is_canonical(image_size)");
            ok(gate != NULL, "SD intake calls the canonical-image predicate");
            if (gate != NULL) {
                /* the rejection must carry the existing size-error code ... */
                ok(strstr(gate, "M1_FW_IMAGE_SIZE_INVALID") != NULL &&
                   strstr(gate, "M1_FW_IMAGE_SIZE_INVALID") - gate < 600,
                   "SD intake rejects with M1_FW_IMAGE_SIZE_INVALID");
                /* ... and must happen before the pre-flight CRC pass/flash phase */
                {
                    const char *crc_init = strstr(src, "bl_get_crc_chunk(&crc32ret, 0, true, false)");
                    ok(crc_init != NULL && gate < crc_init,
                       "SD intake rejects before the image is verified/flashed");
                }
            }
            ok(strstr(src, "FW_IMAGE_SIZE_MAX + FW_IMAGE_CRC_SIZE") == NULL,
               "permissive 1 MiB upper-bound intake check is gone (no fallback path)");
        }
        free(src);
    }

    /* ---- seam: streamed M1CP intake (bl_flash_stream_begin) --------------- */
    {
        char *src = read_repo("m1_csrc/m1_fw_update_bl.c");
        ok(src != NULL, "read m1_csrc/m1_fw_update_bl.c for the intake seam");
        if (src != NULL) {
            char *body = bound_block(src, "uint8_t bl_flash_stream_begin(uint32_t image_size)");
            ok(body != NULL, "streamed intake bl_flash_stream_begin() exists");
            if (body != NULL) {
                const char *gate = strstr(body, "m1_boot_image_size_is_canonical(image_size)");
                ok(gate != NULL, "streamed intake calls the canonical-image predicate");
                ok(strstr(body, "BL_CODE_SIZE_ERROR") != NULL,
                   "streamed intake rejects with BL_CODE_SIZE_ERROR");
                {
                    const char *erase = strstr(body, "bl_flash_start(image_size)");
                    ok(gate != NULL && erase != NULL && gate < erase,
                       "streamed intake rejects before erasing the target bank");
                }
            }
            free(body);
        }
        free(src);
    }

    /* ---- seam: compile-time tie to FW_CRC_ADDRESS - FW_START_ADDRESS ------ */
    {
        char *src = read_repo("m1_csrc/m1_fw_update_bl.h");
        ok(src != NULL, "read m1_csrc/m1_fw_update_bl.h for the contract seam");
        if (src != NULL) {
            ok(strstr(src,
                      "((FW_CRC_ADDRESS - FW_START_ADDRESS) + FW_IMAGE_CRC_SIZE) == M1_BOOT_IMAGE_FILE_SIZE_CANONICAL") != NULL,
               "static assert ties the intake size to the failsafe's span + CRC word");
        }
        free(src);
    }

    /* ---- seam: build/release contract uses the same number ---------------- */
    {
        char *src = read_repo("build.sh");
        unsigned long wcrc = 0;
        ok(src != NULL, "read build.sh for the release size contract");
        if (src != NULL) {
            ok(uint_after(src, "WCRC_EXPECTED_BYTES=", &wcrc) &&
               wcrc == (unsigned long)M1_BOOT_IMAGE_FILE_SIZE_CANONICAL,
               "build.sh asserts the same canonical 0xFFC18 size");
        }
        free(src);
    }

    printf("\n%s (%d/%d)\n", fails == 0 ? "ALL PASS" : "FAILURES", count - fails, count);
    fflush(stdout);
    return fails ? 1 : 0;
}
