/* See COPYING.txt for license details. */

/*
*
* m1_settings_store.c
*
* Durable user-settings store: RAM image + SD persistence.
*
* M1 Project
*
*/

/*
 * Layout note for the host extraction test
 * (m1_csrc/test/m1_settings_store_test.py): the block between the
 * SETTINGS_PURE_CORE markers below is HAL/FatFs-free (plain C99 over
 * stdint/stdbool/string/stdio), and every static function in this file
 * avoids HAL headers, so the python+gcc extraction harness can compile
 * the REAL parse/serialize/CRC/clamp/load/save control flow against an
 * in-memory FatFs stand-in.
 */

/*************************** I N C L U D E S **********************************/
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "ff.h"
#include "m1_file_util.h"
#include "m1_log_debug.h"
#include "m1_crc32c.h"
#include "m1_sdcard_provision.h"
#include "m1_settings_store.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"Settings"

/* File placement: 0:/System is a canonical folder (m1_sdcard_provision.h)
 * but is deliberately NOT in the mount-time provisioning table -- it is
 * ensured lazily by settings_write_file() so a fresh card that never
 * changes a setting does not grow an empty folder. */
#define SETTINGS_DIR         M1_SD_DIR_SYSTEM
#define SETTINGS_PATH        SETTINGS_DIR "/settings.cfg"
#define SETTINGS_TMP_PATH    SETTINGS_DIR "/settings.tmp"
#define SETTINGS_BAK_PATH    SETTINGS_DIR "/settings.bak"

/* Whole-file working buffer. The writer's maximum output is ~350 bytes;
 * a file larger than this is not one we wrote and is treated as corrupt
 * (bounded read, never a RAM spike). */
#define SETTINGS_FILE_MAX    1024u

/* ===================== SETTINGS_PURE_CORE_BEGIN ===================== */

/* Single source of truth for the compiled defaults: both the const default
 * image and the (writable) live image are initialized from this macro, so
 * the live struct is valid BEFORE settings_init() runs. */
#define SETTINGS_DEFAULTS_INIT {                                       \
    .brightness_pct    = 100u,                                         \
    .screen_timeout_s  = 45u,                                          \
    .led_notifications = 1u,                                           \
    .device_name       = "M1",                                         \
    .subghz_mod        = 0u,                                           \
    .subghz_bw_khz     = 0u,                                           \
    .subghz_dev_khz    = 0u,                                           \
    .subghz_power_dbm  = 0,                                            \
    .subghz_freq_hz    = 0u,                                           \
    .subghz_freq_valid = 0u,                                           \
    .bt_adv_name       = "",                                           \
    .bt_preset         = 0u,                                           \
    .bt_interval_ms    = 0u,                                           \
}

/* Numeric storage types handled by the field table. */
typedef enum
{
    SETTINGS_T_U8,
    SETTINGS_T_U16,
    SETTINGS_T_U32,
    SETTINGS_T_I8,
    SETTINGS_T_STR,
} settings_type_t;

/* Field descriptor: one row per persisted key. Numeric bounds are
 * inclusive; zero_ok additionally admits 0 outside [min,max] (the screen
 * timeout's "Off"). Semantics live in docs/architecture.md. */
typedef struct
{
    const char      *key;
    settings_type_t  type;
    size_t           offset;
    size_t           size;
    int32_t          min;
    uint32_t         max;
    bool             zero_ok;
    const char      *def_str;
} settings_field_desc_t;

typedef enum
{
    SETTINGS_F_BRIGHTNESS = 0,
    SETTINGS_F_TIMEOUT,
    SETTINGS_F_LED_NOTIFY,
    SETTINGS_F_DEVICE_NAME,
    SETTINGS_F_SUBGHZ_MOD,
    SETTINGS_F_SUBGHZ_BW,
    SETTINGS_F_SUBGHZ_DEV,
    SETTINGS_F_SUBGHZ_POWER,
    SETTINGS_F_SUBGHZ_FREQ,
    SETTINGS_F_SUBGHZ_FREQ_VALID,
    SETTINGS_F_BT_ADV_NAME,
    SETTINGS_F_BT_PRESET,
    SETTINGS_F_BT_INTERVAL,
    SETTINGS_F_COUNT_
} settings_field_idx_t;

static const S_M1_Settings_t s_settings_defaults = SETTINGS_DEFAULTS_INIT;

/* Persisted key order. Keep in sync with settings_field_idx_t above
 * (enforced by the compile-time count check). Ranges:
 *  - brightness 0..100, timeout 0(Off)/10..120 s, LED 0/1 are product
 *    semantics (PR-2 screens enforce the same bounds);
 *  - subghz_/bt_ fields round-trip at full storage-type width until their
 *    consuming units define tighter ranges (documented in the header). */
static const settings_field_desc_t s_settings_fields[] =
{
    { "brightness_pct",   SETTINGS_T_U8,  offsetof(S_M1_Settings_t, brightness_pct),   sizeof(s_settings_defaults.brightness_pct),   0,      100u,          false, NULL   },
    { "screen_timeout_s", SETTINGS_T_U16, offsetof(S_M1_Settings_t, screen_timeout_s), sizeof(s_settings_defaults.screen_timeout_s), 10,     120u,          true,  NULL   },
    { "led_notifications",SETTINGS_T_U8,  offsetof(S_M1_Settings_t, led_notifications),sizeof(s_settings_defaults.led_notifications),0,      1u,            false, NULL   },
    { "device_name",      SETTINGS_T_STR, offsetof(S_M1_Settings_t, device_name),      sizeof(s_settings_defaults.device_name),      0,      0u,            false, "M1"   },
    { "subghz_mod",       SETTINGS_T_U8,  offsetof(S_M1_Settings_t, subghz_mod),       sizeof(s_settings_defaults.subghz_mod),       0,      255u,          false, NULL   },
    { "subghz_bw_khz",    SETTINGS_T_U16, offsetof(S_M1_Settings_t, subghz_bw_khz),    sizeof(s_settings_defaults.subghz_bw_khz),    0,      65535u,        false, NULL   },
    { "subghz_dev_khz",   SETTINGS_T_U8,  offsetof(S_M1_Settings_t, subghz_dev_khz),   sizeof(s_settings_defaults.subghz_dev_khz),   0,      255u,          false, NULL   },
    { "subghz_power_dbm", SETTINGS_T_I8,  offsetof(S_M1_Settings_t, subghz_power_dbm), sizeof(s_settings_defaults.subghz_power_dbm), -128,   127u,          false, NULL   },
    { "subghz_freq_hz",   SETTINGS_T_U32, offsetof(S_M1_Settings_t, subghz_freq_hz),   sizeof(s_settings_defaults.subghz_freq_hz),   0,      4294967295u,   false, NULL   },
    { "subghz_freq_valid",SETTINGS_T_U8,  offsetof(S_M1_Settings_t, subghz_freq_valid),sizeof(s_settings_defaults.subghz_freq_valid),0,      1u,            false, NULL   },
    { "bt_adv_name",      SETTINGS_T_STR, offsetof(S_M1_Settings_t, bt_adv_name),      sizeof(s_settings_defaults.bt_adv_name),      0,      0u,            false, ""     },
    { "bt_preset",        SETTINGS_T_U8,  offsetof(S_M1_Settings_t, bt_preset),        sizeof(s_settings_defaults.bt_preset),        0,      255u,          false, NULL   },
    { "bt_interval_ms",   SETTINGS_T_U16, offsetof(S_M1_Settings_t, bt_interval_ms),   sizeof(s_settings_defaults.bt_interval_ms),   0,      65535u,        false, NULL   },
};

_Static_assert(SETTINGS_F_COUNT_ == (int)(sizeof(s_settings_fields) / sizeof(s_settings_fields[0])),
               "settings_field_idx_t out of sync with s_settings_fields");

#define SETTINGS_FIELD_COUNT ((size_t)(sizeof(s_settings_fields) / sizeof(s_settings_fields[0])))

typedef enum
{
    SETTINGS_LOAD_OK = 0,    /* file found, CRC valid, values applied (possibly clamped) */
    SETTINGS_LOAD_MISSING,   /* no file -> defaults */
    SETTINGS_LOAD_CORRUPT,   /* CRC/truncation/unsupported version, no validating .bak -> defaults, files kept */
    SETTINGS_LOAD_RECOVERED, /* settings.cfg unreadable, but a validating .bak was adopted */
    SETTINGS_LOAD_SD_ERROR,  /* filesystem error (incl. absent card) -> defaults */
} settings_load_status_t;

static S_M1_Settings_t g_m1_settings = SETTINGS_DEFAULTS_INIT;

/* Single-writer bookkeeping (see header ownership contract). */
static bool g_settings_ready     = false;
static bool g_settings_loaded_ok = false;
/* True only when the boot load accounted for the card's content: a file
 * was read and validated (SETTINGS_LOAD_OK, or SETTINGS_LOAD_RECOVERED
 * when an unreadable settings.cfg fell back to a validating .bak -- the
 * RAM image then mirrors the card), or the card provably held no
 * settings file (SETTINGS_LOAD_MISSING). Left false for SD errors and
 * fully unreadable content so settings_save() can refuse before a
 * compiled-defaults RAM image overwrites a live settings.cfg this boot
 * never read (the late-inserted-card guard: one load attempt per boot,
 * so a card that appears after init is invisible to the store until the
 * next boot). */
static bool g_settings_save_permitted = false;

/* True while the on-disk settings.cfg is known corrupt from the boot
 * load that recovered from settings.bak (SETTINGS_LOAD_RECOVERED):
 * settings.bak is then the ONLY good copy on the card, so
 * settings_write_file() displaces the corrupt settings.cfg directly
 * instead of unlinking .bak to make room for the rotation -- a failure
 * or power cut inside that heal save must always leave either the
 * validating .bak or the complete promotable settings.tmp, never zero
 * good copies. Cleared by the first successful (healing) write. */
static bool g_settings_cfg_unreadable = false;

/* Fields reset to their compiled default by the LAST
 * settings_parse_buffer() run (malformed numeral, or a control byte
 * inside a string value). Pure-core bookkeeping so the extraction
 * harnesses see it; settings_init() turns a nonzero count after a
 * successful load into one warn -- without it a CRC-valid foreign
 * value silently reverts a user-visible field and the next save makes
 * the loss permanent with no in-band diagnostic. */
static uint32_t g_settings_field_resets = 0u;

/* ====================== SETTINGS_PURE_CORE_END ====================== */

/* Shared file-IO work buffer for settings_load_file()/settings_write_file().
 * File-scope on purpose (stack relief): the 1 KiB transient no longer sits
 * on the boot task's -- or the future UI writer task's -- stack. Safe
 * under the ownership contract: the load runs once at boot before any
 * other task exists, saves are single-writer (UI task only) and are
 * refused until that load finished, so the two users are strictly
 * sequenced and never nest. */
static char s_settings_work[SETTINGS_FILE_MAX];

/********************* F U N C T I O N   P R O T O T Y P E S ******************/



/*************** F U N C T I O N   I M P L E M A T I O N ****************/

/* Bounded strlen (strnlen is not ISO C11; keep the core portable). */
static size_t settings_bounded_len(const char *s, size_t max)
{
	size_t n = 0;
	while ((n < max) && (s[n] != '\0')) { n++; }
	return n;
}

/* Strict signed-decimal parser: full-token digits only, no atoi-style
 * prefix truncation, no silent overflow. Returns false on malformed
 * input (caller falls back to the field default). */
static bool settings_parse_i64(const char *s, size_t len, int64_t *out)
{
	uint64_t acc = 0;
	bool neg = false;
	size_t i = 0;

	if ((len == 0U) || (len > 20U)) { return false; }
	if (s[0] == '-') { neg = true; i = 1U; }
	if (i >= len) { return false; }

	for (; i < len; i++)
	{
		uint64_t d;
		if ((s[i] < '0') || (s[i] > '9')) { return false; }
		d = (uint64_t)(s[i] - '0');
		if (acc > ((UINT64_MAX - d) / 10u)) { return false; } /* would overflow */
		acc = (acc * 10u) + d;
	}

	if (!neg)
	{
		if (acc > (uint64_t)INT64_MAX) { return false; }
		*out = (int64_t)acc;
	}
	else if (acc == ((uint64_t)INT64_MAX + 1u))
	{
		*out = INT64_MIN;
	}
	else
	{
		if (acc > ((uint64_t)INT64_MAX + 1u)) { return false; }
		*out = -(int64_t)acc;
	}
	return true;
}

/* Strict 8-hex-digit parser (case-insensitive) for the crc= trailer. */
static bool settings_parse_hex32(const char *s, size_t len, uint32_t *out)
{
	uint32_t v = 0;
	size_t i;

	if (len != 8U) { return false; }
	for (i = 0; i < len; i++)
	{
		char c = s[i];
		uint32_t d;
		if ((c >= '0') && (c <= '9')) { d = (uint32_t)(c - '0'); }
		else if ((c >= 'a') && (c <= 'f')) { d = (uint32_t)(c - 'a') + 10u; }
		else if ((c >= 'A') && (c <= 'F')) { d = (uint32_t)(c - 'A') + 10u; }
		else { return false; }
		v = (v << 4) | d;
	}
	*out = v;
	return true;
}

/* Clamp a numeric value into the descriptor's bounds and store it into
 * the live struct via the descriptor's type/offset. */
static void settings_store_numeric(const settings_field_desc_t *fd, int64_t value)
{
	int64_t v = value;

	if (!((v == 0) && fd->zero_ok))
	{
		if (v < (int64_t)fd->min) { v = (int64_t)fd->min; }
		if (v > (int64_t)fd->max) { v = (int64_t)fd->max; }
	}

	switch (fd->type)
	{
	case SETTINGS_T_U8:  *(uint8_t  *)((uint8_t *)&g_m1_settings + fd->offset) = (uint8_t)v;  break;
	case SETTINGS_T_U16: *(uint16_t *)((uint8_t *)&g_m1_settings + fd->offset) = (uint16_t)v; break;
	case SETTINGS_T_U32: *(uint32_t *)((uint8_t *)&g_m1_settings + fd->offset) = (uint32_t)v; break;
	case SETTINGS_T_I8:  *(int8_t   *)((uint8_t *)&g_m1_settings + fd->offset) = (int8_t)v;   break;
	default: break; /* STR descriptors never reach here */
	}
}

/* Store a string value bounded to the field capacity; an empty value
 * selects the field's compiled default. */
static void settings_store_string(const settings_field_desc_t *fd, const char *val, size_t val_len)
{
	char *dst = (char *)((uint8_t *)&g_m1_settings + fd->offset);
	const char *src = val;
	size_t n = val_len;

	if (n == 0U)
	{
		src = (fd->def_str != NULL) ? fd->def_str : "";
		n = settings_bounded_len(src, fd->size - 1U);
	}
	if (n > (fd->size - 1U)) { n = fd->size - 1U; }

	/* Zero the whole field, then copy: every intermediate state stays
	 * NUL-terminated (the bytes past the copy frontier remain zero), so
	 * a concurrent settings_get() reader can never run off the array --
	 * the header ownership contract. A trailing dst[n] = '\0' after the
	 * memcpy would leave the buffer un-terminated mid-update when the
	 * new value is longer than the old; writing it before the memcpy
	 * would leave a stale tail on shrink ("Bright" -> "Dim" reading
	 * "Dimght"). Pinned by the host suite (static memset-before-memcpy
	 * check + shrink/grow regression). */
	memset(dst, 0, fd->size);
	memcpy(dst, src, n);
}

/* Reset one field to its compiled default (used for malformed values). */
static void settings_store_field_default(const settings_field_desc_t *fd)
{
	memcpy((uint8_t *)&g_m1_settings + fd->offset,
	       (const uint8_t *)&s_settings_defaults + fd->offset,
	       fd->size);
}

/* Apply one "key=value" line (line_len excludes CRLF). Unknown keys and
 * malformed lines are skipped; malformed numerals reset that field to its
 * default; out-of-range numerals clamp to the field bounds. */
static void settings_apply_kv(const char *line, size_t line_len)
{
	const char *eq = (const char *)memchr(line, '=', line_len);
	const settings_field_desc_t *fd = NULL;
	size_t k;
	size_t i;

	if (eq == NULL) { return; } /* malformed line in a CRC-valid file: tolerated */
	k = (size_t)(eq - line);

	for (i = 0; i < SETTINGS_FIELD_COUNT; i++)
	{
		size_t key_len = settings_bounded_len(s_settings_fields[i].key, 64U);
		if ((key_len == k) && (strncmp(line, s_settings_fields[i].key, k) == 0))
		{
			fd = &s_settings_fields[i];
			break;
		}
	}
	if (fd == NULL) { return; } /* unknown key: skipped */

	if (fd->type == SETTINGS_T_STR)
	{
		size_t vlen = line_len - k - 1U;
		size_t i;

		/* The serializer refuses control characters (they would corrupt
		 * the line format), so a value imported here with one would
		 * poison every later save. A CRC-valid file can still carry one
		 * (hand edit, foreign writer); the loader applies the same
		 * policy at the door: any control byte in a string value resets
		 * that field to its default. */
		for (i = 0; i < vlen; i++)
		{
			unsigned char ch = (unsigned char)eq[1U + i];
			if ((ch < 0x20U) || (ch == 0x7FU))
			{
				g_settings_field_resets++;
				settings_store_field_default(fd);
				return;
			}
		}
		settings_store_string(fd, eq + 1, vlen);
	}
	else
	{
		int64_t v;
		if (!settings_parse_i64(eq + 1, line_len - k - 1U, &v))
		{
			g_settings_field_resets++;
			settings_store_field_default(fd);
			return;
		}
		settings_store_numeric(fd, v);
	}
}

/* Parse a whole (already CRC-verified) settings file image. Resets the
 * live struct to defaults first, so absent keys keep their default.
 * Returns false only for an unsupported format version (caller reports
 * the file as corrupt); everything else is tolerated per-field. */
static bool settings_parse_buffer(const char *buf, size_t len)
{
	size_t i = 0;
	bool first = true;

	if (len == 0U) { return false; }

	g_m1_settings = s_settings_defaults;
	g_settings_field_resets = 0u; /* counts only this parse's per-field resets */

	while (i < len)
	{
		const char *nl = (const char *)memchr(&buf[i], '\n', len - i);
		const char *line = &buf[i];
		size_t raw = (nl != NULL) ? (size_t)(nl - &buf[i]) : (len - i);
		size_t line_len = raw;

		i += raw + ((nl != NULL) ? 1U : 0U);
		if ((line_len > 0U) && (line[line_len - 1U] == '\r')) { line_len--; }

		if (first)
		{
			first = false;
			if ((line_len != 6U) || (strncmp(line, "#ver=1", 6U) != 0)) { return false; }
			continue;
		}
		if ((line_len == 0U) || (line[0] == '#')) { continue; } /* blank/comment */
		if ((line_len >= 4U) && (strncmp(line, "crc=", 4U) == 0)) { break; } /* data ends at the trailer */
		settings_apply_kv(line, line_len);
	}
	return true;
}

/* Locate the trailing "crc=XXXXXXXX" line, require it to be the final
 * line, and verify it against the CRC-32C of every preceding byte.
 * A file whose trailer is missing (truncated), misplaced, malformed, or
 * stale (content edited) fails here. */
static bool settings_buffer_crc_valid(const char *buf, size_t len)
{
	size_t i = 0;

	while (i < len)
	{
		const char *nl = (const char *)memchr(&buf[i], '\n', len - i);
		size_t raw = (nl != NULL) ? (size_t)(nl - &buf[i]) : (len - i);
		size_t line_len = raw;
		size_t after = i + raw + ((nl != NULL) ? 1U : 0U);

		if ((line_len > 0U) && (buf[i + line_len - 1U] == '\r')) { line_len--; }

		if ((line_len >= 4U) && (strncmp(&buf[i], "crc=", 4U) == 0))
		{
			uint32_t want = 0;
			if (after != len) { return false; } /* content after the trailer */
			if (!settings_parse_hex32(&buf[i + 4U], line_len - 4U, &want)) { return false; }
			return (want == m1_crc32c(buf, i)); /* i = trailer line offset = CRC coverage */
		}
		i = after;
	}
	return false; /* no trailer: truncated or foreign file */
}

/* Read a numeric field (sign-extended) for serialization. */
static int64_t settings_read_numeric(const settings_field_desc_t *fd)
{
	const uint8_t *p = (const uint8_t *)&g_m1_settings + fd->offset;

	switch (fd->type)
	{
	case SETTINGS_T_U8:  return (int64_t)*p;
	case SETTINGS_T_U16: return (int64_t)*(const uint16_t *)p;
	case SETTINGS_T_U32: return (int64_t)*(const uint32_t *)p;
	case SETTINGS_T_I8:  return (int64_t)*(const int8_t *)p;
	default: return 0;
	}
}

/* Serialize the live struct into out[0..cap) as
 * "#ver=1" + one "key=value" line per field + "crc=<8 hex>" trailer.
 * Returns false (with no partial file emitted by callers) if the output
 * would not fit, or if a string field contains a control character (a
 * CR/LF inside a value would corrupt the line format -- refuse loudly). */
static bool settings_serialize(char *out, size_t cap, size_t *out_len)
{
	size_t off = 0;
	size_t i;
	uint32_t crc;
	int n;

	n = snprintf(&out[off], cap - off, "#ver=1\r\n");
	if ((n < 0) || ((size_t)n >= (cap - off))) { return false; }
	off += (size_t)n;

	for (i = 0; i < SETTINGS_FIELD_COUNT; i++)
	{
		const settings_field_desc_t *fd = &s_settings_fields[i];

		if (fd->type == SETTINGS_T_STR)
		{
			const char *v = (const char *)((const uint8_t *)&g_m1_settings + fd->offset);
			size_t vlen = settings_bounded_len(v, fd->size - 1U);
			size_t j;

			for (j = 0; j < vlen; j++)
			{
				unsigned char ch = (unsigned char)v[j];
				if ((ch < 0x20U) || (ch == 0x7FU)) { return false; }
			}
			n = snprintf(&out[off], cap - off, "%s=%.*s\r\n", fd->key, (int)vlen, v);
		}
		else if (fd->type == SETTINGS_T_I8)
		{
			n = snprintf(&out[off], cap - off, "%s=%d\r\n", fd->key, (int)settings_read_numeric(fd));
		}
		else
		{
			n = snprintf(&out[off], cap - off, "%s=%lu\r\n", fd->key, (unsigned long)settings_read_numeric(fd));
		}

		if ((n < 0) || ((size_t)n >= (cap - off))) { return false; }
		off += (size_t)n;
	}

	crc = m1_crc32c(out, off);
	n = snprintf(&out[off], cap - off, "crc=%08X\r\n", (unsigned)crc);
	if ((n < 0) || ((size_t)n >= (cap - off))) { return false; }
	off += (size_t)n;

	*out_len = off;
	return true;
}

/* Content probe for reconciliation: bounded read + CRC gate only, no
 * parse and no application to the live struct (the loader keeps the
 * final word). Shared by settings_recover_replace() to decide whether
 * an existing settings.cfg is one of ours before discarding a
 * settings.tmp beside it. */
static bool settings_file_crc_ok(const TCHAR *path)
{
	FIL f;
	FRESULT fr;
	UINT br = 0;

	fr = f_open(&f, path, FA_READ);
	if (fr != FR_OK) { return false; }

	fr = f_read(&f, s_settings_work, (UINT)sizeof(s_settings_work), &br);
	if (fr != FR_OK) { (void)f_close(&f); return false; }
	if (!f_eof(&f)) { (void)f_close(&f); return false; } /* oversize: not ours */
	if (f_close(&f) != FR_OK) { return false; }

	return settings_buffer_crc_valid(s_settings_work, (size_t)br);
}

/* Deterministically reconcile settings.cfg/.tmp/.bak left behind by a
 * save interrupted at any point (power loss, SD removal). Must run before
 * settings.cfg is opened for reading.
 *
 * An orphaned settings.tmp is promoted ONLY when settings.cfg is absent;
 * when settings.cfg exists AND passes the CRC probe the tmp is discarded
 * -- it may be an incomplete write whose cleanup path never ran. The one
 * exception is a settings.cfg that FAILS the probe beside a tmp that
 * PASSES it (e.g. a heal save interrupted after its tmp write): the tmp
 * is then the newer good candidate and displaces the corrupt cfg. The
 * .bak is consumed only when neither cfg nor tmp survives. Content
 * decisions use the CRC probe above; the full CRC + parse gate in
 * settings_load_file() has the final word on whatever this promotes. */
static void settings_recover_replace(void)
{
	bool final_exists = (f_stat(SETTINGS_PATH, NULL) == FR_OK);
	bool bak_exists   = (f_stat(SETTINGS_BAK_PATH, NULL) == FR_OK);
	bool tmp_exists   = (f_stat(SETTINGS_TMP_PATH, NULL) == FR_OK);

	if (!final_exists && tmp_exists)
	{
		if (f_rename(SETTINGS_TMP_PATH, SETTINGS_PATH) == FR_OK)
		{
			final_exists = true;
			tmp_exists = false;
		}
	}

	if (!final_exists && !tmp_exists && bak_exists)
	{
		(void)f_rename(SETTINGS_BAK_PATH, SETTINGS_PATH); /* last known-good */
	}
	else if (final_exists && tmp_exists)
	{
		if (!settings_file_crc_ok(SETTINGS_PATH) && settings_file_crc_ok(SETTINGS_TMP_PATH))
		{
			/* Corrupt cfg + validating tmp: displace the corrupt cfg so
			 * the newer good candidate can be promoted. A failed unlink
			 * or rename leaves the pair for the load's .bak fallback. */
			if (f_unlink(SETTINGS_PATH) == FR_OK)
			{
				(void)f_rename(SETTINGS_TMP_PATH, SETTINGS_PATH);
			}
		}
		else
		{
			(void)f_unlink(SETTINGS_TMP_PATH); /* orphan from an abandoned attempt */
		}
	}
	else
	{
		/* normal state (final present, no tmp): nothing to reconcile */
	}
}

/* Read one candidate settings file through the bounded read + CRC
 * gate + parse and apply it to the live struct. Shared by the
 * settings.cfg path and the corrupt-file .bak fallback below; returns
 * the outcome for that path. Every non-OK return leaves the compiled
 * defaults in the live struct. */
static settings_load_status_t settings_read_and_validate(const TCHAR *path)
{
	FIL f;
	FRESULT fr;
	UINT br = 0;

	fr = f_open(&f, path, FA_READ);
	if (fr != FR_OK)
	{
		if ((fr == FR_NO_FILE) || (fr == FR_NO_PATH)) { return SETTINGS_LOAD_MISSING; }
		/* Everything else -- absent card, I/O error, a present card with
		 * no usable filesystem (FR_NO_FILESYSTEM et al.) -- reports the
		 * generic SD-unavailable line: defaults are the correct outcome
		 * for all of them, and this module never mounts the volume, so
		 * it cannot usefully classify further. */
		return SETTINGS_LOAD_SD_ERROR;
	}

	fr = f_read(&f, s_settings_work, (UINT)sizeof(s_settings_work), &br);
	if (fr != FR_OK) { (void)f_close(&f); return SETTINGS_LOAD_SD_ERROR; }
	if (!f_eof(&f)) { (void)f_close(&f); return SETTINGS_LOAD_CORRUPT; } /* larger than our bound: not ours */
	if (f_close(&f) != FR_OK) { return SETTINGS_LOAD_SD_ERROR; }

	if (!settings_buffer_crc_valid(s_settings_work, (size_t)br)) { return SETTINGS_LOAD_CORRUPT; }
	if (!settings_parse_buffer(s_settings_work, (size_t)br)) { return SETTINGS_LOAD_CORRUPT; } /* unsupported version */
	return SETTINGS_LOAD_OK;
}

/* Load settings.cfg into the live struct. Never writes, never retries.
 * A settings.cfg that exists but is unreadable as ours (CRC mismatch,
 * truncation, unsupported version, oversize) falls back to settings.bak:
 * when the backup passes the same content gates it is adopted
 * (SETTINGS_LOAD_RECOVERED) -- exactly the scenario the retained backup
 * exists for -- and the boot again accounted for the card's content, so
 * saving stays permitted and the next save heals settings.cfg (marked
 * by g_settings_cfg_unreadable, which arms the heal ordering in
 * settings_write_file()). The fallback is strictly content-gated: a
 * missing or failing .bak leaves the CORRUPT outcome with both files
 * untouched on disk. */
static settings_load_status_t settings_load_file(void)
{
	settings_load_status_t st;

	g_settings_cfg_unreadable = false;
	settings_reset_defaults(); /* guaranteed defaults unless a valid file overlays them */
	settings_recover_replace();

	st = settings_read_and_validate(SETTINGS_PATH);
	if ((st == SETTINGS_LOAD_CORRUPT)
	    && (settings_read_and_validate(SETTINGS_BAK_PATH) == SETTINGS_LOAD_OK))
	{
		g_settings_cfg_unreadable = true; /* cfg on the card is known corrupt */
		return SETTINGS_LOAD_RECOVERED;
	}
	return st;
}

/* Recoverable atomic promotion (same proven pattern as the Wi-Fi saved-
 * network store): fully write+close settings.tmp, unlink the superseded
 * settings.bak (FatFs renames cannot overwrite an existing destination),
 * rotate the previous settings.cfg to settings.bak, then rename
 * settings.tmp to settings.cfg. A failure at any step leaves the previous
 * good settings.cfg in place (or restores it); while it exists, the
 * retained .bak is the last known-good copy. Returns true on success.
 *
 * Heal mode (g_settings_cfg_unreadable, set by the SETTINGS_LOAD_RECOVERED
 * boot): the current settings.cfg is KNOWN corrupt and settings.bak is
 * the only good copy, so the rotation's first step (unlinking .bak)
 * would destroy it. Instead the corrupt cfg is displaced directly and
 * .bak stays untouched until the next save's rotation: every failure
 * path then leaves either the validating .bak or the complete promotable
 * settings.tmp on the card, so a retry (or the next boot's
 * reconciliation) can always recover. */
static bool settings_write_file(void)
{
	size_t len = 0;
	FIL f;
	FRESULT fr;
	UINT bw = 0;

	if (!settings_serialize(s_settings_work, sizeof(s_settings_work), &len)) { return false; }

	if (fs_directory_ensure(SETTINGS_DIR) != FR_OK) { return false; }

	fr = f_open(&f, SETTINGS_TMP_PATH, FA_CREATE_ALWAYS | FA_WRITE);
	if (fr != FR_OK) { return false; }

	fr = f_write(&f, s_settings_work, (UINT)len, &bw);
	if ((fr != FR_OK) || (bw != (UINT)len))
	{
		(void)f_close(&f);
		(void)f_unlink(SETTINGS_TMP_PATH);
		return false;
	}

	if (f_close(&f) != FR_OK)
	{
		(void)f_unlink(SETTINGS_TMP_PATH);
		return false;
	}

	if (g_settings_cfg_unreadable)
	{
		/* Heal save after a .bak recovery: settings.cfg is the known-
		 * corrupt file and settings.bak the only good copy. Displace the
		 * corrupt cfg (FatFs renames cannot overwrite) and promote the
		 * tmp over it, keeping .bak for the next save's rotation. A
		 * missing cfg is fine (e.g. a previous heal attempt's failed
		 * promotion left only the tmp); any other unlink failure (e.g. a
		 * write-protected card) aborts with the tmp cleaned up and the
		 * actual cause named -- .bak is untouched either way. */
		fr = f_unlink(SETTINGS_PATH);
		if ((fr != FR_OK) && (fr != FR_NO_FILE) && (fr != FR_NO_PATH))
		{
			(void)f_unlink(SETTINGS_TMP_PATH);
			M1_LOG_W(M1_LOGDB_TAG, "Settings corrupt cfg not removable, save aborted\r\n");
			return false;
		}

		if (f_rename(SETTINGS_TMP_PATH, SETTINGS_PATH) != FR_OK)
		{
			/* Deliberately NOT unlinking the tmp: it is complete, and
			 * with the corrupt cfg gone the next boot's reconciliation
			 * promotes it. .bak also survives as the older good copy. */
			return false;
		}

		g_settings_cfg_unreadable = false; /* healed: cfg is good again, .bak kept */
		return true;
	}

	/* Clear the superseded backup so the rotation rename cannot hit
	 * FR_EXIST (FatFs never renames onto an existing file). A missing
	 * .bak is fine; any other failure (e.g. a write-protected card)
	 * would make the rotation fail identically on every future save,
	 * so abort with the tmp cleaned up and the actual cause named. */
	fr = f_unlink(SETTINGS_BAK_PATH);
	if ((fr != FR_OK) && (fr != FR_NO_FILE))
	{
		(void)f_unlink(SETTINGS_TMP_PATH);
		M1_LOG_W(M1_LOGDB_TAG, "Settings .bak not removable, save aborted\r\n");
		return false;
	}

	fr = f_rename(SETTINGS_PATH, SETTINGS_BAK_PATH);
	if ((fr != FR_OK) && (fr != FR_NO_FILE) && (fr != FR_NO_PATH))
	{
		(void)f_unlink(SETTINGS_TMP_PATH); /* could not preserve previous -> abort, it is untouched */
		return false;
	}

	if (f_rename(SETTINGS_TMP_PATH, SETTINGS_PATH) != FR_OK)
	{
		(void)f_rename(SETTINGS_BAK_PATH, SETTINGS_PATH); /* roll the previous good file back */
		(void)f_unlink(SETTINGS_TMP_PATH);
		return false;
	}

	return true; /* .bak intentionally retained (last known-good) */
}

/******************************************************************************/
/*
*	Load settings once at boot; tolerant of every failure mode.
*
*/
/******************************************************************************/
bool settings_init(void)
{
	settings_load_status_t st;

	if (g_settings_ready) { return g_settings_loaded_ok; } /* idempotent: one attempt per boot, no retry storm */

	st = settings_load_file();
	g_settings_ready = true;

	switch (st)
	{
	case SETTINGS_LOAD_OK:
		g_settings_loaded_ok = true;
		g_settings_save_permitted = true; /* RAM image mirrors the card */
		M1_LOG_I(M1_LOGDB_TAG, "Settings loaded from SD\r\n");
		break;
	case SETTINGS_LOAD_MISSING:
		g_settings_save_permitted = true; /* card holds no file to clobber */
		M1_LOG_I(M1_LOGDB_TAG, "No settings file, defaults in effect\r\n");
		break;
	case SETTINGS_LOAD_RECOVERED:
		g_settings_loaded_ok = true;
		g_settings_save_permitted = true; /* RAM image mirrors the card's .bak content */
		M1_LOG_W(M1_LOGDB_TAG, "Settings corrupt, restored from .bak\r\n");
		break;
	case SETTINGS_LOAD_CORRUPT:
		M1_LOG_W(M1_LOGDB_TAG, "Settings corrupt, defaults in effect (.bak kept)\r\n");
		break;
	case SETTINGS_LOAD_SD_ERROR:
		M1_LOG_W(M1_LOGDB_TAG, "SD unavailable, settings defaults in effect\r\n");
		break;
	}

	/* A file that passed the CRC gate can still have needed per-field
	 * resets (malformed numeral, control byte in a string value): the
	 * field(s) silently reverted to defaults and the next save makes
	 * that permanent, so name the count once -- the only in-band clue
	 * the operator gets. */
	if (((st == SETTINGS_LOAD_OK) || (st == SETTINGS_LOAD_RECOVERED))
	    && (g_settings_field_resets != 0u))
	{
		M1_LOG_W(M1_LOGDB_TAG, "Settings load: %lu field(s) reset to default\r\n",
		         (unsigned long)g_settings_field_resets);
	}
	return g_settings_loaded_ok;
}

const S_M1_Settings_t *settings_get(void)
{
	return &g_m1_settings;
}

void settings_set_brightness_pct(uint8_t pct)
{
	settings_store_numeric(&s_settings_fields[SETTINGS_F_BRIGHTNESS], (int64_t)pct);
}

void settings_set_screen_timeout_s(uint16_t seconds)
{
	settings_store_numeric(&s_settings_fields[SETTINGS_F_TIMEOUT], (int64_t)seconds);
}

void settings_set_led_notifications(uint8_t enabled)
{
	settings_store_numeric(&s_settings_fields[SETTINGS_F_LED_NOTIFY], (int64_t)(enabled ? 1u : 0u));
}

void settings_set_device_name(const char *name)
{
	const settings_field_desc_t *fd = &s_settings_fields[SETTINGS_F_DEVICE_NAME];

	if (name == NULL) { settings_store_field_default(fd); return; }
	settings_store_string(fd, name, settings_bounded_len(name, fd->size - 1U));
}

void settings_set_subghz_mod(uint8_t mod)
{
	settings_store_numeric(&s_settings_fields[SETTINGS_F_SUBGHZ_MOD], (int64_t)mod);
}

void settings_set_subghz_bw_khz(uint16_t bw_khz)
{
	settings_store_numeric(&s_settings_fields[SETTINGS_F_SUBGHZ_BW], (int64_t)bw_khz);
}

void settings_set_subghz_dev_khz(uint8_t dev_khz)
{
	settings_store_numeric(&s_settings_fields[SETTINGS_F_SUBGHZ_DEV], (int64_t)dev_khz);
}

void settings_set_subghz_power_dbm(int8_t power_dbm)
{
	settings_store_numeric(&s_settings_fields[SETTINGS_F_SUBGHZ_POWER], (int64_t)power_dbm);
}

void settings_set_subghz_freq_hz(uint32_t freq_hz, uint8_t valid)
{
	settings_store_numeric(&s_settings_fields[SETTINGS_F_SUBGHZ_FREQ], (int64_t)freq_hz);
	settings_store_numeric(&s_settings_fields[SETTINGS_F_SUBGHZ_FREQ_VALID], (int64_t)(valid ? 1u : 0u));
}

void settings_set_bt_adv_name(const char *name)
{
	const settings_field_desc_t *fd = &s_settings_fields[SETTINGS_F_BT_ADV_NAME];

	if (name == NULL) { settings_store_field_default(fd); return; }
	settings_store_string(fd, name, settings_bounded_len(name, fd->size - 1U));
}

void settings_set_bt_preset(uint8_t preset)
{
	settings_store_numeric(&s_settings_fields[SETTINGS_F_BT_PRESET], (int64_t)preset);
}

void settings_set_bt_interval_ms(uint16_t interval_ms)
{
	settings_store_numeric(&s_settings_fields[SETTINGS_F_BT_INTERVAL], (int64_t)interval_ms);
}

bool settings_save(void)
{
	if (!g_settings_ready)
	{
		M1_LOG_W(M1_LOGDB_TAG, "Save before init refused\r\n");
		return false;
	}

	/* Late-inserted-card guard: the boot load either read a file or
	 * proved the card holds none (g_settings_save_permitted). Anything
	 * else -- absent/failed SD, or a corrupt/unsupported-version file
	 * with no validating .bak fallback -- left compiled defaults in RAM;
	 * saving that image could overwrite a live settings.cfg this boot
	 * never read (e.g. the card was inserted after init). Remedy depends
	 * on the cause: reboot with the card readable, or (both files
	 * unreadable) replace the settings file -- see the header contract. */
	if (!g_settings_save_permitted)
	{
		M1_LOG_W(M1_LOGDB_TAG, "Save refused: settings unreadable at boot (reboot with SD or replace file)\r\n");
		return false;
	}

	if (!settings_write_file())
	{
		M1_LOG_W(M1_LOGDB_TAG, "Settings save failed\r\n");
		return false;
	}
	return true;
}

void settings_reset_defaults(void)
{
	g_m1_settings = s_settings_defaults;
}
