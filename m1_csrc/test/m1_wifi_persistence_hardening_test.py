#!/usr/bin/env python3
"""Host tests for the Wi-Fi/MonstaShark SD-file persistence hardening pass.

m1_wifi.c and m1_capture_link.c require STM32 HAL/FreeRTOS/ESP-UART/u8g2 and
cannot be host-built as whole translation units (same limitation documented
in m1_csrc/test/m1_wifi_capture_ui_unification_test.c and
m1_csrc/test/m1_pass3_helpers_test.py). This file uses the same technique as
m1_pass3_helpers_test.py: extract the REAL, unmodified function bodies (and
the plain #define/typedef/static-array declarations they depend on) out of
the live source file with a source-text regex, and host-compile them against
a small in-memory FatFs stand-in defined here, so the actual production
control flow -- not a re-implementation of it -- is what gets exercised
under fault injection.

From the repository root: python3 m1_csrc/test/m1_wifi_persistence_hardening_test.py
"""

import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    return (ROOT / path).read_text()


def function(text, name):
    match = re.search(r"^[^\n;]*\b" + name + r"\([^;{}]*\)\s*\n\{", text, re.M)
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
    m = re.search(r"^#define\s+" + name + r"\b.*$", text, re.M)
    assert m, name
    return m.group()


def line(text, pattern):
    m = re.search(pattern, text, re.M)
    assert m, pattern
    return m.group()


def simple_typedef(text, name):
    """Extract a flat (non-nested-brace) typedef struct ending in '} name;',
    anchored on the closing marker so it can't run away and swallow an
    unrelated earlier 'typedef struct {' elsewhere in the file."""
    end_marker = "} " + name + ";"
    end_idx = text.index(end_marker) + len(end_marker)
    start_idx = text.rindex("typedef struct", 0, end_idx)
    return text[start_idx:end_idx]


def enum_typedef(text, name):
    """Extract a 'typedef enum { ... } name;' block, anchored on the
    closing marker (same anti-runaway technique as simple_typedef --
    a naive non-greedy regex would start at the file's FIRST 'typedef
    enum {' and swallow everything up to this one's closer)."""
    end_marker = "} " + name + ";"
    end_idx = text.index(end_marker) + len(end_marker)
    start_idx = text.rindex("typedef enum", 0, end_idx)
    return text[start_idx:end_idx]


def host_cc():
    """Same compiler probe as m1_crypto_test.py / m1_ir_favorites_test.py:
    cc (POSIX) then gcc/clang (a Windows host has no cc)."""
    for cc in ("cc", "gcc", "clang"):
        try:
            subprocess.run([cc, "--version"], capture_output=True, check=True)
            return cc
        except (FileNotFoundError, subprocess.CalledProcessError):
            continue
    raise AssertionError("no usable host C compiler (cc/gcc/clang) found")


# Per-build sanitizer availability, so the final banner reports what
# actually ran (plain builds on toolchains without libasan/libubsan).
SANITIZER_MODES = []


def build_run(tmp, name, text, extra_sources=None, extra_flags=None):
    src = Path(tmp) / f"{name}.c"
    src.write_text(text)
    exe = Path(tmp) / name
    cmd = [
        host_cc(),
        "-std=c11",
        # The extracted store code calls strnlen(), which POSIX/glibc exposes
        # only under a feature macro; -std=c11 alone hides it on Linux CI.
        "-D_GNU_SOURCE",
        "-Wall",
        "-Wextra",
        "-Wno-unused-function",
        "-g",
    ]
    if extra_flags:
        cmd += extra_flags
    cmd.append(str(src))
    if extra_sources:
        cmd += [str(s) for s in extra_sources]
    cmd += ["-o", str(exe)]
    # Sanitizers when the toolchain ships the runtime (Linux/macOS CI),
    # plain build otherwise -- same fallback policy as the
    # feedback-manager suite in scripts/run_host_tests.sh (e.g. WinLibs
    # mingw has no libasan/libubsan).
    r = subprocess.run(
        cmd + ["-fsanitize=address,undefined"], cwd=ROOT, capture_output=True, text=True
    )
    if r.returncode != 0:
        subprocess.run(cmd, cwd=ROOT, check=True)
        SANITIZER_MODES.append(False)
    else:
        SANITIZER_MODES.append(True)
    subprocess.run([str(exe)], check=True)


# ============================================================================
# Shared in-memory FatFs stand-in. Paths are plain strings; content is a
# small fixed buffer per slot. Every primitive the extracted production code
# actually calls is backed by real logic here (including real FatFs
# semantics that matter to the logic under test -- e.g. f_rename() fails
# FR_EXIST if the destination already exists, it does not overwrite), with
# fault-injection knobs so tests can force a specific call to fail.
# ============================================================================
VFS_STUBS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

typedef char TCHAR;
typedef unsigned int UINT;
typedef unsigned long DWORD;
typedef unsigned char BYTE;

typedef enum {
    FR_OK = 0, FR_DISK_ERR, FR_INT_ERR, FR_NOT_READY, FR_NO_FILE, FR_NO_PATH,
    FR_INVALID_NAME, FR_DENIED, FR_EXIST, FR_INVALID_OBJECT, FR_WRITE_PROTECTED,
    FR_INVALID_DRIVE, FR_NOT_ENABLED, FR_NO_FILESYSTEM, FR_MKFS_ABORTED,
    FR_TIMEOUT, FR_LOCKED, FR_NOT_ENOUGH_CORE, FR_TOO_MANY_OPEN_FILES,
    FR_INVALID_PARAMETER
} FRESULT;

#define FA_READ          0x01
#define FA_WRITE         0x02
#define FA_CREATE_NEW    0x04
#define FA_CREATE_ALWAYS 0x08

#define FF_MAX_SS 512
#define SDCARD_DEFAULT_DRIVE_PATH "0:/"

typedef struct { DWORD csize; } FATFS;
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

static int  g_fail_write_after = -1;   /* fail the Nth f_write call (1-based); -1 = never */
static int  g_write_calls = 0;
static bool g_fail_write_short = false; /* short write (FR_OK, fewer bytes) instead of hard error */

static bool g_fail_sync = false;
static bool g_fail_close = false;

static char    g_fail_rename_old[80] = ""; static char g_fail_rename_new[80] = "";
static FRESULT g_fail_rename_code = FR_DISK_ERR;

static char    g_stat_error_path[80] = ""; static FRESULT g_stat_error_code = FR_DISK_ERR;

static DWORD   g_free_clusters = 1000000; static FRESULT g_getfree_result = FR_OK;
static FATFS   s_fatfs = { 8 };

static bool g_fail_dir_ensure = false;

static void fault_reset(void)
{
    g_fail_open_n = 0;
    g_fail_write_after = -1; g_write_calls = 0; g_fail_write_short = false;
    g_fail_sync = false; g_fail_close = false;
    g_fail_rename_old[0] = 0; g_fail_rename_new[0] = 0;
    g_stat_error_path[0] = 0;
    g_free_clusters = 1000000; g_getfree_result = FR_OK;
    g_fail_dir_ensure = false;
    s_calls_n = 0;
}

FRESULT fs_directory_ensure(const char *path)
{
    logcall("dir_ensure", path);
    return g_fail_dir_ensure ? FR_DISK_ERR : FR_OK;
}

FRESULT f_open(FIL *fp, const char *path, BYTE mode)
{
    int i, slot;
    logcall("open", path);
    for (i = 0; i < g_fail_open_n; i++) { if (strcmp(g_fail_open_names[i], path) == 0) { return g_fail_open_codes[i]; } }

    slot = vfs_slot_for(path);
    if (mode & FA_CREATE_NEW) {
        if (s_vfs[slot].exists) { return FR_EXIST; }
        s_vfs[slot].exists = 1; s_vfs[slot].len = 0; s_vfs[slot].data[0] = '\0';
    } else if (mode & FA_CREATE_ALWAYS) {
        s_vfs[slot].exists = 1; s_vfs[slot].len = 0; s_vfs[slot].data[0] = '\0';
    } else {
        if (!s_vfs[slot].exists) { return FR_NO_FILE; }
    }
    fp->slot = slot; fp->pos = 0;
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

TCHAR *f_gets(TCHAR *buf, int len, FIL *fp)
{
    int slot = fp->slot;
    int n = 0;
    if (fp->pos >= (long)s_vfs[slot].len) { return NULL; }
    while (n < (len - 1) && (fp->pos + n) < (long)s_vfs[slot].len) {
        char c = s_vfs[slot].data[fp->pos + n];
        buf[n] = c;
        n++;
        if (c == '\n') { break; }
    }
    buf[n] = '\0';
    fp->pos += n;
    return buf;
}
#define f_eof(fp) ((fp)->pos >= (long)s_vfs[(fp)->slot].len)

FRESULT f_sync(FIL *fp) { logcall("sync", s_vfs[fp->slot].path); return g_fail_sync ? FR_DISK_ERR : FR_OK; }
FRESULT f_close(FIL *fp) { logcall("close", s_vfs[fp->slot].path); return g_fail_close ? FR_DISK_ERR : FR_OK; }

FRESULT f_unlink(const char *path)
{
    int i = vfs_find(path);
    logcall("unlink", path);
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
    if (g_stat_error_path[0] && strcmp(path, g_stat_error_path) == 0) { return g_stat_error_code; }
    return vfs_exists(path) ? FR_OK : FR_NO_FILE;
}

FRESULT f_getfree(const char *path, DWORD *nclst, FATFS **fatfs)
{
    (void)path;
    *nclst = g_free_clusters;
    *fatfs = &s_fatfs;
    return g_getfree_result;
}

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) { g_pass++; } else { g_fail++; printf("  FAIL: %s\n", (m)); } } while (0)
"""


def eapol_tests(tmp):
    wifi = source("m1_csrc/m1_wifi.c")
    code = VFS_STUBS
    code += "\n" + define(wifi, "WIFI_EAPOL_SPACE_MARGIN_BYTES")
    code += "\n" + function(wifi, "wifi_sd_has_free_bytes")
    code += "\n" + function(wifi, "wifi_eapol_save")
    code += r"""
int main(void)
{
    FIL dummy; (void)dummy;
    char buf[8] = "ABCDEFG";

    /* 1: NULL path rejected, nothing touched */
    vfs_reset(); fault_reset();
    CHECK(wifi_eapol_save(NULL, buf, 7) == false, "NULL path rejected");
    CHECK(s_calls_n == 0, "NULL path: no FatFs calls made");

    /* 2: NULL buf rejected */
    vfs_reset(); fault_reset();
    CHECK(wifi_eapol_save("0:/wifi/handshake_x.txt", NULL, 7) == false, "NULL buf rejected");

    /* 3: zero-size rejected */
    vfs_reset(); fault_reset();
    CHECK(wifi_eapol_save("0:/wifi/handshake_x.txt", buf, 0) == false, "zero size rejected");
    CHECK(s_calls_n == 0, "zero size: no FatFs calls made");

    /* 4: insufficient space -> fails before any f_open */
    vfs_reset(); fault_reset();
    g_free_clusters = 0; /* 0 bytes free */
    CHECK(wifi_eapol_save("0:/wifi/handshake_x.txt", buf, 7) == false, "insufficient space rejected");
    CHECK(call_count("open") == 0, "insufficient space: file never opened");

    /* 5: exact boundary -- free bytes just under required+margin -> rejected */
    vfs_reset(); fault_reset();
    g_free_clusters = 1; s_fatfs.csize = 1; /* 512 bytes free total, via FF_MAX_SS */
    CHECK(wifi_eapol_save("0:/wifi/handshake_x.txt", buf, 7) == false, "just-under-margin space rejected");

    /* 6: full successful save */
    vfs_reset(); fault_reset();
    CHECK(wifi_eapol_save("0:/wifi/handshake_x.txt", buf, 7) == true, "full save succeeds");
    CHECK(strcmp(vfs_get("0:/wifi/handshake_x.txt"), "ABCDEFG") == 0, "saved bytes are byte-for-byte correct");
    CHECK(call_count("sync") == 1 && call_count("close") == 1, "sync and close both called on success");

    /* 7: existing user file is never overwritten (FA_CREATE_NEW -> FR_EXIST) */
    vfs_reset(); fault_reset();
    vfs_put("0:/wifi/handshake_x.txt", "PRIOR-USER-DATA");
    CHECK(wifi_eapol_save("0:/wifi/handshake_x.txt", buf, 7) == false, "existing file rejected");
    CHECK(strcmp(vfs_get("0:/wifi/handshake_x.txt"), "PRIOR-USER-DATA") == 0, "existing file left untouched");
    CHECK(call_count("unlink") == 0, "existing-file case: no cleanup unlink issued");

    /* 8: short write -> failure + cleanup unlink of the newly-created file */
    vfs_reset(); fault_reset();
    g_fail_write_after = 1; g_fail_write_short = true;
    CHECK(wifi_eapol_save("0:/wifi/handshake_y.txt", buf, 7) == false, "short write reported as failure");
    CHECK(!vfs_exists("0:/wifi/handshake_y.txt"), "incomplete new file cleaned up after short write");

    /* 9: write hard failure -> failure + cleanup */
    vfs_reset(); fault_reset();
    g_fail_write_after = 1; g_fail_write_short = false;
    CHECK(wifi_eapol_save("0:/wifi/handshake_y.txt", buf, 7) == false, "write error reported as failure");
    CHECK(!vfs_exists("0:/wifi/handshake_y.txt"), "incomplete new file cleaned up after write error");

    /* 10: sync failure -> failure + cleanup, even though write fully succeeded */
    vfs_reset(); fault_reset();
    g_fail_sync = true;
    CHECK(wifi_eapol_save("0:/wifi/handshake_y.txt", buf, 7) == false, "sync failure reported as failure");
    CHECK(!vfs_exists("0:/wifi/handshake_y.txt"), "incomplete new file cleaned up after sync failure");

    /* 11: close failure -> failure + cleanup */
    vfs_reset(); fault_reset();
    g_fail_close = true;
    CHECK(wifi_eapol_save("0:/wifi/handshake_y.txt", buf, 7) == false, "close failure reported as failure");
    CHECK(!vfs_exists("0:/wifi/handshake_y.txt"), "incomplete new file cleaned up after close failure");

    printf("wifi_eapol_save / wifi_sd_has_free_bytes: %d/%d checks passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
"""
    build_run(tmp, "eapol_tests", code)


SAVED_NET_STUB_PRELUDE = r"""
typedef struct {
    char ssid[33];
    char bssid[18];
    int  channel;
    int  rssi;
    int  encryption_mode;
} wifi_scanlist_t;
/* Real wifi_auth_mode_to_str() depends on the vendor ESP-AT header's
 * WIFI_AUTH_* enum (not present in this host-test tree); this test only
 * needs SOME deterministic string to flow through, since the persistence
 * logic under test treats it as an opaque field -- not the real function,
 * disclosed here rather than pulled from the unavailable vendor header. */
static const char *wifi_auth_mode_to_str(int mode) { return mode ? "WPA2" : "OPEN"; }
/* Real wifi_cred_device_uid() reads the STM32H5 UID words through the
 * HAL (not host-buildable); the credential codec compiled here calls
 * this injected stand-in, which is exactly the foreign-UID test seam
 * (change t_uid between seal and load to simulate another device). */
#include "m1_crypto.h"
static uint8_t t_uid[M1_UID_LEN] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC};
static void wifi_cred_device_uid(uint8_t uid[M1_UID_LEN]) { memcpy(uid, t_uid, M1_UID_LEN); }
"""

SAVED_NET_MAIN_START = r"""
int main(void)
{
"""


def saved_net_prefix(wifi, provision_h):
    """Defines + types + seams shared by every saved-networks host
    program: the size/#define set, the credential-state enum, the entry
    typedef, the in-memory store globals, and the stand-in prelude with
    the injectable device-UID seam (real crypto linked from
    m1_crypto.c via -I)."""
    code = VFS_STUBS
    code += "\n" + define(provision_h, "M1_SD_DIR_WIFI")
    for d in (
        "DRIVE0_WIFI",
        "WIFI_SAVED_PATH",
        "WIFI_SAVED_TMP_PATH",
        "WIFI_SAVED_BAK_PATH",
        "WIFI_SAVED_MAX",
        "WIFI_SAVED_FMT_VER",
        "WIFI_SAVED_LINE_MAX",
        "WIFI_SAVED_LINE_BUF",
        "WIFI_CRED_FIELD_MAX",
        "WIFI_PSK_MAX_CHARS",
        "WIFI_PSK_PT_LEN",
        "WIFI_PSK_IV_LEN",
        "WIFI_PSK_CT_LEN",
        "WIFI_CRED_B64_IVCT_LEN",
        "WIFI_CRED_B64_MAC_LEN",
    ):
        code += "\n" + define(wifi, d)
    code += "\n" + enum_typedef(wifi, "wifi_cred_state_t")
    code += "\n" + simple_typedef(wifi, "wifi_saved_entry_t")
    code += "\n" + line(wifi, r"^static wifi_saved_entry_t g_saved\[WIFI_SAVED_MAX\];$")
    code += "\n" + line(wifi, r"^static uint16_t g_saved_count = 0;$")
    code += SAVED_NET_STUB_PRELUDE
    return code


def saved_networks_tests(tmp):
    wifi = source("m1_csrc/m1_wifi.c")
    provision_h = source("m1_csrc/m1_sdcard_provision.h")
    code = saved_net_prefix(wifi, provision_h)
    for fn in (
        "wifi_bssid_to_upper",
        "wifi_bssid_is_canonical",
        "wifi_parse_int_field",
        "wifi_csv_quote",
        "wifi_csv_next_field",
        "wifi_b64_val",
        "wifi_b64_decode",
        "wifi_cred_key",
        "wifi_cred_parse_verify",
        "wifi_cred_check",
        "wifi_saved_find",
        "wifi_saved_recover_replace",
        "wifi_saved_load",
        "wifi_saved_write_recoverable",
        "wifi_saved_add_or_update",
        "wifi_saved_delete_index",
    ):
        code += "\n" + function(wifi, fn)
    code += SAVED_NET_MAIN_START
    code += r"""
    wifi_scanlist_t ap;
    memset(&ap, 0, sizeof(ap));
    strcpy(ap.ssid, "TestNet"); strcpy(ap.bssid, "aa:bb:cc:dd:ee:01");
    ap.channel = 6; ap.rssi = -50;

    /* ---- Part B: recoverable write ---- */

    /* 1: first-ever save (no prior final) succeeds, .bak/.tmp cleaned up */
    vfs_reset(); fault_reset(); g_saved_count = 0;
    CHECK(wifi_saved_add_or_update(&ap) == 1, "first add reports 'added'");
    CHECK(vfs_exists(WIFI_SAVED_PATH), "final database present after first save");
    CHECK(!vfs_exists(WIFI_SAVED_TMP_PATH) && !vfs_exists(WIFI_SAVED_BAK_PATH), "tmp/bak cleaned up on success");
    CHECK(g_saved_count == 1, "in-memory count reflects the add");

    /* 2: update the same BSSID -> 'updated', still one entry, previous db preserved+cleaned */
    ap.channel = 11;
    CHECK(wifi_saved_add_or_update(&ap) == 0, "second call on same BSSID reports 'updated'");
    CHECK(g_saved_count == 1, "update does not grow the count");
    CHECK(g_saved[0].channel == 11, "updated field is reflected in memory");

    /* 3: promotion-rename failure (tmp->final) restores the previous database and rolls back memory */
    {
        wifi_saved_entry_t before = g_saved[0];
        vfs_reset(); fault_reset(); g_saved_count = 0;
        CHECK(wifi_saved_add_or_update(&ap) == 1, "seed a valid prior database");
        before = g_saved[0];
        strcpy(g_fail_rename_old, WIFI_SAVED_TMP_PATH); strcpy(g_fail_rename_new, WIFI_SAVED_PATH);
        g_fail_rename_code = FR_DISK_ERR;
        ap.rssi = -30; /* attempted update */
        CHECK(wifi_saved_add_or_update(&ap) == 3, "promotion failure reports save-failed");
        CHECK(g_saved[0].rssi == before.rssi, "in-memory update rolled back after save failure");
        CHECK(vfs_exists(WIFI_SAVED_PATH), "a database is still present after failed promotion");
        {
            wifi_saved_entry_t saved_prev = g_saved[0];
            g_saved_count = 0;
            fault_reset();
            CHECK(wifi_saved_load() == 0, "reload after failed promotion succeeds");
            CHECK(g_saved_count == 1 && g_saved[0].rssi == saved_prev.rssi,
                  "reloaded database still holds the pre-failure value, not lost");
        }
    }

    /* 4: add-failure rolls back the in-memory add (list does not grow) */
    vfs_reset(); fault_reset(); g_saved_count = 0;
    fault_open(WIFI_SAVED_TMP_PATH, FR_DISK_ERR);
    CHECK(wifi_saved_add_or_update(&ap) == 3, "add with unwritable tmp reports failure");
    CHECK(g_saved_count == 0, "failed add does not leave a phantom entry");

    /* 5: list-full rejects before ever touching SD (brand-new BSSID, not
     * already present, so this really exercises the full-list branch and
     * not an in-place update of an existing entry) */
    vfs_reset(); fault_reset();
    g_saved_count = WIFI_SAVED_MAX;
    {
        wifi_scanlist_t new_ap = ap;
        strcpy(new_ap.bssid, "aa:bb:cc:dd:ee:99");
        CHECK(wifi_saved_add_or_update(&new_ap) == 2, "full list rejected");
    }
    CHECK(s_calls_n == 0, "full-list rejection touches no FatFs call");
    g_saved_count = 0;

    /* 6: delete failure restores the removed entry and original ordering */
    vfs_reset(); fault_reset(); g_saved_count = 0;
    {
        wifi_scanlist_t a2 = ap; strcpy(a2.bssid, "aa:bb:cc:dd:ee:02");
        wifi_scanlist_t a3 = ap; strcpy(a3.bssid, "aa:bb:cc:dd:ee:03");
        CHECK(wifi_saved_add_or_update(&ap) == 1, "seed entry 1");
        CHECK(wifi_saved_add_or_update(&a2) == 1, "seed entry 2");
        CHECK(wifi_saved_add_or_update(&a3) == 1, "seed entry 3");
    }
    CHECK(g_saved_count == 3, "three entries present before delete attempt");
    {
        wifi_saved_entry_t mid = g_saved[1];
        fault_open(WIFI_SAVED_TMP_PATH, FR_DISK_ERR);
        CHECK(wifi_saved_delete_index(1) == 1, "delete-with-write-failure reports failure");
        CHECK(g_saved_count == 3, "count restored to 3 after failed delete");
        CHECK(strcmp(g_saved[1].bssid, mid.bssid) == 0, "deleted entry restored at its original index");
    }

    /* 7: successful delete persists and shrinks the count */
    fault_reset();
    CHECK(wifi_saved_delete_index(1) == 0, "delete succeeds once SD is writable again");
    CHECK(g_saved_count == 2, "count reflects the successful delete");

    /* ---- interrupted-replacement recovery (deterministic, at load time) ---- */

    /* 8: final absent, bak+tmp present (proven complete) -> forward-completes */
    vfs_reset(); fault_reset(); g_saved_count = 0;
    vfs_put(WIFI_SAVED_BAK_PATH, "#M1SAVEDNET\r\nOLD\r\n");
    vfs_put(WIFI_SAVED_TMP_PATH, "#M1SAVEDNET\r\n1,\"N\",AA:BB:CC:DD:EE:09,1,-40,\"OPEN\",\r\n");
    CHECK(wifi_saved_load() == 0, "load recovers an interrupted promotion forward");
    CHECK(vfs_exists(WIFI_SAVED_PATH) && !vfs_exists(WIFI_SAVED_BAK_PATH) && !vfs_exists(WIFI_SAVED_TMP_PATH),
          "forward recovery leaves exactly one clean final database");
    CHECK(g_saved_count == 1 && strcmp(g_saved[0].bssid, "AA:BB:CC:DD:EE:09") == 0,
          "forward-recovered database is the new (tmp) content");

    /* 9: final present, bak present (crash right after promotion, before bak cleanup) -> bak discarded, final used */
    vfs_reset(); fault_reset(); g_saved_count = 0;
    vfs_put(WIFI_SAVED_PATH, "#M1SAVEDNET\r\n1,\"CUR\",AA:BB:CC:DD:EE:10,1,-40,\"OPEN\",\r\n");
    vfs_put(WIFI_SAVED_BAK_PATH, "#M1SAVEDNET\r\nOLD\r\n");
    CHECK(wifi_saved_load() == 0, "load with a stale leftover bak succeeds");
    CHECK(!vfs_exists(WIFI_SAVED_BAK_PATH), "stale bak cleaned up");
    CHECK(g_saved_count == 1 && strcmp(g_saved[0].bssid, "AA:BB:CC:DD:EE:10") == 0, "current final database used, not the stale bak");

    /* 10: final absent, bak present, tmp absent (new content lost) -> restore from bak */
    vfs_reset(); fault_reset(); g_saved_count = 0;
    vfs_put(WIFI_SAVED_BAK_PATH, "#M1SAVEDNET\r\n1,\"RESTORED\",AA:BB:CC:DD:EE:11,1,-40,\"OPEN\",\r\n");
    CHECK(wifi_saved_load() == 0, "load with only a bak present restores it");
    CHECK(vfs_exists(WIFI_SAVED_PATH) && !vfs_exists(WIFI_SAVED_BAK_PATH), "bak promoted to final");
    CHECK(g_saved_count == 1 && strcmp(g_saved[0].bssid, "AA:BB:CC:DD:EE:11") == 0, "restored database is the last known-good one");

    /* 11: missing file entirely -> empty list, not an error */
    vfs_reset(); fault_reset(); g_saved_count = 99;
    CHECK(wifi_saved_load() == 0, "missing database file is not an error");
    CHECK(g_saved_count == 0, "missing database loads as empty");

    /* 12: genuine SD error on open (not missing) is reported distinctly */
    vfs_reset(); fault_reset();
    fault_open(WIFI_SAVED_PATH, FR_DISK_ERR);
    CHECK(wifi_saved_load() == 2, "real SD error on open is reported, not treated as empty");

    /* ---- Part C: bounded loader ---- */

    /* 13: max 64-entry reload enforced even if the file holds more */
    {
        char big[20000]; size_t off = 0; int i;
        off += (size_t)snprintf(big + off, sizeof(big) - off, "#M1SAVEDNET\r\n");
        for (i = 0; i < WIFI_SAVED_MAX + 5; i++) {
            off += (size_t)snprintf(big + off, sizeof(big) - off,
                "1,\"N%d\",AA:BB:CC:DD:%02X:%02X,%d,-40,\"OPEN\",\r\n", i, (i >> 8) & 0xFF, i & 0xFF, i % 14);
        }
        vfs_reset(); fault_reset(); g_saved_count = 0;
        vfs_put(WIFI_SAVED_PATH, big);
        CHECK(wifi_saved_load() == 0, "oversized file loads without error");
        CHECK(g_saved_count == WIFI_SAVED_MAX, "load is capped at WIFI_SAVED_MAX entries");
    }

    /* 14: overlong/truncated record (no line ending in the buffer) is rejected, not misparsed */
    {
        char content[600]; size_t off = 0; size_t i;
        off += (size_t)snprintf(content + off, sizeof(content) - off, "#M1SAVEDNET\r\n");
        for (i = 0; i < 300; i++) { content[off++] = 'A'; } /* no comma/newline: one giant unterminated field */
        content[off] = '\0';
        strcat(content, "\r\n1,\"OK\",AA:BB:CC:DD:EE:12,1,-40,\"OPEN\",\r\n");
        vfs_reset(); fault_reset(); g_saved_count = 0;
        vfs_put(WIFI_SAVED_PATH, content);
        CHECK(wifi_saved_load() == 0, "file with an overlong record still loads");
        CHECK(g_saved_count == 1 && strcmp(g_saved[0].bssid, "AA:BB:CC:DD:EE:12") == 0,
              "overlong record rejected, following valid record still recovered");
    }

    /* 15: unsupported format version rejected */
    vfs_reset(); fault_reset(); g_saved_count = 0;
    vfs_put(WIFI_SAVED_PATH, "#M1SAVEDNET\r\n99,\"N\",AA:BB:CC:DD:EE:13,1,-40,\"OPEN\",\r\n");
    CHECK(wifi_saved_load() == 0 && g_saved_count == 0, "wrong format version row is skipped");

    /* 16: malformed BSSID rejected */
    vfs_reset(); fault_reset(); g_saved_count = 0;
    vfs_put(WIFI_SAVED_PATH, "#M1SAVEDNET\r\n1,\"N\",not-a-bssid,1,-40,\"OPEN\",\r\n");
    CHECK(wifi_saved_load() == 0 && g_saved_count == 0, "malformed BSSID row is skipped");

    /* 17: malformed numeric field rejected (not silently truncated like atoi) */
    vfs_reset(); fault_reset(); g_saved_count = 0;
    vfs_put(WIFI_SAVED_PATH, "#M1SAVEDNET\r\n1,\"N\",AA:BB:CC:DD:EE:14,6xyz,-40,\"OPEN\",\r\n");
    CHECK(wifi_saved_load() == 0 && g_saved_count == 0, "malformed channel field row is skipped");

    /* 18: duplicate BSSID across rows is deduplicated deterministically (first wins) */
    vfs_reset(); fault_reset(); g_saved_count = 0;
    vfs_put(WIFI_SAVED_PATH,
        "#M1SAVEDNET\r\n"
        "1,\"First\",AA:BB:CC:DD:EE:15,1,-40,\"OPEN\",\r\n"
        "1,\"Second\",aa:bb:cc:dd:ee:15,6,-70,\"WPA2\",\r\n");
    CHECK(wifi_saved_load() == 0, "file with a duplicate BSSID loads");
    CHECK(g_saved_count == 1 && strcmp(g_saved[0].ssid, "First") == 0, "first-seen entry wins the duplicate BSSID");

    /* 19: close error on load is reported */
    vfs_reset(); fault_reset(); g_saved_count = 0;
    vfs_put(WIFI_SAVED_PATH, "#M1SAVEDNET\r\n1,\"N\",AA:BB:CC:DD:EE:16,1,-40,\"OPEN\",\r\n");
    g_fail_close = true;
    CHECK(wifi_saved_load() == 2, "close failure on load is reported as an error");

    printf("saved-networks recoverable persistence: %d/%d checks passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
"""
    build_run(
        tmp,
        "saved_networks_tests",
        code,
        extra_sources=[ROOT / "m1_csrc" / "m1_crypto.c"],
        extra_flags=["-I", str(ROOT / "m1_csrc")],
    )


CRED_RNG_SEAM = r"""
/* Injectable hardware-RNG seam (real m1_rng.c is HAL-bound): a
 * deterministic stream so tests can reproduce fields, plus a
 * failure knob proving the seal refuses instead of falling back.
 * Signature pinned by the real header. */
#include "m1_rng.h"
static bool t_rng_fail = false;
static int  t_rng_calls = 0;
static unsigned t_rng_state = 0x1234u;
bool m1_rng_bytes(uint8_t *buf, size_t len)
{
    size_t i;
    t_rng_calls++;
    if (t_rng_fail) { return false; }
    for (i = 0; i < len; i++)
    {
        t_rng_state = (t_rng_state * 1103515245u) + 12345u;
        buf[i] = (uint8_t)(t_rng_state >> 16);
    }
    return true;
}
"""


def saved_networks_cred_tests(tmp):
    """PR-9 encrypted-credential battery: base64 vectors, seal/unseal
    round-trips, RNG refusal, 7/8-column compatibility, ciphertext
    tamper, wrong CMAC, truncation, foreign-UID via the injectable key
    seam, corrupt-row tolerance, rollback, and the host-testable
    zeroization contracts."""
    wifi = source("m1_csrc/m1_wifi.c")
    provision_h = source("m1_csrc/m1_sdcard_provision.h")
    code = saved_net_prefix(wifi, provision_h)
    code += CRED_RNG_SEAM
    for fn in (
        "wifi_bssid_to_upper",
        "wifi_bssid_is_canonical",
        "wifi_parse_int_field",
        "wifi_csv_quote",
        "wifi_csv_next_field",
        "wifi_b64_encode",
        "wifi_b64_val",
        "wifi_b64_decode",
        "wifi_cred_key",
        "wifi_cred_parse_verify",
        "wifi_cred_check",
        "wifi_cred_seal",
        "wifi_cred_unseal",
        "wifi_saved_find",
        "wifi_saved_recover_replace",
        "wifi_saved_load",
        "wifi_saved_write_recoverable",
        "wifi_saved_add_or_update",
        "wifi_saved_delete_index",
        "wifi_cred_set_psk",
        "wifi_cred_get_psk",
    ):
        code += "\n" + function(wifi, fn)
    code += SAVED_NET_MAIN_START
    code += r"""
    wifi_scanlist_t ap;
    /* +2: one NUL for the max PSK, and one more byte so the "65 chars is
     * refused" over-length case can be built without overflowing. */
    char psk[WIFI_PSK_MAX_CHARS + 2];
    char out[WIFI_PSK_MAX_CHARS + 1];
    char field[WIFI_CRED_FIELD_MAX];
    char field2[WIFI_CRED_FIELD_MAX];
    char row[512];
    char filebuf[8192];
    char *hit;
    size_t i;
    int commas;

    memset(&ap, 0, sizeof(ap));
    strcpy(ap.ssid, "CredNet"); strcpy(ap.bssid, "aa:bb:cc:dd:ee:01");
    ap.channel = 6; ap.rssi = -50;

    /* ---- Part A: base64 helpers (RFC 4648 vectors + strictness) ---- */

    /* 1: 3-byte group encodes without padding */
    {
        uint8_t in[3] = { 'M', 'a', 'n' };
        CHECK(wifi_b64_encode(in, 3, out, sizeof(out)) && strcmp(out, "TWFu") == 0,
              "base64 'Man' -> 'TWFu'");
        memset(out, 0, sizeof(out));
    }
    /* 2: 2-byte tail pads once */
    {
        uint8_t in[2] = { 'M', 'a' };
        CHECK(wifi_b64_encode(in, 2, out, sizeof(out)) && strcmp(out, "TWE=") == 0,
              "base64 2-byte tail -> 'TWE='");
        memset(out, 0, sizeof(out));
    }
    /* 3: 1-byte tail pads twice */
    {
        uint8_t in[1] = { 'M' };
        CHECK(wifi_b64_encode(in, 1, out, sizeof(out)) && strcmp(out, "TQ==") == 0,
              "base64 1-byte tail -> 'TQ=='");
        memset(out, 0, sizeof(out));
    }
    /* 4: exact-size 80-byte round trip (the real iv||ct size) */
    {
        uint8_t in[80], back[80];
        size_t n = 0;
        for (i = 0; i < 80; i++) { in[i] = (uint8_t)(i * 7 + 3); }
        CHECK(wifi_b64_encode(in, 80, field, sizeof(field)) && strlen(field) == 108,
              "80 bytes encode to exactly 108 chars");
        CHECK(wifi_b64_decode(field, strlen(field), back, sizeof(back), &n) && n == 80,
              "108 chars decode back to exactly 80 bytes");
        CHECK(memcmp(in, back, 80) == 0, "80-byte base64 round trip is byte-exact");
    }
    /* 5: strict decode rejects every malformation */
    {
        uint8_t db[80];
        size_t n = 0;
        CHECK(!wifi_b64_decode("", 0, db, sizeof(db), &n), "empty base64 rejected");
        CHECK(!wifi_b64_decode("TWFuX", 5, db, sizeof(db), &n), "length %% 4 != 0 rejected");
        CHECK(!wifi_b64_decode("TW!u", 4, db, sizeof(db), &n), "non-alphabet char rejected");
        CHECK(!wifi_b64_decode("TW=u", 4, db, sizeof(db), &n), "mid-string '=' rejected");
        CHECK(!wifi_b64_decode("T===", 4, db, sizeof(db), &n), "three pads rejected");
        CHECK(!wifi_b64_decode("TWFu", 4, db, 2, &n), "destination too small rejected");
    }
    /* 6: encode refuses an undersized destination without writing */
    {
        uint8_t in[3] = { 1, 2, 3 };
        char tiny[3];
        tiny[0] = 'Z';
        CHECK(!wifi_b64_encode(in, 3, tiny, sizeof(tiny)) && tiny[0] == 'Z',
              "encode with too-small dst fails and leaves dst untouched");
    }

    /* ---- Part B: seal/unseal (direct, crypto real, RNG injected) ---- */

    /* 7: field shape and round trip */
    strcpy(psk, "CorrectHorse9");
    CHECK(wifi_cred_seal(psk, field, sizeof(field)), "seal succeeds");
    CHECK(strlen(field) == 133 && field[108] == ':',
          "field is base64(iv||ct):base64(cmac) with exact lengths 108+1+24");
    memset(out, 0xAA, sizeof(out));
    CHECK(wifi_cred_unseal(field, out, sizeof(out)) && strcmp(out, psk) == 0,
          "unseal returns the sealed PSK");
    /* 8: fresh IV per seal */
    CHECK(wifi_cred_seal(psk, field2, sizeof(field2)) && strcmp(field, field2) != 0,
          "two seals of the same PSK differ (fresh RNG IV)");
    /* 9: length bounds: empty and >64 refused, 64-char (hex-PMK form) accepted */
    CHECK(!wifi_cred_seal("", field, sizeof(field)), "empty PSK refused");
    memset(psk, 'k', 65); psk[65] = '\0';
    CHECK(!wifi_cred_seal(psk, field, sizeof(field)), "65-char PSK refused");
    memset(psk, '0', 64); psk[64] = '\0';
    CHECK(wifi_cred_seal(psk, field, sizeof(field)), "64-char hex-PMK form accepted");
    memset(out, 0xAA, sizeof(out));
    CHECK(wifi_cred_unseal(field, out, sizeof(out)) && strlen(out) == 64,
          "64-char PSK round-trips");
    /* 10: RNG refusal -> seal refuses, output untouched */
    strcpy(psk, "abc");
    memset(field, 0x5A, sizeof(field) - 1); field[sizeof(field) - 1] = '\0';
    strcpy(field2, field);
    t_rng_fail = true;
    CHECK(!wifi_cred_seal(psk, field, sizeof(field)), "RNG failure refuses the seal");
    t_rng_fail = false;
    CHECK(strcmp(field, field2) == 0, "refused seal leaves the field untouched");
    /* 11: malformed fields refuse the unseal (fail closed) */
    {
        const char *bad[] = {
            "AAAA",                               /* no ':'            */
            ":",                                  /* both parts empty  */
            "TWFu:TWFu",                          /* wrong part lengths*/
            "TWFuTWFuTWFuTWFuTWFuTWFuTWFuTWFuTWFuTWFuTWFuTWFuTWFuTWFuTWFuTWFuTWFuTWFuTWFuTWFuTWFuTWFuTWFuTWFuTWFuTWFuTW==:TWFu", /* 104 */
        };
        int k;
        for (k = 0; k < 4; k++)
        {
            memset(out, 0xAA, sizeof(out));
            CHECK(!wifi_cred_unseal(bad[k], out, sizeof(out)), "malformed field refused");
            for (i = 0; i < sizeof(out); i++) { if (out[i] != 0) { break; } }
            CHECK(i == sizeof(out), "failed unseal zeroizes the output buffer");
        }
    }

    /* ---- Part C: set/get through the real store ---- */

    /* 12: set_psk on a saved network persists an encrypted field */
    vfs_reset(); fault_reset(); g_saved_count = 0;
    CHECK(wifi_saved_add_or_update(&ap) == 1, "seed network for credential tests");
    strcpy(psk, "hunter2networks");
    {
        size_t plen = strlen(psk);
        CHECK(wifi_cred_set_psk("AA:BB:CC:DD:EE:01", psk) == 0, "set_psk reports ok");
        for (i = 0; i < plen; i++) { if (psk[i] != 0) { break; } }
        CHECK(i == plen, "set_psk absorbs (zeroizes) the caller's plaintext PSK");
    }
    strcpy(field, g_saved[0].psk_enc);
    CHECK(field[0] != '\0' && g_saved[0].cred_state == WIFI_CRED_OK,
          "in-memory entry carries the sealed field and OK state");
    /* 13: the file holds the sealed field, never the plaintext */
    strcpy(filebuf, vfs_get(WIFI_SAVED_PATH));
    CHECK(strstr(filebuf, field) != NULL, "persisted CSV contains the sealed field text");
    CHECK(strstr(filebuf, "hunter2networks") == NULL, "plaintext PSK never appears in the CSV");
    hit = strstr(filebuf, field);
    CHECK(hit > filebuf && hit[-1] == '"' && hit[strlen(field)] == '"',
          "credential is the trailing 8th column, emitted quoted");
    for (commas = 0, hit = filebuf; *hit; hit++) { if (*hit == ',') { commas++; } }
    CHECK(commas == 14, "header (7) + data row (7) each separate 8 columns");
    CHECK(strstr(filebuf, "timestamp,credential") != NULL, "header names the credential column");
    /* 14: reload verifies + join-time decrypt round-trips through the file */
    g_saved_count = 0; fault_reset();
    CHECK(wifi_saved_load() == 0 && g_saved_count == 1, "reload after set_psk succeeds");
    CHECK(g_saved[0].cred_state == WIFI_CRED_OK, "reload classifies the credential OK");
    memset(out, 0xAA, sizeof(out));
    CHECK(wifi_cred_get_psk(0, out, sizeof(out)) && strcmp(out, "hunter2networks") == 0,
          "get_psk decrypts the stored credential at join time");
    /* 15: absent/OOB handling */
    memset(out, 0xAA, sizeof(out));
    CHECK(!wifi_cred_get_psk(1, out, sizeof(out)), "out-of-range index refused");
    for (i = 0; i < sizeof(out); i++) { if (out[i] != 0) { break; } }
    CHECK(i == sizeof(out), "refused get_psk zeroizes the output buffer");
    /* 16: scan refresh preserves the credential */
    ap.rssi = -42;
    CHECK(wifi_saved_add_or_update(&ap) == 0, "refresh scan data on the saved network");
    CHECK(strcmp(g_saved[0].psk_enc, field) == 0 && g_saved[0].cred_state == WIFI_CRED_OK,
          "credential survives a scan-data refresh");
    CHECK(strstr(vfs_get(WIFI_SAVED_PATH), field) != NULL,
          "credential survives the rewrite to SD");
    /* 17: unknown network */
    strcpy(psk, "whatever1");
    CHECK(wifi_cred_set_psk("AA:BB:CC:DD:EE:77", psk) == 1, "set_psk on unknown BSSID -> 1");
    /* 18: clearing */
    CHECK(wifi_cred_set_psk("AA:BB:CC:DD:EE:01", NULL) == 0, "clear (NULL) reports ok");
    CHECK(g_saved[0].psk_enc[0] == '\0' && g_saved[0].cred_state == WIFI_CRED_ABSENT,
          "cleared entry is ABSENT");
    g_saved_count = 0; fault_reset();
    CHECK(wifi_saved_load() == 0 && g_saved_count == 1 && g_saved[0].cred_state == WIFI_CRED_ABSENT,
          "cleared credential reloads as legacy-style ABSENT row");
    memset(out, 0xAA, sizeof(out));
    CHECK(!wifi_cred_get_psk(0, out, sizeof(out)), "get_psk on ABSENT credential fails");
    /* 19: save failure rolls back to the OLD credential */
    strcpy(psk, "newpsk12345");
    CHECK(wifi_cred_set_psk("AA:BB:CC:DD:EE:01", psk) == 0, "re-set credential before failure test");
    strcpy(field2, g_saved[0].psk_enc);
    strcpy(psk, "replacement9");
    fault_open(WIFI_SAVED_TMP_PATH, FR_DISK_ERR);
    CHECK(wifi_cred_set_psk("AA:BB:CC:DD:EE:01", psk) == 2, "save failure reports 2");
    CHECK(strcmp(g_saved[0].psk_enc, field2) == 0 && g_saved[0].cred_state == WIFI_CRED_OK,
          "save failure rolls the credential back to the stored one");
    CHECK(strstr(vfs_get(WIFI_SAVED_PATH), field2) != NULL, "file still holds the old credential");
    /* 20: RNG refusal on set_psk changes nothing */
    fault_reset();
    strcpy(psk, "refused-psk-1");
    t_rng_fail = true;
    CHECK(wifi_cred_set_psk("AA:BB:CC:DD:EE:01", psk) == 3, "RNG refusal reports 3");
    t_rng_fail = false;
    for (i = 0; i < strlen("refused-psk-1"); i++) { if (psk[i] != 0) { break; } }
    CHECK(i == strlen("refused-psk-1"), "refused set_psk still absorbs the plaintext");
    CHECK(strcmp(g_saved[0].psk_enc, field2) == 0, "refused set_psk leaves the stored credential intact");

    /* ---- Part D: 7-column (legacy) / 8-column compatibility ---- */

    /* 21: legacy rows without the credential column stay valid */
    vfs_reset(); fault_reset(); g_saved_count = 0;
    vfs_put(WIFI_SAVED_PATH,
        "#M1SAVEDNET\r\n"
        "1,\"Legacy\",AA:BB:CC:DD:EE:20,6,-50,\"WPA2\",\r\n"
        "1,\"LegacyTs\",AA:BB:CC:DD:EE:21,6,-50,\"WPA2\",1729000000\r\n"
        "1,\"Empty8\",AA:BB:CC:DD:EE:22,6,-50,\"WPA2\",\r\n");
    CHECK(wifi_saved_load() == 0, "legacy/mixed-column file loads");
    CHECK(g_saved_count == 3, "all three rows load");
    CHECK(g_saved[0].cred_state == WIFI_CRED_ABSENT && g_saved[1].cred_state == WIFI_CRED_ABSENT
              && g_saved[2].cred_state == WIFI_CRED_ABSENT,
          "rows without credential content are ABSENT (unencrypted-by-absence)");
    /* 22: hand-spliced 8-column row with a really sealed field verifies OK */
    strcpy(psk, "spliced-psk-1");
    CHECK(wifi_cred_seal(psk, field, sizeof(field)), "seal for spliced row");
    snprintf(row, sizeof(row), "1,\"Splice\",AA:BB:CC:DD:EE:33,6,-50,\"WPA2\",,%s\r\n", field);
    snprintf(filebuf, sizeof(filebuf), "#M1SAVEDNET\r\n%s", row);
    vfs_reset(); fault_reset(); g_saved_count = 0;
    vfs_put(WIFI_SAVED_PATH, filebuf);
    CHECK(wifi_saved_load() == 0 && g_saved_count == 1 && g_saved[0].cred_state == WIFI_CRED_OK,
          "spliced 8-column row with valid field classifies OK");
    memset(out, 0, sizeof(out));
    CHECK(wifi_cred_get_psk(0, out, sizeof(out)) && strcmp(out, "spliced-psk-1") == 0,
          "spliced row's credential decrypts");
    /* 23: a 9th field is ignored, exactly as pre-9th fields always were */
    snprintf(filebuf, sizeof(filebuf), "#M1SAVEDNET\r\n1,\"Nine\",AA:BB:CC:DD:EE:34,6,-50,\"WPA2\",,%s,extra\r\n", field);
    vfs_reset(); fault_reset(); g_saved_count = 0;
    vfs_put(WIFI_SAVED_PATH, filebuf);
    CHECK(wifi_saved_load() == 0 && g_saved_count == 1 && g_saved[0].cred_state == WIFI_CRED_OK,
          "fields beyond the credential column are ignored");
    /* 23b: the writer quotes the credential column, so hostile text with
     * an embedded comma round-trips verbatim (load -> save -> load) and
     * every rewritten row keeps exactly 8 columns. A bare re-emission
     * would turn the quoted 8th field "a,b:c" into a 9-column row and
     * silently truncate the stored text to "a" on the next load. */
    strcpy(psk, "roundtrip-1");
    CHECK(wifi_cred_seal(psk, field, sizeof(field)), "seal canonical field for round trip");
    snprintf(filebuf, sizeof(filebuf),
        "#M1SAVEDNET\r\n"
        "1,\"Canon\",AA:BB:CC:DD:EE:40,6,-50,\"WPA2\",,%s\r\n"
        "1,\"Hostile\",AA:BB:CC:DD:EE:41,6,-50,\"WPA2\",,\"a,b:c\"\r\n",
        field);
    vfs_reset(); fault_reset(); g_saved_count = 0;
    vfs_put(WIFI_SAVED_PATH, filebuf);
    CHECK(wifi_saved_load() == 0 && g_saved_count == 2, "comma-bearing hostile file loads");
    CHECK(strcmp(g_saved[0].psk_enc, field) == 0 && g_saved[0].cred_state == WIFI_CRED_OK,
          "canonical sealed field parses from a bare column");
    CHECK(strcmp(g_saved[1].psk_enc, "a,b:c") == 0 && g_saved[1].cred_state == WIFI_CRED_INVALID,
          "hostile comma-bearing field unquotes to its full text, INVALID");
    CHECK(wifi_saved_write_recoverable() == 0, "rewrite of the hostile row succeeds");
    strcpy(filebuf, vfs_get(WIFI_SAVED_PATH));
    hit = strstr(filebuf, field);
    CHECK(hit != NULL && hit[-1] == '"' && hit[strlen(field)] == '"',
          "canonical credential re-emitted verbatim, quoted");
    CHECK(strstr(filebuf, ",\"a,b:c\"\r\n") != NULL,
          "hostile credential re-emitted verbatim as the quoted last column");
    {
        /* every comma-bearing line (column header + both rows) must carry
         * exactly 7 commas outside quoted regions -> a stable 8-column
         * shape; the comma inside "a,b:c" must not count as a separator,
         * and only the comma-less "#M1SAVEDNET" magic line is exempt */
        int outside = 0, inq = 0, bad = 0;
        for (hit = filebuf; *hit; hit++)
        {
            if (*hit == '"') { inq = !inq; }
            else if ((*hit == ',') && !inq) { outside++; }
            else if (*hit == '\n') { if (outside != 7 && outside != 0) { bad = 1; } outside = 0; inq = 0; }
        }
        CHECK(!bad && outside == 0, "rewritten file keeps exactly 8 columns per row");
    }
    g_saved_count = 0; fault_reset();
    CHECK(wifi_saved_load() == 0 && g_saved_count == 2, "reload after rewrite succeeds");
    CHECK(strcmp(g_saved[0].psk_enc, field) == 0 && g_saved[0].cred_state == WIFI_CRED_OK,
          "canonical credential survives the load/save/load round trip unchanged");
    CHECK(strcmp(g_saved[1].psk_enc, "a,b:c") == 0 && g_saved[1].cred_state == WIFI_CRED_INVALID,
          "hostile credential survives the load/save/load round trip unchanged");

    /* 23c: the absolute structural worst case round-trips. A 32-quote
     * SSID, a 15-quote security string, INT_MIN channel/RSSI and a
     * 135-quote credential (the "" escape branch at its maximum, built
     * by doubling inside this source) make a 419-byte row:
     * 1 + 66 + 17 + 11 + 11 + 32 + 0 + 272 + 7 commas + CRLF. The old
     * 320-byte writer bound rejected this shape and, because a save
     * rewrites the whole store, disabled every subsequent save; the
     * new bound must accept it AND the loader must re-read it, or the
     * store could not read back a file it just wrote. */
    {
        char ws[33], wsec[16], wc[WIFI_CRED_FIELD_MAX];
        char q_ws[2 * 33 + 2], q_sec[2 * 16 + 2];
        char q_wc[(2 * WIFI_CRED_FIELD_MAX) + 8];
        char *nl, *nl2;
        int rowlen = -1;

        memset(ws, '"', 32); ws[32] = '\0';    /* max-length all-quote SSID */
        memset(wsec, '"', 15); wsec[15] = '\0';/* max-length all-quote security */
        memset(wc, '"', 135); wc[135] = '\0';  /* max-length all-quote credential */
        wifi_csv_quote(q_ws, sizeof(q_ws), ws);
        wifi_csv_quote(q_sec, sizeof(q_sec), wsec);
        wifi_csv_quote(q_wc, sizeof(q_wc), wc);
        snprintf(filebuf, sizeof(filebuf),
            "#M1SAVEDNET\r\n"
            "1,%s,AA:BB:CC:DD:EE:42,-2147483648,-2147483648,%s,,%s\r\n",
            q_ws, q_sec, q_wc);
        vfs_reset(); fault_reset(); g_saved_count = 0;
        vfs_put(WIFI_SAVED_PATH, filebuf);
        CHECK(wifi_saved_load() == 0 && g_saved_count == 1, "worst-case quote-heavy file loads");
        CHECK(strcmp(g_saved[0].ssid, ws) == 0,
              "max-length all-quote SSID unescapes to its full 32 chars on load");
        CHECK(strcmp(g_saved[0].security, wsec) == 0,
              "max-length all-quote security unescapes to its full 15 chars on load");
        CHECK(strcmp(g_saved[0].psk_enc, wc) == 0 && g_saved[0].cred_state == WIFI_CRED_INVALID,
              "135-quote credential unescapes to its full text, INVALID");
        CHECK(wifi_saved_write_recoverable() == 0,
              "save of the 419-byte worst-case row does NOT fail");
        strcpy(filebuf, vfs_get(WIFI_SAVED_PATH));
        nl = strchr(filebuf, '\n');                 /* end of "#M1SAVEDNET" */
        nl = nl ? strchr(nl + 1, '\n') : NULL;      /* end of column header */
        nl2 = nl ? strchr(nl + 1, '\n') : NULL;     /* end of the data row */
        CHECK(nl2 != NULL && (rowlen = (int)(nl2 - nl)) == 419,
              "rewritten worst-case row is exactly 419 bytes incl. CRLF");
        CHECK(rowlen + 1 <= (int)WIFI_SAVED_LINE_BUF && rowlen <= (int)WIFI_SAVED_LINE_MAX - 1,
              "worst-case row fits the writer buffer and the loader cap (read-back invariant)");
        g_saved_count = 0; fault_reset();
        CHECK(wifi_saved_load() == 0 && g_saved_count == 1,
              "reload after worst-case rewrite succeeds");
        CHECK(strcmp(g_saved[0].psk_enc, wc) == 0 && strcmp(g_saved[0].ssid, ws) == 0,
              "worst-case SSID and credential survive load/save/load verbatim");
        {
            int outside = 0, inq = 0, bad = 0;
            for (hit = filebuf; *hit; hit++)
            {
                if (*hit == '"') { inq = !inq; }
                else if ((*hit == ',') && !inq) { outside++; }
                else if (*hit == '\n') { if (outside != 7 && outside != 0) { bad = 1; } outside = 0; inq = 0; }
            }
            CHECK(!bad && outside == 0, "worst-case rewrite keeps exactly 8 columns per row");
        }
    }

    /* 23d: size-boundary tripwire. A 331-byte row (20-char plain SSID,
     * ordinary channel/RSSI/security, 135-quote credential) lands
     * between the old 320-byte writer bound, which rejected it and
     * suspended every save, and the new 448-byte bound: it must save
     * and reload successfully. */
    {
        char wc[WIFI_CRED_FIELD_MAX];
        char q_wc[(2 * WIFI_CRED_FIELD_MAX) + 8];
        char *nl, *nl2;

        memset(wc, '"', 135); wc[135] = '\0';
        wifi_csv_quote(q_wc, sizeof(q_wc), wc);
        snprintf(filebuf, sizeof(filebuf),
            "#M1SAVEDNET\r\n"
            "1,\"01234567890123456789\",AA:BB:CC:DD:EE:43,6,-50,\"WPA2\",,%s\r\n",
            q_wc);
        vfs_reset(); fault_reset(); g_saved_count = 0;
        vfs_put(WIFI_SAVED_PATH, filebuf);
        CHECK(wifi_saved_load() == 0 && g_saved_count == 1
                  && strcmp(g_saved[0].psk_enc, wc) == 0,
              "boundary-size hostile file loads with the credential verbatim");
        CHECK(wifi_saved_write_recoverable() == 0,
              "331-byte row (old bound: save suspended) now saves successfully");
        strcpy(filebuf, vfs_get(WIFI_SAVED_PATH));
        nl = strchr(filebuf, '\n');                 /* end of "#M1SAVEDNET" */
        nl = nl ? strchr(nl + 1, '\n') : NULL;      /* end of column header */
        nl2 = nl ? strchr(nl + 1, '\n') : NULL;     /* end of the data row */
        CHECK(nl2 != NULL && (int)(nl2 - nl) == 331,
              "rewritten boundary row is exactly 331 bytes incl. CRLF");
        g_saved_count = 0; fault_reset();
        CHECK(wifi_saved_load() == 0 && g_saved_count == 1
                  && strcmp(g_saved[0].psk_enc, wc) == 0,
              "boundary row reloads with the credential verbatim");
    }

    /* ---- Part E: tamper / wrong CMAC / truncation / foreign UID ---- */

    /* helper: re-seed one credentialed network and return its field text */
    #define RESEED_CRED() \
        do { \
            vfs_reset(); fault_reset(); g_saved_count = 0; \
            (void)wifi_saved_add_or_update(&ap); \
            strcpy(psk, "tamper-me-123"); \
            CHECK(wifi_cred_set_psk("AA:BB:CC:DD:EE:01", psk) == 0, "reseed credentialed network"); \
            strcpy(field2, g_saved[0].psk_enc); \
        } while (0)

    /* 24: flip one ciphertext byte -> INVALID, row survives, join skips */
    RESEED_CRED();
    strcpy(filebuf, vfs_get(WIFI_SAVED_PATH));
    hit = strstr(filebuf, field2);
    CHECK(hit != NULL, "tamper test finds the field in the file");
    hit[10] = (hit[10] == 'A') ? 'B' : 'A'; /* inside base64(iv||ct) */
    vfs_put(WIFI_SAVED_PATH, filebuf);
    g_saved_count = 0; fault_reset();
    CHECK(wifi_saved_load() == 0 && g_saved_count == 1, "tampered row still loads (no brick)");
    CHECK(strcmp(g_saved[0].bssid, "AA:BB:CC:DD:EE:01") == 0, "tampered row keeps its identity");
    CHECK(g_saved[0].cred_state == WIFI_CRED_INVALID, "flipped ciphertext byte -> INVALID");
    memset(out, 0xAA, sizeof(out));
    CHECK(!wifi_cred_get_psk(0, out, sizeof(out)), "join-time decrypt of tampered credential fails");
    for (i = 0; i < sizeof(out); i++) { if (out[i] != 0) { break; } }
    CHECK(i == sizeof(out), "tampered get_psk leaves the output zeroized");
    CHECK(g_saved[0].cred_state == WIFI_CRED_INVALID, "entry stays marked unusable");
    /* 25: flip one CMAC byte -> INVALID */
    RESEED_CRED();
    strcpy(filebuf, vfs_get(WIFI_SAVED_PATH));
    hit = strstr(filebuf, field2);
    hit[108 + 1 + 5] = (hit[108 + 1 + 5] == 'A') ? 'B' : 'A'; /* inside base64(cmac) */
    vfs_put(WIFI_SAVED_PATH, filebuf);
    g_saved_count = 0; fault_reset();
    CHECK(wifi_saved_load() == 0 && g_saved_count == 1 &&
              g_saved[0].cred_state == WIFI_CRED_INVALID,
          "wrong CMAC -> INVALID, row still a valid target");
    /* 26: truncated credential (4 chars cut from iv||ct) -> INVALID */
    RESEED_CRED();
    strcpy(filebuf, vfs_get(WIFI_SAVED_PATH));
    hit = strstr(filebuf, field2);
    memmove(hit + 40, hit + 44, strlen(hit + 44) + 1);
    vfs_put(WIFI_SAVED_PATH, filebuf);
    g_saved_count = 0; fault_reset();
    CHECK(wifi_saved_load() == 0 && g_saved_count == 1 &&
              g_saved[0].cred_state == WIFI_CRED_INVALID,
          "truncated field -> INVALID");
    /* 27: ':mac' half missing entirely -> INVALID */
    RESEED_CRED();
    strcpy(filebuf, vfs_get(WIFI_SAVED_PATH));
    hit = strstr(filebuf, field2);
    hit[108] = '\0'; /* cut the separator and everything after */
    vfs_put(WIFI_SAVED_PATH, filebuf);
    g_saved_count = 0; fault_reset();
    CHECK(wifi_saved_load() == 0 && g_saved_count == 1 &&
              g_saved[0].cred_state == WIFI_CRED_INVALID,
          "field without the mac half -> INVALID");
    /* 28: foreign device UID (SD moved to another unit) -> INVALID, and back */
    RESEED_CRED();
    t_uid[0] ^= 0xFF; /* different device */
    g_saved_count = 0; fault_reset();
    CHECK(wifi_saved_load() == 0 && g_saved_count == 1 &&
              g_saved[0].cred_state == WIFI_CRED_INVALID,
          "credential sealed under another device's UID -> INVALID");
    t_uid[0] ^= 0xFF; /* back to the sealing device */
    g_saved_count = 0; fault_reset();
    CHECK(wifi_saved_load() == 0 && g_saved[0].cred_state == WIFI_CRED_OK,
          "same file verifies OK again on the sealing device (marking is per-verification)");
    /* 29: oversized garbage credential field -> row loads, INVALID */
    {
        static const char *big =
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAA:TWFuTWFuTWFuTWFu";
        snprintf(filebuf, sizeof(filebuf),
                 "#M1SAVEDNET\r\n1,\"Big\",AA:BB:CC:DD:EE:35,6,-50,\"WPA2\",,%s\r\n", big);
        vfs_reset(); fault_reset(); g_saved_count = 0;
        vfs_put(WIFI_SAVED_PATH, filebuf);
        CHECK(wifi_saved_load() == 0 && g_saved_count == 1 &&
                  g_saved[0].cred_state == WIFI_CRED_INVALID,
              "oversized credential field loads the row but marks the credential INVALID");
    }
    /* 30: corrupt rows mixed with good ones do not brick the store */
    strcpy(psk, "goodone12345");
    CHECK(wifi_cred_seal(psk, field, sizeof(field)), "seal for the mixed-file row");
    snprintf(filebuf, sizeof(filebuf),
        "#M1SAVEDNET\r\n"
        "garbage,not,a,bssid\r\n"
        "1,\"Good\",AA:BB:CC:DD:EE:36,6,-50,\"WPA2\",,%s\r\n"
        "1,\"BadCred\",AA:BB:CC:DD:EE:37,6,-50,\"WPA2\",,!!not-base64-at-all:!!\r\n"
        "99,\"WrongVer\",AA:BB:CC:DD:EE:38,6,-50,\"WPA2\",\r\n",
        field);
    vfs_reset(); fault_reset(); g_saved_count = 0;
    vfs_put(WIFI_SAVED_PATH, filebuf);
    CHECK(wifi_saved_load() == 0 && g_saved_count == 2, "mixed corrupt file loads its two valid rows");
    CHECK(g_saved[0].cred_state == WIFI_CRED_OK && g_saved[1].cred_state == WIFI_CRED_INVALID,
          "good credential OK, garbage credential INVALID");
    CHECK(wifi_saved_add_or_update(&ap) == 1, "store remains writable after loading corruption");

    printf("saved-networks encrypted-credential battery: %d/%d checks passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
"""
    build_run(
        tmp,
        "saved_networks_cred_tests",
        code,
        extra_sources=[ROOT / "m1_csrc" / "m1_crypto.c"],
        extra_flags=["-I", str(ROOT / "m1_csrc")],
    )


PICKPATH_STUBS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

typedef char TCHAR;
typedef unsigned int UINT;
typedef unsigned long DWORD;
typedef unsigned char BYTE;

typedef enum {
    FR_OK = 0, FR_DISK_ERR, FR_INT_ERR, FR_NOT_READY, FR_NO_FILE, FR_NO_PATH,
    FR_INVALID_NAME, FR_DENIED, FR_EXIST, FR_INVALID_OBJECT, FR_WRITE_PROTECTED,
    FR_INVALID_DRIVE, FR_NOT_ENABLED, FR_NO_FILESYSTEM, FR_MKFS_ABORTED,
    FR_TIMEOUT, FR_LOCKED, FR_NOT_ENOUGH_CORE, FR_TOO_MANY_OPEN_FILES,
    FR_INVALID_PARAMETER
} FRESULT;

typedef struct { int dummy; } FILINFO;

#define M1_SD_DIR_WIFI "0:/wifi"

/* Existence-only virtual filesystem, sized for the full 1000-name search
 * space cap_pick_path() must cover (only f_stat() is exercised -- content
 * is irrelevant to this search logic). */
#define VFS_MAX 1100
static char s_vfs_paths[VFS_MAX][40];
static int  s_vfs_n = 0;
static bool vfs_exists(const char *path)
{
    int i;
    for (i = 0; i < s_vfs_n; i++) { if (strcmp(s_vfs_paths[i], path) == 0) { return true; } }
    return false;
}
static void vfs_put(const char *path)
{
    if (vfs_exists(path)) { return; }
    strncpy(s_vfs_paths[s_vfs_n++], path, 39);
}
static void vfs_reset(void) { s_vfs_n = 0; }

static char    g_stat_error_path[40] = ""; static FRESULT g_stat_error_code = FR_DISK_ERR;
static bool    g_fail_dir_ensure = false;
static int     g_dir_ensure_calls = 0;

static void fault_reset(void)
{
    g_stat_error_path[0] = 0;
    g_fail_dir_ensure = false;
    g_dir_ensure_calls = 0;
}

FRESULT fs_directory_ensure(const char *path)
{
    (void)path;
    g_dir_ensure_calls++;
    return g_fail_dir_ensure ? FR_DISK_ERR : FR_OK;
}

FRESULT f_stat(const char *path, FILINFO *fi)
{
    (void)fi;
    if (g_stat_error_path[0] && strcmp(path, g_stat_error_path) == 0) { return g_stat_error_code; }
    return vfs_exists(path) ? FR_OK : FR_NO_FILE;
}

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) { g_pass++; } else { g_fail++; printf("  FAIL: %s\n", (m)); } } while (0)
"""


def cap_pick_path_tests(tmp):
    cap = source("m1_csrc/m1_capture_link.c")
    code = PICKPATH_STUBS
    code += "\n" + line(cap, r"typedef enum \{[\s\S]*?\} cap_pick_result_t;")
    code += "\n" + function(cap, "cap_pick_path")
    code += r"""
int main(void)
{
    char path[64];

    /* 1: directory-ensure failure is reported distinctly, no search performed */
    vfs_reset(); fault_reset();
    g_fail_dir_ensure = true;
    CHECK(cap_pick_path(path, sizeof(path)) == CAP_PICK_SD_ERROR, "dir-ensure failure -> SD error");

    /* 2: empty wifi dir -> first name picked, under 0:/wifi, not SD root */
    vfs_reset(); fault_reset();
    CHECK(cap_pick_path(path, sizeof(path)) == CAP_PICK_OK, "empty dir -> a name is picked");
    CHECK(strcmp(path, "0:/wifi/mcap_000.pcapng") == 0, "first free name is mcap_000 under 0:/wifi");

    /* 3: some names taken -> first true gap is picked, legacy SD-root files ignored */
    vfs_reset(); fault_reset();
    vfs_put("0:/wifi/mcap_000.pcapng");
    vfs_put("0:/wifi/mcap_001.pcapng");
    vfs_put("0:/mcap_000.pcapng");  /* legacy root file: must not influence the search */
    CHECK(cap_pick_path(path, sizeof(path)) == CAP_PICK_OK, "search finds a free name");
    CHECK(strcmp(path, "0:/wifi/mcap_002.pcapng") == 0, "first true gap under 0:/wifi is picked, root file ignored");

    /* 4: real SD error during search (not "missing") is reported, not treated as free */
    vfs_reset(); fault_reset();
    strcpy(g_stat_error_path, "0:/wifi/mcap_000.pcapng");
    g_stat_error_code = FR_DISK_ERR;
    CHECK(cap_pick_path(path, sizeof(path)) == CAP_PICK_SD_ERROR, "a real f_stat error aborts the search as an SD error");

    /* 5: FR_NO_PATH (not just FR_NO_FILE) is also correctly treated as "free" */
    vfs_reset(); fault_reset();
    strcpy(g_stat_error_path, "0:/wifi/mcap_000.pcapng");
    g_stat_error_code = FR_NO_PATH;
    CHECK(cap_pick_path(path, sizeof(path)) == CAP_PICK_OK, "FR_NO_PATH is treated as an available name");
    CHECK(strcmp(path, "0:/wifi/mcap_000.pcapng") == 0, "FR_NO_PATH slot is the one picked");

    /* 6: all 1000 names occupied -> exhaustion is reported, never a silent overwrite */
    {
        int i; char p[64];
        vfs_reset(); fault_reset();
        for (i = 0; i < 1000; i++) { snprintf(p, sizeof(p), "0:/wifi/mcap_%03d.pcapng", i); vfs_put(p); }
        CHECK(cap_pick_path(path, sizeof(path)) == CAP_PICK_EXHAUSTED, "all 1000 names taken -> exhaustion reported");
    }

    /* 7: directory is (re-)ensured every call -- lazy self-healing if removed after boot */
    vfs_reset(); fault_reset();
    (void)cap_pick_path(path, sizeof(path));
    CHECK(g_dir_ensure_calls == 1, "0:/wifi directory is ensured before searching");

    printf("cap_pick_path: %d/%d checks passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
"""
    build_run(tmp, "cap_pick_path_tests", code)


def capture_open_write_checks_source_text():
    """m1_capture_stream_run() -- the file-open, SHB/IDB/ISB-write and final
    flush/sync/close checks live inline inside a single large STM32
    HAL/FreeRTOS/ESP-UART-coupled function together with real-time DMA
    capture control flow, so they cannot be pulled out and host-linked the
    way cap_pick_path() and the Wi-Fi persistence functions above are.
    Same DISCLOSED LIMITATION as every other HAL-coupled file in this
    project (see e.g. m1_sdcard_provision_test.c, Part 2): verified here
    against the real, committed source text instead of by execution."""
    cap = source("m1_csrc/m1_capture_link.c")
    checks = [
        (
            "capture file opened with FA_CREATE_NEW (never overwrites)",
            "f_open(&cf, out->path, FA_CREATE_NEW | FA_WRITE)" in cap,
        ),
        (
            "SHB write result is checked",
            "if (m1_pcapng_write_shb(&ctx.w, &si) != 0) { out->sd_ok = false; }" in cap,
        ),
        (
            "IDB write result is checked",
            "if (m1_pcapng_write_idb(&ctx.w, &ii) != 0) { out->sd_ok = false; }" in cap,
        ),
        (
            "ISB write result is checked",
            "if (m1_pcapng_write_isb(&ctx.w, ctx.iface_id, 0, &stx) != 0) { out->sd_ok = false; }"
            in cap,
        ),
        (
            "final flush result is checked",
            "if (cap_pcap_flush(&ctx) != 0) { out->sd_ok = false; }" in cap,
        ),
        (
            "final f_sync result is checked",
            "if (f_sync(&cf) != FR_OK) { out->sd_ok = false; }" in cap,
        ),
        (
            "final f_close result is checked",
            "if (f_close(&cf) != FR_OK) { out->sd_ok = false; }" in cap,
        ),
        ("no .part workflow was introduced", ".part" not in cap),
    ]
    passed = 0
    for msg, ok in checks:
        if ok:
            passed += 1
        else:
            print(f"  FAIL: {msg}")
    print(
        f"capture open/write-check source verification: {passed}/{len(checks)} checks passed"
    )
    assert passed == len(checks)


def cred_zeroization_source_checks():
    """Host-testable zeroization assertions, part 2: what cannot be
    observed at runtime (stack buffers inside the codec) is pinned
    against the real source text -- the exact bodies extracted and
    compiled by the suites above -- plus format/wiring invariants."""
    wifi = source("m1_csrc/m1_wifi.c")
    header = source("m1_csrc/m1_wifi.h")
    seal = function(wifi, "wifi_cred_seal")
    unseal = function(wifi, "wifi_cred_unseal")
    setfn = function(wifi, "wifi_cred_set_psk")
    checks = [
        ("seal zeroizes the derived key", "m1_secure_zero(key, sizeof(key));" in seal),
        (
            "seal zeroizes the iv||ct workspace",
            "m1_secure_zero(ivct, sizeof(ivct));" in seal,
        ),
        ("seal zeroizes the tag copy", "m1_secure_zero(mac, sizeof(mac));" in seal),
        (
            "unseal zeroizes the derived key",
            "m1_secure_zero(key, sizeof(key));" in unseal,
        ),
        (
            "unseal zeroizes the decrypted frame",
            "m1_secure_zero(ivct, sizeof(ivct));" in unseal,
        ),
        (
            "unseal zeroizes its output on failure",
            "m1_secure_zero(psk_out, outsz);" in unseal,
        ),
        (
            "set_psk absorbs the caller's plaintext",
            "m1_secure_zero(psk, plen);" in setfn,
        ),
        (
            "writer header keeps FMT_VER and names the credential column",
            'timestamp,credential\\r\\n"' in wifi
            and "#define WIFI_SAVED_FMT_VER          1" in wifi,
        ),
        (
            "no plaintext PSK column exists",
            "version,ssid,bssid,channel,rssi,security,psk" not in wifi,
        ),
        (
            "public API declared in m1_wifi.h",
            "uint8_t wifi_cred_set_psk(const char *bssid_upper, char *psk);" in header
            and "bool wifi_cred_get_psk(uint16_t idx, char *psk_out, size_t outsz);"
            in header,
        ),
    ]
    passed = 0
    for msg, ok in checks:
        if ok:
            passed += 1
        else:
            print(f"  FAIL: {msg}")
    print(
        f"credential zeroization/format source verification: {passed}/{len(checks)} checks passed"
    )
    assert passed == len(checks)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        eapol_tests(tmp)
        saved_networks_tests(tmp)
        saved_networks_cred_tests(tmp)
        cap_pick_path_tests(tmp)
    capture_open_write_checks_source_text()
    cred_zeroization_source_checks()
    if all(SANITIZER_MODES):
        print(
            "All Wi-Fi/MonstaShark persistence hardening host tests PASS (ASan/UBSan)."
        )
    elif any(SANITIZER_MODES):
        print(
            "All Wi-Fi/MonstaShark persistence hardening host tests PASS "
            "(mixed sanitizer availability -- see per-build note)."
        )
    else:
        print(
            "All Wi-Fi/MonstaShark persistence hardening host tests PASS "
            "(plain build: this toolchain ships no ASan/UBSan runtime)."
        )
