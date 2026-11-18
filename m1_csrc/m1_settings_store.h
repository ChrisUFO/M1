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
 *  - FATFS SERIALIZATION: volume 0's FatFs is non-reentrant in this
 *    build (FF_FS_REENTRANT=0, no global FS mutex), so settings_save()
 *    must not run concurrently with any other FatFs user (capture,
 *    Wi-Fi store, file browser) until such a mutex exists. True by
 *    construction today: no runtime writer is wired yet and the boot
 *    load runs before any other task exists.
 *
 * Persistence format (see docs/architecture.md "Storage model"):
 *  - line 1: "#ver=2" (v1 files written by pre-Radio-Settings firmware
 *    are still accepted on load: their then-inert subghz_power_dbm=0
 *    migrates to the +20 dBm default, and the next save rewrites the
 *    file as v2)
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
 *  load attempt per boot, no retries. A missing file, an absent/failed
 *  SD card, or a corrupt/truncated/unsupported-version file whose .bak
 *  fallback is absent or equally unreadable leaves the compiled
 *  defaults in effect (logged once each). When settings.cfg is
 *  unreadable but settings.bak passes the same checks, the .bak content
 *  is adopted (logged once, both files left in place; the first save
 *  afterwards heals settings.cfg while keeping .bak -- then the only
 *  good copy -- untouched until the heal succeeds). A CRC-valid file
 *  whose values needed per-field resets (malformed numeral, control
 *  byte in a string value) loads normally with one additional warning
 *  naming how many fields reverted to their default. Returns true
 *  iff a persisted file was found and CRC-validated -- directly or via
 *  the .bak fallback (individual out-of-range values may still have
 *  been clamped); false when defaults are in effect. Never blocks or
 *  hangs. */
bool settings_init(void);

/** Read-only view of the current settings (see ownership contract above). */
const S_M1_Settings_t *settings_get(void);

/** Typed setters (UI task only). Numeric values are clamped to the same
 *  bounds the file parser enforces, so the RAM image can never hold an
 *  out-of-range number. String setters bound length only; string content
 *  is stored verbatim -- if a control character ever reaches a string
 *  field, settings_save() refuses and warns rather than emit a
 *  corruptible line format. The file parser cannot introduce one: a
 *  loaded value containing a control byte resets that field to its
 *  default (and settings_init() logs one warning naming how many
 *  fields were reset). Setters update RAM only; persistence is
 *  settings_save(). */
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
 *  write settings.tmp (full write + close), unlink the superseded
 *  settings.bak (FatFs renames cannot overwrite an existing
 *  destination), rotate the previous settings.cfg to settings.bak, then
 *  rename settings.tmp to settings.cfg. On any failure the previous
 *  good settings.cfg is left in place (or restored), so a save can
 *  never destroy the last good file. While it exists, the retained
 *  .bak is the last known-good copy -- it is unlinked during every
 *  save attempt, so a failed rotation destroys the older backup while
 *  settings.cfg survives.
 *
 *  Heal save: the FIRST save after settings_init() adopted settings.bak
 *  (settings.cfg corrupt on disk) uses a protective ordering instead --
 *  settings.bak is then the only good copy, so it is never unlinked:
 *  the known-corrupt settings.cfg is displaced directly and the
 *  complete settings.tmp is renamed over it, keeping .bak untouched
 *  until the next save's rotation. Every failure path in this mode
 *  leaves either the validating .bak or the complete promotable
 *  settings.tmp on the card (a failed promotion deliberately retains
 *  the tmp; the next boot's reconciliation promotes it), so a retry
 *  or a reboot always recovers and the store can never dead-end.
 *
 *  Refused (false, one warning) when settings_init() could not account
 *  for the card's content: absent/failed SD, or a settings.cfg that is
 *  corrupt/truncated/unsupported-version with no validating .bak
 *  fallback. The RAM image is then compiled defaults, and writing it
 *  could clobber a live settings.cfg after a late card insertion (the
 *  store loads exactly once per boot). A reboot with the card readable
 *  re-enables saving whenever that load then accounts for the card: a
 *  valid file, a corrupt file recovered from its .bak, or a provably
 *  file-less card. When settings.cfg AND settings.bak are both
 *  unreadable, rebooting re-reads the same corrupt pair -- the
 *  unreadable file must be removed or replaced (e.g. over USB MSC)
 *  before saving works again. Clearing the SD copy is the caller's
 *  factory-reset policy (see settings_reset_defaults()).
 *
 *  The serialization work buffer is file-scope; the calling task still
 *  needs roughly 1.5 KiB of stack for the FatFs file object (~550 B,
 *  FF_FS_TINY=0), the FatFs LFN working buffer (512 B, FF_USE_LFN=2)
 *  and call depth -- a static-analysis bound, no hardware high-water
 *  measurement yet. Returns true on success. UI task only. */
bool settings_save(void);

/** Restore the compiled defaults in RAM. RAM-only by design: clearing the
 *  SD copy (and rebooting) is the caller's policy, implemented by the
 *  System-settings UI. */
void settings_reset_defaults(void);

#endif /* M1_SETTINGS_STORE_H_ */
