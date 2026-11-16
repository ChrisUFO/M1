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
 *   - the same decision at the full f_size()/FSIZE_t width: a >4 GiB candidate
 *     whose low 32 bits equal the canonical size is rejected (the SD intake
 *     must not narrow the size before the gate),
 *   - the constant itself: 0xFFC18 == 1047576 == crop end 0xFFC14 + 4, and the
 *     same number in build.sh's release contract,
 *   - source seams on the two intakes: m1_fw_update.c (SD path) feeds the gate
 *     the wide f_size() value and only narrows after it accepts, rejecting with
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

/* Polarity pin for the intake gate. Walks back from an occurrence of the
 * predicate call (so whitespace and line breaks do not matter) and requires the
 * exact rejecting shape `if ( !predicate(...)`. This is what an inverted rewrite
 * breaks while still containing the predicate text: bare `if (predicate(` , a
 * nested `if (false && !predicate(` , or a double negation `if (!!predicate(` .
 * Returns 1 only for the rejecting form. */
static int rejects_on_predicate(const char *src, const char *call) {
    const char *p = call;

    if ((src == NULL) || (call == NULL)) return 0;
    while ((p > src) && ((p[-1] == ' ') || (p[-1] == '\t') || (p[-1] == '\r') || (p[-1] == '\n'))) p--;
    if ((p == src) || (p[-1] != '!')) return 0;      /* negated at all?          */
    p--;
    while ((p > src) && ((p[-1] == ' ') || (p[-1] == '\t') || (p[-1] == '\r') || (p[-1] == '\n'))) p--;
    if ((p == src) || (p[-1] != '(')) return 0;      /* `if ( !call`             */
    p--;
    while ((p > src) && ((p[-1] == ' ') || (p[-1] == '\t') || (p[-1] == '\r') || (p[-1] == '\n'))) p--;
    if ((p - src) < 2) return 0;
    if ((p[-1] != 'f') || (p[-2] != 'i')) return 0;  /* the keyword `if`         */
    if ((p - src) > 2) {
        char before = p[-3];
        if ((before != ' ') && (before != '\t') && (before != '\r') && (before != '\n') &&
            (before != '{') && (before != '}') && (before != ';') && (before != ')')) return 0;
    }
    return 1;
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
       "unaligned size (0xFFC16, 2 mod 4) rejected");
    ok(!m1_boot_image_size_is_canonical(0x000FFC10U),
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

    /* ---- the decision at the f_size()/FSIZE_t width ----------------------- */
    /* FatFs f_size() returns FSIZE_t: a QWORD with FF_FS_EXFAT=1. These cases
     * pin the truncation class -- a >4 GiB file whose low 32 bits equal the
     * canonical size must be rejected at full width. */
    ok(m1_boot_image_size_is_canonical_wide((uint64_t)CANONICAL_FILE_BYTES),
       "canonical size accepted at f_size() width");
    ok(!m1_boot_image_size_is_canonical_wide(0x100000000ULL + (uint64_t)CANONICAL_FILE_BYTES),
       "4 GiB + canonical size rejected (low 32 bits alone are not the contract)");
    ok(!m1_boot_image_size_is_canonical_wide(0x0000000100000000ULL),
       "exactly 4 GiB rejected (low 32 bits zero)");
    ok(!m1_boot_image_size_is_canonical_wide(~(uint64_t)0),
       "UINT64_MAX rejected (no wrap onto the canonical size)");
    ok(m1_boot_image_size_is_canonical(CANONICAL_FILE_BYTES) ==
       m1_boot_image_size_is_canonical_wide((uint64_t)CANONICAL_FILE_BYTES),
       "32-bit and wide decisions agree for a representable size");

    /* ---- seam: SD intake (m1_fw_update.c) --------------------------------- */
    {
        char *src = read_repo("m1_csrc/m1_fw_update.c");
        ok(src != NULL, "read m1_csrc/m1_fw_update.c for the intake seam");
        if (src != NULL) {
            /* The gate must be fed the wide f_size() value -- never the narrowed
             * 32-bit image_size (the truncation class this test pins). */
            const char *gate = strstr(src, "m1_boot_image_size_is_canonical_wide((uint64_t)image_file_size)");
            const char *wide_src = strstr(src, "image_file_size = f_size(&hfile_fw)");
            const char *narrow = strstr(src, "image_size = (uint32_t)image_file_size");
            ok(gate != NULL, "SD intake calls the canonical-image predicate at f_size() width");
            if (gate != NULL) {
                /* the call must be the REJECTION branch, not the acceptance one */
                ok(rejects_on_predicate(src, gate),
                   "SD intake rejects on !m1_boot_image_size_is_canonical_wide(...)");
                /* the rejection must carry the existing size-error code ... */
                ok(strstr(gate, "M1_FW_IMAGE_SIZE_INVALID") != NULL &&
                   strstr(gate, "M1_FW_IMAGE_SIZE_INVALID") - gate < 900,
                   "SD intake rejects with M1_FW_IMAGE_SIZE_INVALID");
                /* ... and must happen before the pre-flight CRC pass/flash phase */
                {
                    const char *crc_init = strstr(src, "bl_get_crc_chunk(&crc32ret, 0, true, false)");
                    ok(crc_init != NULL && gate < crc_init,
                       "SD intake rejects before the image is verified/flashed");
                }
                /* the size reaches the gate straight from f_size(), and the
                 * 32-bit narrowing happens only after the gate accepted */
                ok(wide_src != NULL && wide_src < gate,
                   "SD intake gates the un-narrowed FSIZE_t from f_size()");
                ok(narrow != NULL && gate < narrow,
                   "SD intake narrows to 32 bits only after the gate accepts");
            }
            ok(strstr(src, "image_size = f_size(&hfile_fw)") == NULL,
               "SD intake never assigns f_size() to the 32-bit image_size");
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
                ok((gate != NULL) && rejects_on_predicate(body, gate),
                   "streamed intake rejects on !m1_boot_image_size_is_canonical(image_size)");
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
