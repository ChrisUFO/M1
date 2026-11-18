/* See COPYING.txt for license details. */

/*
 *
 *  m1_esp_feature_gate.c
 *
 *  Live ESP32 firmware capability gate. The contract, the transport
 *  decision, and the per-feature policy live in m1_esp_feature_gate.h
 *  (read the header first). Implementation notes:
 *
 *  - The two console-touching operations (prompt ping, version round
 *    trip) sit behind esp_gate_console_ping_seam() /
 *    esp_gate_console_version_reply_seam() so the decision logic
 *    compiles identically on the target and in the host harness
 *    (-DM1_ESP_GATE_HOST_TEST provides fault-injectable, simulated
 *    versions of the two -- same seam technique as m1_rng.c).
 *  - Everything below the seam is pure: version compare, marker
 *    parsing, the minimum table, the state normalization, and the
 *    diagnostic strings. No dynamic allocation anywhere.
 *
 * M1 Project
 *
 */

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stddef.h>     /* NULL */
#include <string.h>     /* strstr, strncmp */
#include <ctype.h>      /* isdigit, isspace */
#include <stdbool.h>

#include "m1_esp_feature_gate.h"
#include "m1_esp_version.h"

/*************************** D E F I N E S ************************************/

/* Reply scratch for the version round trip. A version line plus the
 * echoed command and prompt is a few dozen bytes; if a chatty console
 * overflows this the marker simply is not found -> ABSENT (an honest
 * unknown, not a fault). */
#define ESP_GATE_REPLY_BUF  128u

/*************************** B A C K E N D ************************************/

#ifdef M1_ESP_GATE_HOST_TEST
/* Host harness provides both console operations (simulation/fault
 * injection). Declared here so the target build never sees them. */
bool esp_gate_console_ping_seam(uint32_t timeout_ms);
bool esp_gate_console_version_reply_seam(char *buf, uint16_t buf_size,
                                         uint32_t timeout_ms);
#else /* target build: the real ESP32 UART console session */
#include "m1_esp_uart_transport.h"

/* Liveness check -- the exact pattern every shipped ESP32 tool uses
 * (m1_bt.c / m1_wifi.c / m1_capture_link.c): a bare "\r\n" and a
 * bounded wait for the ">> " prompt. Read-only for the console. */
static bool esp_gate_console_ping_seam(uint32_t timeout_ms)
{
    static const char ping[] = "\r\n";
    char scratch[16];

    if (esp32_uart_write((const uint8_t *)ping,
                         (uint16_t)(sizeof(ping) - 1u), 100u) == 0u)
    {
        return false;
    }
    return (esp32_uart_read_complete_prompt(scratch,
                                            (uint16_t)sizeof(scratch),
                                            timeout_ms, ">> ") > 0u);
}

/* Live version round trip: send the read-only `version` console verb,
 * capture the reply up to the returning ">> " prompt. */
static bool esp_gate_console_version_reply_seam(char *buf, uint16_t buf_size,
                                                uint32_t timeout_ms)
{
    static const char cmd[] = ESP_GATE_VERSION_CMD;

    if (esp32_uart_write((const uint8_t *)cmd,
                         (uint16_t)(sizeof(cmd) - 1u), 100u) == 0u)
    {
        return false;
    }
    return (esp32_uart_read_complete_prompt(buf, buf_size,
                                            timeout_ms, ">> ") > 0u);
}
#endif /* M1_ESP_GATE_HOST_TEST */

/*************************** T A B L E S **************************************/

/* Per-feature minimum ESP32 firmware versions (see the header's
 * policy note: PR-11/PR-12 own these entries once their paired ESP32
 * handlers land). */
static const uint8_t s_feature_min[ESP_FEATURE__COUNT][4] =
{
    { 1u, 0u, 0u, 0u },   /* ESP_FEATURE_WIFI_JOIN     */
    { 1u, 0u, 0u, 0u }    /* ESP_FEATURE_BLE_ADVERTISE */
};

/* User-facing diagnostics, one per state (ABSENT split by reason).
 * Kept short -- these go on a 128x64 LCD next to the feature name. */
static const char ESP_GATE_DIAG_NO_CONSOLE[]  = "ESP not responding";
static const char ESP_GATE_DIAG_TRANSPORT[]   = "ESP link failed";
static const char ESP_GATE_DIAG_NO_VERSION[]  = "ESP fw unknown";
static const char ESP_GATE_DIAG_BAD_FEATURE[] = "ESP gate misuse";
static const char ESP_GATE_DIAG_OLDER[]       = "ESP update required";
static const char ESP_GATE_DIAG_EQUIVALENT[]  = "ESP fw supported";
static const char ESP_GATE_DIAG_NEWER[]       = "ESP fw newer";

/*************************** P U R E   C O R E *******************************/

int esp_fw_version_compare(const uint8_t a[4], const uint8_t b[4])
{
    int i;

    if ((a == NULL) || (b == NULL))
    {
        return 0;   /* defensive: callers pass real versions */
    }

    for (i = 0; i < 4; i++)
    {
        if (a[i] < b[i]) { return -1; }
        if (a[i] > b[i]) { return 1; }
    }
    return 0;
}

/* Parse one decimal field (1..3 digits, value 0..255) starting at *p.
 * Advances *p past the digits. Returns -1 when nothing valid parsed,
 * else the value. */
static int parse_field(const char **p)
{
    int value = 0;
    int digits = 0;

    while (isdigit((unsigned char)(*p)[0]) && (digits < 3))
    {
        value = (value * 10) + ((*p)[0] - '0');
        (*p)++;
        digits++;
    }

    if (digits == 0)
    {
        return -1;
    }
    return value;
}

/* Try to parse "ESPFW <a>.<b>.<c>.<d>" exactly at marker position m
 * (m points at the first character of the marker). Returns true and
 * fills out on success; *next is always set past this marker text
 * (and past the parsed fields on success), so the caller can keep
 * scanning later occurrences on failure. */
static bool parse_at_marker(const char *m, const char **next, uint8_t out[4])
{
    const char *p = m + (sizeof(ESP_GATE_VERSION_MARKER) - 1u);
    int fields[4];
    int i;

    while (isspace((unsigned char)p[0]))
    {
        p++;
    }

    for (i = 0; i < 4; i++)
    {
        fields[i] = parse_field(&p);
        if ((fields[i] < 0) || (fields[i] > 255))
        {
            *next = p;
            return false;
        }
        if (i < 3)
        {
            if (p[0] != '.')
            {
                *next = p;
                return false;
            }
            p++;
        }
    }

    out[0] = (uint8_t)fields[0];
    out[1] = (uint8_t)fields[1];
    out[2] = (uint8_t)fields[2];
    out[3] = (uint8_t)fields[3];
    *next = p;
    return true;
}

bool esp_fw_parse_version_reply(const char *reply, uint8_t out[4])
{
    const char *scan;

    if ((reply == NULL) || (out == NULL))
    {
        return false;
    }

    /* First recognizable marker wins; a garbled occurrence does not
     * stop a later well-formed one from parsing. parse_at_marker()
     * always advances *next past the marker text, so the loop
     * strictly advances. */
    scan = strstr(reply, ESP_GATE_VERSION_MARKER);
    while (scan != NULL)
    {
        const char *next = scan;
        if (parse_at_marker(scan, &next, out))
        {
            return true;
        }
        scan = strstr(next, ESP_GATE_VERSION_MARKER);
    }
    return false;
}

/*************************** P U B L I C **************************************/

bool esp_feature_minimum(esp_feature_t feature, uint8_t out[4])
{
    /* Casts keep the bounds check warning-clean: an enum with only
     * non-negative values may have an unsigned underlying type. */
    if (((int)feature < 0) || ((int)feature >= (int)ESP_FEATURE__COUNT) ||
        (out == NULL))
    {
        return false;
    }
    out[0] = s_feature_min[feature][0];
    out[1] = s_feature_min[feature][1];
    out[2] = s_feature_min[feature][2];
    out[3] = s_feature_min[feature][3];
    return true;
}

esp_fw_state_t esp_feature_gate_query(esp_feature_t feature,
                                      esp_feature_gate_result_t *detail_out)
{
    esp_feature_gate_result_t local;
    uint8_t minimum[4];
    char reply[ESP_GATE_REPLY_BUF];
    int cmp;

    memset(&local, 0, sizeof(local));
    local.state = ESP_FW_ABSENT;
    local.reason = ESP_GATE_REASON_NO_CONSOLE;

    /* The cache is diagnostic-only. Read it up front so every return
     * path below reports the persisted record alongside the live
     * answer (this is what makes a stale DR2/DR3 record visible),
     * while never letting it influence the decision. */
    local.cache_valid = m1cp_esp_version_get(local.cache_version);

    if (((int)feature < 0) || ((int)feature >= (int)ESP_FEATURE__COUNT))
    {
        local.reason = ESP_GATE_REASON_BAD_FEATURE;
        if (detail_out != NULL) { *detail_out = local; }
        return ESP_FW_ABSENT;
    }
    minimum[0] = s_feature_min[feature][0];
    minimum[1] = s_feature_min[feature][1];
    minimum[2] = s_feature_min[feature][2];
    minimum[3] = s_feature_min[feature][3];

    /* Live handshake, step 1: is the console there at all? */
    if (!esp_gate_console_ping_seam(ESP_GATE_PING_TIMEOUT_MS))
    {
        local.reason = ESP_GATE_REASON_NO_CONSOLE;
        if (detail_out != NULL) { *detail_out = local; }
        return ESP_FW_ABSENT;
    }

    /* Step 2: the version round trip itself. */
    memset(reply, 0, sizeof(reply));
    if (!esp_gate_console_version_reply_seam(reply, (uint16_t)sizeof(reply),
                                             ESP_GATE_VERSION_TIMEOUT_MS))
    {
        local.reason = ESP_GATE_REASON_TRANSPORT;
        if (detail_out != NULL) { *detail_out = local; }
        return ESP_FW_ABSENT;
    }

    /* Step 3: normalize the reply. A console that does not answer
     * `version` with the ESPFW marker is an honest unknown, never a
     * permission. */
    if (!esp_fw_parse_version_reply(reply, local.live_version))
    {
        local.reason = ESP_GATE_REASON_NO_VERSION;
        if (detail_out != NULL) { *detail_out = local; }
        return ESP_FW_ABSENT;
    }

    local.live_valid = true;
    if (local.cache_valid)
    {
        local.cache_disagrees =
            (esp_fw_version_compare(local.cache_version,
                                    local.live_version) != 0);
    }

    cmp = esp_fw_version_compare(local.live_version, minimum);
    if (cmp < 0)       { local.state = ESP_FW_OLDER; }
    else if (cmp == 0) { local.state = ESP_FW_EQUIVALENT; }
    else               { local.state = ESP_FW_NEWER; }

    if (detail_out != NULL) { *detail_out = local; }
    return local.state;
}

bool esp_feature_gate_allows(esp_fw_state_t state)
{
    return (state == ESP_FW_EQUIVALENT) || (state == ESP_FW_NEWER);
}

const char *esp_feature_gate_diag(const esp_feature_gate_result_t *detail)
{
    if (detail == NULL)
    {
        return ESP_GATE_DIAG_NO_CONSOLE;   /* generic ABSENT text */
    }

    switch (detail->state)
    {
        case ESP_FW_ABSENT:
            switch (detail->reason)
            {
                case ESP_GATE_REASON_NO_CONSOLE:  return ESP_GATE_DIAG_NO_CONSOLE;
                case ESP_GATE_REASON_TRANSPORT:   return ESP_GATE_DIAG_TRANSPORT;
                case ESP_GATE_REASON_NO_VERSION:  return ESP_GATE_DIAG_NO_VERSION;
                case ESP_GATE_REASON_BAD_FEATURE: return ESP_GATE_DIAG_BAD_FEATURE;
                default: break;
            }
            return ESP_GATE_DIAG_NO_CONSOLE;

        case ESP_FW_OLDER:      return ESP_GATE_DIAG_OLDER;
        case ESP_FW_EQUIVALENT: return ESP_GATE_DIAG_EQUIVALENT;
        case ESP_FW_NEWER:      return ESP_GATE_DIAG_NEWER;
        default: break;
    }
    return ESP_GATE_DIAG_NO_CONSOLE;
}
