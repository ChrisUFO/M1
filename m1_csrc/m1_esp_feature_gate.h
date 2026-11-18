/* See COPYING.txt for license details. */

/*
 *
 *  m1_esp_feature_gate.h
 *
 *  Live ESP32 firmware capability gate (PR-10 of the Merge 3 series,
 *  PLAN section 13). One shared mechanism for "may this STM32 feature
 *  use its ESP32-side handler?" -- consumed by the Wi-Fi join flow
 *  (PR-11) and the Bluetooth advertisement editor (PR-12).
 *
 *  CONTRACT
 *  --------
 *  1. LIVE, NEVER THE CACHE. Every gate decision is derived from a
 *     version the ESP32 reports over the console transport RIGHT NOW
 *     (see the transport note below). The persisted record from
 *     m1cp_esp_version_get() (RTC/TAMP backup registers DR2/DR3, set
 *     by the STM32 after it flashes the ESP32) is VBAT-dependent and
 *     describes what the STM32 last installed, not what is running:
 *     the ESP32 can be externally re-flashed or swapped while the
 *     backup-domain record survives, so the record may claim a newer
 *     firmware than the live console reports. The cache is therefore
 *     read for DIAGNOSTICS ONLY (reported in the result struct as
 *     cache_valid/cache_version/cache_disagrees so a stale record is
 *     visible) and never influences the returned state. This module
 *     NEVER writes the cache (no m1cp_esp_version_set): the DR2/DR3
 *     record's meaning ("what the STM32 installed") feeds the Web
 *     Manager's flash-skip decision and must not be redefined here.
 *  2. FOUR STATES. The live version is compared against the feature's
 *     minimum version (major, minor, patch, rc -- each field compared
 *     numerically in that order; rc is a plain fourth number per this
 *     project's versioning, NOT pre-release-ordered):
 *       ABSENT     no live answer (console dead, query failed, or the
 *                  reply carried no recognizable version)
 *       OLDER      live < feature minimum
 *       EQUIVALENT live == feature minimum
 *       NEWER      live > feature minimum
 *     Only EQUIVALENT and NEWER may execute a gated feature:
 *     esp_feature_gate_allows() enforces exactly that. ABSENT and
 *     OLDER must show the diagnostic and refuse -- a silent failure
 *     is worse than an honest unknown.
 *  3. TRANSPORT (what the live query actually rides): the ESP32 UART
 *     console session every shipped Wi-Fi/BT/capture tool uses --
 *     a "\r\n" ping confirming the ">> " prompt, then the read-only
 *     console verb `version` (ESP_GATE_VERSION_CMD), reply read back
 *     to the same ">> " prompt. The ESP32 firmware is maintained
 *     outside this repository; its console is expected to answer
 *     `version` with a line "ESPFW <major>.<minor>.<patch>.<rc>"
 *     (ESP_GATE_VERSION_MARKER). Until an image that does so is
 *     flashed, the parser finds no marker and the gate honestly
 *     reports ABSENT -- which is the correct refusal, not a fault.
 *     The vendor SPI-AT control enums (CTRL_REQ_GET_FW_VERSION in
 *     Esp_spi_at/.../ctrl_api.h) have no shipped requester and are
 *     deliberately NOT used: that would be a parallel protocol.
 *  4. CALLING CONTEXT: task context only, with the ESP32 session
 *     owned by the caller (the same contract as esp32_uart_write /
 *     esp32_uart_read_complete_prompt -- this module does not
 *     acquire ownership, reset the radio, or change baud/framing).
 *     Each query costs two bounded console round trips
 *     (ESP_GATE_PING_TIMEOUT_MS / ESP_GATE_VERSION_TIMEOUT_MS) --
 *     call it at connect time, not per button press. No dynamic
 *     allocation. No writes to flash metadata or the backup domain.
 *
 *  M1 Project
 *
 */

#ifndef M1_ESP_FEATURE_GATE_H_
#define M1_ESP_FEATURE_GATE_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Console verb and reply marker of the live version query -- the
 * shared contract with the external ESP32 console firmware. Changing
 * either is an inter-processor interface change (see
 * docs/architecture.md). */
#define ESP_GATE_VERSION_CMD       "version\r\n"
#define ESP_GATE_VERSION_MARKER    "ESPFW"

/* Bounded round-trip budgets (milliseconds) for the live query. */
#define ESP_GATE_PING_TIMEOUT_MS     800u
#define ESP_GATE_VERSION_TIMEOUT_MS 1500u

/* Relation of the LIVE ESP32 firmware version to a feature minimum. */
typedef enum
{
    ESP_FW_ABSENT     = 0,  /* no live answer -- refuse                  */
    ESP_FW_OLDER      = 1,  /* live firmware below the feature minimum   */
    ESP_FW_EQUIVALENT = 2,  /* live firmware exactly at the minimum      */
    ESP_FW_NEWER      = 3   /* live firmware above the minimum           */
} esp_fw_state_t;

/* Why a query came back ABSENT (diagnostic granularity only; every
 * reason refuses identically). */
typedef enum
{
    ESP_GATE_REASON_NO_CONSOLE  = 0,  /* prompt ping failed: no ESP32 / dead console */
    ESP_GATE_REASON_TRANSPORT   = 1,  /* version round trip failed after ping ok     */
    ESP_GATE_REASON_NO_VERSION  = 2,  /* console alive but no recognizable version   */
    ESP_GATE_REASON_BAD_FEATURE = 3,  /* caller asked for an unknown feature         */
    ESP_GATE_REASON__COUNT
} esp_gate_absent_reason_t;

/* Gated ESP32-dependent features. The minimums below say which ESP32
 * firmware first shipped the matching handler; both start at a
 * version no shipped ESP32 image reports, so the gate refuses until
 * the paired firmware lands (PR-11 owns the join-handler minimum,
 * PR-12 the advertise-command minimum -- bump them there, not here).*/
typedef enum
{
    ESP_FEATURE_WIFI_JOIN     = 0,  /* PR-11: AP join handler on the console firmware */
    ESP_FEATURE_BLE_ADVERTISE = 1,  /* PR-12: BLE advertise console command            */
    ESP_FEATURE__COUNT
} esp_feature_t;

/* Full query outcome (detail_out is optional; NULL is fine -- the
 * return value alone is the gate decision). The cache_* fields are
 * diagnostics about the persisted DR2/DR3 record and NEVER gate. */
typedef struct
{
    esp_fw_state_t          state;          /* the decision                       */
    esp_gate_absent_reason_t reason;        /* valid when state == ESP_FW_ABSENT  */
    bool                    live_valid;     /* a live version was obtained       */
    uint8_t                 live_version[4];/* major, minor, patch, rc (live)    */
    bool                    cache_valid;    /* DR2/DR3 record readable (diagnostic) */
    uint8_t                 cache_version[4];/* major, minor, patch, rc (record)  */
    bool                    cache_disagrees;/* record valid AND != live version  */
} esp_feature_gate_result_t;

/* Compare two (major, minor, patch, rc) versions field-by-field,
 * numerically, most significant field first. Returns -1 / 0 / +1. */
int esp_fw_version_compare(const uint8_t a[4], const uint8_t b[4]);

/* Scan a console reply for the first "ESPFW <a>.<b>.<c>.<d>" marker
 * line and parse it into out (each field decimal, 0..255; arbitrary
 * trailing non-digit text after the fourth field is tolerated).
 * Returns true iff all four fields parsed in range. */
bool esp_fw_parse_version_reply(const char *reply, uint8_t out[4]);

/* Copy the feature's minimum ESP32 firmware version into out.
 * Returns false for an unknown feature. */
bool esp_feature_minimum(esp_feature_t feature, uint8_t out[4]);

/* THE gate. Performs the live handshake (two bounded console round
 * trips -- task context, caller owns the ESP32 session) and returns
 * the live version's relation to the feature's minimum. If
 * detail_out is non-NULL it receives the full outcome including the
 * diagnostic-only cache comparison. */
esp_fw_state_t esp_feature_gate_query(esp_feature_t feature,
                                      esp_feature_gate_result_t *detail_out);

/* The execution policy in one place: exactly EQUIVALENT and NEWER
 * may run a gated feature. ABSENT and OLDER must not. */
bool esp_feature_gate_allows(esp_fw_state_t state);

/* User-facing diagnostic text for an outcome (static string; never
 * NULL). ABSENT distinguishes its reasons; pass NULL for the generic
 * per-state text of a non-ABSENT state. */
const char *esp_feature_gate_diag(const esp_feature_gate_result_t *detail);

#ifdef __cplusplus
}
#endif

#endif /* M1_ESP_FEATURE_GATE_H_ */
