#!/usr/bin/env python3
"""Host tests for the BLE advertisement editor (PR-12 of the Merge 3
series, PLAN section 15; issue #11).

Same technique as m1_wifi_join_test.py / m1_esp_feature_gate_test.py:
compile the REAL pure core (m1_csrc/m1_bt_adv.c is HAL/FreeRTOS/FatFs
free by design, so the whole translation unit host-compiles unchanged),
compile the REAL feature-gate core (-DM1_ESP_GATE_HOST_TEST, simulated
console) for the advertise-specific gate battery, and pin the wiring
seams statically in the live sources.

Layers:

1. Compiled adv core (m1_csrc/m1_bt_adv.c, real TU):
   - preset table: every fixed preset passes the module's own
     AD-structure validator, fits the 31-byte legacy cap, and matches
     its documented byte image exactly (provenance pin):
       Apple proximity   1E FF 4C 00 07 19 ... (31 bytes, full cap)
       Samsung Easysetup 1B FF 75 00 42 09 ... (28 bytes)
       Swift Pair        06 FF 06 00 03 00 80  (7 bytes; length byte
         corrected to the Core-Spec count -- the plan's anchor literal
         07 FF ... 80 claimed 7 following bytes but has 6)
   - AD-structure validity: walks on exact boundaries; negatives (zero
     length byte, structure overrun, trailing garbage, 32-byte payload,
     empty, NULL);
   - payload builder: Name beacon (empty name -> flags only 02 01 06;
     26-char name -> exactly 31 bytes; 27 refused), mimic presets
     byte-exact and name-independent, custom passthrough re-validated,
     unknown preset refused;
   - interval conversion: 100/250/500/1000/2000 ms -> 160/400/800/
     1600/3200 units (0.625 ms BLE unit, ms*8/5); exactness refusals
     (7 ms, 15 ms, 1 ms), range refusals (0, 5 ms, 15 ms below the
     0x20-unit minimum, 10245 ms and 20000 ms above the 0x4000-unit
     maximum), boundary accepts (20 ms -> 32, 10240 ms -> 16384);
   - command construction: exact "adv <units> <HEX>" strings for the
     shipped presets, stop command, overflow refusal (dst too small --
     never truncated), invalid interval / invalid payload refusals,
     worst-case fit against M1_BT_ADV_CMD_MAX;
   - reply classification: line-anchored ADV:OK / ADV:FAIL, echo
     battery (marker substrings mid-line never classify), CRLF and
     bare-\\n anchors, prompt-only, NULL;
   - custom hex parser: whitespace-tolerant parse of an .adv file
     image, 31-byte cap, odd nibble / non-hex / empty refusals,
     invalid-payload refusal; .adv extension check.

2. Compiled ESP pairing gate (m1_csrc/m1_esp_feature_gate.c +
   m1_csrc/m1_esp_version.c, simulated console):
   - the advertise minimum is 1.2.0.0 (the version the first console
     image carrying the adv verb must report);
   - the currently paired image version (ESP32C6_FW_VERSION_LINE =
     "ESPFW 1.1.0.0", pinned from Esp_spi_at/.../esp_at_list.h) parses
     via the REAL parser and is OLDER/refused for advertise -- the
     honest configure-and-save-but-do-not-advertise state;
   - a dead console is ABSENT/refused; a 1.2.0.0 image is
     EQUIVALENT/allowed; a 1.3.0.0 image is NEWER/allowed;
   - a stale DR2/DR3 record claiming 1.2.0.0 never flips a live 1.1.0.0
     refusal.

2b. Compiled gate-refusal hint mapping (bt_adv_gate_hint extracted
    verbatim from m1_csrc/m1_bt.c -- it is pure over the gate header):
    OLDER maps to "Update ESP fw" (the firmware-version remedy), every
    ABSENT reason and NULL map to "Check ESP32" (the link remedy), and
    the allowed states map to the link line too (total function; the
    editor only consults it on refusal).

3. Static layer on the real sources:
   - m1_bt_adv.c is in M1_APP_SOURCES (scoped -Werror);
   - the advertise transport is gated BEFORE the adv send: inside
     bt_adv_exec_start the live query precedes bt_cmd_process (the
     only direct console writer; the sole pre-gate console use is the
     idempotent bt_session_ensure bring-up), and the editor's start
     path goes only through bt_adv_exec_start;
   - g_bt_adv_active is set only after the ADV:OK classification (no
     success without the console's confirmation), with the comparison
     polarity pinned (classify != OK refuses; == OK stops);
   - adv stop is only ever sent while advertising is active
     (bt_adv_exec_stop guards on g_bt_adv_active, and menu exit funnels
     through it), and the stop reply is CONSULTED: classification
     precedes the latch clear, only an == OK reply counts as stopped,
     and a non-OK/timeout reply surfaces the
     "Adv stop failed / Check ESP32" message (never a silent success);
   - menu wiring: Bluetooth menu has 5 children ending with the
     Adv Editor leaf; bluetooth_adv_editor is declared in m1_bt.h;
   - persistence: the editor applies settings_set_bt_adv_name /
     settings_set_bt_preset / settings_set_bt_interval_ms and exits
     through settings_exit_save (one atomic save);
   - reload mapping: a stored interval outside the table (the PR-1
     default 0) maps to the documented 1000 ms entry, and a
     hand-edited preset id clamps into the valid range;
   - stale-click discipline on the RUN row (iter-1 review): the button
     queue reset runs on BOTH the start and the stop path (after the
     multi-second console exchanges), before the editor loop's single
     bt_wait_button reader -- a click queued during a stop can never
     restart advertising;
   - the gate-refusal second line is state-aware (bt_adv_gate_hint),
     not a hardcoded constant, at the refusal site;
   - no vendor Esp_spi_at advertise example usage: the BT module never
     references AT+BLEADVDATAEX or the vendor SPI-AT control enums for
     advertising (the shipped console is a custom verb surface);
   - scripts/run_host_tests.sh runs this suite.

From the repository root: python3 m1_csrc/test/m1_bt_adv_test.py
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


def host_cc():
    for cc in ("cc", "gcc", "clang"):
        try:
            subprocess.run([cc, "--version"], capture_output=True, check=True)
            return cc
        except (FileNotFoundError, subprocess.CalledProcessError):
            continue
    raise AssertionError("no usable host C compiler (cc/gcc/clang) found")


# ---------------------------------------------------------------------------
# Compiled layer 1: the real adv core TU
# ---------------------------------------------------------------------------

ADV_HARNESS = r"""
/* Generated by m1_csrc/test/m1_bt_adv_test.py -- do not edit. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "m1_bt_adv.h"

static int fails = 0;

#define FAILF(...) do { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } while (0)
#define CHECKN(c, m) do { if (!(c)) FAILF("%s", m); } while (0)

static const uint8_t EXP_APPLE[31] = {
    0x1E, 0xFF, 0x4C, 0x00, 0x07, 0x19,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00
};
static const uint8_t EXP_SAMSUNG[28] = {
    0x1B, 0xFF, 0x75, 0x00, 0x42, 0x09, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t EXP_SWIFT[7] = {
    0x06, 0xFF, 0x06, 0x00, 0x03, 0x00, 0x80
};

static void test_presets(void)
{
    uint8_t p[31];
    uint8_t n = 0;
    char nm[16];
    int r;

    /* Every fixed preset passes the module's own validator and the cap. */
    r = m1_bt_adv_build_payload(M1_BT_ADV_PRESET_APPLE, "name ignored",
                                NULL, 0, p, &n);
    CHECKN(r == 0 && n == 31, "apple builds, 31 bytes (full cap)");
    CHECKN(m1_bt_adv_payload_valid(p, n), "apple passes own validator");
    CHECKN(memcmp(p, EXP_APPLE, 31) == 0, "apple byte image pinned");
    CHECKN(m1_bt_adv_build_payload(M1_BT_ADV_PRESET_APPLE, NULL,
                                   NULL, 0, p, &n) == 0 && n == 31,
           "apple name-independent");

    r = m1_bt_adv_build_payload(M1_BT_ADV_PRESET_SAMSUNG, "", NULL, 0, p, &n);
    CHECKN(r == 0 && n == 28, "samsung builds, 28 bytes");
    CHECKN(m1_bt_adv_payload_valid(p, n), "samsung passes own validator");
    CHECKN(memcmp(p, EXP_SAMSUNG, 28) == 0, "samsung byte image pinned");

    r = m1_bt_adv_build_payload(M1_BT_ADV_PRESET_SWIFT, NULL, NULL, 0, p, &n);
    CHECKN(r == 0 && n == 7, "swift builds, 7 bytes");
    CHECKN(m1_bt_adv_payload_valid(p, n), "swift passes own validator");
    CHECKN(memcmp(p, EXP_SWIFT, 7) == 0, "swift byte image pinned");

    /* Display names resolve for every shipped id; table count sane. */
    CHECKN(m1_bt_adv_preset_name(M1_BT_ADV_PRESET_NAME, nm, sizeof nm) &&
           strcmp(nm, "Name beacon") == 0, "name preset label");
    CHECKN(m1_bt_adv_preset_name(M1_BT_ADV_PRESET_CUSTOM, nm, sizeof nm) &&
           strcmp(nm, "Custom (SD)") == 0, "custom preset label");
    CHECKN(m1_bt_adv_preset_name(M1_BT_ADV_PRESET_APPLE, nm, sizeof nm) &&
           strcmp(nm, "Apple Prox") == 0, "apple preset label");
    CHECKN(m1_bt_adv_preset_name(200, nm, sizeof nm) == false,
           "unknown preset refused");
    CHECKN(m1_bt_adv_preset_table_count() == 3,
           "three fixed-byte presets (custom resolves from SD)");

    /* Unknown preset ids do not build. */
    CHECKN(m1_bt_adv_build_payload(200, "", NULL, 0, p, &n) == -1,
           "unknown preset refused");
    CHECKN(m1_bt_adv_build_payload(M1_BT_ADV_PRESET_CUSTOM, "", NULL, 0,
                                   p, &n) == -1,
           "custom without bytes refused");
    printf("presets done (%d fails so far)\n", fails);
}

static void test_validity(void)
{
    CHECKN(m1_bt_adv_payload_valid(EXP_APPLE, 31), "31-byte walk valid");
    CHECKN(m1_bt_adv_payload_valid((const uint8_t[]){0x02, 0x01, 0x06}, 3),
           "flags structure valid");
    CHECKN(m1_bt_adv_payload_valid(NULL, 3) == false, "NULL refused");
    CHECKN(m1_bt_adv_payload_valid(EXP_APPLE, 0) == false, "empty refused");
    /* 32 bytes of valid-looking structures: over the legacy cap. */
    {
        uint8_t over[32];
        memset(over, 0, sizeof over);
        over[0] = 30; over[31] = 0x02; over[30] = 0x01; /* len 30 + 2 = 32 payload bytes */
        CHECKN(m1_bt_adv_payload_valid(over, 32) == false,
               "32-byte payload over cap refused");
    }
    CHECKN(m1_bt_adv_payload_valid((const uint8_t[]){0x00, 0x01, 0x06}, 3) == false,
           "zero length byte refused");
    CHECKN(m1_bt_adv_payload_valid((const uint8_t[]){0x03, 0x01, 0x06}, 3) == false,
           "structure overrun refused");
    CHECKN(m1_bt_adv_payload_valid((const uint8_t[]){0x02, 0x01, 0x06, 0x01}, 4) == false,
           "trailing garbage refused");
    CHECKN(m1_bt_adv_payload_valid((const uint8_t[]){0x02, 0x01, 0x06, 0x02, 0x01}, 5) == false,
           "short second structure refused");
    CHECKN(m1_bt_adv_payload_valid(
               (const uint8_t[]){0x02, 0x01, 0x06, 0x02, 0x0A, 'M'}, 6),
           "two structures back-to-back valid");
    printf("validity done (%d fails so far)\n", fails);
}

static void test_name_beacon(void)
{
    uint8_t p[31];
    uint8_t n = 0;
    char name27[28];
    char name26[27];
    int i;

    /* Empty name: flags-only beacon. */
    CHECKN(m1_bt_adv_build_payload(M1_BT_ADV_PRESET_NAME, "", NULL, 0, p, &n) == 0
           && n == 3 && p[0] == 0x02 && p[1] == 0x01 && p[2] == 0x06,
           "empty name -> flags only");
    CHECKN(m1_bt_adv_build_payload(M1_BT_ADV_PRESET_NAME, NULL, NULL, 0, p, &n) == 0
           && n == 3, "NULL name treated as empty");

    /* Complete Local Name structure: [1+n][09][name]. */
    CHECKN(m1_bt_adv_build_payload(M1_BT_ADV_PRESET_NAME, "m1beacon", NULL, 0,
                                   p, &n) == 0
           && n == 13 && p[3] == 0x09 && p[4] == 0x09
           && memcmp(&p[5], "m1beacon", 8) == 0,
           "name AD structure [len][09][name]");

    /* 26 chars fills the cap exactly; 27 is refused, never truncated. */
    for (i = 0; i < 26; i++) { name26[i] = (char)('a' + (i % 26)); }
    name26[26] = '\0';
    for (i = 0; i < 27; i++) { name27[i] = (char)('a' + (i % 26)); }
    name27[27] = '\0';
    CHECKN(m1_bt_adv_build_payload(M1_BT_ADV_PRESET_NAME, name26, NULL, 0,
                                   p, &n) == 0 && n == 31,
           "26-char name fills the 31-byte cap");
    CHECKN(m1_bt_adv_build_payload(M1_BT_ADV_PRESET_NAME, name27, NULL, 0,
                                   p, &n) == -1,
           "27-char name refused");
    printf("name beacon done (%d fails so far)\n", fails);
}

static void test_custom(void)
{
    uint8_t c[31];
    uint8_t cn = 0;
    uint8_t p[31];
    uint8_t n = 0;
    char txt[160];
    size_t k;
    uint8_t i;

    /* Whitespace-tolerant hex text (an SD .adv file image): the exact
     * apple byte image rendered as MIXED separators -- spaces, a CR/LF
     * pair, a tab, and a trailing newline. */
    k = 0;
    for (i = 0; i < 31; i++)
    {
        const char *sep = (i == 2) ? "\r\n" : ((i == 17) ? "\t" : " ");
        k += (size_t)snprintf(&txt[k], sizeof txt - k, "%02X%s",
                              EXP_APPLE[i], sep);
    }
    CHECKN(k < sizeof txt, "mixed-separator text fits");
    CHECKN(m1_bt_adv_custom_parse(txt, c, &cn) == 0 && cn == 31,
           "whitespace-tolerant parse of the apple image");
    CHECKN(memcmp(c, EXP_APPLE, 31) == 0, "parsed bytes match the image");

    CHECKN(m1_bt_adv_custom_parse("020106", c, &cn) == 0 && cn == 3,
           "compact hex parses");
    CHECKN(m1_bt_adv_custom_parse("", c, &cn) == -1, "empty refused");
    CHECKN(m1_bt_adv_custom_parse("02010", c, &cn) == -1, "odd nibbles refused");
    CHECKN(m1_bt_adv_custom_parse("0201ZZ", c, &cn) == -1, "non-hex refused");
    CHECKN(m1_bt_adv_custom_parse("Address: AA:BB\r\n", c, &cn) == -1,
           "a .ble text record is not adv hex");

    /* 32 bytes of well-formed hex AD structure [1F][FF][30x00]: over
     * the 31-byte legacy cap -- the parser must refuse. */
    {
        uint8_t over[32];
        over[0] = 31;   /* length byte claiming 31 following bytes */
        over[1] = 0xFF;
        for (i = 2; i < 32; i++) { over[i] = 0x00; }
        k = 0;
        for (i = 0; i < 32; i++)
        {
            k += (size_t)snprintf(&txt[k], sizeof txt - k, "%02X ", over[i]);
        }
        CHECKN(m1_bt_adv_custom_parse(txt, c, &cn) == -1,
               "over-cap hex refused");
    }
    /* Structurally invalid but well-formed hex: refused by the parser. */
    CHECKN(m1_bt_adv_custom_parse("000106", c, &cn) == -1,
           "zero-length AD refused at parse");

    /* Custom payload flows through the builder, re-validated. */
    for (i = 0; i < 31; i++) { c[i] = EXP_APPLE[i]; }
    CHECKN(m1_bt_adv_build_payload(M1_BT_ADV_PRESET_CUSTOM, "x", c, 31,
                                   p, &n) == 0 && n == 31,
           "custom bytes build byte-exact");
    c[0] = 0x00;
    CHECKN(m1_bt_adv_build_payload(M1_BT_ADV_PRESET_CUSTOM, "x", c, 31,
                                   p, &n) == -1,
           "invalid custom bytes refused by the builder");

    /* .adv extension check. */
    CHECKN(m1_bt_adv_file_is_adv("a.adv"), "a.adv accepted");
    CHECKN(m1_bt_adv_file_is_adv("PAYLOAD_1.Adv"), "mixed case accepted");
    CHECKN(m1_bt_adv_file_is_adv("x.ble") == false, ".ble refused");
    CHECKN(m1_bt_adv_file_is_adv("adv") == false, "bare stem refused");
    CHECKN(m1_bt_adv_file_is_adv("a.advp") == false, "longer suffix refused");
    CHECKN(m1_bt_adv_file_is_adv(NULL) == false, "NULL refused");
    printf("custom done (%d fails so far)\n", fails);
}

static void test_intervals(void)
{
    static const uint16_t shipped[] = {100, 250, 500, 1000, 2000};
    static const int expect[] = {160, 400, 800, 1600, 3200};
    int i;

    for (i = 0; i < 5; i++)
    {
        CHECKN(m1_bt_adv_ms_to_units(shipped[i]) == expect[i],
               "shipped interval converts exactly");
    }
    /* BLE bounds: 0x20 units (20 ms) .. 0x4000 units (10.24 s). */
    CHECKN(m1_bt_adv_ms_to_units(20) == 32, "20 ms -> 0x20 units (spec min)");
    CHECKN(m1_bt_adv_ms_to_units(10240) == 16384, "10240 ms -> 0x4000 (spec max)");
    CHECKN(m1_bt_adv_ms_to_units(0) == -1, "0 ms refused");
    CHECKN(m1_bt_adv_ms_to_units(5) == -1, "5 ms below spec min refused");
    CHECKN(m1_bt_adv_ms_to_units(15) == -1, "15 ms below spec min refused");
    CHECKN(m1_bt_adv_ms_to_units(10245) == -1, "over spec max refused");
    CHECKN(m1_bt_adv_ms_to_units(20000) == -1, "20000 ms refused");
    /* Exactness: only multiples of 5 ms convert losslessly. */
    CHECKN(m1_bt_adv_ms_to_units(7) == -1, "7 ms not a unit multiple");
    CHECKN(m1_bt_adv_ms_to_units(1001) == -1, "1001 ms not a unit multiple");
    CHECKN(m1_bt_adv_ms_to_units(1) == -1, "1 ms refused");
    printf("intervals done (%d fails so far)\n", fails);
}

static void test_commands(void)
{
    char cmd[M1_BT_ADV_CMD_MAX];
    uint8_t p[31];
    uint8_t n = 0;
    int r;

    (void)m1_bt_adv_build_payload(M1_BT_ADV_PRESET_SWIFT, NULL, NULL, 0, p, &n);

    r = m1_bt_adv_build_cmd(cmd, sizeof cmd, 1000, p, n);
    CHECKN(r == 23, "swift command length 23 (verb+units+14 hex)");
    CHECKN(strcmp(cmd, "adv 1600 06FF0600030080") == 0,
           "exact swift command (units + uppercase hex, no CRLF)");

    /* Apple preset: worst case, 62 hex chars -- must fit CMD_MAX. */
    (void)m1_bt_adv_build_payload(M1_BT_ADV_PRESET_APPLE, NULL, NULL, 0, p, &n);
    r = m1_bt_adv_build_cmd(cmd, sizeof cmd, 2000, p, n);
    CHECKN(r > 0 && (size_t)r < sizeof cmd, "apple worst case fits CMD_MAX");
    CHECKN(strncmp(cmd, "adv 3200 1EFF4C000719", 21) == 0,
           "apple command prefix (units=3200)");
    CHECKN(strlen(cmd) == 9 + 62, "apple command is units + 62 hex chars");

    /* Stop command. */
    r = m1_bt_adv_build_stop_cmd(cmd, sizeof cmd);
    CHECKN(r == 8 && strcmp(cmd, "adv stop") == 0, "stop command exact");

    /* Refusals, never truncation. */
    CHECKN(m1_bt_adv_build_cmd(cmd, 8, 1000, p, n) == -1,
           "too-small dst refused");
    CHECKN(cmd[0] == '\0', "refused build leaves dst empty");
    CHECKN(m1_bt_adv_build_cmd(cmd, sizeof cmd, 7, p, n) == -1,
           "non-convertible interval refused");
    CHECKN(m1_bt_adv_build_cmd(cmd, sizeof cmd, 1000, p, 0) == -1,
           "empty payload refused");
    CHECKN(m1_bt_adv_build_cmd(cmd, sizeof cmd, 1000, NULL, n) == -1,
           "NULL payload refused");
    CHECKN(m1_bt_adv_build_cmd(NULL, 16, 1000, p, n) == -1, "NULL dst refused");
    CHECKN(m1_bt_adv_build_stop_cmd(cmd, 4) == -1, "stop too-small refused");
    printf("commands done (%d fails so far)\n", fails);
}

static void test_classifier(void)
{
    CHECKN(m1_bt_adv_classify_reply("ADV:OK\r\n>> ") == M1_BT_ADV_REPLY_OK,
           "clean OK");
    CHECKN(m1_bt_adv_classify_reply("adv 1600 06FF\r\nADV:OK\r\n>> ") == M1_BT_ADV_REPLY_OK,
           "OK after echo");
    CHECKN(m1_bt_adv_classify_reply("ADV:OK started\r\n>> ") == M1_BT_ADV_REPLY_OK,
           "OK with suffix");
    CHECKN(m1_bt_adv_classify_reply("ADV:FAIL busy\r\n>> ") == M1_BT_ADV_REPLY_FAIL,
           "FAIL");
    CHECKN(m1_bt_adv_classify_reply("echo: ADV:OK mid-line\r\n>> ") == M1_BT_ADV_REPLY_UNKNOWN,
           "mid-line marker never classifies (echo discipline)");
    CHECKN(m1_bt_adv_classify_reply("noise ADV:FAIL x\r\n>> ") == M1_BT_ADV_REPLY_UNKNOWN,
           "mid-line FAIL never classifies");
    CHECKN(m1_bt_adv_classify_reply("junk\r\nADV:FAIL y\r\nADV:OK\r\n>> ") == M1_BT_ADV_REPLY_FAIL,
           "first line-anchored marker wins (FAIL before later OK)");
    CHECKN(m1_bt_adv_classify_reply("ADV:OK a\nADV:FAIL b\r\n>> ") == M1_BT_ADV_REPLY_OK,
           "bare-\\n line anchor counts");
    CHECKN(m1_bt_adv_classify_reply(">> ") == M1_BT_ADV_REPLY_UNKNOWN,
           "prompt only is an honest unknown");
    CHECKN(m1_bt_adv_classify_reply("") == M1_BT_ADV_REPLY_UNKNOWN, "empty");
    CHECKN(m1_bt_adv_classify_reply(NULL) == M1_BT_ADV_REPLY_UNKNOWN, "NULL");
    printf("classifier done (%d fails so far)\n", fails);
}

int main(void)
{
    test_presets();
    test_validity();
    test_name_beacon();
    test_custom();
    test_intervals();
    test_commands();
    test_classifier();

    if (fails == 0)
    {
        printf("BT ADV CORE OK\n");
        return 0;
    }
    printf("BT ADV CORE FAILED (%d)\n", fails);
    return 1;
}
"""


def compiled_adv_core(cc):
    with tempfile.TemporaryDirectory() as td:
        tdir = Path(td)
        harness = tdir / "bt_adv_core_test.c"
        harness.write_text(ADV_HARNESS)
        exe = tdir / "bt_adv_core_test"
        cmd = [
            cc,
            "-std=c99",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic",
            "-I",
            str(ROOT / "m1_csrc"),
            str(ROOT / "m1_csrc" / "m1_bt_adv.c"),
            str(harness),
            "-o",
            str(exe),
        ]
        r = subprocess.run(cmd, capture_output=True, text=True)
        CHECK(r.returncode == 0, f"adv core compile failed: {r.stderr.strip()[:400]}")
        if r.returncode != 0:
            return
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        CHECK(
            r.returncode == 0 and "BT ADV CORE OK" in r.stdout,
            f"adv core run failed:\n{r.stdout}",
        )


# ---------------------------------------------------------------------------
# Compiled layer 2: the advertise gate battery (real gate core)
# ---------------------------------------------------------------------------

GATE_HARNESS = r"""
/* Generated by m1_csrc/test/m1_bt_adv_test.py -- do not edit. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "m1_esp_feature_gate.h"
#include "m1_esp_version.h"

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
    if (!sim_version_ok) { return false; }
    if (buf_size == 0u) { return false; }
    strncpy(buf, sim_reply, buf_size - 1u);
    buf[buf_size - 1u] = '\0';
    return true;
}

static void sim_set(bool ping_ok, bool version_ok, const char *reply)
{
    sim_ping_ok = ping_ok;
    sim_version_ok = version_ok;
    memset(sim_reply, 0, sizeof(sim_reply));
    if (reply != NULL) { strncpy(sim_reply, reply, sizeof(sim_reply) - 1u); }
}

int main(void)
{
    const uint8_t adv_min[4] = {1, 2, 0, 0};
    const uint8_t paired[4] = {1, 1, 0, 0};
    uint8_t m[4];
    uint8_t parsed[4];
    esp_feature_gate_result_t r;
    esp_fw_state_t s;

    /* The pinned minimum: the version the first adv-verb image must report. */
    CHECKN(esp_feature_minimum(ESP_FEATURE_BLE_ADVERTISE, m) &&
           memcmp(m, adv_min, 4) == 0, "advertise minimum is 1.2.0.0");

    /* The paired image version line (ESP32C6_FW_VERSION_LINE) parses
     * and is OLDER for advertise: configure-and-save, never advertise. */
    {
        const char *src = NULL;
        /* ESPFW_PAIRED_LINE injected via -D; see the python layer. */
#ifdef ESPFW_PAIRED_LINE
        src = ESPFW_PAIRED_LINE;
#endif
        CHECKN(src != NULL, "paired version line injected");
        if (src != NULL)
        {
            CHECKN(esp_fw_parse_version_reply(src, parsed),
                   "paired version line parses");
            CHECKN(memcmp(parsed, paired, 4) == 0,
                   "paired image is ESPFW 1.1.0.0");
        }
    }

    /* Live 1.1.0.0 (the shipped paired image, no adv verb): OLDER. */
    sim_set(true, true, "ESPFW 1.1.0.0\r\n>> ");
    s = esp_feature_gate_query(ESP_FEATURE_BLE_ADVERTISE, &r);
    CHECKN(s == ESP_FW_OLDER && !esp_feature_gate_allows(s),
           "live 1.1.0.0 is OLDER/refused for advertise");
    CHECKN(strcmp(esp_feature_gate_diag(&r), "ESP update required") == 0,
           "older diag is ESP update required");

    /* Stale DR2/DR3 record claiming 1.2.0.0 never flips that refusal. */
    m1cp_esp_version_set(adv_min);
    sim_set(true, true, "ESPFW 1.1.0.0\r\n>> ");
    s = esp_feature_gate_query(ESP_FEATURE_BLE_ADVERTISE, &r);
    CHECKN(s == ESP_FW_OLDER && !esp_feature_gate_allows(s),
           "stale cache never gates advertise");
    CHECKN(r.cache_valid && r.cache_disagrees,
           "stale record surfaced as disagreement");

    /* Dead console: ABSENT, refused. */
    sim_set(false, true, "");
    s = esp_feature_gate_query(ESP_FEATURE_BLE_ADVERTISE, &r);
    CHECKN(s == ESP_FW_ABSENT && !esp_feature_gate_allows(s),
           "dead console ABSENT/refused");
    CHECKN(strcmp(esp_feature_gate_diag(&r), "ESP not responding") == 0,
           "absent diag");

    /* Console alive, no version marker: ABSENT. */
    sim_set(true, true, "unknown command\r\n>> ");
    s = esp_feature_gate_query(ESP_FEATURE_BLE_ADVERTISE, &r);
    CHECKN(s == ESP_FW_ABSENT && !esp_feature_gate_allows(s),
           "no-marker ABSENT/refused");

    /* First adv-verb image: EQUIVALENT/allowed. */
    sim_set(true, true, "ESPFW 1.2.0.0\r\n>> ");
    s = esp_feature_gate_query(ESP_FEATURE_BLE_ADVERTISE, &r);
    CHECKN(s == ESP_FW_EQUIVALENT && esp_feature_gate_allows(s),
           "1.2.0.0 EQUIVALENT/allowed");

    /* Later image: NEWER/allowed. */
    sim_set(true, true, "ESPFW 1.3.0.0\r\n>> ");
    s = esp_feature_gate_query(ESP_FEATURE_BLE_ADVERTISE, &r);
    CHECKN(s == ESP_FW_NEWER && esp_feature_gate_allows(s),
           "1.3.0.0 NEWER/allowed");

    if (fails == 0)
    {
        printf("BT ADV GATE OK\n");
        return 0;
    }
    printf("BT ADV GATE FAILED (%d)\n", fails);
    return 1;
}
"""


def compiled_gate_layer(cc):
    esp_at_list = source(
        "Esp_spi_at/examples/at_spi_master/spi/stm32/main/esp_at_list.h"
    )
    m = re.search(r'#define\s+ESP32C6_FW_VERSION_LINE\s+"([^"]*)"', esp_at_list)
    CHECK(m is not None, "ESP32C6_FW_VERSION_LINE found in esp_at_list.h")
    paired_line = m.group(1) if m else ""

    with tempfile.TemporaryDirectory() as td:
        tdir = Path(td)
        harness = tdir / "bt_adv_gate_test.c"
        harness.write_text(GATE_HARNESS)
        exe = tdir / "bt_adv_gate_test"
        cmd = [
            cc,
            "-std=c99",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic",
            "-DM1_ESP_GATE_HOST_TEST",
            "-DM1CP_HOST_TEST",
            f'-DESPFW_PAIRED_LINE="{paired_line}"',
            "-I",
            str(ROOT / "m1_csrc"),
            str(ROOT / "m1_csrc" / "m1_esp_feature_gate.c"),
            str(ROOT / "m1_csrc" / "m1_esp_version.c"),
            str(harness),
            "-o",
            str(exe),
        ]
        r = subprocess.run(cmd, capture_output=True, text=True)
        CHECK(r.returncode == 0, f"gate compile failed: {r.stderr.strip()[:400]}")
        if r.returncode != 0:
            return
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        CHECK(
            r.returncode == 0 and "BT ADV GATE OK" in r.stdout,
            f"gate run failed:\n{r.stdout}",
        )


# ---------------------------------------------------------------------------
# Compiled layer 2b: the gate-refusal hint mapping (real helper text)
# ---------------------------------------------------------------------------

HINT_HARNESS_HEAD = r"""
/* Generated by m1_csrc/test/m1_bt_adv_test.py -- do not edit. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "m1_esp_feature_gate.h"

static int fails = 0;
#define FAILF(...) do { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } while (0)
#define CHECKN(c, m) do { if (!(c)) FAILF("%s", m); } while (0)

/* bt_adv_gate_hint is extracted VERBATIM from m1_csrc/m1_bt.c below. */

"""

HINT_HARNESS_TAIL = r"""

static void check(const char *name, esp_fw_state_t state,
                  esp_gate_absent_reason_t reason, const char *want)
{
    esp_feature_gate_result_t r;
    memset(&r, 0, sizeof(r));
    r.state = state;
    r.reason = reason;
    if (strcmp(bt_adv_gate_hint(&r), want) != 0)
    {
        FAILF("%s: hint \"%s\" want \"%s\"", name, bt_adv_gate_hint(&r), want);
    }
}

int main(void)
{
    /* OLDER is the only firmware-version problem -> firmware remedy. */
    check("older", ESP_FW_OLDER, ESP_GATE_REASON__COUNT, "Update ESP fw");

    /* Every ABSENT reason is a link/console problem -> link remedy. */
    check("absent no-console", ESP_FW_ABSENT, ESP_GATE_REASON_NO_CONSOLE,
          "Check ESP32");
    check("absent transport", ESP_FW_ABSENT, ESP_GATE_REASON_TRANSPORT,
          "Check ESP32");
    check("absent no-version", ESP_FW_ABSENT, ESP_GATE_REASON_NO_VERSION,
          "Check ESP32");
    check("absent bad-feature", ESP_FW_ABSENT, ESP_GATE_REASON_BAD_FEATURE,
          "Check ESP32");

    /* Allowed states (never consulted by the editor, but the helper is
     * total): link line, never the firmware line. */
    check("equivalent", ESP_FW_EQUIVALENT, ESP_GATE_REASON__COUNT,
          "Check ESP32");
    check("newer", ESP_FW_NEWER, ESP_GATE_REASON__COUNT, "Check ESP32");

    /* NULL detail must not crash and takes the link line. */
    CHECKN(strcmp(bt_adv_gate_hint(NULL), "Check ESP32") == 0,
           "NULL detail takes the link remedy");

    if (fails == 0)
    {
        printf("BT ADV HINT OK\n");
        return 0;
    }
    printf("BT ADV HINT FAILED (%d)\n", fails);
    return 1;
}
"""


def compiled_hint_layer(cc):
    bt = source("m1_csrc/m1_bt.c")
    helper = function(bt, "bt_adv_gate_hint")

    with tempfile.TemporaryDirectory() as td:
        tdir = Path(td)
        harness = tdir / "bt_adv_hint_test.c"
        harness.write_text(HINT_HARNESS_HEAD + helper + "\n" + HINT_HARNESS_TAIL)
        exe = tdir / "bt_adv_hint_test"
        cmd = [
            cc,
            "-std=c99",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic",
            "-I",
            str(ROOT / "m1_csrc"),
            str(harness),
            "-o",
            str(exe),
        ]
        r = subprocess.run(cmd, capture_output=True, text=True)
        CHECK(
            r.returncode == 0, f"hint helper compile failed: {r.stderr.strip()[:400]}"
        )
        if r.returncode != 0:
            return
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        CHECK(
            r.returncode == 0 and "BT ADV HINT OK" in r.stdout,
            f"hint mapping run failed:\n{r.stdout}",
        )


# ---------------------------------------------------------------------------
# Static layer: wiring seams in the live sources
# ---------------------------------------------------------------------------


def static_checks():
    bt = source("m1_csrc/m1_bt.c")
    bt_header = source("m1_csrc/m1_bt.h")
    menu = source("m1_csrc/m1_menu.c")
    gate_c = source("m1_csrc/m1_esp_feature_gate.c")

    # 1. The adv core is inside the scoped -Werror app-source list.
    cmake = source("cmake/m1_01/CMakeLists.txt")
    CHECK(
        "../../m1_csrc/m1_bt_adv.c" in cmake,
        "m1_bt_adv.c not in M1_APP_SOURCES (scoped -Werror gate)",
    )

    # 2. THE ordering seam: inside bt_adv_exec_start the live gate runs
    #    before the adv send. The pins are in terms of bt_cmd_process,
    #    the only console writer this function uses directly (a future
    #    write routed through a helper would not be counted) -- so the
    #    claim is scoped to the adv command, and the one deliberate
    #    pre-gate console interaction is pinned explicitly: the
    #    idempotent 1.1.0.0-safe session bring-up (bt_session_ensure).
    exec_fn = function(bt, "bt_adv_exec_start")
    CHECK(
        "esp_feature_gate_query(ESP_FEATURE_BLE_ADVERTISE" in exec_fn,
        "advertise transport queries the live gate",
    )
    CHECK(
        exec_fn.index("esp_feature_gate_query") < exec_fn.index("bt_cmd_process("),
        "gate decision precedes the adv send (bt_cmd_process)",
    )
    CHECK(
        exec_fn.index("esp_feature_gate_allows") < exec_fn.index("bt_cmd_process("),
        "allows() check precedes the adv send (bt_cmd_process)",
    )
    CHECK(
        exec_fn.count("bt_cmd_process(") == 1,
        "bt_adv_exec_start sends exactly one command (the adv send)",
    )
    CHECK(
        exec_fn.index("bt_session_ensure()") < exec_fn.index("esp_feature_gate_query"),
        "pre-gate console use is only the idempotent session bring-up",
    )

    # 3. Success is latched only after the console confirmed ADV:OK.
    #    The ordering pin alone survives an inverted comparison, so the
    #    operator polarity is pinned too (iter-1 Fix-now): the start
    #    path must REFUSE on non-OK (classify != OK -> error return,
    #    latch reachable only via OK).
    CHECK(
        exec_fn.index("m1_bt_adv_classify_reply")
        < exec_fn.index("g_bt_adv_active = 1U"),
        "g_bt_adv_active set only after the ADV:OK classification",
    )
    CHECK(
        re.search(
            r"m1_bt_adv_classify_reply\([^)]*\)\s*!=\s*M1_BT_ADV_REPLY_OK",
            exec_fn,
        )
        is not None,
        "start refuses on a non-OK reply (polarity: classify != OK)",
    )

    # 4. The stop verb is only ever sent while advertising is active, and
    #    the editor/menu funnel through bt_adv_exec_stop.
    stop_fn = function(bt, "bt_adv_exec_stop")
    CHECK(
        "if (!g_bt_adv_active) { return; }" in stop_fn,
        "bt_adv_exec_stop guards on the active flag",
    )
    CHECK(
        "m1_bt_adv_build_stop_cmd(" in stop_fn,
        "stop command is built by the shared builder (single grammar)",
    )
    menu_exit = function(bt, "menu_bluetooth_exit")
    CHECK("bt_adv_exec_stop();" in menu_exit, "Bluetooth exit stops advertising first")

    # 4b. Harden V1 (PR #93): the stop path CONSULTS the reply -- a
    #     non-OK or timed-out stop is never treated as a silent
    #     success. The classification gates a failure branch that
    #     surfaces the message screen; the latch still clears on every
    #     path (menu exit remains the backstop) and discovery resumes.
    CHECK(
        "bt_cmd_process(" in stop_fn and "m1_bt_adv_classify_reply(" in stop_fn,
        "stop path consults the console reply (send result + classifier)",
    )
    CHECK(
        stop_fn.index("m1_bt_adv_classify_reply")
        < stop_fn.index("g_bt_adv_active = 0U"),
        "stop classification precedes the latch clear",
    )
    CHECK(
        "M1_BT_ADV_REPLY_OK" in stop_fn,
        "only an ADV:OK-classified reply counts as stopped",
    )
    CHECK(
        re.search(
            r"m1_bt_adv_classify_reply\([^)]*\)\s*==\s*M1_BT_ADV_REPLY_OK",
            stop_fn,
        )
        is not None,
        "stop confirms via equality (polarity: only OK counts as stopped)",
    )
    CHECK(
        "bt_message_screen(" in stop_fn
        and '"Adv stop failed"' in stop_fn
        and '"Check ESP32"' in stop_fn,
        "stop failure message exists (Adv stop failed / Check ESP32)",
    )
    CHECK(
        "bt_session_resume_scan();" in stop_fn,
        "stop still resumes background discovery",
    )

    # 5. The adv verb never appears outside the gated transport /
    #    guarded stop: no other writer of the advertise command.
    adv_writers = re.findall(r'bt_cmd_process\(\s*"adv\b[^"]*"', bt)
    CHECK(
        adv_writers == [],
        f"no literal adv command bypasses the builders/gate (found {adv_writers})",
    )

    # 6. Menu wiring: 5 Bluetooth children, Adv Editor leaf last.
    menu_bt = re.search(r"S_M1_Menu_t menu_Bluetooth\s*=\s*\{(.*?)\};", menu, re.S)
    CHECK(menu_bt is not None, "menu_Bluetooth definition found")
    if menu_bt is not None:
        body = menu_bt.group(1)
        CHECK("5, 0," in body, "Bluetooth submenu declares 5 children")
        CHECK("&menu_Bluetooth_AdvEditor" in body, "Adv Editor is a Bluetooth child")
        CHECK(
            body.count("&menu_Bluetooth_") == 5,
            "exactly five Bluetooth children (within capacity 10)",
        )
    CHECK(
        '"Adv Editor", bluetooth_adv_editor' in menu,
        "Adv Editor leaf wired to bluetooth_adv_editor",
    )
    CHECK(
        "void bluetooth_adv_editor(void);" in bt_header,
        "bluetooth_adv_editor declared in m1_bt.h",
    )

    # 7. Persistence: the editor applies the three PR-1 setters and
    #    exits through the shared atomic save.
    editor = function(bt, "bluetooth_adv_editor")
    for setter in (
        "settings_set_bt_adv_name",
        "settings_set_bt_preset",
        "settings_set_bt_interval_ms",
    ):
        CHECK(setter in editor, f"editor applies {setter}")
    CHECK(
        "settings_exit_save(session_dirty);" in editor,
        "editor exits through settings_exit_save (one atomic save)",
    )

    # 8. No vendor SPI-AT advertise usage: the BT module must not lean
    #    on the vendor AT example as if it were the shipped console.
    CHECK(
        "AT+BLEADVDATAEX" not in bt,
        "BT module must not use the vendor AT+BLEADVDATAEX example",
    )
    CHECK(
        "ESP32C6_AT_REQ_ADVERTISE" not in bt,
        "BT module must not use the vendor SPI-AT advertise request",
    )

    # 9. The gate's advertise minimum is pinned in the table.
    CHECK(
        re.search(
            r"\{\s*1u,\s*2u,\s*0u,\s*0u\s*\}\s*/\*\s*ESP_FEATURE_BLE_ADVERTISE",
            gate_c,
        )
        is not None,
        "advertise minimum pinned to 1.2.0.0 in the gate table",
    )

    # 10. The aggregator runs this suite.
    runner = source("scripts/run_host_tests.sh")
    CHECK(
        "m1_csrc/test/m1_bt_adv_test.py" in runner,
        "suite not wired into scripts/run_host_tests.sh",
    )

    # 11. Stale-click discipline on the RUN row (iter-1 Fix-now): the
    #     button-queue reset must run on BOTH the start and the stop
    #     path -- after either multi-second console exchange and before
    #     the editor loop's single bt_wait_button reader -- so a click
    #     queued during a stop can never restart advertising.
    editor = function(bt, "bluetooth_adv_editor")
    run_m = re.search(r"default:\s*/\* BT_ADV_ROW_RUN \*/(.*?)break;", editor, re.S)
    CHECK(run_m is not None, "RUN row branch found in the editor")
    if run_m is not None:
        run_branch = run_m.group(1)
        CHECK("bt_adv_exec_stop();" in run_branch, "RUN branch stops when active")
        CHECK("bt_adv_exec_start(" in run_branch, "RUN branch starts when inactive")
        CHECK(
            run_branch.count("xQueueReset(button_events_q_hdl);") == 1,
            "RUN branch resets the button queue exactly once (hoisted)",
        )
        reset_at = run_branch.index("xQueueReset(button_events_q_hdl);")
        CHECK(
            reset_at > run_branch.index("bt_adv_exec_stop();")
            and reset_at > run_branch.index("bt_adv_exec_start("),
            "RUN reset follows BOTH paths (start and stop), not just start",
        )
    CHECK(
        editor.count("bt_wait_button()") == 1,
        "editor has exactly one button reader (the loop-top wait)",
    )
    CHECK(
        editor.count("xQueueReset(button_events_q_hdl);") == 4,
        "every sub-screen/exchange branch resets the queue "
        "(NAME, PRESET-custom, FILE, RUN)",
    )

    # 12. State-aware gate-refusal hint (iter-1 Advisory): the refusal
    #     site derives its second line from the gate result, not a
    #     hardcoded constant (the compiled layer pins the mapping).
    exec_fn = function(bt, "bt_adv_exec_start")
    CHECK(
        "bt_adv_gate_hint(&gate)" in exec_fn,
        "gate refusal second line is state-aware (bt_adv_gate_hint)",
    )
    CHECK(
        '"Update ESP fw"' not in exec_fn,
        "no hardcoded firmware hint at the refusal site",
    )
    CHECK(
        exec_fn.index("esp_feature_gate_diag(&gate)")
        < exec_fn.index("bt_adv_gate_hint(&gate)"),
        "primary diagnostic still comes from the gate itself",
    )

    # 13. Stored-value mapping helpers (iter-1 Advisory): on reload the
    #     editor maps the stored interval onto the table (documented
    #     contract, m1_bt_adv.h: a stored 0 -- the PR-1 default -- or
    #     any value not in the table lands on the 1000 ms default
    #     entry) and clamps a hand-edited preset id into the valid
    #     range. Both helpers are pure but sit in the HAL-bound TU
    #     outside the compiled harnesses, so their contracts get
    #     static pins here.
    iv_fn = function(bt, "bt_adv_interval_index")
    CHECK(
        re.search(r"==\s*ms.*M1_BT_ADV_INTERVAL_DEFAULT_MS", iv_fn, re.S) is not None,
        "interval mapping: exact table match first, then the "
        "documented 1000 ms default entry",
    )
    clamp_fn = function(bt, "bt_adv_preset_clamp")
    CHECK(
        re.search(
            r"M1_BT_ADV_PRESET__COUNT.*\?\s*preset\s*:\s*"
            r"\(uint8_t\)\s*M1_BT_ADV_PRESET_NAME",
            clamp_fn,
            re.S,
        )
        is not None,
        "preset clamp keeps in-range ids, clamps the rest to PRESET_NAME",
    )


def main():
    cc = host_cc()
    compiled_adv_core(cc)
    compiled_gate_layer(cc)
    compiled_hint_layer(cc)
    static_checks()

    print(f"\n{PASSED} passed, {FAILED} failed")
    if FAILED:
        sys_exit(1)
    print("ALL BT ADV EDITOR TESTS PASSED")


def sys_exit(code):
    import sys

    sys.exit(code)


if __name__ == "__main__":
    main()
