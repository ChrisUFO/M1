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
 *
 * Run either from the repository root or from this directory:
 *   ./tools/m1cp_host_test/test_cli_commands
 *   make -C tools/m1cp_host_test test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* Non-empty string literal: the opening quote is followed by something other
 * than the closing quote. `""` is not a help string. */
static int nonempty_string_literal_at(const char *p) {
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

int main(void) {
    char *raw = seam_read_repo("Core/Src/cli_app.c");
    char *code;
    char *table;
    /* Device-side log plumbing. */
    char *logc_raw, *logc, *logh, *enqueue, *getter;

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

    free(code);
    free(raw);

    printf("\n%s (%d/%d)\n", fails == 0 ? "ALL PASS" : "FAILURES", count - fails, count);
    fflush(stdout);
    return fails ? 1 : 0;
}
