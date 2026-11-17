/* Host test for the CLI command table and the log capture wiring (issue #44,
 * part B).
 *
 * Core/Src/cli_app.c and m1_csrc/m1_log_debug.c are HAL/FreeRTOS/FatFs code and
 * cannot be linked into a host binary, so this test asserts the properties that
 * matter on their (comment-stripped) source text, in the same seam idiom as
 * test_cdc_rx_stats.c / test_m1cp_dfu.c. It pins:
 *
 *   1. every command ported in issue #44 part B (log, status, version, sdcard,
 *      wifi, battery, reboot, memory) plus `dfu` is present in xCommandList[],
 *      with a NON-EMPTY pcHelpString (that string is what `help` prints), a
 *      distinct interpreter and helper, and cExpectedNumberOfParameters == 0;
 *   2. every `BaseType_t cmd_*` handler DEFINED in cli_app.c is reachable: it is
 *      registered as a .pxCommandInterpreter in the table (no orphan handlers),
 *      and vRegisterCLICommands() still walks the table (nothing hidden);
 *   3. no handler can overflow the 200-byte CLI output buffer: no
 *      strcpy/sprintf/strcat/gets in the file, and every snprintf() writing to
 *      pcWriteBuffer passes xWriteBufferLen as the bound;
 *   4. `log` is a bounded multi-call handler over the linear capture window, and
 *      m1_log_debug.c feeds the window from the single m1_logdb_enqueue()
 *      funnel using the CLAMPED count (never the TX-ring return value -- that
 *      mistake is what froze the fork's capture);
 *   5. m1_logdb_get_recent() is out-of-line, takes the log mutex with a bounded
 *      pdMS_TO_TICKS(20) wait, peeks under it and prints nothing while holding
 *      it;
 *   6. `reboot` flushes (cliWrite + bounded delay) BEFORE NVIC_SystemReset(), and
 *      `dfu` calls firmware_update_enter_usb_dfu() as its last statement -- the
 *      fork raced both resets against the CLI output buffer, which the CLI only
 *      writes after the handler returns.
 *   7. `mtest 9` is the fork's destructive firmware-corruption test, re-homed
 *      to a free ID, and is compiled out of shipped images: the flash
 *      unlock/erase/program/lock path sits inside `#if defined(M1_TEST_CMDS)`,
 *      the `#else` branch explains the disabled build, and no build file or
 *      project source file defines that guard or `M1_SUBGHZ_CW_TEST_TX` (the
 *      source set is walked from the project trees, not hardcoded). The
 *      published ID map (cmd_m1_mtest_help()) lists 9 as dev-only and marks
 *      22/23/24 legacy-equivalent; IDs 7/8 stay free and nothing is renumbered.
 *   8. every `pcHelpString` in the table is SHORTER than
 *      configCOMMAND_INT_MAX_OUTPUT_SIZE (read from Core/Inc/FreeRTOSConfig.h),
 *      and the built-in help path NUL-terminates what it copies
 *      (prvHelpCommand(), Core/Src/FreeRTOS_CLI.c) -- a longer help string used
 *      to leave the 200-byte CLI output buffer unterminated, which the caller's
 *      strlen() then read past.
 *
 * Run either from the repository root or from this directory:
 *   ./tools/m1cp_host_test/test_cli_commands
 *   make -C tools/m1cp_host_test test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "seam_util.h"

static int count = 0, fails = 0;
static void ok(int cond, const char *what) {
    count++;
    if (cond) { printf("  ok   %s\n", what); }
    else       { printf("  FAIL %s\n", what); fails++; }
}

/* The command table as the firmware ships it, in the order the entries appear. */
static const char *const commands[] = {
    "cls", "mtest", "log", "status", "version", "sdcard", "wifi",
    "battery", "reboot", "memory", "cdcstats", "cdcreset", "dfu"
};
#define NUM_COMMANDS (sizeof(commands) / sizeof(commands[0]))

/* Every pcHelpString must fit the CLI response slot: `help` sends one help
 * string per response, so a longer one is truncated (and, before the
 * prvHelpCommand() terminator below, left unterminated). One entry predates
 * that rule and is deliberately kept word-for-word: `cdcstats` (289 B, already
 * on main). It is truncated at the slot, not overrun, and its counter
 * semantics are documented in docs/protocols/usb-cdc.md. */
static const char *const help_over_slot_known[] = { "cdcstats" };

static int help_over_slot_allowed(const char *command) {
    size_t i;

    for (i = 0; i < sizeof(help_over_slot_known) / sizeof(help_over_slot_known[0]); i++) {
        if (strcmp(command, help_over_slot_known[i]) == 0) return 1;
    }
    return 0;
}

/* Non-empty string literal: the opening quote is followed by something other
 * than the closing quote. `""` is not a help string. */static int nonempty_string_literal_at(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return 0;
    p++;
    return (*p != '"');
}

/* Index of the first offending write into the CLI buffer, or -1 when every
 * occurrence is bounded. "sprintf("/"vsnprintf(" share the tail of "sprintf(",
 * so a match only counts when it is not preceded by an identifier character. */
static int first_unbounded_write(const char *code) {
    static const char *const bad[] = { "strcpy(", "sprintf(", "strcat(", "gets(" };
    size_t i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        const char *p = code;
        size_t len = strlen(bad[i]);

        while ((p = strstr(p, bad[i])) != NULL) {
            char prev = (p > code) ? p[-1] : ' ';
            int ident = (prev == '_' || (prev >= 'a' && prev <= 'z') ||
                         (prev >= 'A' && prev <= 'Z') || (prev >= '0' && prev <= '9'));
            if (!ident) return (int)(p - code);
            p += len;
        }
    }

    /* Every snprintf(pcWriteBuffer, ...) must pass xWriteBufferLen next. */
    {
        const char *p = code;
        while ((p = strstr(p, "snprintf(pcWriteBuffer")) != NULL) {
            const char *a = p + strlen("snprintf(pcWriteBuffer");
            while (*a == ' ' || *a == '\t' || *a == '\r' || *a == '\n') a++;
            if (*a != ',') return (int)(p - code);
            a++;
            while (*a == ' ' || *a == '\t' || *a == '\r' || *a == '\n') a++;
            if (strncmp(a, "xWriteBufferLen", 15U) != 0) return (int)(p - code);
            p += strlen("snprintf(pcWriteBuffer");
        }
    }
    return -1;
}

/* True when `body` ends with `call` (only whitespace or the closing brace may
 * follow it): "no code after a noreturn call". */
static int ends_with_call(const char *body, const char *call) {
    const char *p = strstr(body, call);
    if (p == NULL) return 0;
    p += strlen(call);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return (*p == '}');
}

/* Copy [from, to) into a fresh NUL-terminated buffer; NULL on bad args. Used to
 * bound the region between two preprocessor lines. */
static char *copy_span(const char *from, const char *to) {
    size_t len;
    char *s;

    if (from == NULL || to == NULL || to < from) return NULL;
    len = (size_t)(to - from);
    s = malloc(len + 1U);
    if (s == NULL) return NULL;
    memcpy(s, from, len);
    s[len] = '\0';
    return s;
}

/* Bound the LAST occurrence of `sig` (its definition when the file also has a
 * prototype, e.g. static functions like prvHelpCommand()). Caller frees. */
static char *bound_last_block(const char *src, const char *sig) {
    const char *p = src, *last = NULL;
    char *body;

    while ((p = strstr(p, sig)) != NULL) { last = p; p++; }
    if (last == NULL) return NULL;
    return seam_bound_block(last, sig);
}

/* Decoded length of the C string literal that starts at `p` (which must point
 * at the opening quote), or -1 when it is not a plain terminated literal.
 * Escapes count as the single byte they produce, so "\\r\\n" counts as two --
 * this is the number of bytes the CLI buffer would hold. */
static int literal_length(const char *p) {
    int len = 0;

    if (p == NULL) return -1;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return -1;
    p++;
    while (*p != '"') {
        if (*p == '\0' || *p == '\n') return -1;   /* unterminated literal */
        if (*p == '\\') {
            p++;
            if (*p == '\0') return -1;
            if (*p >= '0' && *p <= '7') {          /* \ooo: up to three digits */
                int digits = 0;
                while (digits < 3 && *p >= '0' && *p <= '7') { p++; digits++; }
                len++;
                continue;
            }
            p++;                                   /* \r \n \t \" \\ ...: one byte */
            len++;
            continue;
        }
        p++;
        len++;
    }
    return len;
}

/* Value of `#define <name> <decimal>` in `src`, or -1 when it is not there. */
static int macro_value(const char *src, const char *name) {
    char needle[96];
    const char *p;

    if (src == NULL) return -1;
    snprintf(needle, sizeof(needle), "#define %s", name);
    p = strstr(src, needle);
    if (p == NULL) return -1;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t') p++;
    if (*p < '0' || *p > '9') return -1;
    return (int)strtol(p, NULL, 10);
}

/* ---- dev-guard scan -------------------------------------------------------
 * The guards must be off in every shipped build, so no build definition and no
 * project source file may define either macro. The source set is WALKED from
 * the project trees instead of being a fixed list of paths: a new file cannot
 * hide from the scan. `tools/` is deliberately not walked -- this test and
 * check_mtest_compile.sh name the macros on purpose (the compile check passes
 * -DM1_TEST_CMDS to prove the guarded code still builds).
 */
static const char *const guard_macros[] = { "M1_TEST_CMDS", "M1_SUBGHZ_CW_TEST_TX" };
#define NUM_GUARD_MACROS (sizeof(guard_macros) / sizeof(guard_macros[0]))

static int guard_files_scanned;
static int guard_dirs_missing;
static int guard_definitions;

/* "" when the current directory is the repository root, "../../" when it is
 * this test directory -- the same discovery seam_read_repo() performs. */
static const char *repo_root_prefix(void) {
    static char prefix[8];
    static int resolved = 0;

    if (!resolved) {
        FILE *fp = fopen("Core/Src/cli_app.c", "rb");
        if (fp != NULL) { fclose(fp); prefix[0] = '\0'; }
        else { snprintf(prefix, sizeof(prefix), "../../"); }
        resolved = 1;
    }
    return prefix;
}

/* Files whose text can carry a build definition. */
static int guard_scan_wants(const char *path) {
    static const char *const exts[] = { ".c", ".h", ".cmake", ".sh" };
    size_t n = strlen(path), i;

    for (i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        size_t e = strlen(exts[i]);
        if (n >= e && strcmp(path + n - e, exts[i]) == 0) return 1;
    }
    return 0;
}

static void guard_scan_file(const char *path) {
    char *raw = seam_read_all(path);
    char *code;
    size_t i;

    if (raw == NULL) { printf("       (unreadable: %s)\n", path); return; }
    code = seam_strip_comments(raw);   /* a commented-out define is not a build */
    free(raw);
    if (code == NULL) return;
    guard_files_scanned++;
    for (i = 0; i < NUM_GUARD_MACROS; i++) {
        char define_tok[96], dash_tok[96];

        snprintf(define_tok, sizeof(define_tok), "#define %s", guard_macros[i]);
        snprintf(dash_tok, sizeof(dash_tok), "-D%s", guard_macros[i]);
        if (strstr(code, define_tok) != NULL || strstr(code, dash_tok) != NULL) {
            printf("       (defines %s: %s)\n", guard_macros[i], path);
            guard_definitions++;
        }
    }
    free(code);
}

static void guard_scan_tree(const char *dir) {
    DIR *d = opendir(dir);
    struct dirent *e;

    if (d == NULL) {
        printf("       (missing tree: %s)\n", dir);
        guard_dirs_missing++;
        return;
    }
    while ((e = readdir(d)) != NULL) {
        char path[1024];
        struct stat st;

        if (e->d_name[0] == '.') continue;      /* "." ".." and dot-entries */
        if (snprintf(path, sizeof(path), "%s/%s", dir, e->d_name) >= (int)sizeof(path)) continue;
        if (stat(path, &st) != 0) continue;
        if ((st.st_mode & S_IFMT) == S_IFDIR) guard_scan_tree(path);
        else if (guard_scan_wants(path)) guard_scan_file(path);
    }
    closedir(d);
}

int main(void) {
    char *raw = seam_read_repo("Core/Src/cli_app.c");
    char *code;
    char *table;
    /* Device-side log plumbing. */
    char *logc_raw, *logc, *logh, *enqueue, *getter;
    /* Help-string slot accounting (from FreeRTOSConfig.h + the table). */
    int help_ceiling = 0;
    int longest_help = 0;
    int help_over_slot = 0;
    const char *longest_help_name = "";

    ok(raw != NULL, "read Core/Src/cli_app.c (cwd-independent)");
    if (raw == NULL) {
        printf("\nFAILURES (%d/%d)\n", count - fails, count);
        return 1;
    }
    code = seam_strip_comments(raw);
    ok(code != NULL, "strip comments from cli_app.c");
    if (code == NULL) { free(raw); return 1; }

    /* ---- 1. the command table -------------------------------------------- */
    printf("command table\n");
    /* The ceiling comes from the firmware config, not from this test: a change
     * to configCOMMAND_INT_MAX_OUTPUT_SIZE must move this assertion with it. */
    {
        char *frc = seam_read_repo("Core/Inc/FreeRTOSConfig.h");
        help_ceiling = macro_value(frc, "configCOMMAND_INT_MAX_OUTPUT_SIZE");
        ok(help_ceiling > 0, "configCOMMAND_INT_MAX_OUTPUT_SIZE is defined in Core/Inc/FreeRTOSConfig.h");
        if (help_ceiling <= 0) help_ceiling = 200;   /* keep going, but the line above failed */
        free(frc);
    }
    table = seam_bound_block(code, "const CLI_Command_Definition_t xCommandList[]");
    ok(table != NULL, "xCommandList[] is defined (not just declared)");
    if (table != NULL) {
        for (size_t i = 0; i < NUM_COMMANDS; i++) {
            char needle[64];
            const char *entry;
            char *span;
            size_t span_len;
            const char *next;

            snprintf(needle, sizeof(needle), ".pcCommand = \"%s\"", commands[i]);
            entry = strstr(table, needle);
            ok(entry != NULL, commands[i]);
            if (entry == NULL) continue;

            /* Bound this entry: from its .pcCommand to the next one. */
            next = strstr(entry + 1U, ".pcCommand = ");
            span_len = (next != NULL) ? (size_t)(next - entry) : strlen(entry);
            span = malloc(span_len + 1U);
            if (span == NULL) continue;
            memcpy(span, entry, span_len);
            span[span_len] = '\0';

            {
                const char *help = strstr(span, ".pcHelpString =");
                const char *interp = strstr(span, ".pxCommandInterpreter = cmd_");
                const char *helper = strstr(span, ".pxCommandHelper = cmd_");
                ok(help != NULL && nonempty_string_literal_at(help + strlen(".pcHelpString =")),
                   "  has a non-empty pcHelpString");
                /* `help` prints one help string per response through
                 * prvHelpCommand(): anything at or above the ceiling is
                 * truncated, and used to leave the buffer unterminated. */
                {
                    int help_len = (help != NULL) ? literal_length(help + strlen(".pcHelpString =")) : -1;
                    int allowed = help_over_slot_allowed(commands[i]);
                    if (help_len > longest_help) {
                        longest_help = help_len;
                        longest_help_name = commands[i];
                    }
                    if (help_len < 0 || help_len >= help_ceiling) {
                        if (allowed) {
                            help_over_slot++;
                        }
                        printf("       (%s help string is %d bytes, ceiling %d%s)\n",
                               commands[i], help_len, help_ceiling,
                               allowed ? ", documented pre-existing exception" : "");
                    }
                    ok(help_len > 0 && (help_len < help_ceiling || allowed),
                       "  help string fits configCOMMAND_INT_MAX_OUTPUT_SIZE");
                }
                ok(interp != NULL, "  is registered with a .pxCommandInterpreter");
                ok(helper != NULL, "  has a .pxCommandHelper");
                {
                    const char *params = strstr(span, ".cExpectedNumberOfParameters =");
                    const char *expect = (strcmp(commands[i], "mtest") == 0) ? "-1" : "0";
                    char expect_tok[8];
                    snprintf(expect_tok, sizeof(expect_tok), " %s", expect);
                    ok(params != NULL &&
                       strncmp(params + strlen(".cExpectedNumberOfParameters ="),
                               expect_tok, strlen(expect_tok)) == 0,
                       (strcmp(commands[i], "mtest") == 0) ?
                       "  expects variable parameters (-1)" : "  expects 0 parameters");
                }
            }
            free(span);
        }
        ok(strstr(table, ".pcCommand = NULL") != NULL,
           "the table stays NULL-terminated");
        printf("       (longest help string: %s, %d bytes; ceiling %d)\n",
               longest_help_name, longest_help, help_ceiling);
        ok(longest_help > 0 && (longest_help < help_ceiling ||
                                help_over_slot_allowed(longest_help_name)),
           "no table entry overflows the help response beyond the known exception");
        ok(help_over_slot <= 1,
           "at most the pre-existing cdcstats help line is over the slot (it is truncated, not overrun)");
    }
    free(table);

    /* ---- 2. no orphan handlers; registration loop intact ------------------ */
    printf("handler registration\n");
    {
        const char *p = code;
        int orphans = 0, definitions = 0;

        while ((p = strstr(p, "BaseType_t cmd_")) != NULL) {
            const char *name_at = p + strlen("BaseType_t ");
            const char *paren = strchr(name_at, '(');
            const char *after;
            char name[64];
            size_t name_len;
            char in_table[128];

            if (paren == NULL) break;
            name_len = (size_t)(paren - name_at);
            if (name_len >= sizeof(name)) break;
            memcpy(name, name_at, name_len);
            name[name_len] = '\0';

            after = paren;
            while (*after != '\0' && *after != ')') after++;
            after++;
            while (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n') after++;

            if ((*after != ';') && (strstr(name, "_help") == NULL)) {
                definitions++;
                snprintf(in_table, sizeof(in_table), ".pxCommandInterpreter = %s,", name);
                if (strstr(code, in_table) == NULL) {
                    printf("       (orphan handler: %s)\n", name);
                    orphans++;
                }
            }
            p = paren;
        }
        ok(definitions == (int)NUM_COMMANDS - 1,
           "every command handler is defined here (mtest's lives in m1_cli.c)");
        ok(orphans == 0, "no handler definition is left unregistered (no orphans)");
        ok(strstr(code, "xCommandList[i].pcCommand") != NULL,
           "vRegisterCLICommands() still walks xCommandList[]");
    }

    /* ---- 3. bounded output ----------------------------------------------- */
    printf("bounded output\n");
    {
        int bad = first_unbounded_write(code);
        if (bad >= 0) printf("       (offending write near: %.60s)\n", code + bad);
        ok(bad < 0, "no strcpy/sprintf/strcat/gets; every snprintf() is bounded by xWriteBufferLen");
        ok(strstr(code, "FreeRTOS_CLIProcessCommand(pcInputString, cOutputBuffer, configCOMMAND_INT_MAX_OUTPUT_SIZE)") != NULL,
           "the CLI still hands handlers the 200-byte output buffer");
    }

    /* ---- 4. `log`: bounded multi-call over the linear window -------------- */
    printf("log handler\n");
    {
        char *body = seam_bound_block(code, "BaseType_t cmd_log(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)");
        ok(body != NULL, "cmd_log() is defined");
        if (body != NULL) {
            ok(strstr(body, "m1_logdb_get_recent(") != NULL,
               "snapshots through m1_logdb_get_recent()");
            ok(strstr(body, "M1_LOG_CAPTURE_SIZE") != NULL,
               "sizes its static snapshot from the capture window");
            ok(strstr(body, "xWriteBufferLen - 1U") != NULL,
               "chunks at xWriteBufferLen - 1 (never past the CLI buffer)");
            ok(strstr(body, "pdTRUE") != NULL && strstr(body, "pdFALSE") != NULL,
               "uses the multi-call pdTRUE/pdFALSE protocol");
            ok(strstr(body, "window %u B") != NULL,
               "reports the window size when the capture wrapped");
            ok(strstr(body, "\"\\r\\n\"") != NULL, "emits a trailing CRLF when done");
        }
        free(body);
    }

    /* ---- 5. the capture funnel in m1_log_debug.c -------------------------- */
    printf("log capture funnel\n");
    logc_raw = seam_read_repo("m1_csrc/m1_log_debug.c");
    logc = (logc_raw != NULL) ? seam_strip_comments(logc_raw) : NULL;
    logh = seam_read_repo("m1_csrc/m1_log_debug.h");
    ok(logc != NULL, "read m1_csrc/m1_log_debug.c (cwd-independent)");
    ok(logh != NULL, "read m1_csrc/m1_log_debug.h (cwd-independent)");
    if (logc != NULL) {
        enqueue = seam_bound_block(logc, "static int m1_logdb_enqueue(");
        ok(enqueue != NULL, "m1_logdb_enqueue() is defined");
        if (enqueue != NULL) {
            ok(strstr(enqueue, "m1_log_capture_put(&log_capture, data, count)") != NULL,
               "captures the CLAMPED count at the funnel");
            ok(strstr(enqueue, "m1_log_capture_put(&log_capture, data, written)") == NULL,
               "never captures the TX-ring return value (the fork's bug)");
            ok(strstr(enqueue, "xSemaphoreTake(mutex_log_write_trans, 0)") != NULL,
               "keeps the non-blocking write-mutex window");
            ok(strstr(enqueue, "taskENTER_CRITICAL") == NULL,
               "adds no critical section (enqueue already refuses ISR context)");
            ok(strstr(enqueue, "__get_IPSR()") != NULL,
               "still refuses ISR context before touching shared state");
        }
        free(enqueue);

        getter = seam_bound_block(logc, "size_t m1_logdb_get_recent(");
        ok(getter != NULL, "m1_logdb_get_recent() is defined out-of-line");
        if (getter != NULL) {
            ok(strstr(getter, "pdMS_TO_TICKS(20)") != NULL,
               "takes the log mutex with a bounded 20 ms wait");
            ok(seam_in_order(getter,
                             (const char *const[]){ "xSemaphoreTake(mutex_log_write_trans",
                                                    "m1_log_capture_peek(",
                                                    "xSemaphoreGive(mutex_log_write_trans" },
                             3U),
               "peeks only while holding the mutex");
            ok(strstr(getter, "printf(") == NULL && strstr(getter, "cliWrite(") == NULL,
               "never prints while holding the log mutex");
        }
        free(getter);
        ok(strstr(logc, "#include \"m1_log_capture.h\"") != NULL,
           "m1_log_debug.c includes the capture window header");
    }
    if (logh != NULL) {
        ok(strstr(logh, "size_t m1_logdb_get_recent(char *out, size_t max_len, uint32_t *total_written);") != NULL,
           "m1_log_debug.h declares m1_logdb_get_recent()");
    }
    free(logc);
    free(logc_raw);
    free(logh);

    /* ---- 6. reboot / dfu must flush before the reset ---------------------- */
    printf("reboot and dfu\n");
    {
        char *reboot = seam_bound_block(code, "BaseType_t cmd_reboot(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)");
        char *dfu = seam_bound_block(code, "BaseType_t cmd_dfu(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)");

        ok(reboot != NULL, "cmd_reboot() is defined");
        if (reboot != NULL) {
            ok(seam_in_order(reboot,
                             (const char *const[]){ "cliWrite(pcWriteBuffer)",
                                                    "vTaskDelay(pdMS_TO_TICKS(",
                                                    "NVIC_SystemReset()" },
                             3U),
               "reboot writes the acknowledgement and flushes it BEFORE the reset");
            ok(ends_with_call(reboot, "NVIC_SystemReset();"),
               "nothing follows NVIC_SystemReset() in cmd_reboot()");
        }
        free(reboot);

        ok(dfu != NULL, "cmd_dfu() is defined");
        if (dfu != NULL) {
            ok(seam_has_code_token(dfu, "firmware_update_enter_usb_dfu()"),
               "dfu calls the one shared DFU entry path");
            ok(seam_in_order(dfu,
                             (const char *const[]){ "cliWrite(pcWriteBuffer)",
                                                    "firmware_update_enter_usb_dfu()" },
                             2U),
               "dfu acknowledges before entering DFU");
            ok(ends_with_call(dfu, "firmware_update_enter_usb_dfu();"),
               "no code follows the noreturn DFU call (no bogus return status)");
        }
        free(dfu);
    }

    /* ---- sdcard / battery handlers: honest failures, no 32-bit overflow --- */
    printf("sdcard and battery handlers\n");
    {
        char *sdcard = seam_bound_block(code, "BaseType_t cmd_sdcard(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)");
        char *battery = seam_bound_block(code, "BaseType_t cmd_battery(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)");

        ok(sdcard != NULL, "cmd_sdcard() is defined");
        if (sdcard != NULL) {
            ok(strstr(sdcard, "m1_sdcard_used_percent(") != NULL,
               "sdcard takes the used percentage from the host-tested helper");
            ok(strstr(sdcard, "* 100U") == NULL,
               "sdcard keeps no 32-bit used_kb * 100 expression (overflows above ~41 GiB)");
            ok(strstr(sdcard, "capacity probe failed") != NULL,
               "a failed capacity probe gets its own status line, not the cached \"OK\"");
            ok(seam_in_order(sdcard,
                             (const char *const[]){ "status != SD_access_OK",
                                                    "m1_sd_error_msg(status)",
                                                    "info == NULL" },
                             3U),
               "the cached-status and probe-failure branches stay separate");
        }
        free(sdcard);

        ok(battery != NULL, "cmd_battery() is defined");
        if (battery != NULL) {
            ok(strstr(battery, "bq27421_readStateofCharge_percent(") != NULL,
               "battery reads the state of charge through the checked accessor");
            ok(strstr(battery, "bq27421_soc(") == NULL,
               "battery no longer prints Level: 0 % for a failed SoC read");
        }
        free(battery);
    }

    /* ---- 7. mtest ID allocation: 9 is the dev-only corruption test --------- */
    printf("mtest command IDs\n");
    {
        char *cli_raw = seam_read_repo("m1_csrc/m1_cli.c");
        char *cli = (cli_raw != NULL) ? seam_strip_comments(cli_raw) : NULL;
        char *help_raw = seam_read_repo("m1_csrc/m1_cli_help.c");
        char *help = (help_raw != NULL) ? seam_strip_comments(help_raw) : NULL;

        ok(cli != NULL, "read m1_csrc/m1_cli.c (cwd-independent)");
        ok(help != NULL, "read m1_csrc/m1_cli_help.c (cwd-independent)");

        if (cli != NULL) {
            char *body = seam_bound_block(cli, "void cmd_m1_mtest_basic_system(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type)");
            ok(body != NULL, "cmd_m1_mtest_basic_system() is defined");
            if (body != NULL) {
                const char *guard_in = strstr(body, "#if defined(M1_TEST_CMDS)");
                const char *guard_else = (guard_in != NULL) ? strstr(guard_in, "#else") : NULL;
                const char *guard_end = (guard_else != NULL) ? strstr(guard_else, "#endif") : NULL;
                char *guarded = copy_span(guard_in, guard_else);
                char *fallback = copy_span(guard_else, guard_end);

                ok(strstr(body, "case 9:") != NULL,
                   "mtest 9 is a case in the basic-system switch");
                ok(guard_in != NULL && guard_else != NULL && guard_end != NULL,
                   "the destructive path is #if defined(M1_TEST_CMDS) / #else / #endif");
                if (guarded != NULL) {
                    ok(strstr(guarded, "HAL_FLASH_Unlock()") != NULL,
                       "  HAL_FLASH_Unlock() sits inside the guard");
                    ok(strstr(guarded, "HAL_FLASHEx_Erase(") != NULL,
                       "  the config sector erase sits inside the guard");
                    ok(strstr(guarded, "HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD") != NULL,
                       "  the programming call sits inside the guard");
                    ok(strstr(guarded, "HAL_FLASH_Lock()") != NULL,
                       "  HAL_FLASH_Lock() sits inside the guard");
                    ok(strstr(guarded, "FW_CRC_ADDRESS") != NULL && strstr(guarded, "FW_CONFIG_RESERVED_ADDRESS") != NULL,
                       "  it corrupts the stored CRC word and restores the metadata");
                    ok(strstr(guarded, "HAL_OK") != NULL,
                       "  the HAL results are checked, not ignored");
                }
                ok(fallback != NULL &&
                   strstr(fallback, "Firmware corruption test not available in this build") != NULL,
                   "the #else branch explains the disabled build");
                ok(strstr(body, "case 7:") == NULL && strstr(body, "case 8:") == NULL,
                   "IDs 7 and 8 stay unused (nothing was renumbered)");
                free(guarded);
                free(fallback);
                free(body);
            }
            ok(strstr(cli, "(cmd_type != 9)") != NULL,
               "mtest 9 needs no parameter (dispatch exception, like mtest 5)");
            ok(strstr(cli, "#define M1_TEST_CMDS") == NULL,
               "m1_cli.c never defines M1_TEST_CMDS itself");
        }

        /* The guards must be off in every shipped build: no build definition
         * and no project source file may define either macro. (Using them in
         * `#if defined(...)` is fine -- that is what makes them off.) Both
         * macros are checked, over a source set WALKED from the project trees:
         * a new file cannot hide from the scan. `tools/` is excluded on purpose
         * -- this test and check_mtest_compile.sh name the macros, and the
         * compile check passes -DM1_TEST_CMDS to the compiler. */
        {
            static const char *const source_trees[] = {
                "Core", "m1_csrc", "USB", "Battery", "Sub_Ghz", "Infrared",
                "lfrfid", "Esp32_serial_flasher", "cmake"
            };
            /* Build definitions live in a small, known set of root files; the
             * cmake/ tree is covered by the walk above. */
            static const char *const build_files[] = {
                "CMakeLists.txt", "CMakePresets.json", "build.sh", "Makefile"
            };
            const char *prefix = repo_root_prefix();
            size_t i;

            for (i = 0; i < sizeof(source_trees) / sizeof(source_trees[0]); i++) {
                char path[512];
                snprintf(path, sizeof(path), "%s%s", prefix, source_trees[i]);
                guard_scan_tree(path);
            }
            for (i = 0; i < sizeof(build_files) / sizeof(build_files[0]); i++) {
                char path[512];
                snprintf(path, sizeof(path), "%s%s", prefix, build_files[i]);
                guard_scan_file(path);
            }

            printf("       (scanned %d files over %d trees and %d build files for %d macros)\n",
                   guard_files_scanned,
                   (int)(sizeof(source_trees) / sizeof(source_trees[0])),
                   (int)(sizeof(build_files) / sizeof(build_files[0])),
                   (int)NUM_GUARD_MACROS);
            ok(guard_dirs_missing == 0, "every project source tree in the guard scan exists");
            ok(guard_files_scanned > 0, "the guard scan actually read files");
            ok(guard_definitions == 0,
               "no build file or project source defines M1_TEST_CMDS or M1_SUBGHZ_CW_TEST_TX");
        }

        if (help != NULL) {
            ok(strstr(help, "9 firmware CRC corruption test (DEV-ONLY") != NULL,
               "the published ID map lists 9 as the dev-only corruption test");
            ok(strstr(help, "22/23/24") != NULL && strstr(help, "legacy-equivalent") != NULL,
               "the ID map marks 22/23/24 as legacy-equivalent");
            ok(strstr(help, "cmd_type 53:") == NULL && strstr(help, "cmd_type 54:") == NULL,
               "the help advertises no ID that no handler implements (53/54 are case 52)");
            ok(strstr(help, "cmd_type 52:") != NULL && strstr(help, "bc|pd") != NULL,
               "the power page documents 52 bc|pd");
        }

        free(help);
        free(help_raw);
        free(cli);
        free(cli_raw);
    }

    /* ---- 8. the help path terminates what it copies ----------------------- */
    printf("help framework\n");
    {
        char *fcli_raw = seam_read_repo("Core/Src/FreeRTOS_CLI.c");
        char *fcli = (fcli_raw != NULL) ? seam_strip_comments(fcli_raw) : NULL;

        ok(fcli != NULL, "read Core/Src/FreeRTOS_CLI.c (cwd-independent)");
        if (fcli != NULL) {
            char *body = bound_last_block(fcli, "static BaseType_t prvHelpCommand(");

            ok(body != NULL, "prvHelpCommand() is defined");
            if (body != NULL) {
                /* strncpy() writes no NUL when the help string is at least
                 * xWriteBufferLen bytes, and the caller strlen()s the buffer:
                 * the terminator must follow the copy. */
                ok(seam_in_order(body,
                                 (const char *const[]){ "strncpy( pcWriteBuffer",
                                                        "xWriteBufferLen - 1U ] = '\\0';" },
                                 2U),
                   "prvHelpCommand() NUL-terminates the copied help string");
                ok(strstr(body, "[ xWriteBufferLen - 1U ]") != NULL,
                   "the terminator is the last byte of the CLI buffer, never past it");
            }
            free(body);
        }
        free(fcli);
        free(fcli_raw);
    }

    free(code);
    free(raw);

    printf("\n%s (%d/%d)\n", fails == 0 ? "ALL PASS" : "FAILURES", count - fails, count);
    fflush(stdout);
    return fails ? 1 : 0;
}
