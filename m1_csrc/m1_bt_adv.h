/* See COPYING.txt for license details. */

/*
 * m1_bt_adv.h
 *
 * BLE advertisement configuration core (PR-12 of the Merge 3 series,
 * PLAN section 15; issue #11): the payload preset table, the legacy
 * 31-byte payload validator, the ms -> 0.625 ms-unit interval
 * conversion, and the advertise console-command builders used by the
 * Bluetooth -> Adv Editor screen (m1_bt.c).
 *
 * DESIGN CONTRACT
 * ---------------
 * 1. PURE CORE. This module is deliberately free of HAL/FreeRTOS/
 *    FatFs includes so the whole translation unit compiles in the host
 *    test harness unchanged (same technique as m1_esp_feature_gate.c).
 *    Everything that touches hardware (SD payload files, the ESP32
 *    console session) lives in m1_bt.c and calls in here.
 * 2. PRESET BYTES ARE PROTOCOL FACTS. Each preset encodes the
 *    Bluetooth Core Specification AD-structure format plus
 *    publicly-documented manufacturer identifiers (Apple 0x004C
 *    continuity, Samsung 0x0075 Easysetup, Microsoft 0x0006 Swift
 *    Pair). No application code was copied from any GPL
 *    implementation: the byte tables below were constructed from the
 *    format specifications and are pinned byte-for-byte by the host
 *    suite (see m1_csrc/test/m1_bt_adv_test.py for the provenance
 *    notes per preset).
 * 3. ONE VALIDITY RULE. A payload is usable iff it is a sequence of
 *    AD structures -- [length][type][data...] with length counting
 *    type+data -- that ends exactly on a structure boundary and fits
 *    the 31-byte legacy advertising cap. The table's own presets must
 *    pass m1_bt_adv_payload_valid() (the Swift Pair skeleton's length
 *    byte is the standard count, so the anchor literal from the plan
 *    needed its length byte corrected -- see the table comment).
 * 4. THE WIRE CARRIES BLE-NATIVE UNITS. The console command is
 *     adv <interval_units> <PAYLOAD-HEX>      (start advertising)
 *     adv stop                                 (stop advertising)
 *    where <interval_units> is the advertising interval in the BLE
 *    standard 0.625 ms unit (the native unit of
 *    esp_ble_gap_set_adv_params adv_int_min/adv_int_max on the ESP32
 *    side), converted on the STM32 from the user's millisecond
 *    preset. PLAN section 15 named the field "<interval_ms>"; the
 *    shipped grammar carries units instead so the external console
 *    handler needs no lossy arithmetic -- a deliberate, documented
 *    deviation. m1_bt_adv_ms_to_units() is the conversion and the
 *    single source of its exactness/range policy (multiple of 5 ms,
 *    20 ms..10240 ms -> 0x20..0x4000 units).
 * 5. COMMANDS ARE BUILT, NEVER GLUED. m1_bt_adv_build_cmd() /
 *    m1_bt_adv_build_stop_cmd() emit the verb without CRLF (the BT
 *    plumbing bt_cmd_process() appends CRLF and reads to the ">> "
 *    prompt). Builders refuse (return -1) on any out-of-bound input
 *    rather than truncate: a payload that cannot be represented is
 *    never sent half-formed.
 * 6. NO SENDING HERE. This module never touches the console. The
 *    gated transport (live feature gate BEFORE any write) lives in
 *    m1_bt.c's bt_adv_exec() and is pinned by the host suite's
 *    static seam.
 *
 * M1 Project
 *
 */

#ifndef M1_BT_ADV_H_
#define M1_BT_ADV_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Legacy advertising payload cap (Bluetooth Core Spec: 31 bytes). */
#define M1_BT_ADV_MAX_PAYLOAD   31u

/* Complete Local Name AD that still fits after the flags structure:
 * 3 (flags) + 2 (length+type) + name <= 31. The virtual keyboard caps
 * entry at 20 characters, so the editor cannot even reach this bound;
 * the builder enforces it independently of the input path. */
#define M1_BT_ADV_NAME_MAX      26u

/* Longest console command the builders can emit, without CRLF:
 * "adv " + 5 digits (units <= 16384) + ' ' + 62 hex chars + NUL. */
#define M1_BT_ADV_CMD_MAX       80u

/* Payload preset ids -- the values persisted as the settings store's
 * bt_preset field. Order is part of the persisted contract; append
 * only. */
typedef enum
{
    M1_BT_ADV_PRESET_NAME    = 0,  /* flags + Complete Local Name          */
    M1_BT_ADV_PRESET_APPLE   = 1,  /* Apple proximity (continuity 0x07)    */
    M1_BT_ADV_PRESET_SAMSUNG = 2,  /* Samsung Easysetup                    */
    M1_BT_ADV_PRESET_SWIFT   = 3,  /* Microsoft Swift Pair                 */
    M1_BT_ADV_PRESET_CUSTOM  = 4,  /* raw hex payload loaded from SD       */
    M1_BT_ADV_PRESET__COUNT
} m1_bt_adv_preset_id_t;

/* Fixed advertising-interval presets (milliseconds) offered by the
 * editor; every entry is an exact multiple of 5 ms so the 0.625 ms
 * unit conversion is lossless. The persisted bt_interval_ms holds one
 * of these (0 -- the store default -- is treated by the editor as the
 * 1000 ms entry). */
#define M1_BT_ADV_INTERVALS_MS { 100u, 250u, 500u, 1000u, 2000u }
#define M1_BT_ADV_INTERVAL_COUNT 5u
#define M1_BT_ADV_INTERVAL_DEFAULT_MS 1000u

/* Reply classification for an advertise command (line-anchored
 * markers, exactly the Wi-Fi join classifier's discipline). */
typedef enum
{
    M1_BT_ADV_REPLY_FAIL    = -1,  /* reply line beginning ADV:FAIL  */
    M1_BT_ADV_REPLY_UNKNOWN = 0,   /* no recognizable marker (timeout) */
    M1_BT_ADV_REPLY_OK      = 1    /* reply line beginning ADV:OK    */
} m1_bt_adv_reply_t;

/* ---- preset table -------------------------------------------------------- */

/* Number of fixed-byte table presets (APPLE/SAMSUNG/SWIFT): excludes
 * CUSTOM (bytes come from SD) and NAME (bytes derive from the
 * configured name). */
uint8_t m1_bt_adv_preset_table_count(void);

/* Copy preset id's display name (static string) into out. Returns true
 * for every editor id -- NAME, the table presets, and CUSTOM (shown as
 * "Custom (SD)") -- and false only for an unknown id or a NULL/out_size
 * 0 buffer. */
bool m1_bt_adv_preset_name(uint8_t preset, char *out, uint8_t out_size);

/* ---- payload construction / validation ----------------------------------- */

/* Bluetooth AD-structure validity: len > 0, every structure's length
 * byte counts its type + data, the walk ends exactly at len, and the
 * total fits the 31-byte legacy cap. */
bool m1_bt_adv_payload_valid(const uint8_t *bytes, uint8_t len);

/* Build the final advertising payload for a preset selection.
 *   NAME:    02 01 06 + [2+n][09][name]  (empty name -> flags only)
 *   APPLE / SAMSUNG / SWIFT: the fixed table bytes; name is NOT
 *            appended (mimic presets ship byte-exact; the name field
 *            applies to the NAME preset -- the editor documents this)
 *   CUSTOM:  the caller-provided bytes (already SD-loaded), re-validated
 * Returns 0 and sets *out_len on success; -1 for an unknown preset, a
 * NULL/invalid custom payload, or a name that cannot fit. */
int m1_bt_adv_build_payload(uint8_t preset, const char *name,
                            const uint8_t *custom, uint8_t custom_len,
                            uint8_t *out, uint8_t *out_len);

/* ---- interval conversion -------------------------------------------------- */

/* Convert milliseconds to BLE 0.625 ms advertising units. Returns the
 * unit count (20 ms..10240 ms -> 0x20..0x4000) or -1 when ms is not an
 * exact multiple of 5 ms or is outside the representable BLE range. */
int m1_bt_adv_ms_to_units(uint32_t ms);

/* ---- console command construction (no CRLF; bt_cmd_process appends) ------ */

/* Build "adv <units> <PAYLOAD-HEX>" into dst. interval_ms is converted
 * through m1_bt_adv_ms_to_units(); payload must be valid (checked).
 * Returns the string length, or -1 (dst too small, invalid interval,
 * invalid payload). dst is NUL-terminated and fully rewritten. */
int m1_bt_adv_build_cmd(char *dst, size_t dst_size, uint32_t interval_ms,
                        const uint8_t *payload, uint8_t len);

/* Build "adv stop" into dst. Returns the string length or -1. */
int m1_bt_adv_build_stop_cmd(char *dst, size_t dst_size);

/* ---- reply classification -------------------------------------------------- */

/* Line-anchored classification of an advertise reply: a line beginning
 * "ADV:OK" wins as OK, a line beginning "ADV:FAIL" as FAIL; anything
 * else (including a marker substring mid-line, e.g. inside the echoed
 * command) is UNKNOWN -- an honest timeout, never a false success. */
m1_bt_adv_reply_t m1_bt_adv_classify_reply(const char *resp);

/* ---- custom payload text --------------------------------------------------- */

/* Parse whitespace-tolerant hex text (an SD .adv file's content) into
 * raw payload bytes and validate the result. Accepts 1..31 bytes;
 * embedded spaces/newlines/CR are skipped. Returns 0 and sets *out_len,
 * or -1 on odd nibbles, non-hex characters, or an invalid payload. */
int m1_bt_adv_custom_parse(const char *hex_text, uint8_t *out, uint8_t *out_len);

/* True iff filename carries the .adv extension used for SD advertise
 * payload files under 0:/ble (case-insensitive). */
bool m1_bt_adv_file_is_adv(const char *filename);

#ifdef __cplusplus
}
#endif

#endif /* M1_BT_ADV_H_ */
