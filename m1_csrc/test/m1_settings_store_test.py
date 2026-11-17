#!/usr/bin/env python3
"""Host tests for m1_settings_store.c (durable user-settings store).

The module's parse/serialize/CRC/clamp core is deliberately HAL/FatFs-free
(see the SETTINGS_PURE_CORE markers in m1_csrc/m1_settings_store.c), so
Part 1 compiles the REAL core -- extracted verbatim, plus the real
m1_crc32c.c -- with no stubs at all. Part 2 additionally extracts the REAL
FatFs-coupled load/save/recover functions (same extraction technique as
m1_csrc/test/m1_wifi_persistence_hardening_test.py) and runs them against
an in-memory FatFs stand-in with fault injection, so the actual production
promotion/recovery control flow is what gets exercised. Part 3 verifies
the boot wiring in m1_sys_init.c against the committed source text.

From the repository root: python3 m1_csrc/test/m1_settings_store_test.py
"""

import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

PURE_BEGIN = (
    "/* ===================== SETTINGS_PURE_CORE_BEGIN ===================== */"
)
PURE_END = "/* ====================== SETTINGS_PURE_CORE_END ====================== */"

PURE_FUNCTIONS = [
    "settings_bounded_len",
    "settings_parse_i64",
    "settings_parse_hex32",
    "settings_store_numeric",
    "settings_store_string",
    "settings_store_field_default",
    "settings_apply_kv",
    "settings_parse_buffer",
    "settings_buffer_crc_valid",
    "settings_read_numeric",
    "settings_serialize",
    "settings_reset_defaults",
]

FILE_FUNCTIONS = [
    "settings_recover_replace",
    "settings_load_file",
    "settings_write_file",
    "settings_init",
    "settings_save",
]


def source(path):
    return (ROOT / path).read_text()


def function(text, name):
    """Extract a top-level function definition (body included).

    Unlike the historical pattern in m1_wifi_persistence_hardening_test.py,
    the signature match is anchored to lines starting with an identifier
    character: this module's doc comments mention sibling function names
    with parentheses (e.g. "settings_load_file() has the final word"), and
    without the anchor the cross-line [^;{}]* could bridge from such a
    comment into the next definition. All definitions in this module start
    at column 0 per house style."""
    import re

    match = re.search(r"^[A-Za-z_][^\n;]*\b" + name + r"\([^;{}]*\)\s*\n\{", text, re.M)
    assert match, name
    start = match.start()
    depth = 1
    body = text[match.end() :]
    for token in re.finditer(
        r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|'
        r"//[^\n]*|/\*[\s\S]*?\*/|[{}]",
        body,
    ):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
        if depth == 0:
            return text[start : match.end() + token.end()]
    raise AssertionError(name)


def define(text, name):
    import re

    m = re.search(r"^#define\s+" + name + r"\b.*$", text, re.M)
    assert m, name
    return m.group()


def static_buffer(text, name):
    """Extract a top-level `static char name[MACRO];` file-scope buffer
    definition (the shared load/save work buffer lives outside the pure
    core because it exists only for the FatFs-coupled functions)."""
    import re

    m = re.search(r"^static char\s+" + name + r"\s*\[[^]]*\]\s*;$", text, re.M)
    assert m, name
    return m.group()


def region(text, begin_marker, end_marker):
    i = text.index(begin_marker)
    j = text.index(end_marker, i)
    return text[i + len(begin_marker) : j]


def host_cc():
    for cc in ("cc", "gcc", "clang"):
        try:
            subprocess.run([cc, "--version"], capture_output=True, check=True)
            return cc
        except (FileNotFoundError, subprocess.CalledProcessError):
            continue
    raise AssertionError("no usable host C compiler (cc/gcc/clang) found")


CC = host_cc()
SANITIZERS = ["-fsanitize=address,undefined"]


def build_run(tmp, name, text):
    """Compile+run a generated TU together with the REAL m1_crc32c.c.
    Sanitizers are used when the toolchain supports them (CI); a toolchain
    without the runtime (some MinGW hosts) falls back to a plain build
    rather than failing -- the checks themselves are unaffected."""
    src = Path(tmp) / f"{name}.c"
    src.write_text(text)
    exe = Path(tmp) / name
    log = ""
    for extra in (SANITIZERS, []):
        cmd = (
            [CC, "-std=c11", "-Wall", "-Wextra", "-Wno-unused-function", "-g"]
            + extra
            + [
                "-I",
                str(ROOT / "m1_csrc"),
                str(src),
                str(ROOT / "m1_csrc" / "m1_crc32c.c"),
                "-o",
                str(exe),
            ]
        )
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        log = r.stderr
        if r.returncode == 0:
            break
    else:
        print(log)
        raise AssertionError(f"{name} failed to build")
    subprocess.run([str(exe)], cwd=ROOT, check=True)


CHECK_MACROS = r"""
static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) { g_pass++; } else { g_fail++; printf("  FAIL: %s\n", (m)); } } while (0)
"""


def pure_core_tests(tmp):
    mod = source("m1_csrc/m1_settings_store.c")

    # Read-race contract pin (header ownership contract): a concurrent
    # settings_get() reader must never observe an un-NUL-terminated string
    # field mid-update. A threaded race test is impractical in this
    # harness, so the invariant is checked statically: settings_store_string
    # must zero the whole destination BEFORE copying into it.
    # (memcpy-then-terminate leaves the buffer un-terminated mid-update on
    # grow; terminator-first leaves a stale tail on shrink.)
    store_string_src = function(mod, "settings_store_string")
    assert store_string_src.index("memset(") < store_string_src.index("memcpy("), (
        "settings_store_string must memset the destination before memcpy"
    )

    core = region(mod, PURE_BEGIN, PURE_END) + "\n" + CHECK_MACROS
    for fn in PURE_FUNCTIONS:
        core += "\n" + function(mod, fn)
    core += r"""
static int settings_equal(const S_M1_Settings_t *a, const S_M1_Settings_t *b)
{
    return (a->brightness_pct    == b->brightness_pct)
        && (a->screen_timeout_s  == b->screen_timeout_s)
        && (a->led_notifications == b->led_notifications)
        && (strcmp(a->device_name, b->device_name) == 0)
        && (a->subghz_mod        == b->subghz_mod)
        && (a->subghz_bw_khz     == b->subghz_bw_khz)
        && (a->subghz_dev_khz    == b->subghz_dev_khz)
        && (a->subghz_power_dbm  == b->subghz_power_dbm)
        && (a->subghz_freq_hz    == b->subghz_freq_hz)
        && (a->subghz_freq_valid == b->subghz_freq_valid)
        && (strcmp(a->bt_adv_name, b->bt_adv_name) == 0)
        && (a->bt_preset         == b->bt_preset)
        && (a->bt_interval_ms    == b->bt_interval_ms);
}

/* Assemble a CRC-valid file image: body (header+kv lines) + trailer. */
static size_t build_file(char *out, size_t cap, const char *body)
{
    size_t n = strlen(body);
    uint32_t crc = m1_crc32c(body, n);
    int m = snprintf(out + n, cap - n, "crc=%08X\r\n", (unsigned)crc);
    assert(n > 0 && (size_t)m > 0 && (size_t)(n + m) < cap);
    memcpy(out, body, n);
    return (size_t)(n + m);
}

int main(void)
{
    char buf[2048];
    size_t len;

    /* 1: compiled defaults are live before any init call */
    {
        S_M1_Settings_t d;
        memset(&d, 0, sizeof(d));
        settings_reset_defaults(); /* writes the same defaults; compare against literals */
        CHECK(g_m1_settings.brightness_pct == 100, "default brightness 100");
        CHECK(g_m1_settings.screen_timeout_s == 45, "default timeout 45 s");
        CHECK(g_m1_settings.led_notifications == 1, "default LED notifications on");
        CHECK(strcmp(g_m1_settings.device_name, "M1") == 0, "default device name M1");
        CHECK(g_m1_settings.subghz_freq_valid == 0 && g_m1_settings.subghz_freq_hz == 0, "default subghz override inactive");
        CHECK(g_m1_settings.bt_adv_name[0] == '\0', "default bt adv name empty");
        CHECK(g_m1_settings.subghz_power_dbm == 0 && g_m1_settings.subghz_mod == 0
              && g_m1_settings.subghz_bw_khz == 0 && g_m1_settings.subghz_dev_khz == 0
              && g_m1_settings.bt_preset == 0 && g_m1_settings.bt_interval_ms == 0,
              "remaining defaults zero");
        (void)d;
    }

    /* 2: serialize/parse round-trip of every field at non-default values */
    {
        S_M1_Settings_t ref;
        g_m1_settings.brightness_pct = 1;
        g_m1_settings.screen_timeout_s = 0; /* Off: exercises zero_ok */
        g_m1_settings.led_notifications = 0;
        strcpy(g_m1_settings.device_name, "Bedroom Remote = 2");
        g_m1_settings.subghz_mod = 255;
        g_m1_settings.subghz_bw_khz = 200;
        g_m1_settings.subghz_dev_khz = 47;
        g_m1_settings.subghz_power_dbm = -10;
        g_m1_settings.subghz_freq_hz = 433920000u;
        g_m1_settings.subghz_freq_valid = 1;
        strcpy(g_m1_settings.bt_adv_name, "FindMy-M1");
        g_m1_settings.bt_preset = 3;
        g_m1_settings.bt_interval_ms = 65535;
        ref = g_m1_settings;

        CHECK(settings_serialize(buf, sizeof(buf), &len), "serialize extremes succeeds");
        CHECK(len > 0 && strncmp(buf, "#ver=1\r\n", 8) == 0, "file starts with #ver=1 header");
        CHECK(strncmp(buf + len - 2, "\r\n", 2) == 0, "file ends with CRLF");
        CHECK(strstr(buf, "\r\ncrc=") != NULL, "trailer line present");
        CHECK(settings_buffer_crc_valid(buf, len), "own serialization passes CRC check");

        settings_reset_defaults();
        CHECK(settings_parse_buffer(buf, len), "parse of own serialization succeeds");
        CHECK(settings_equal(&g_m1_settings, &ref), "round-trip preserves every field");
    }

    /* 3: defaults round-trip (all-default file is valid and stable) */
    {
        S_M1_Settings_t ref;
        settings_reset_defaults();
        ref = g_m1_settings;
        CHECK(settings_serialize(buf, sizeof(buf), &len), "serialize defaults succeeds");
        CHECK(strstr(buf, "brightness_pct=100\r\n") != NULL, "default brightness serialized");
        CHECK(strstr(buf, "screen_timeout_s=45\r\n") != NULL, "default timeout serialized");
        CHECK(strstr(buf, "device_name=M1\r\n") != NULL, "default name serialized");
        CHECK(settings_parse_buffer(buf, len), "parse defaults file succeeds");
        CHECK(settings_equal(&g_m1_settings, &ref), "defaults round-trip identical");
    }

    /* 4: CRC tamper detection */
    {
        settings_reset_defaults();
        CHECK(settings_serialize(buf, sizeof(buf), &len), "serialize for tamper tests");
        buf[12] = (char)(buf[12] ^ 0x01); /* corrupt inside a value */
        CHECK(!settings_buffer_crc_valid(buf, len), "tampered value fails CRC");
        settings_reset_defaults();
        CHECK(settings_serialize(buf, sizeof(buf), &len), "re-serialize");
        buf[len - 3] = (char)(buf[len - 3] ^ 0x02); /* corrupt trailer digit */
        CHECK(!settings_buffer_crc_valid(buf, len), "tampered trailer fails CRC");
    }

    /* 5: truncation detection */
    {
        size_t hdr;
        settings_reset_defaults();
        CHECK(settings_serialize(buf, sizeof(buf), &len), "serialize for truncation tests");
        hdr = (size_t)(strstr(buf, "\r\ncrc=") - buf) + 2; /* offset of trailer */
        CHECK(!settings_buffer_crc_valid(buf, hdr - 1), "file cut before trailer rejected");
        CHECK(!settings_buffer_crc_valid(buf, hdr + 10), "trailer cut mid-line rejected");
        CHECK(settings_buffer_crc_valid(buf, len - 2), "missing final CRLF still accepted");
    }

    /* 6: trailer syntax tolerance */
    {
        size_t hdr;
        settings_reset_defaults();
        CHECK(settings_serialize(buf, sizeof(buf), &len), "serialize for trailer tests");
        hdr = (size_t)(strstr(buf, "\r\ncrc=") - buf) + 2;
        { /* lowercase hex accepted */
            size_t i;
            for (i = hdr + 4; i < hdr + 12; i++) {
                buf[i] = (char)((buf[i] >= 'A' && buf[i] <= 'F') ? buf[i] - 'A' + 'a' : buf[i]);
            }
        }
        CHECK(settings_buffer_crc_valid(buf, len), "lowercase trailer digits accepted");
        settings_reset_defaults();
        CHECK(settings_serialize(buf, sizeof(buf), &len), "re-serialize");
        hdr = (size_t)(strstr(buf, "\r\ncrc=") - buf) + 2;
        memmove(&buf[hdr + 11], &buf[hdr + 12], len - hdr - 12); /* drop one hex digit */
        CHECK(!settings_buffer_crc_valid(buf, len - 1), "7-digit trailer rejected");
    }

    /* 7: unknown keys skipped, known keys still applied; blank lines and
     * mid-file comments tolerated between settings lines */
    {
        len = build_file(buf, sizeof(buf),
            "#ver=1\r\n"
            "\r\n"
            "# mid-file comment\r\n"
            "brightness_pct=50\r\n"
            "future_key=9\r\n"
            "subghz_mod=2\r\n");
        CHECK(settings_parse_buffer(buf, len), "file with unknown key parses");
        CHECK(g_m1_settings.brightness_pct == 50, "known key applied around unknown key");
        CHECK(g_m1_settings.subghz_mod == 2, "second known key applied after unknown key");
        CHECK(g_m1_settings.screen_timeout_s == 45, "absent key keeps default");
    }

    /* 8: out-of-range values clamp to bounds */
    {
        len = build_file(buf, sizeof(buf),
            "#ver=1\r\n"
            "brightness_pct=150\r\n"
            "screen_timeout_s=5\r\n"
            "led_notifications=7\r\n"
            "subghz_mod=300\r\n"
            "subghz_bw_khz=70000\r\n"
            "subghz_dev_khz=300\r\n"
            "subghz_power_dbm=199\r\n"
            "subghz_freq_hz=4294967296\r\n"
            "subghz_freq_valid=9\r\n"
            "bt_preset=300\r\n"
            "bt_interval_ms=70000\r\n");
        CHECK(settings_parse_buffer(buf, len), "clamp file parses");
        CHECK(g_m1_settings.brightness_pct == 100, "brightness clamps to 100");
        CHECK(g_m1_settings.screen_timeout_s == 10, "timeout 5 clamps up to 10");
        CHECK(g_m1_settings.led_notifications == 1, "LED flag coerces to 1");
        CHECK(g_m1_settings.subghz_mod == 255, "subghz_mod clamps to 255");
        CHECK(g_m1_settings.subghz_bw_khz == 65535, "bandwidth clamps to 65535");
        CHECK(g_m1_settings.subghz_dev_khz == 255, "deviation clamps to 255");
        CHECK(g_m1_settings.subghz_power_dbm == 127, "power clamps to 127");
        CHECK(g_m1_settings.subghz_freq_hz == 4294967295u, "frequency clamps to u32 max");
        CHECK(g_m1_settings.subghz_freq_valid == 1, "freq_valid coerces to 1");
        CHECK(g_m1_settings.bt_preset == 255, "bt preset clamps to 255");
        CHECK(g_m1_settings.bt_interval_ms == 65535, "bt interval clamps to 65535");
    }

    /* 9: upper bounds and Off are exact-passing */
    {
        len = build_file(buf, sizeof(buf),
            "#ver=1\r\n"
            "brightness_pct=0\r\n"
            "screen_timeout_s=0\r\n"
            "screen_timeout_s=120\r\n"
            "subghz_bw_khz=-5\r\n"
            "subghz_power_dbm=-128\r\n");
        CHECK(settings_parse_buffer(buf, len), "boundary file parses");
        CHECK(g_m1_settings.brightness_pct == 0, "brightness 0 allowed");
        CHECK(g_m1_settings.screen_timeout_s == 120, "later duplicate key wins (120)");
        CHECK(g_m1_settings.subghz_bw_khz == 0, "negative numeral into unsigned field clamps to 0");
        CHECK(g_m1_settings.subghz_power_dbm == -128, "power -128 allowed");
    }

    /* 10: malformed values reset that field to its default */
    {
        len = build_file(buf, sizeof(buf),
            "#ver=1\r\n"
            "brightness_pct=abc\r\n"
            "led_notifications=\r\n"
            "screen_timeout_s=12x4\r\n"
            "subghz_power_dbm=--5\r\n"
            "subghz_freq_hz=99999999999999999999999999\r\n");
        CHECK(settings_parse_buffer(buf, len), "malformed file still parses (tolerant)");
        CHECK(g_m1_settings.brightness_pct == 100, "non-numeric -> default");
        CHECK(g_m1_settings.led_notifications == 1, "empty numeric -> default");
        CHECK(g_m1_settings.screen_timeout_s == 45, "trailing garbage -> default");
        CHECK(g_m1_settings.subghz_power_dbm == 0, "double sign -> default");
        CHECK(g_m1_settings.subghz_freq_hz == 0, "i64 overflow -> default");
    }

    /* 11: string fields */
    {
        len = build_file(buf, sizeof(buf),
            "#ver=1\r\n"
            "device_name=\r\n"
            "bt_adv_name=Keep = Empty\r\n");
        CHECK(settings_parse_buffer(buf, len), "string file parses");
        CHECK(strcmp(g_m1_settings.device_name, "M1") == 0, "empty device_name -> default M1");
        CHECK(strcmp(g_m1_settings.bt_adv_name, "Keep = Empty") == 0, "'=' inside value preserved");

        len = build_file(buf, sizeof(buf), "#ver=1\r\ndevice_name=0123456789012345678901234567890123456789\r\n");
        CHECK(settings_parse_buffer(buf, len), "long name parses");
        CHECK(strlen(g_m1_settings.device_name) == 32, "overlong name truncated to 32 chars");
    }

    /* 12: version gate */
    {
        len = build_file(buf, sizeof(buf), "#ver=2\r\nbrightness_pct=1\r\n");
        CHECK(!settings_parse_buffer(buf, len), "unsupported version rejected");
        CHECK(g_m1_settings.brightness_pct == 100, "rejected version leaves defaults");
    }

    /* 13: serializer refuses control characters in strings (never emit a
     * corruptible line structure) */
    {
        settings_reset_defaults();
        strcpy(g_m1_settings.device_name, "bad\nname");
        CHECK(!settings_serialize(buf, sizeof(buf), &len), "newline in name refused");
        settings_reset_defaults();
        strcpy(g_m1_settings.bt_adv_name, "x\ry");
        CHECK(!settings_serialize(buf, sizeof(buf), &len), "CR in name refused");
    }

    /* 14: string updates leave the field fully terminated with no stale
     * tail -- the final-state half of the memset-then-copy invariant that
     * keeps concurrent settings_get() readers inside the array (the
     * transient itself is not observable single-threaded; the python-side
     * static memset-before-memcpy check pins that half) */
    {
        size_t i;
        int clean_tail = 1;
        settings_reset_defaults();
        settings_store_string(&s_settings_fields[SETTINGS_F_DEVICE_NAME],
                              "0123456789012345678901234567890X", 32);
        CHECK(strlen(g_m1_settings.device_name) == 32, "grow to capacity stores 32 chars");
        CHECK(g_m1_settings.device_name[32] == '\0', "terminator present at full capacity");
        settings_store_string(&s_settings_fields[SETTINGS_F_DEVICE_NAME], "Dim", 3);
        CHECK(strcmp(g_m1_settings.device_name, "Dim") == 0, "shrink leaves no stale tail");
        for (i = 3; i < sizeof(g_m1_settings.device_name); i++)
        {
            if (g_m1_settings.device_name[i] != '\0') { clean_tail = 0; }
        }
        CHECK(clean_tail, "bytes past the new length are all zeroed");
        settings_store_string(&s_settings_fields[SETTINGS_F_BT_ADV_NAME], "Grow", 4);
        CHECK(strcmp(g_m1_settings.bt_adv_name, "Grow") == 0, "empty-field update stores exactly");
    }

    printf("settings pure core: %d/%d checks passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
"""
    build_run(
        tmp,
        "settings_pure_core",
        "#include <stdio.h>\n#include <stdlib.h>\n"
        "#include <string.h>\n#include <stdint.h>\n#include <stdbool.h>\n"
        "#include <stddef.h>\n#include <assert.h>\n"
        '#include "m1_settings_store.h"\n#include "m1_crc32c.h"\n' + core,
    )


# ============================================================================
# In-memory FatFs stand-in (same shape as m1_wifi_persistence_hardening_test.py,
# trimmed to the primitives the settings store calls, plus f_read).
# ============================================================================
VFS_STUBS = (
    r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include "m1_settings_store.h"
#include "m1_crc32c.h"

typedef char TCHAR;
typedef unsigned int UINT;

typedef enum {
    FR_OK = 0, FR_DISK_ERR, FR_INT_ERR, FR_NOT_READY, FR_NO_FILE, FR_NO_PATH,
    FR_INVALID_NAME, FR_DENIED, FR_EXIST, FR_INVALID_OBJECT, FR_WRITE_PROTECTED,
    FR_INVALID_DRIVE, FR_NOT_ENABLED, FR_NO_FILESYSTEM, FR_MKFS_ABORTED,
    FR_TIMEOUT, FR_LOCKED, FR_NOT_ENOUGH_CORE, FR_TOO_MANY_OPEN_FILES,
    FR_INVALID_PARAMETER
} FRESULT;

#define FA_READ          0x01
#define FA_WRITE         0x02
#define FA_CREATE_ALWAYS 0x08

typedef struct { int dummy; } FILINFO;
typedef struct { int slot; long pos; } FIL;

#define VFS_MAX 8
#define VFS_CAP 16384
static struct {
    char   path[80];
    int    exists;
    char   data[VFS_CAP];
    size_t len;
} s_vfs[VFS_MAX];
static int s_vfs_n = 0;

static int vfs_find(const char *path)
{
    int i;
    for (i = 0; i < s_vfs_n; i++) { if (s_vfs[i].exists && strcmp(s_vfs[i].path, path) == 0) return i; }
    return -1;
}
static int vfs_slot_for(const char *path)
{
    int i;
    for (i = 0; i < s_vfs_n; i++) { if (strcmp(s_vfs[i].path, path) == 0) return i; }
    if (s_vfs_n >= VFS_MAX) { fprintf(stderr, "VFS_MAX exceeded\n"); abort(); }
    strncpy(s_vfs[s_vfs_n].path, path, sizeof(s_vfs[0].path) - 1);
    s_vfs[s_vfs_n].exists = 0;
    s_vfs[s_vfs_n].len = 0;
    return s_vfs_n++;
}
static int vfs_exists(const char *path) { return vfs_find(path) >= 0; }
static void vfs_put(const char *path, const char *content)
{
    int slot = vfs_slot_for(path);
    s_vfs[slot].exists = 1;
    s_vfs[slot].len = strlen(content);
    memcpy(s_vfs[slot].data, content, s_vfs[slot].len + 1);
}
static const char *vfs_get(const char *path)
{
    int i = vfs_find(path);
    return i >= 0 ? s_vfs[i].data : NULL;
}
static void vfs_reset(void) { s_vfs_n = 0; }

#define LOG_MAX 128
static char s_calls[LOG_MAX][96];
static int  s_calls_n = 0;
static void logcall(const char *tag, const char *arg)
{
    if (s_calls_n < LOG_MAX) { snprintf(s_calls[s_calls_n++], 96, "%s:%s", tag, arg ? arg : ""); }
}
static int call_count(const char *prefix)
{
    int i, n = 0;
    size_t l = strlen(prefix);
    for (i = 0; i < s_calls_n; i++) { if (strncmp(s_calls[i], prefix, l) == 0) { n++; } }
    return n;
}

static char    g_fail_open_names[4][80]; static FRESULT g_fail_open_codes[4]; static int g_fail_open_n = 0;
static void fault_open(const char *path, FRESULT code)
{ strncpy(g_fail_open_names[g_fail_open_n], path, 79); g_fail_open_codes[g_fail_open_n] = code; g_fail_open_n++; }

static int  g_fail_write_after = -1;
static int  g_write_calls = 0;
static bool g_fail_write_short = false;

static bool g_fail_close = false;

static bool g_fail_read = false;

static char    g_fail_rename_old[80] = ""; static char g_fail_rename_new[80] = "";
static FRESULT g_fail_rename_code = FR_DISK_ERR;

static char g_fail_unlink_path[80] = "";

static bool g_fail_dir_ensure = false;

/* Logger stand-in: count emissions so "log once, never spam" is testable. */
static int s_log_info = 0, s_log_warn = 0;
#define M1_LOG_I(tag, fmt, ...) ((void)(s_log_info++))
#define M1_LOG_W(tag, fmt, ...) ((void)(s_log_warn++))

static void fault_reset(void)
{
    g_fail_open_n = 0;
    g_fail_write_after = -1; g_write_calls = 0; g_fail_write_short = false;
    g_fail_close = false;
    g_fail_read = false;
    g_fail_rename_old[0] = 0; g_fail_rename_new[0] = 0;
    g_fail_unlink_path[0] = 0;
    g_fail_dir_ensure = false;
    s_calls_n = 0; s_log_info = 0; s_log_warn = 0;
}

FRESULT fs_directory_ensure(const char *path)
{
    logcall("dir_ensure", path);
    return g_fail_dir_ensure ? FR_DISK_ERR : FR_OK;
}

FRESULT f_open(FIL *fp, const char *path, UINT mode)
{
    int i, slot;
    logcall("open", path);
    for (i = 0; i < g_fail_open_n; i++) { if (strcmp(g_fail_open_names[i], path) == 0) { return g_fail_open_codes[i]; } }

    slot = vfs_slot_for(path);
    if (mode & FA_CREATE_ALWAYS) {
        s_vfs[slot].exists = 1; s_vfs[slot].len = 0; s_vfs[slot].data[0] = '\0';
    } else if (!s_vfs[slot].exists) {
        return FR_NO_FILE;
    }
    fp->slot = slot; fp->pos = 0;
    return FR_OK;
}

FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br)
{
    int slot = fp->slot;
    size_t avail = (s_vfs[slot].len > (size_t)fp->pos) ? (s_vfs[slot].len - (size_t)fp->pos) : 0;
    size_t n = ((size_t)btr < avail) ? (size_t)btr : avail;
    logcall("read", s_vfs[slot].path);
    if (g_fail_read) { *br = 0; return FR_DISK_ERR; }
    memcpy(buff, s_vfs[slot].data + fp->pos, n);
    fp->pos += (long)n;
    *br = (UINT)n;
    return FR_OK;
}

FRESULT f_write(FIL *fp, const void *buf, UINT btw, UINT *bw)
{
    UINT n = btw;
    g_write_calls++;
    logcall("write", s_vfs[fp->slot].path);
    if (g_fail_write_after == g_write_calls) {
        if (g_fail_write_short) { n = (btw > 0) ? (btw - 1) : 0; }
        else { *bw = 0; return FR_DISK_ERR; }
    }
    if ((size_t)fp->pos + n > VFS_CAP - 1) { n = 0; }
    memcpy(s_vfs[fp->slot].data + fp->pos, buf, n);
    fp->pos += (long)n;
    if ((size_t)fp->pos > s_vfs[fp->slot].len) {
        s_vfs[fp->slot].len = (size_t)fp->pos;
        s_vfs[fp->slot].data[s_vfs[fp->slot].len] = '\0';
    }
    *bw = n;
    return FR_OK;
}

FRESULT f_close(FIL *fp) { logcall("close", s_vfs[fp->slot].path); return g_fail_close ? FR_DISK_ERR : FR_OK; }

#define f_eof(fp) ((fp)->pos >= (long)s_vfs[(fp)->slot].len)

FRESULT f_unlink(const char *path)
{
    int i = vfs_find(path);
    logcall("unlink", path);
    if (g_fail_unlink_path[0] && strcmp(path, g_fail_unlink_path) == 0) { return FR_WRITE_PROTECTED; }
    if (i < 0) { return FR_NO_FILE; }
    s_vfs[i].exists = 0;
    return FR_OK;
}

FRESULT f_rename(const char *oldp, const char *newp)
{
    int oi, ni;
    logcall("rename_old", oldp);
    logcall("rename_new", newp);
    if (g_fail_rename_old[0] && strcmp(oldp, g_fail_rename_old) == 0 &&
        g_fail_rename_new[0] && strcmp(newp, g_fail_rename_new) == 0) { return g_fail_rename_code; }
    oi = vfs_find(oldp);
    if (oi < 0) { return FR_NO_FILE; }
    ni = vfs_slot_for(newp);
    if (s_vfs[ni].exists) { return FR_EXIST; } /* real FatFs: destination must not exist */
    s_vfs[ni].exists = 1;
    s_vfs[ni].len = s_vfs[oi].len;
    memcpy(s_vfs[ni].data, s_vfs[oi].data, s_vfs[oi].len + 1);
    s_vfs[oi].exists = 0;
    return FR_OK;
}

FRESULT f_stat(const char *path, FILINFO *fi)
{
    (void)fi;
    return vfs_exists(path) ? FR_OK : FR_NO_FILE;
}
"""
    + CHECK_MACROS
)


def file_io_tests(tmp):
    mod = source("m1_csrc/m1_settings_store.c")
    prov = source("m1_csrc/m1_sdcard_provision.h")

    code = VFS_STUBS
    code += "\n" + define(prov, "M1_SD_DIR_SYSTEM")
    code += "\n" + define(mod, "SETTINGS_DIR")
    code += "\n" + define(mod, "SETTINGS_PATH")
    code += "\n" + define(mod, "SETTINGS_TMP_PATH")
    code += "\n" + define(mod, "SETTINGS_BAK_PATH")
    code += "\n" + define(mod, "SETTINGS_FILE_MAX")
    code += "\n" + static_buffer(mod, "s_settings_work")
    code += "\n" + define(mod, "M1_LOGDB_TAG")
    code += "\n" + region(mod, PURE_BEGIN, PURE_END)
    for fn in PURE_FUNCTIONS + FILE_FUNCTIONS:
        code += "\n" + function(mod, fn)
    code += (
        "\n"
        + r"""
int main(void)
{
    char big[VFS_CAP];
    char prev_content[VFS_CAP];

    /* Helper closure via macro: place a CRC-valid settings file into the
     * VFS (body must end with CRLF; the trailer is computed for real). */
#define vfs_put_valid(path, body) do { \
        size_t bl_ = strlen(body); \
        uint32_t bc_ = m1_crc32c(body, bl_); \
        memcpy(big, body, bl_); \
        snprintf(big + bl_, sizeof(big) - bl_, "crc=%08X\r\n", (unsigned)bc_); \
        vfs_put(path, big); \
    } while (0)

    /* Compare a VFS file against a body + real-CRC expectation. */
#define CHECK_FILE_EQ(path, body, msg) do { \
        char e_[VFS_CAP]; \
        snprintf(e_, sizeof(e_), "%scrc=%08X\r\n", (body), \
                 (unsigned)m1_crc32c((body), strlen(body))); \
        CHECK(vfs_get(path) != NULL && strcmp(vfs_get(path), e_) == 0, (msg)); \
    } while (0)

    /* 1: missing file -> defaults, no diagnostics storm */
    vfs_reset(); fault_reset();
    g_settings_ready = false; g_settings_loaded_ok = false;
    CHECK(settings_load_file() == SETTINGS_LOAD_MISSING, "missing file -> MISSING");
    CHECK(g_m1_settings.brightness_pct == 100 && g_m1_settings.screen_timeout_s == 45,
          "missing file leaves compiled defaults");

    /* 2: SD absent/unusable -> SD_ERROR, init returns false, exactly one warn, idempotent */
    vfs_reset(); fault_reset();
    g_settings_ready = false; g_settings_loaded_ok = false;
    fault_open(SETTINGS_PATH, FR_NOT_READY);
    CHECK(settings_init() == false, "SD error -> init reports defaults in effect");
    CHECK(s_log_warn == 1, "SD error logged exactly once");
    CHECK(settings_init() == false, "second init is a cached no-op");
    CHECK(s_log_warn == 1, "no repeated logging on second init");
    CHECK(call_count("open") == 1, "second init does not touch the filesystem again");

    /* 2b: mid-read I/O failure -> SD_ERROR (f_read fault surfaced, not
     *     hung or trusted; no FS re-touch beyond the single attempt) */
    vfs_reset(); fault_reset();
    g_settings_ready = false; g_settings_loaded_ok = false;
    vfs_put_valid(SETTINGS_PATH, "#ver=1\r\nbrightness_pct=41\r\n");
    g_fail_read = true;
    CHECK(settings_load_file() == SETTINGS_LOAD_SD_ERROR, "mid-read failure -> SD_ERROR");
    g_fail_read = false;
    CHECK(g_m1_settings.brightness_pct == 100, "mid-read failure leaves defaults");

    /* 3: save + load round-trip through the FatFs boundary, with .bak lifecycle */
    vfs_reset(); fault_reset();
    g_settings_ready = true; g_settings_loaded_ok = true;
    g_m1_settings.brightness_pct = 42;
    strcpy(g_m1_settings.device_name, "Round Trip");
    CHECK(settings_write_file(), "first save succeeds");
    CHECK(vfs_exists(SETTINGS_PATH), "final present after first save");
    CHECK(!vfs_exists(SETTINGS_TMP_PATH) && !vfs_exists(SETTINGS_BAK_PATH),
          "no tmp (and no prior -> no bak) after first save");
    {
        const char *first = vfs_get(SETTINGS_PATH);
        CHECK(first != NULL, "first save produced a readable file");
        if (first != NULL) {
            strncpy(prev_content, first, VFS_CAP - 1); prev_content[VFS_CAP - 1] = '\0';
        }
    }
    g_m1_settings.brightness_pct = 43;
    CHECK(settings_write_file(), "second save succeeds");
    CHECK(vfs_exists(SETTINGS_BAK_PATH), "previous file retained as .bak after second save");
    CHECK(strcmp(vfs_get(SETTINGS_BAK_PATH), prev_content) == 0, ".bak holds the previous content");
    CHECK(!vfs_exists(SETTINGS_TMP_PATH), "tmp consumed by promotion");
    {
        settings_reset_defaults();
        g_settings_ready = false; g_settings_loaded_ok = false;
        fault_reset();
        CHECK(settings_load_file() == SETTINGS_LOAD_OK, "reload of saved file succeeds");
        CHECK(g_m1_settings.brightness_pct == 43, "reloaded value matches last save");
        CHECK(strcmp(g_m1_settings.device_name, "Round Trip") == 0, "reloaded string matches");
        CHECK(vfs_exists(SETTINGS_BAK_PATH), "load keeps the .bak (last known-good)");
    }

    /* 4: corrupt final with .bak present -> CORRUPT, defaults, .bak kept */
    vfs_reset(); fault_reset();
    g_settings_ready = false; g_settings_loaded_ok = false;
    {
        settings_reset_defaults();
        g_settings_ready = true;
        g_m1_settings.brightness_pct = 7;
        CHECK(settings_write_file(), "seed a first settings file");
        g_m1_settings.brightness_pct = 8;
        CHECK(settings_write_file(), "seed a second save (creates .bak)");
        memcpy(big, vfs_get(SETTINGS_PATH), strlen(vfs_get(SETTINGS_PATH)) + 1);
        big[10] = (char)(big[10] ^ 0x01); /* flip a bit inside a value */
        vfs_put(SETTINGS_PATH, big);
    }
    CHECK(settings_load_file() == SETTINGS_LOAD_CORRUPT, "tampered file -> CORRUPT");
    CHECK(g_m1_settings.brightness_pct == 100, "corrupt file -> defaults in RAM");
    CHECK(vfs_exists(SETTINGS_BAK_PATH), "corrupt load keeps .bak for manual recovery");
    g_settings_ready = false;
    fault_reset();
    CHECK(settings_init() == false, "init reports defaults after corruption");
    CHECK(s_log_warn == 1, "corruption logged exactly once");

    /* 5: truncated final (no trailer) -> CORRUPT */
    vfs_reset(); fault_reset();
    g_settings_ready = false; g_settings_loaded_ok = false;
    {
        settings_reset_defaults();
        g_settings_ready = true;
        g_m1_settings.screen_timeout_s = 60;
        CHECK(settings_write_file(), "seed file for truncation");
        memcpy(big, vfs_get(SETTINGS_PATH), strlen(vfs_get(SETTINGS_PATH)) + 1);
        big[40] = '\0'; /* cut mid-file, trailer gone */
        vfs_put(SETTINGS_PATH, big);
    }
    CHECK(settings_load_file() == SETTINGS_LOAD_CORRUPT, "truncated file -> CORRUPT");
    CHECK(g_m1_settings.screen_timeout_s == 45, "truncated file -> defaults in RAM");

    /* 6: oversized final -> CORRUPT (bounded read, never trusted) */
    vfs_reset(); fault_reset();
    g_settings_ready = false; g_settings_loaded_ok = false;
    memset(big, 'x', 2000); big[2000] = '\0';
    vfs_put(SETTINGS_PATH, big);
    CHECK(settings_load_file() == SETTINGS_LOAD_CORRUPT, "oversized file -> CORRUPT");

    /* 6b: exact SETTINGS_FILE_MAX boundary -- a 1024-byte file fits the
     * bounded read exactly and is accepted; 1025 bytes is oversized */
    {
        int total;
        for (total = (int)SETTINGS_FILE_MAX; total <= (int)SETTINGS_FILE_MAX + 1; total++)
        {
            size_t body_len = (size_t)total - 14u; /* trailer "crc=XXXXXXXX\r\n" */
            size_t base = strlen("#ver=1\r\nbrightness_pct=41\r\n");
            vfs_reset(); fault_reset();
            g_settings_ready = false; g_settings_loaded_ok = false;
            assert(body_len > base + 2u);
            memset(big, '#', body_len);
            memcpy(big, "#ver=1\r\nbrightness_pct=41\r\n", base);
            big[body_len - 2u] = '\r'; big[body_len - 1u] = '\n'; /* padding comment line */
            snprintf(big + body_len, 16, "crc=%08X\r\n", (unsigned)m1_crc32c(big, body_len));
            vfs_put(SETTINGS_PATH, big);
            CHECK(vfs_get(SETTINGS_PATH) != NULL && strlen(vfs_get(SETTINGS_PATH)) == (size_t)total,
                  "boundary file has the intended exact size");
            if (total == (int)SETTINGS_FILE_MAX)
            {
                CHECK(settings_load_file() == SETTINGS_LOAD_OK, "exactly-1024-byte file accepted");
                CHECK(g_m1_settings.brightness_pct == 41, "exactly-1024-byte file content applied");
            }
            else
            {
                CHECK(settings_load_file() == SETTINGS_LOAD_CORRUPT, "1025-byte file rejected as oversized");
                CHECK(g_m1_settings.brightness_pct == 100, "oversized boundary leaves defaults");
            }
        }
    }

    /* 7: interrupted-save recovery states */
    /* 7a: no final, tmp+bak (crash between the two renames) -> tmp promoted, bak kept */
    vfs_reset(); fault_reset();
    vfs_put_valid(SETTINGS_BAK_PATH, "#ver=1\r\nbrightness_pct=11\r\n");
    vfs_put_valid(SETTINGS_TMP_PATH, "#ver=1\r\nbrightness_pct=22\r\n");
    CHECK(settings_load_file() == SETTINGS_LOAD_OK, "mid-promotion crash recovers");
    CHECK(vfs_exists(SETTINGS_PATH) && vfs_exists(SETTINGS_BAK_PATH) && !vfs_exists(SETTINGS_TMP_PATH),
          "tmp promoted to final, bak retained");
    CHECK(g_m1_settings.brightness_pct == 22, "promoted (new) content wins");

    /* 7b: no final, tmp only (first save interrupted) -> promoted */
    vfs_reset(); fault_reset();
    vfs_put_valid(SETTINGS_TMP_PATH, "#ver=1\r\nbrightness_pct=33\r\n");
    CHECK(settings_load_file() == SETTINGS_LOAD_OK, "first-save interruption recovers forward");
    CHECK(vfs_exists(SETTINGS_PATH) && !vfs_exists(SETTINGS_TMP_PATH), "orphan tmp promoted");

    /* 7c: no final, bak only -> bak restored as final */
    vfs_reset(); fault_reset();
    vfs_put_valid(SETTINGS_BAK_PATH, "#ver=1\r\nbrightness_pct=44\r\n");
    CHECK(settings_load_file() == SETTINGS_LOAD_OK, "missing final restored from bak");
    CHECK(vfs_exists(SETTINGS_PATH) && !vfs_exists(SETTINGS_BAK_PATH), "bak consumed by restore");
    CHECK(g_m1_settings.brightness_pct == 44, "restored content is the last known-good");

    /* 7d: final+tmp (crash during a later tmp write) -> tmp discarded, final used */
    vfs_reset(); fault_reset();
    vfs_put_valid(SETTINGS_PATH, "#ver=1\r\nbrightness_pct=55\r\n");
    vfs_put_valid(SETTINGS_TMP_PATH, "#ver=1\r\nbrightness_pct=66\r\n");
    CHECK(settings_load_file() == SETTINGS_LOAD_OK, "orphan tmp with valid final loads");
    CHECK(!vfs_exists(SETTINGS_TMP_PATH), "orphan tmp deleted");
    CHECK(g_m1_settings.brightness_pct == 55, "valid final wins over orphan tmp");

    /* 7e: final+bak (normal post-save state) -> untouched */
    vfs_reset(); fault_reset();
    vfs_put_valid(SETTINGS_PATH, "#ver=1\r\nbrightness_pct=77\r\n");
    vfs_put_valid(SETTINGS_BAK_PATH, "#ver=1\r\nbrightness_pct=88\r\n");
    CHECK(settings_load_file() == SETTINGS_LOAD_OK, "normal state loads");
    CHECK(vfs_exists(SETTINGS_BAK_PATH), "normal state keeps bak in place");
    CHECK_FILE_EQ(SETTINGS_BAK_PATH, "#ver=1\r\nbrightness_pct=88\r\n", "bak content not disturbed");

    /* 7f: garbage tmp, no final/bak -> promoted, then CRC-rejected:
     *     recovery only moves candidates, the CRC gate has final say */
    vfs_reset(); fault_reset();
    g_settings_ready = false; g_settings_loaded_ok = false;
    vfs_put(SETTINGS_TMP_PATH, "garbage, not a settings file");
    CHECK(settings_load_file() == SETTINGS_LOAD_CORRUPT, "promoted garbage tmp rejected by the CRC gate");
    CHECK(vfs_exists(SETTINGS_PATH) && !vfs_exists(SETTINGS_TMP_PATH), "garbage tmp was promoted to final");
    CHECK(g_m1_settings.brightness_pct == 100, "defaults after CRC rejection of promoted tmp");

    /* 8: save failure modes never destroy the previous good file */
    /* 8a: tmp open failure */
    vfs_reset(); fault_reset();
    vfs_put_valid(SETTINGS_PATH, "#ver=1\r\nbrightness_pct=90\r\n");
    fault_open(SETTINGS_TMP_PATH, FR_DISK_ERR);
    CHECK(!settings_write_file(), "tmp open failure reported");
    CHECK_FILE_EQ(SETTINGS_PATH, "#ver=1\r\nbrightness_pct=90\r\n",
                  "previous final untouched after tmp open failure");

    /* 8b: short write */
    vfs_reset(); fault_reset();
    vfs_put_valid(SETTINGS_PATH, "#ver=1\r\nbrightness_pct=90\r\n");
    g_fail_write_after = 1; g_fail_write_short = true;
    CHECK(!settings_write_file(), "short write reported");
    CHECK(!vfs_exists(SETTINGS_TMP_PATH), "incomplete tmp cleaned up");
    CHECK_FILE_EQ(SETTINGS_PATH, "#ver=1\r\nbrightness_pct=90\r\n",
                  "previous final untouched after short write");

    /* 8c: hard write error */
    vfs_reset(); fault_reset();
    vfs_put_valid(SETTINGS_PATH, "#ver=1\r\nbrightness_pct=90\r\n");
    g_fail_write_after = 1; g_fail_write_short = false;
    CHECK(!settings_write_file(), "write error reported");
    CHECK(!vfs_exists(SETTINGS_TMP_PATH), "failed tmp cleaned up");

    /* 8d: close failure */
    vfs_reset(); fault_reset();
    vfs_put_valid(SETTINGS_PATH, "#ver=1\r\nbrightness_pct=90\r\n");
    g_fail_close = true;
    CHECK(!settings_write_file(), "close failure reported");
    g_fail_close = false;
    CHECK(!vfs_exists(SETTINGS_TMP_PATH), "tmp cleaned up after close failure");

    /* 8e: rotation rename (cfg->bak) failure */
    vfs_reset(); fault_reset();
    vfs_put_valid(SETTINGS_PATH, "#ver=1\r\nbrightness_pct=90\r\n");
    vfs_put_valid(SETTINGS_BAK_PATH, "#ver=1\r\nbrightness_pct=80\r\n"); /* older backup present */
    strcpy(g_fail_rename_old, SETTINGS_PATH); strcpy(g_fail_rename_new, SETTINGS_BAK_PATH);
    g_fail_rename_code = FR_DISK_ERR;
    CHECK(!settings_write_file(), "rotation failure reported");
    CHECK_FILE_EQ(SETTINGS_PATH, "#ver=1\r\nbrightness_pct=90\r\n",
                  "previous final preserved on rotation failure");
    CHECK(!vfs_exists(SETTINGS_TMP_PATH), "tmp cleaned up after rotation failure");
    CHECK(!vfs_exists(SETTINGS_BAK_PATH),
          "older .bak was already unlinked when the rotation failed (documented semantics)");

    /* 8f: promotion rename (tmp->cfg) failure -> rollback restores previous */
    vfs_reset(); fault_reset();
    vfs_put_valid(SETTINGS_PATH, "#ver=1\r\nbrightness_pct=90\r\n");
    strcpy(g_fail_rename_old, SETTINGS_TMP_PATH); strcpy(g_fail_rename_new, SETTINGS_PATH);
    g_fail_rename_code = FR_DISK_ERR;
    CHECK(!settings_write_file(), "promotion failure reported");
    CHECK_FILE_EQ(SETTINGS_PATH, "#ver=1\r\nbrightness_pct=90\r\n",
                  "previous final rolled back after promotion failure");
    CHECK(!vfs_exists(SETTINGS_TMP_PATH) && !vfs_exists(SETTINGS_BAK_PATH),
          "tmp and bak consumed by cleanup/rollback");

    /* 8g: superseded-.bak unlink failure (e.g. write-protected card):
     *     the save aborts with the actual cause named instead of failing
     *     FR_EXIST at the rotation rename identically on every save */
    vfs_reset(); fault_reset();
    vfs_put_valid(SETTINGS_PATH, "#ver=1\r\nbrightness_pct=90\r\n");
    vfs_put_valid(SETTINGS_BAK_PATH, "#ver=1\r\nbrightness_pct=80\r\n");
    strcpy(g_fail_unlink_path, SETTINGS_BAK_PATH);
    CHECK(!settings_write_file(), "unremovable .bak aborts the save");
    CHECK_FILE_EQ(SETTINGS_PATH, "#ver=1\r\nbrightness_pct=90\r\n",
                  "previous final preserved when .bak cannot be cleared");
    CHECK(vfs_exists(SETTINGS_BAK_PATH), "stale .bak left untouched by the abort");
    CHECK(!vfs_exists(SETTINGS_TMP_PATH), "tmp cleaned up after the abort");
    CHECK(s_log_warn == 1, "abort logged with its own distinct warning");

    /* 9: save before init refused, filesystem untouched */
    vfs_reset(); fault_reset();
    g_settings_ready = false;
    CHECK(!settings_save(), "save before init refused");
    CHECK(s_calls_n == 0, "refused save makes no FatFs calls");
    CHECK(s_log_warn == 1, "refusal logged once");

    /* 10: directory-ensure failure -> save fails before touching files */
    vfs_reset(); fault_reset();
    g_settings_ready = true;
    g_fail_dir_ensure = true;
    CHECK(!settings_write_file(), "unwritable System dir -> save fails");
    CHECK(call_count("open") == 0, "no file opened when dir cannot be ensured");

    /* 11: late-inserted-card guard -- a defaults RAM image can never
     *     overwrite a live settings.cfg the boot load could not account
     *     for (init is one attempt per boot; a card arriving later is
     *     invisible until the next boot) */
    /* 11a: card absent at boot (SD_ERROR), card present later: save
     *      refused, customized file untouched, no FS mutation attempted */
    vfs_reset(); fault_reset();
    g_settings_ready = false; g_settings_loaded_ok = false; g_settings_save_permitted = false;
    vfs_put_valid(SETTINGS_PATH, "#ver=1\r\nbrightness_pct=17\r\ndevice_name=Custom\r\n");
    fault_open(SETTINGS_PATH, FR_NOT_READY);
    CHECK(settings_init() == false, "absent card at boot -> defaults in effect");
    CHECK(g_m1_settings.brightness_pct == 100, "absent card leaves compiled defaults");
    fault_reset(); /* card inserted/remounted later in the same boot */
    g_m1_settings.brightness_pct = 99; /* user edits on top of defaults */
    CHECK(!settings_save(), "save still refused: boot load never accounted for the card");
    CHECK_FILE_EQ(SETTINGS_PATH, "#ver=1\r\nbrightness_pct=17\r\ndevice_name=Custom\r\n",
                  "customized file NOT overwritten with the defaults image");
    CHECK(s_calls_n == 0, "refused save makes no FatFs calls at all");
    CHECK(s_log_warn == 1, "refusal logged exactly once");

    /* 11b: corrupt file at boot (CORRUPT) -> save refused, file and .bak
     *      left for recovery/factory reset */
    vfs_reset(); fault_reset();
    g_settings_ready = false; g_settings_loaded_ok = false; g_settings_save_permitted = false;
    vfs_put(SETTINGS_PATH, "#ver=1\r\nbrightness_pct=17\r\ncrc=DEADBEEF\r\n"); /* stale trailer */
    vfs_put_valid(SETTINGS_BAK_PATH, "#ver=1\r\nbrightness_pct=16\r\n");
    CHECK(settings_init() == false, "corrupt file at boot -> defaults in effect");
    g_m1_settings.brightness_pct = 99;
    CHECK(!settings_save(), "save after an unreadable boot load refused");
    CHECK(strcmp(vfs_get(SETTINGS_PATH), "#ver=1\r\nbrightness_pct=17\r\ncrc=DEADBEEF\r\n") == 0,
          "unreadable file left in place (factory reset is the caller's policy)");
    CHECK_FILE_EQ(SETTINGS_BAK_PATH, "#ver=1\r\nbrightness_pct=16\r\n", ".bak preserved by the refusal");
    CHECK(s_log_warn == 2, "corruption warn + refusal warn, nothing more");

    /* 11c: card present, no file at boot (MISSING) -> save permitted:
     *      a fresh card must be able to gain its first settings file */
    vfs_reset(); fault_reset();
    g_settings_ready = false; g_settings_loaded_ok = false; g_settings_save_permitted = false;
    CHECK(settings_init() == false, "no file at boot -> defaults (init still false)");
    CHECK(settings_save(), "save permitted when the card provably held no file");
    CHECK(vfs_exists(SETTINGS_PATH), "fresh card gained its first settings file");
    {
        settings_reset_defaults();
        g_settings_ready = false; g_settings_loaded_ok = false;
        fault_reset();
        CHECK(settings_load_file() == SETTINGS_LOAD_OK, "saved defaults file reloads");
        CHECK(g_m1_settings.brightness_pct == 100, "default image round-trips");
    }

    /* 11d: successful load at boot (OK) -> save permitted and rotates */
    vfs_reset(); fault_reset();
    g_settings_ready = false; g_settings_loaded_ok = false; g_settings_save_permitted = false;
    vfs_put_valid(SETTINGS_PATH, "#ver=1\r\nbrightness_pct=17\r\n");
    CHECK(settings_init() == true, "valid file at boot loads");
    g_m1_settings.brightness_pct = 18;
    CHECK(settings_save(), "save permitted after a successful boot load");
    CHECK(vfs_exists(SETTINGS_BAK_PATH), "previous file rotated to .bak");
    {
        settings_reset_defaults();
        g_settings_ready = false; g_settings_loaded_ok = false;
        fault_reset();
        CHECK(settings_load_file() == SETTINGS_LOAD_OK, "reload after gated save");
        CHECK(g_m1_settings.brightness_pct == 18, "edited value persisted");
    }

    printf("settings file IO: %d/%d checks passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
"""
    )
    build_run(tmp, "settings_file_io", code)


def boot_wiring_checks_source_text():
    """The boot-order wiring in m1_sys_init.c is HAL/FreeRTOS-coupled and
    cannot be host-executed; verified against the committed source text
    (same DISCLOSED LIMITATION pattern as every HAL-coupled file, see
    m1_csrc/test/m1_sdcard_provision_test.c)."""
    init_task = source("m1_csrc/m1_sys_init.c")
    store = source("m1_csrc/m1_settings_store.c")

    i_sd = init_task.index("m1_sdcard_init(&hsd1);")
    i_set = init_task.index("settings_init();")
    i_bright = init_task.index("fb_set_brightness_pct(settings_get()->brightness_pct);")
    i_tasks = init_task.index("m1_tasks_init();")
    checks = [
        ("settings_init() runs after SD init", i_sd < i_set),
        ("settings_init() runs before task creation", i_set < i_tasks),
        (
            "brightness applied after task creation, so fb_manager_init()'s "
            "default reset (reached via m1_tasks_init -> m1_feedback_task_init) "
            "cannot clobber the persisted value",
            i_set < i_tasks < i_bright,
        ),
        (
            "settings store never touches flash config metadata",
            "S_M1_FW_CONFIG_t" not in store and "HAL_FLASH" not in store,
        ),
    ]
    passed = 0
    for msg, ok in checks:
        if ok:
            passed += 1
        else:
            print(f"  FAIL: {msg}")
    print(f"boot wiring source verification: {passed}/{len(checks)} checks passed")
    assert passed == len(checks)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        pure_core_tests(tmp)
        file_io_tests(tmp)
    boot_wiring_checks_source_text()
    print("All settings-store host tests PASS.")
