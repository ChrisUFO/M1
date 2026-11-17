/* Shared source-seam helpers for the host tests that inspect device-only files
 * textually (issue #44, part B).
 *
 * The device files these tests look at (Core/Src/cli_app.c, m1_csrc/m1_log_debug.c,
 * USB/Class/CDC/Src/usbd_cdc_if.c, ...) pull in HAL/FreeRTOS/FatFs and cannot be
 * linked into a host binary, so the properties that matter are asserted on their
 * source text instead. This header is the ONE implementation of the text
 * plumbing -- read a repo file cwd-independently, strip comments, bound a
 * brace-matched block, search only live code -- shared by
 * test_cdc_rx_stats.c and test_cli_commands.c.
 *
 * Everything is `static inline` so each test binary stays self-contained (no
 * extra object file in the Makefile target) and unused helpers never warn.
 */
#ifndef M1CP_SEAM_UTIL_H_
#define M1CP_SEAM_UTIL_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read a whole file; NULL on failure. Caller frees. */
static inline char *seam_read_all(const char *path) {
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

/* Read a path relative to the repository root, from either the repository root
 * or this test directory. Caller frees. */
static inline char *seam_read_repo(const char *path) {
    char alt[512];
    char *s = seam_read_all(path);

    if (s != NULL) return s;
    snprintf(alt, sizeof(alt), "../../%s", path);
    return seam_read_all(alt);
}

/* Copy `src` with `//` and comment markers removed, keeping newlines so line
 * structure (and therefore search order) survives. Commented-out code must not
 * be mistaken for live code. Caller frees. */
static inline char *seam_strip_comments(const char *src) {
    size_t src_len = strlen(src);
    char *code = malloc(src_len + 1U);
    size_t i, out = 0U;
    int in_block = 0, in_line = 0;

    if (code == NULL) return NULL;

    for (i = 0U; i < src_len; i++) {
        char c = src[i];
        if (in_line) {
            if (c == '\n') { in_line = 0; code[out++] = c; }
            continue;
        }
        if (in_block) {
            if (c == '*' && (i + 1U) < src_len && src[i + 1U] == '/') {
                in_block = 0; i++;
            } else if (c == '\n') {
                code[out++] = c;   /* keep line separation */
            }
            continue;
        }
        if (c == '/' && (i + 1U) < src_len && src[i + 1U] == '/') { in_line = 1; i++; continue; }
        if (c == '/' && (i + 1U) < src_len && src[i + 1U] == '*') { in_block = 1; i++; continue; }
        code[out++] = c;
    }
    code[out] = '\0';
    return code;
}

/* Token search that ignores comments (block comments may span lines). */
static inline int seam_has_code_token(const char *src, const char *tok) {
    char *code = seam_strip_comments(src);
    int hit;

    if (code == NULL) return 0;
    hit = (strstr(code, tok) != NULL);
    free(code);
    return hit;
}

/* Brace-matched body bounding: locate `sig`, skip prototypes, then copy the
 * block through its matching '}'. NULL when not found. Caller frees. */
static inline char *seam_bound_block(const char *src, const char *sig) {
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

/* All needles present and in strictly ascending source order. */
static inline int seam_in_order(const char *src, const char *const *needles, size_t n) {
    const char *p = src;
    size_t i;

    for (i = 0; i < n; i++) {
        const char *hit = strstr(p, needles[i]);
        if (hit == NULL) return 0;
        p = hit + strlen(needles[i]);
    }
    return 1;
}

#endif /* M1CP_SEAM_UTIL_H_ */
