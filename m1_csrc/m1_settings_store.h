/* See COPYING.txt for license details. */

/*
 * m1_settings_store.h
 *
 * Durable user-settings store on the SD card (file 0:/System/settings.cfg).
 *
 * M1 Project
 */

#ifndef M1_SETTINGS_STORE_H_
#define M1_SETTINGS_STORE_H_

#include <stdint.h>
#include <stdbool.h>

/*
 * Ownership contract (binding for every consumer):
 *
 *  - READS: any task, via settings_get(). Individual fields are aligned
 *    scalars/char arrays, so a read races at most with the single writer
 *    (below) and never observes a half-written pointer. Boot-time readers
 *    (m1_system_init_task) run strictly before any writer can exist.
 *  - WRITES: the UI task ONLY, through the setters below and
 *    settings_save()/settings_reset_defaults(). No other task may mutate
 *    the store. settings_init() itself runs once from
 *    m1_system_init_task() before m1_tasks_init().
 *
 * Persistence format (see docs/architecture.md "Storage model"):
 *  - line 1: "#ver=1"
 *  - one "key=value" line per field, CRLF endings
 *  - final line: "crc=XXXXXXXX" (8 uppercase hex digits) carrying the
 *    CRC-32C (Castagnoli, poly 0x1EDC6F41 -- m1_crc32c.c, reused) over
 *    EVERY byte of the file preceding that line.
 *
 * Settings live ONLY on the SD card. The boot-signed flash metadata
 * (S_M1_FW_CONFIG_t, sector 127) is never read or written by this module.
 */

/** RAM-resident user settings. Initialized to compiled defaults at startup;
 *  overlaid from SD by settings_init(); mutated only by the UI task. */
typedef struct
{
    uint8_t  brightness_pct;    /**< LCD brightness, 0..100 (default 100) */
    uint16_t screen_timeout_s;  /**< LCD saver timeout: 0 = Off, else 10..120 s (default 45) */
    uint8_t  led_notifications; /**< Non-critical LED feedback enabled: 0/1 (default 1) */
    char     device_name[33];   /**< User device name, NUL-terminated (default "M1") */

    /* Sub-GHz radio overrides -- consumed by the radio-settings unit that
     * defines their semantic ranges; they only round-trip persistently
     * here, clamped to the full width of their storage type. */
    uint8_t  subghz_mod;        /**< Modulation preset id */
    uint16_t subghz_bw_khz;     /**< RX bandwidth in kHz */
    uint8_t  subghz_dev_khz;    /**< FSK deviation in kHz */
    int8_t   subghz_power_dbm;  /**< TX power in dBm */
    uint32_t subghz_freq_hz;    /**< Explicit frequency override in Hz */
    uint8_t  subghz_freq_valid; /**< Frequency override valid: 0/1 */

    /* Bluetooth advertisement -- consumed by the BLE-settings unit that
     * defines their semantic ranges; same persistence-only contract. */
    char     bt_adv_name[33];   /**< Advertisement name, NUL-terminated (default "") */
    uint8_t  bt_preset;         /**< Advertisement payload preset id */
    uint16_t bt_interval_ms;    /**< Advertisement interval in ms */
} S_M1_Settings_t;

/** Load settings from SD (tolerantly) into RAM. Idempotent: exactly one
 *  load attempt per boot, no retries. Missing file, corrupt/truncated
 *  file, or an absent/failed SD card all leave the compiled defaults in
 *  effect (logged once each). Returns true iff a persisted file was found
 *  and CRC-validated (individual out-of-range values may still have been
 *  clamped); false when defaults are in effect. Never blocks or hangs. */
bool settings_init(void);

/** Read-only view of the current settings (see ownership contract above). */
const S_M1_Settings_t *settings_get(void);

/** Typed setters (UI task only). Values are clamped to the same bounds the
 *  file parser enforces, so the RAM image can never hold out-of-range
 *  data. Setters update RAM only; persistence is settings_save(). */
void settings_set_brightness_pct(uint8_t pct);
void settings_set_screen_timeout_s(uint16_t seconds);
void settings_set_led_notifications(uint8_t enabled);
void settings_set_device_name(const char *name);
void settings_set_subghz_mod(uint8_t mod);
void settings_set_subghz_bw_khz(uint16_t bw_khz);
void settings_set_subghz_dev_khz(uint8_t dev_khz);
void settings_set_subghz_power_dbm(int8_t power_dbm);
void settings_set_subghz_freq_hz(uint32_t freq_hz, uint8_t valid);
void settings_set_bt_adv_name(const char *name);
void settings_set_bt_preset(uint8_t preset);
void settings_set_bt_interval_ms(uint16_t interval_ms);

/** Serialize the current RAM settings and atomically promote them:
 *  write settings.tmp (full write + close), rotate the previous
 *  settings.cfg to settings.bak, then rename settings.tmp to
 *  settings.cfg. On any failure the previous good settings.cfg is left in
 *  place (or restored), so a save can never destroy the last good file.
 *  The retained .bak is the last known-good copy. Returns true on
 *  success. UI task only. */
bool settings_save(void);

/** Restore the compiled defaults in RAM. RAM-only by design: clearing the
 *  SD copy (and rebooting) is the caller's policy, implemented by the
 *  System-settings UI. */
void settings_reset_defaults(void);

#endif /* M1_SETTINGS_STORE_H_ */
