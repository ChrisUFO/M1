#!/usr/bin/env python3
"""Host tests for the Wi-Fi join flow (PR-11 of the Merge 3 series,
PLAN section 14; issue #42 completion).

Same technique as m1_wifi_persistence_hardening_test.py /
m1_esp_feature_gate_test.py: extract the REAL, unmodified pure helpers
out of the live m1_wifi.c with a source-text regex and host-compile
them (linked with the REAL m1_crypto.c, so m1_secure_zero is the
production zeroization, not a stub); separately, compile the REAL
feature-gate core (-DM1_ESP_GATE_HOST_TEST, simulated console) for the
ESP pairing/version-gate battery; and pin the wiring seams statically
in the actual files.

Layers:

1. Compiled join core (extracted from m1_csrc/m1_wifi.c):
   - PSK grammar: 8..63 printable no-quote passphrases, the exact
     64-hex PMK form, and the negatives (7 chars, 65 chars, 64
     non-hex, embedded quote, CR, LF, non-printable, NULL/empty);
   - rotor-alphabet round trip: every character the masked editor can
     produce builds a join-legal PSK (the editor can never type an
     unsendable secret);
   - SSID grammar: empty (hidden) .. 32 printable, negatives;
   - command construction: exact `join "<ssid>" "<psk>"<CRLF>` bytes,
     open-network empty field, hidden SSID, dropped-PSK refusal,
     grammar refusals, the no-truncation overflow refusal (dst fully
     zeroized), and the WIFI_JOIN_CMD_MAX worst-case fit;
   - reply classification: JOINED (+noise/+IP), JOIN_FAIL (+code),
     prompt-only, NULL, FAIL-checked-first, and the echo battery --
     the console echoes the typed command, so a marker substring
     inside a grammar-legal SSID must never classify (mid-line
     JOINED/JOIN_FAIL ignored, line-anchored markers win, bare-\n and
     \r\n anchors, buffer-start anchors);
   - reconnect pick: first credentialed row in store order; all
     ABSENT/INVALID -> -1.

2. Compiled ESP pairing/version gate (m1_csrc/m1_esp_feature_gate.c
   + m1_csrc/m1_esp_version.c, simulated console):
   - the in-repo ESP application's ESP32C6_FW_VERSION_LINE parses, via
     the REAL esp_fw_parse_version_reply, to exactly the gate's
     ESP_FEATURE_WIFI_JOIN minimum (the paired image is accepted);
   - a 1.0.0.0 image (pre-pairing) is OLDER/refused, a dead console is
     ABSENT/refused, and the paired version is EQUIVALENT/allowed.

3. Static layer on the real sources:
   - the join transport is gated BEFORE any send: inside
     wifi_join_exec the live query precedes esp32_uart_write, and
     wifi_join_exec is the join path's only console writer;
   - the command buffer (which carries the plaintext PSK) is
     m1_secure_zero()ed on both exit paths after send;
   - every decrypted/entered PSK buffer is caller-side zeroized
     (from_scan / from_saved / reconnect_boot);
   - no M1_LOG call anywhere in the join section (the PSK is never
     logged, and no error string embeds it);
   - menu wiring: Wi-Fi menu "Join Network" leaf, the Networks action
     entry, and the saved-browser Join action;
   - boot reconnect is hooked in menu_wifi_init, once per boot;
   - the ESP-side handler exists, returns CTRL_RESP_CONNECT_AP with
     the CTRL_ERR_* codes, and wipes its join command before free;
   - scripts/run_host_tests.sh runs this suite.

From the repository root: python3 m1_csrc/test/m1_wifi_join_test.py
"""

import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

PASSED = 0
FAILED = 0


def CHECK(cond, msg):
    global PASSED, FAILED
    if cond:
        PASSED += 1
    else:
        FAILED += 1
        print(f"  FAIL: {msg}")


def source(path):
    return (ROOT / path).read_text()


def function(text, name):
    match = re.search(r"^[^\n;]*\b" + name + r"\([^;{}]*\)\s*\n\{", text, re.M)
    assert match, name
    start = match.start()
    depth = 1
    body = text[match.end() :]
    for token in re.finditer(
        r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'])*\'|'
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


def enum_typedef(text, name):
    end_marker = "} " + name + ";"
    end_idx = text.index(end_marker) + len(end_marker)
    start_idx = text.rindex("typedef enum", 0, end_idx)
    return text[start_idx:end_idx]


def simple_typedef(text, name):
    end_marker = "} " + name + ";"
    end_idx = text.index(end_marker) + len(end_marker)
    start_idx = text.rindex("typedef struct", 0, end_idx)
    return text[start_idx:end_idx]


def host_cc():
    for cc in ("cc", "gcc", "clang"):
        try:
            subprocess.run([cc, "--version"], capture_output=True, check=True)
            return cc
        except (FileNotFoundError, subprocess.CalledProcessError):
            continue
    raise AssertionError("no usable host C compiler (cc/gcc/clang) found")


# ============================================================================
# Compiled layer 1: extracted join core + real m1_crypto.c
# ============================================================================

JOIN_CORE = None  # assembled per-build (module-level for error reporting)


def assemble_join_core():
    wifi = source("m1_csrc/m1_wifi.c")
    parts = [
        "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n"
        "#include <stdbool.h>\n#include <stdint.h>\n#include <ctype.h>\n"
        "#include <stddef.h>\n",
        "void m1_secure_zero(void *p, size_t len); /* real one: m1_crypto.c */\n",
        define(wifi, "WIFI_PSK_MAX_CHARS"),
        define(wifi, "WIFI_JOIN_PSK_MIN_CHARS"),
        define(wifi, "WIFI_JOIN_RESP_JOINED"),
        define(wifi, "WIFI_JOIN_RESP_FAIL"),
        define(wifi, "WIFI_JOIN_CMD_MAX"),
        enum_typedef(wifi, "wifi_join_result_t"),
        define(wifi, "WIFI_CRED_FIELD_MAX"),
        enum_typedef(wifi, "wifi_cred_state_t"),
        simple_typedef(wifi, "wifi_saved_entry_t"),
        define(wifi, "WIFI_SAVED_MAX"),
        line(wifi, r"^static wifi_saved_entry_t g_saved\[WIFI_SAVED_MAX\];$"),
        line(wifi, r"^static uint16_t g_saved_count = 0;$"),
        function(wifi, "wifi_join_psk_valid"),
        function(wifi, "wifi_join_ssid_valid"),
        function(wifi, "wifi_join_build_cmd"),
        function(wifi, "wifi_join_reply_has_line"),
        function(wifi, "wifi_join_classify_reply"),
        function(wifi, "wifi_join_reconnect_pick"),
    ]
    # The rotor string constant spans several quoted lines; the final one
    # ends with '";' (a bare ';' search would stop at the ';' that is
    # itself one of the rotor's symbol characters).
    rotor_start = wifi.index("static const char WIFI_PSK_ROTOR")
    rotor_lines = []
    for ln in wifi[rotor_start:].splitlines():
        rotor_lines.append(ln)
        if ln.rstrip().endswith('";'):
            break
    rotor_decl = "\n".join(rotor_lines)
    parts.insert(parts.index(function(wifi, "wifi_join_psk_valid")), rotor_decl)
    return "\n".join(p for p in parts if p)


JOIN_HARNESS = r"""
/* Generated by m1_csrc/test/m1_wifi_join_test.py -- do not edit. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>
#include <stddef.h>

/* real production zeroization (m1_crypto.c) */
void m1_secure_zero(void *p, size_t len);

/* extracted from m1_wifi.c */
{EXTRACTED}

static int fails = 0;
#define FAILF(...) do { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } while (0)
#define CHECKN(c, m) do { if (!(c)) FAILF("%s", m); } while (0)

static void seed_row(uint16_t i, const char *ssid, const char *bssid,
                     const char *field, int cred_state)
{
    memset(&g_saved[i], 0, sizeof(g_saved[i]));
    snprintf(g_saved[i].ssid, sizeof(g_saved[i].ssid), "%s", ssid);
    snprintf(g_saved[i].bssid, sizeof(g_saved[i].bssid), "%s", bssid);
    snprintf(g_saved[i].psk_enc, sizeof(g_saved[i].psk_enc), "%s", field);
    g_saved[i].cred_state = (uint8_t)cred_state;
}

static void test_psk_grammar(void)
{
    char hex64[65];
    char eight[9];
    char p65[66];
    char p63[64];
    uint8_t i;

    for (i = 0; i < 64u; i++) hex64[i] = "0123456789ABCDEF"[i % 16u];
    hex64[64] = '\0';
    memset(eight, 'a', 8); eight[8] = '\0';
    memset(p63, 'K', 63); p63[63] = '\0';
    memset(p65, 'z', 65); p65[65] = '\0';

    CHECKN(wifi_join_psk_valid("pass1234"), "8-char passphrase");
    CHECKN(wifi_join_psk_valid(eight), "exact 8 boundary");
    CHECKN(wifi_join_psk_valid(p63), "exact 63 boundary");
    CHECKN(wifi_join_psk_valid(hex64), "64-hex PMK form");
    CHECKN(wifi_join_psk_valid("pass word1"), "embedded space legal");
    CHECKN(wifi_join_psk_valid("p@$$w0rd!<>?"), "symbols legal");
    CHECKN(wifi_join_psk_valid("pass123") == false, "7 chars refused");
    CHECKN(wifi_join_psk_valid(p65) == false, "65 chars refused");
    CHECKN(wifi_join_psk_valid(hex64) , "hex64 pre-check");
    hex64[10] = 'g';
    CHECKN(wifi_join_psk_valid(hex64) == false, "64 non-hex refused");
    hex64[10] = 'A';
    CHECKN(wifi_join_psk_valid(hex64), "64 hex restored");
    CHECKN(wifi_join_psk_valid("pass\"1234") == false, "embedded quote refused");
    CHECKN(wifi_join_psk_valid("pass1234\r") == false, "CR refused");
    CHECKN(wifi_join_psk_valid("pass1234\n") == false, "LF refused");
    CHECKN(wifi_join_psk_valid("pass\x01\x02x1") == false, "control char refused");
    CHECKN(wifi_join_psk_valid("") == false, "empty refused");
    CHECKN(wifi_join_psk_valid(NULL) == false, "NULL refused");
    printf("psk grammar done (%d fails so far)\n", fails);
}

static void test_rotor_roundtrip(void)
{
    char probe[9];
    size_t i, j;
    bool seen[128] = {false};

    CHECKN(strlen(WIFI_PSK_ROTOR) == 94u, "rotor alphabet is exactly 94 chars");
    for (i = 0; i < 94u; i++)
    {
        unsigned char c = (unsigned char)WIFI_PSK_ROTOR[i];
        CHECKN(c >= 0x20u && c <= 0x7eu, "rotor char printable");
        CHECKN(c != '"', "rotor never offers a quote");
        CHECKN(!seen[c], "rotor chars unique");
        seen[c] = true;
    }
    /* every editor-producible PSK is join-legal: build 8-char probes
     * from each rotor character */
    for (i = 0; i < 94u; i++)
    {
        for (j = 0; j < 8u; j++) probe[j] = WIFI_PSK_ROTOR[i];
        probe[8] = '\0';
        if (!wifi_join_psk_valid(probe))
            FAILF("rotor char '%c' builds an illegal PSK", WIFI_PSK_ROTOR[i]);
    }
    printf("rotor round trip done (%d fails so far)\n", fails);
}

static void test_ssid_grammar(void)
{
    char s33[34];
    memset(s33, 's', 33); s33[33] = '\0';

    CHECKN(wifi_join_ssid_valid(""), "empty (hidden) ssid");
    CHECKN(wifi_join_ssid_valid("Net"), "plain ssid");
    CHECKN(wifi_join_ssid_valid("with space and !@#"), "symbol ssid");
    CHECKN(wifi_join_ssid_valid(s33) == false, "33 chars refused");
    CHECKN(wifi_join_ssid_valid("bad\"quote") == false, "quote refused");
    CHECKN(wifi_join_ssid_valid("bad\rcr") == false, "CR refused");
    CHECKN(wifi_join_ssid_valid(NULL) == false, "NULL refused");
    printf("ssid grammar done (%d fails so far)\n", fails);
}

static void test_build_cmd(void)
{
    char buf[WIFI_JOIN_CMD_MAX];
    char tiny[8];
    char hex64[65];
    char ssid32[33];
    char p65[66];
    int n;
    uint8_t i;

    for (i = 0; i < 64u; i++) hex64[i] = "0123456789abcdef"[i % 16u];
    hex64[64] = '\0';
    memset(ssid32, 'S', 32); ssid32[32] = '\0';
    memset(p65, 'z', 65); p65[65] = '\0';

    n = wifi_join_build_cmd(buf, sizeof(buf), "MyNet", "pass1234", true);
    CHECKN(n == (int)strlen("join \"MyNet\" \"pass1234\"\r\n") &&
           strcmp(buf, "join \"MyNet\" \"pass1234\"\r\n") == 0,
           "exact command bytes");
    n = wifi_join_build_cmd(buf, sizeof(buf), "", "pass1234", true);
    CHECKN(n > 0 && strcmp(buf, "join \"\" \"pass1234\"\r\n") == 0,
           "hidden ssid builds an empty first field");
    n = wifi_join_build_cmd(buf, sizeof(buf), "OpenNet", NULL, false);
    CHECKN(n > 0 && strcmp(buf, "join \"OpenNet\" \"\"\r\n") == 0,
           "open network builds an empty psk field");
    CHECKN(wifi_join_build_cmd(buf, sizeof(buf), "OpenNet", "dropped", false) < 0,
           "non-empty psk with have_psk=false refused (dropped secret)");
    CHECKN(wifi_join_build_cmd(buf, sizeof(buf), "Net", "pass123", true) < 0,
           "short psk refused at build time");
    CHECKN(wifi_join_build_cmd(buf, sizeof(buf), "Net", p65, true) < 0,
           "long psk refused at build time");
    CHECKN(wifi_join_build_cmd(buf, sizeof(buf), "Net", "bad\"psk!", true) < 0,
           "quote psk refused at build time");
    CHECKN(wifi_join_build_cmd(buf, sizeof(buf), "ba\"d", "pass1234", true) < 0,
           "quote ssid refused at build time");
    CHECKN(wifi_join_build_cmd(buf, sizeof(buf), "Net", hex64, true) > 0,
           "64-hex psk builds");

    /* worst case fits the production buffer exactly as sized */
    n = wifi_join_build_cmd(buf, sizeof(buf), ssid32, hex64, true);
    CHECKN(n == 6 + 32 + 3 + 64 + 1 + 2, "worst-case length 108");
    CHECKN(buf[n] == '\0' && buf[0] == 'j' && buf[n-1] == '\n',
           "worst-case well formed");

    /* no truncation, ever: a too-small destination refuses and the
     * partially written plaintext is wiped */
    memset(tiny, 0xAA, sizeof(tiny));
    n = wifi_join_build_cmd(tiny, sizeof(tiny), "MyNet", "pass1234", true);
    CHECKN(n < 0, "overflow refused");
    for (i = 0; i < (uint8_t)sizeof(tiny); i++)
        if ((unsigned char)tiny[i] != 0u) FAILF("overflow left byte %u = %02x", i, (unsigned char)tiny[i]);
    CHECKN(wifi_join_build_cmd(NULL, 16, "N", "pass1234", true) < 0,
           "NULL dst refused");
    printf("build cmd done (%d fails so far)\n", fails);
}

static void test_classify(void)
{
    CHECKN(wifi_join_classify_reply("JOINED 192.168.1.5\r\n>> ") == WIFI_JOIN_RESULT_JOINED,
           "JOINED with IP suffix");
    CHECKN(wifi_join_classify_reply("noise\r\nJOINED\r\n>> ") == WIFI_JOIN_RESULT_JOINED,
           "JOINED amid noise");
    CHECKN(wifi_join_classify_reply("JOIN_FAIL 2\r\n>> ") == WIFI_JOIN_RESULT_FAIL,
           "JOIN_FAIL with code");
    CHECKN(wifi_join_classify_reply("\r\n>> ") == WIFI_JOIN_RESULT_TIMEOUT,
           "prompt only = timeout/unknown");
    CHECKN(wifi_join_classify_reply("") == WIFI_JOIN_RESULT_TIMEOUT, "empty");
    CHECKN(wifi_join_classify_reply(NULL) == WIFI_JOIN_RESULT_TIMEOUT, "NULL");

    /* Echo-shaped cases: the console echoes the typed command line, and
     * a grammar-legal SSID may itself contain a marker substring -- a
     * mid-line occurrence must never classify (the harden fix). */
    CHECKN(wifi_join_classify_reply("join \"JOIN_FAILX\" \"p\"\r\nJOIN_FAIL 3\r\n>> ")
           == WIFI_JOIN_RESULT_FAIL,
           "mid-line JOIN_FAIL in the echo ignored; anchored reply wins");
    CHECKN(wifi_join_classify_reply("join \"JOINEDX\" \"p\"\r\n>> ")
           == WIFI_JOIN_RESULT_TIMEOUT,
           "mid-line JOINED in the echo is NOT a success");
    CHECKN(wifi_join_classify_reply("join \"JOINED_TEST\" \"p\"\r\nJOINED 10.0.0.2\r\n>> ")
           == WIFI_JOIN_RESULT_JOINED,
           "marker-substring SSID echo + genuine anchored success");
    CHECKN(wifi_join_classify_reply("join \"JOIN_FAIL_TEST\" \"p\"\r\nJOINED\r\n>> ")
           == WIFI_JOIN_RESULT_JOINED,
           "anchored success beats a mid-line FAIL in the echo");
    CHECKN(wifi_join_classify_reply("JOINED\r\n") == WIFI_JOIN_RESULT_JOINED,
           "marker at the very start of the buffer");
    CHECKN(wifi_join_classify_reply("JOIN_FAIL 4\r\n") == WIFI_JOIN_RESULT_FAIL,
           "FAIL marker at the very start of the buffer");
    CHECKN(wifi_join_classify_reply("esp console v1\nJOINED\r\n>> ")
           == WIFI_JOIN_RESULT_JOINED,
           "marker anchored after a bare \\n");
    CHECKN(wifi_join_classify_reply("esp console v1\r\nJOIN_FAIL 1\r\n>> ")
           == WIFI_JOIN_RESULT_FAIL,
           "marker anchored after \\r\\n");
    CHECKN(wifi_join_classify_reply("XJOINEDX\r\nXJOIN_FAILX\r\n>> ")
           == WIFI_JOIN_RESULT_TIMEOUT,
           "mid-line-only occurrences never classify");
    CHECKN(wifi_join_classify_reply("noise XJOIN_FAILX noise\r\nno markers here\r\n>> ")
           == WIFI_JOIN_RESULT_TIMEOUT,
           "mid-line-only occurrences never classify, suffix or not");
    /* Both markers ANCHORED: FAIL must win regardless of order -- this
     * pins the check ordering the harden fix relies on (a swap of the
     * two anchored tests would otherwise still pass the suite). */
    CHECKN(wifi_join_classify_reply("JOIN_FAIL 1\r\nJOINED\r\n>> ")
           == WIFI_JOIN_RESULT_FAIL,
           "both markers anchored: FAIL wins");
    CHECKN(wifi_join_classify_reply("JOINED\r\nJOIN_FAIL 2\r\n>> ")
           == WIFI_JOIN_RESULT_FAIL,
           "both markers anchored, mirror order: FAIL still wins");
    printf("classify done (%d fails so far)\n", fails);
}

static void test_editor_builder_agreement(void)
{
    char probe[65];
    char cmd[WIFI_JOIN_CMD_MAX];
    size_t i;

    /* The editor's save rule IS wifi_join_psk_valid (statically pinned
     * in the static layer); these pin that the command builder agrees
     * with the same rule on every probe class, so the editor can never
     * hand the flow a secret the transport refuses -- including at
     * length 64, where only the all-hex PMK form is legal. */
    for (i = 0; i < 94u; i++)
    {
        memset(probe, WIFI_PSK_ROTOR[i], 63);
        probe[63] = '\0';
        if (!wifi_join_psk_valid(probe))
            FAILF("63-char rotor probe '%c' must be valid", WIFI_PSK_ROTOR[i]);
        if (wifi_join_build_cmd(cmd, sizeof(cmd), "N", probe, true) <= 0)
            FAILF("63-char rotor probe '%c' must build", WIFI_PSK_ROTOR[i]);
    }
    memset(probe, 'z', 64); probe[64] = '\0';
    CHECKN(wifi_join_psk_valid(probe) == false,
           "64-char non-hex refused by the grammar rule");
    CHECKN(wifi_join_build_cmd(cmd, sizeof(cmd), "N", probe, true) < 0,
           "64-char non-hex refused by the builder (editor rejects it in-editor)");
    for (i = 0; i < 64u; i++) probe[i] = "0123456789ABCDEF"[i % 16u];
    probe[64] = '\0';
    CHECKN(wifi_join_psk_valid(probe), "64-hex PMK accepted by the grammar rule");
    CHECKN(wifi_join_build_cmd(cmd, sizeof(cmd), "N", probe, true) > 0,
           "64-hex PMK accepted by the builder");
    printf("editor/builder agreement done (%d fails so far)\n", fails);
}

static void test_reconnect_pick(void)
{
    g_saved_count = 0;
    CHECKN(wifi_join_reconnect_pick() < 0, "empty store -> none");

    seed_row(0, "A", "AA:AA:AA:AA:AA:AA", "", 0 /* ABSENT */);
    seed_row(1, "B", "BB:BB:BB:BB:BB:BB", "x", 2 /* INVALID */);
    g_saved_count = 2;
    CHECKN(wifi_join_reconnect_pick() < 0, "no usable credential -> none");

    seed_row(2, "C", "CC:CC:CC:CC:CC:CC", "ok", 1 /* OK */);
    seed_row(3, "D", "DD:DD:DD:DD:DD:DD", "ok", 1 /* OK */);
    g_saved_count = 4;
    CHECKN(wifi_join_reconnect_pick() == 2, "first credentialed row wins");

    g_saved[2].cred_state = 2; /* the pick's credential turned invalid */
    CHECKN(wifi_join_reconnect_pick() == 3, "later credentialed row is next");
    printf("reconnect pick done (%d fails so far)\n", fails);
}

int main(void)
{
    test_psk_grammar();
    test_rotor_roundtrip();
    test_ssid_grammar();
    test_build_cmd();
    test_classify();
    test_editor_builder_agreement();
    test_reconnect_pick();

    if (fails == 0)
    {
        printf("JOIN CORE OK\n");
        return 0;
    }
    printf("JOIN CORE FAILED (%d)\n", fails);
    return 1;
}
"""


def compiled_join_layer(cc):
    with tempfile.TemporaryDirectory() as td:
        tdir = Path(td)
        harness = tdir / "join_core_test.c"
        extracted = assemble_join_core()
        harness.write_text(JOIN_HARNESS.replace("{EXTRACTED}", extracted))
        exe = tdir / "join_core_test"
        cmd = [
            cc,
            "-std=c11",
            "-D_GNU_SOURCE",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic",
            "-I",
            str(ROOT / "m1_csrc"),
            str(harness),
            str(ROOT / "m1_csrc" / "m1_crypto.c"),
            "-o",
            str(exe),
        ]
        r = subprocess.run(cmd, capture_output=True, text=True)
        CHECK(r.returncode == 0, f"join core compile failed: {r.stderr.strip()[:600]}")
        if r.returncode != 0:
            return
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        CHECK(
            r.returncode == 0 and "JOIN CORE OK" in r.stdout,
            f"join core run failed:\n{r.stdout}{r.stderr}",
        )


# ============================================================================
# Compiled layer 2: ESP pairing / version gate (real gate core, simulated
# console; the join minimum must accept exactly the in-repo ESP app's
# advertised version)
# ============================================================================

PAIRING_HARNESS = r"""
/* Generated by m1_csrc/test/m1_wifi_join_test.py -- do not edit. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "m1_esp_feature_gate.h"

static int fails = 0;
#define FAILF(...) do { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } while (0)
#define CHECKN(c, m) do { if (!(c)) FAILF("%s", m); } while (0)

static bool sim_ping_ok;
static bool sim_version_ok;
static char sim_reply[256];

bool esp_gate_console_ping_seam(uint32_t timeout_ms)
{
    (void)timeout_ms;
    return sim_ping_ok;
}

bool esp_gate_console_version_reply_seam(char *buf, uint16_t buf_size,
                                         uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!sim_version_ok || (buf_size == 0u)) { return false; }
    strncpy(buf, sim_reply, buf_size - 1u);
    buf[buf_size - 1u] = '\0';
    return true;
}

static void sim_set(bool ping, bool version, const char *reply)
{
    sim_ping_ok = ping;
    sim_version_ok = version;
    memset(sim_reply, 0, sizeof(sim_reply));
    if (reply) { strncpy(sim_reply, reply, sizeof(sim_reply) - 1u); }
}

/* The ESP-side constant under test, injected verbatim from
 * Esp_spi_at/.../esp_at_list.h by the python layer. */
static const char esp_fw_line[] = {ESP_FW_LINE_JSON};

int main(void)
{
    uint8_t esp_ver[4];
    uint8_t minimum[4];
    esp_feature_gate_result_t r;
    esp_fw_state_t s;

    /* 1. THE PAIRING: the in-repo ESP application's advertised version
     * line must parse, with the real parser, to exactly the join
     * feature's minimum -- the paired image is EQUIVALENT (allowed). */
    CHECKN(esp_fw_parse_version_reply(esp_fw_line, esp_ver),
           "ESP app version line parses");
    CHECKN(esp_feature_minimum(ESP_FEATURE_WIFI_JOIN, minimum),
           "join minimum readable");
    CHECKN(memcmp(esp_ver, minimum, 4u) == 0,
           "ESP app version == join minimum (paired)");
    printf("paired version %u.%u.%u.%u\n", esp_ver[0], esp_ver[1], esp_ver[2], esp_ver[3]);

    /* 2. live == paired version -> EQUIVALENT, allowed. */
    sim_set(true, true, "version\r\nESPFW 1.1.0.0\r\n>> ");
    s = esp_feature_gate_query(ESP_FEATURE_WIFI_JOIN, &r);
    CHECKN(s == ESP_FW_EQUIVALENT && esp_feature_gate_allows(s),
           "paired image allowed");

    /* 3. pre-pairing image (1.0.0.0) -> OLDER, refused. */
    sim_set(true, true, "version\r\nESPFW 1.0.0.0\r\n>> ");
    s = esp_feature_gate_query(ESP_FEATURE_WIFI_JOIN, &r);
    CHECKN(s == ESP_FW_OLDER && !esp_feature_gate_allows(s),
           "pre-pairing image refused");

    /* 4. dead console -> ABSENT, refused (the flash-gate: an ESP32
     * without the paired handler never hears a join command). */
    sim_set(false, true, "");
    s = esp_feature_gate_query(ESP_FEATURE_WIFI_JOIN, &r);
    CHECKN(s == ESP_FW_ABSENT && !esp_feature_gate_allows(s),
           "absent console refused");

    /* 5. console alive but no recognizable version -> ABSENT, refused. */
    sim_set(true, true, "unknown command\r\n>> ");
    s = esp_feature_gate_query(ESP_FEATURE_WIFI_JOIN, &r);
    CHECKN(s == ESP_FW_ABSENT && !esp_feature_gate_allows(s),
           "no-version console refused");

    if (fails == 0)
    {
        printf("PAIRING OK\n");
        return 0;
    }
    printf("PAIRING FAILED (%d)\n", fails);
    return 1;
}
"""


def compiled_pairing_layer(cc):
    at = source("Esp_spi_at/examples/at_spi_master/spi/stm32/main/esp_at_list.h")
    m = re.search(r'^#define\s+ESP32C6_FW_VERSION_LINE\s+"([^"]*)"', at, re.M)
    CHECK(m is not None, "ESP32C6_FW_VERSION_LINE missing from esp_at_list.h")
    if not m:
        return
    fw_line = m.group(1)
    # The four numeric fields must also agree with the component macros.
    for field, macro in zip(
        fw_line.split(" ")[1].split("."),
        (
            "ESP32C6_FW_VERSION_MAJOR",
            "ESP32C6_FW_VERSION_MINOR",
            "ESP32C6_FW_VERSION_PATCH",
            "ESP32C6_FW_VERSION_RC",
        ),
    ):
        mm = re.search(r"^#define\s+" + macro + r"\s+(\d+)", at, re.M)
        CHECK(
            mm is not None and mm.group(1) == field,
            f"{macro} disagrees with ESP32C6_FW_VERSION_LINE",
        )

    with tempfile.TemporaryDirectory() as td:
        tdir = Path(td)
        harness = tdir / "pairing_test.c"
        quoted = "{" + ",".join(str(ord(ch)) for ch in fw_line) + ",0}"
        harness.write_text(PAIRING_HARNESS.replace("{ESP_FW_LINE_JSON}", quoted))
        exe = tdir / "pairing_test"
        cmd = [
            cc,
            "-std=c99",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic",
            "-DM1_ESP_GATE_HOST_TEST",
            "-DM1CP_HOST_TEST",
            "-I",
            str(ROOT / "m1_csrc"),
            str(ROOT / "m1_csrc" / "m1_esp_feature_gate.c"),
            str(ROOT / "m1_csrc" / "m1_esp_version.c"),
            str(harness),
            "-o",
            str(exe),
        ]
        r = subprocess.run(cmd, capture_output=True, text=True)
        CHECK(r.returncode == 0, f"pairing compile failed: {r.stderr.strip()[:600]}")
        if r.returncode != 0:
            return
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        CHECK(
            r.returncode == 0 and "PAIRING OK" in r.stdout,
            f"pairing run failed:\n{r.stdout}{r.stderr}",
        )


# ============================================================================
# Static layer: wiring seams in the real sources
# ============================================================================


def static_checks():
    wifi = source("m1_csrc/m1_wifi.c")
    menu = source("m1_csrc/m1_menu.c")
    esp_app = source("Esp_spi_at/examples/at_spi_master/spi/stm32/main/esp_app_main.c")
    esp_list = source("Esp_spi_at/examples/at_spi_master/spi/stm32/main/esp_at_list.h")

    # The section banner (the prototype block near the top uses a similar
    # comment, so anchor on the LAST occurrence).
    join_section = wifi.rsplit("Wi-Fi join flow (PR-11", 1)[1]

    # 1. Gate BEFORE send, and exactly one console writer on the join path.
    exec_fn = function(join_section, "wifi_join_exec")
    CHECK(
        "esp_feature_gate_query(ESP_FEATURE_WIFI_JOIN" in exec_fn,
        "join transport runs the live gate",
    )
    CHECK(
        exec_fn.index("esp_feature_gate_query") < exec_fn.index("esp32_uart_write"),
        "gate decision precedes any console write",
    )
    writers = re.findall(r"esp32_uart_write\(", join_section)
    CHECK(
        len(writers) == 1,
        f"join section has {len(writers)} console writers (must be exactly 1)",
    )

    # 2. Command buffer zeroization on both post-send exits.
    CHECK(
        exec_fn.count("m1_secure_zero(g_wifi_join_cmd") == 2,
        "command buffer wiped on both send-path exits",
    )

    # 2b. Reply-buffer wipe: the console echoes the typed command line,
    # so resp can transiently hold the plaintext PSK -- it must be
    # m1_secure_zero()ed after classification on every post-send exit.
    CHECK(
        exec_fn.count("m1_secure_zero(resp") == 2,
        "reply buffer wiped on both post-send exits",
    )
    CHECK(
        exec_fn.rindex("m1_secure_zero(resp")
        > exec_fn.index("esp32_uart_read_until_prompt"),
        "reply-buffer wipe follows the read that captured the echo",
    )

    # 3. Caller-side PSK hygiene on every plaintext holder.
    for fn in (
        "wifi_join_from_scan",
        "wifi_join_from_saved",
        "wifi_join_reconnect_boot",
    ):
        body = function(join_section, fn)
        CHECK("m1_secure_zero(psk" in body, f"{fn} zeroizes its psk buffer")
    editor = function(join_section, "wifi_psk_entry")
    CHECK("m1_secure_zero(psk" in editor, "editor zeroizes on cancel")
    # 3b. The editor's save branch must CONSUME the real join grammar's
    #     verdict (wifi_join_psk_valid -- the same rule the builder
    #     enforces), not merely mention it: pin the exact conditional
    #     shape, the accept path (return 1) directly under it, and the
    #     distinct 64-non-hex notice. A refactor that computes the
    #     predicate and ignores the result, or drops the distinct
    #     notice, fails these.
    editor_save = editor.split("WIFI_PSK_ROTOR_END_IDX) /* '>' : save */")[1][:700]
    CHECK(
        "if (wifi_join_psk_valid(psk))" in editor_save,
        "editor save branch does not branch on the real grammar verdict",
    )
    save_cond = editor_save.split("if (wifi_join_psk_valid(psk))")[1][:400]
    CHECK(
        "return 1;" in save_cond.split("}")[0],
        "editor accept path must return directly under the validator",
    )
    CHECK(
        '"64=hex"' in editor_save,
        "editor 64-non-hex refusal notice missing",
    )

    # 4. The PSK is never logged and never embedded in an error string:
    #    the join section contains no logging call at all.
    CHECK(
        "M1_LOG" not in join_section,
        "join section logs something (PSK must never be logged)",
    )

    # 5. Menu wiring.
    CHECK(
        '"Join Network", wifi_join_network' in menu,
        "Wi-Fi menu Join Network leaf missing",
    )
    CHECK(
        "menu_Wifi_Join" in menu.split("S_M1_Menu_t menu_Wifi =")[1].split("};")[0],
        "Join leaf not listed under menu_Wifi",
    )
    CHECK(
        '"Join Network"' in wifi and "wifi_join_from_scan(net)" in wifi,
        "Networks action menu Join Network wiring missing",
    )
    CHECK(
        '"Join"' in wifi and "wifi_join_from_saved(sel)" in wifi,
        "saved-browser Join wiring missing",
    )
    wifi_menu = menu.split("S_M1_Menu_t menu_Wifi =")[1].split("};")[0]
    n_children = len(re.findall(r"&menu_", wifi_menu))
    CHECK(
        n_children == 7 and "NULL, 7," in wifi_menu,
        f"menu_Wifi child count wrong ({n_children})",
    )

    # 6. Boot reconnect hook, once per boot.
    init_fn = function(wifi, "menu_wifi_init")
    CHECK(
        "wifi_join_reconnect_boot();" in init_fn,
        "menu_wifi_init does not run the boot reconnect",
    )
    rc = function(join_section, "wifi_join_reconnect_boot")
    CHECK("s_reconnect_done" in rc, "reconnect is not once-per-boot")

    # 7. Reconnect bounded budget + interactive budget constants.
    CHECK(
        "#define WIFI_JOIN_RECONNECT_TIMEOUT_MS 15000u" in wifi,
        "bounded reconnect budget missing",
    )
    CHECK(
        "SEC_TO_MILLISEC(DEFAULT_CTRL_RESP_CONNECT_AP_TIMEOUT)" in wifi,
        "interactive join budget not tied to the paired 45 s constant",
    )

    # 8. ESP-side handler: CTRL_RESP_CONNECT_AP + PSK wipe.
    handler = function(esp_app, "wifi_connect_ap")
    CHECK("CTRL_RESP_CONNECT_AP" in handler, "handler answers CTRL_RESP_CONNECT_AP")
    for code in (
        "CTRL_ERR_INVALID_PASSWORD",
        "CTRL_ERR_NO_AP_FOUND",
        "CTRL_ERR_NOT_CONNECTED",
        "CTRL_ERR_REQUEST_TIMEOUT",
        "CTRL_ERR_INVALID_ARGUMENT",
    ):
        CHECK(code in handler, f"handler response code {code} missing")
    CHECK(
        "volatile char *wipe = join_cmd" in handler,
        "handler does not wipe the join command before free",
    )
    # 8b. The SPI TX stream-buffer copy: the command (plaintext PSK)
    #     rode into spi_master_tx_ring_buf via xStreamBufferSend and
    #     FreeRTOS does not zero storage on read -- a bounded scrub
    #     helper must ride the driver's single-flight semaphore, gate
    #     on genuine EMPTINESS (occupancy, not free space -- a pending
    #     command must never be drained into the helper's buffers),
    #     go ONLY through the stream-buffer API (no blind memset of
    #     driver internals), never notify the slave, zeroize its own
    #     drain target, and be invoked from the join handler after the
    #     exchange.
    scrub = function(esp_app, "esp_scrub_spi_tx_ring_buf")
    CHECK(
        "xSemaphoreTake(esp_ctrl_req_sem" in scrub,
        "tx-ring scrub does not ride the single-flight semaphore",
    )
    CHECK(
        "xStreamBufferIsEmpty(spi_master_tx_ring_buf)" in scrub,
        "tx-ring scrub lacks the occupancy (IsEmpty) precondition",
    )
    CHECK(
        scrub.index("xStreamBufferIsEmpty(spi_master_tx_ring_buf)")
        < scrub.index("while ( done"),
        "occupancy precondition must precede the fill loop",
    )
    CHECK(
        "xStreamBufferSend(" in scrub and "xStreamBufferReceive(" in scrub,
        "tx-ring scrub must go through the stream-buffer API",
    )
    CHECK("memset" not in scrub, "tx-ring scrub must not blind-memset driver internals")
    CHECK(
        "notify_slave_to_recv" not in scrub,
        "tx-ring scrub must never trigger a transmission",
    )
    CHECK(
        "wipe[wi] = 0;" in scrub,
        "tx-ring scrub does not zeroize its drain target",
    )
    CHECK(
        "esp_scrub_spi_tx_ring_buf();" in handler,
        "join handler does not invoke the tx-ring scrub",
    )
    # 8c. The DMA staging region (trans_data) in the control task is a
    #     second copy: it must be wiped right after the transmit.
    after_send = esp_app.split("ret = spi_write_data(trans_data, plan_send_len);")[1][
        :900
    ]
    CHECK(
        "wipe[wi] = 0;" in after_send,
        "DMA staging region not scrubbed after the transmit",
    )
    CHECK(
        "ESP32C6_AT_REQ_CONNECT_AP" in esp_list,
        "AT+CWJAP define missing from esp_at_list.h",
    )

    # 9. The suite is wired into the aggregator.
    runner = source("scripts/run_host_tests.sh")
    CHECK(
        "m1_csrc/test/m1_wifi_join_test.py" in runner,
        "suite not wired into scripts/run_host_tests.sh",
    )


def main():
    cc = host_cc()
    compiled_join_layer(cc)
    compiled_pairing_layer(cc)
    static_checks()

    print(f"\n{PASSED} passed, {FAILED} failed")
    if FAILED:
        sys_exit(1)
    print("ALL WIFI JOIN TESTS PASSED")


def sys_exit(code):
    raise SystemExit(code)


if __name__ == "__main__":
    main()
