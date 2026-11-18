/* See COPYING.txt for license details. */
/*
*
* m1_ir_browser.c
*
* SD-card IR database browser (issue #43 part 2 / PR-5)
*
* M1 Project
*
*/

/*
* Port of the legacy fork's browser UX (m1_ir_universal.c) onto the PR-4
* streaming loader. Only the view/model layer was ported: the dashboard,
* the category -> device -> button lists with 6-row pagination, the search
* screen and the back-out behavior follow the legacy interaction intent.
* The legacy whole-file parser, its 64-command cap and its ir_proto_map
* alias table are intentionally NOT ported -- every data access goes
* through Infrared/m1_ir_file.h (index pass + on-demand parse per block,
* strict-skip through the single upstream protocol map).
*
* Transmit uses the upstream ir_remote_function_play path: the loaded
* signal is published into ir_universal_payload exactly as the upstream
* universal remotes do, then irsnd_generate_tx_data() + infrared_transmit()
* play it. Raw blocks stream into the module-static tick buffer through
* m1_ir_file_signal_load(); nothing here allocates at run time. A raw
* signal only transmits when its tick count is odd and longer than the
* lone leading pause -- empty/zero-sample data lines, bare-pause counts
* and even counts (the loader edge cases tracked in issue #74) surface
* as an error notice instead.
*
* PR-6 adds the legacy favorites/recent stores on top of the same UX:
* two text files under 0:/System (one device path per line, LRU caps in
* m1_ir_browser.h), dashboard rows that list them, a favorites toggle on
* a long OK press in the button view (title marker "* "), and a recent
* entry per successful transmit. Store loads and rewrites are plain
* filesystem work done between transmits in this UI task -- the transmit
* path itself (irb_transmit) contains no file writes, so favorites/
* recent handling can never overlap IRMP TX or learn.
*
* See m1_ir_browser.h for the ownership rule and the 0:/IR SD convention;
* docs/ir-provisioning.md documents the side-load steps.
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_ir_browser.h"
#include "m1_ir_file.h"
#include "m1_ir_signals.h"
#include "m1_ir_remotes.h"
#include "m1_infrared.h"
#include "irsnd.h"
#include "ff.h"
#include "m1_file_browser.h"
#include "m1_sdcard_provision.h"
#include "m1_display.h"
#include "m1_system.h"
#include "m1_tasks.h"
#include "m1_sdcard.h"
#include "m1_virtual_kb.h"
#include "m1_buzzer.h"
#include "m1_led_indicator.h"
#include "m1_log_debug.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"IR_BROWSER"

#define M1_IRB_SD_ROOT				"0:/IR"

/* Favorites/recent stores (PR-6, legacy fork format): one full device
 * path per line under the canonical System folder. */
#define M1_IRB_FAV_FILE				M1_SD_DIR_SYSTEM "/ir_favorites.txt"
#define M1_IRB_RECENT_FILE			M1_SD_DIR_SYSTEM "/ir_recent.txt"

#define M1_IRB_ROW_H				9 /* pixel, 6 rows below the 10-px title bar */
#define M1_IRB_TITLE_H				10 /* pixel */
#define M1_IRB_COL_MAX				20 /* visible chars per row (5x8 font) */

#define M1_IRB_NOTICE_MS_DEFAULT	1500
#define M1_IRB_NOTICE_MS_SHORT		800

/* ===================== M1_IR_BROWSER_PURE_CORE_BEGIN ===================== */
/*
* Pure view/model core: no FatFs, no HAL, no FreeRTOS, no allocation. The
* host test m1_csrc/test/m1_ir_browser_test.py extracts this region
* verbatim and exercises pagination, search matching, entry classification
* (category dir vs .ir device, category-name fit for the bounded rows),
* display-name derivation and the strict-skip notice aggregation.
*/

/* List window: selection + first visible row (6-row pagination). */
typedef struct
{
	uint16_t sel; /* selected row, 0-based */
	uint16_t top; /* first visible row */
} S_M1_IRB_Win_t;

/* Result of a bounded scan / list build, driving the screen choice. */
typedef enum
{
	M1_IRB_SCAN_DATA = 0, /* at least one entry found */
	M1_IRB_SCAN_EMPTY, /* scan completed, nothing matched */
	M1_IRB_SCAN_IO, /* directory could not be read */
	M1_IRB_SCAN_EOL
} S_M1_IRB_Scan_Status_t;

/* Aggregated strict-skip notices (PR-4 notice-hook target). One counter
 * plus the first offending protocol name: the browser surfaces a single
 * summary per file, never one modal per skipped block. */
typedef struct
{
	uint16_t skipped;
	char first_name[M1_IR_FILE_PROTO_LEN_MAX + 1];
} S_M1_IRB_SkipAcc_t;

/*============================================================================*/
/**
  * @brief  Move the selection inside a count-row list and keep the window
  *         scrolled onto it. delta is +1/-1 for row steps or +/-ROWS for
  *         page steps; both ends clamp (legacy browse UX intent).
  */
/*============================================================================*/
static void m1_irb_win_move(S_M1_IRB_Win_t *w, uint16_t count, int16_t delta)
{
	int32_t sel, max_top;

	if ( (w == NULL) || (count == 0u) )
		return;
	sel = (int32_t)w->sel + delta;
	if ( sel < 0 )
		sel = 0;
	if ( sel >= (int32_t)count )
		sel = (int32_t)count - 1;
	w->sel = (uint16_t)sel;

	if ( (int32_t)w->sel < (int32_t)w->top )
		w->top = w->sel;
	if ( (int32_t)(w->sel - w->top) >= M1_IR_BROWSER_ROWS )
		w->top = (uint16_t)(w->sel - M1_IR_BROWSER_ROWS + 1u);
	max_top = (int32_t)count - M1_IR_BROWSER_ROWS; /* signed: no underflow
													* when count < ROWS */
	if ( max_top < 0 )
		max_top = 0;
	if ( (int32_t)w->top > max_top )
		w->top = (uint16_t)max_top;
} // static void m1_irb_win_move(...)

/*============================================================================*/
/**
  * @brief  Is this directory entry a device file: not hidden/system, name
  *         not starting with '.', extension exactly ".ir" (any case)?
  *         This is the scan-entry type mapping: such entries become device
  *         rows; plain sub-directories become category rows.
  */
/*============================================================================*/
static uint8_t m1_irb_is_ir_file(const char *fname)
{
	size_t len;
	char c;

	if ( (fname == NULL) || (fname[0] == '\0') || (fname[0] == '.') )
		return 0;
	len = 0;
	while ( fname[len] != '\0' )
		len++;
	if ( len < 3u ) /* shorter than any "x.ir" */
		return 0;
	if ( fname[len - 3u] != '.' )
		return 0;
	c = fname[len - 2u];
	if ( !((c == 'i') || (c == 'I')) )
		return 0;
	c = fname[len - 1u];
	if ( !((c == 'r') || (c == 'R')) )
		return 0;
	return 1;
} // static uint8_t m1_irb_is_ir_file(const char *fname)

/*============================================================================*/
/**
  * @brief  Can this directory name be stored whole as a category row (and
  *         therefore re-opened from the category list)? Names at or above
  *         M1_IR_BROWSER_NAME_LEN_MAX chars cannot fit the bounded
  *         irb_cat_names rows; listing them truncated would later build an
  *         unopenable scan root and a misleading error, so the scan skips
  *         them instead -- same philosophy as over-long paths
  *         (docs/ir-provisioning.md).
  */
/*============================================================================*/
static uint8_t m1_irb_cat_name_ok(const char *fname)
{
	size_t len = 0;

	if ( (fname == NULL) || (fname[0] == '\0') )
		return 0;
	while ( fname[len] != '\0' )
		len++;
	return (uint8_t)(len < (size_t)M1_IR_BROWSER_NAME_LEN_MAX);
} // static uint8_t m1_irb_cat_name_ok(const char *fname)

/*============================================================================*/
/**
  * @brief  Case-insensitive substring match of a pre-lowercased query
  *         against a file/directory name (search model, legacy intent).
  */
/*============================================================================*/
static uint8_t m1_irb_search_match(const char *fname, const char *lower_query)
{
	char fc;
	const char *p, *q, *m;

	if ( (fname == NULL) || (lower_query == NULL) || (lower_query[0] == '\0') )
		return 0;
	for ( p = fname; *p != '\0'; p++ )
	{
		m = p;
		q = lower_query;
		while ( 1 )
		{
			fc = *m;
			if ( (fc >= 'A') && (fc <= 'Z') )
				fc = (char)(fc + ('a' - 'A'));
			if ( fc != *q )
				break;
			q++;
			m++;
			if ( *q == '\0' )
				return 1; /* whole query matched */
		}
	}
	return 0;
} // static uint8_t m1_irb_search_match(...)

/*============================================================================*/
/**
  * @brief  Lowercase a query in place. Returns the resulting length.
  */
/*============================================================================*/
static uint8_t m1_irb_query_lower(char *query)
{
	uint8_t n = 0;

	if ( query == NULL )
		return 0;
	while ( query[n] != '\0' )
	{
		if ( (query[n] >= 'A') && (query[n] <= 'Z') )
			query[n] = (char)(query[n] + ('a' - 'A'));
		n++;
	}
	return n;
} // static uint8_t m1_irb_query_lower(char *query)

/*============================================================================*/
/**
  * @brief  Derive a list row label from a path relative to the list root:
  *         keep the whole relative path (category rows get the bare
  *         directory name; device rows keep "Brand/Device"), strip a
  *         trailing ".ir", then truncate to the buffer.
  */
/*============================================================================*/
static void m1_irb_row_label(char *dst, uint8_t dstsz, const char *rel_path)
{
	uint8_t n = 0;

	if ( (dst == NULL) || (dstsz == 0u) || (rel_path == NULL) )
	{
		if ( (dst != NULL) && (dstsz > 0u) )
			dst[0] = '\0';
		return;
	}
	while ( (n < (uint8_t)(dstsz - 1u)) && (rel_path[n] != '\0') )
	{
		dst[n] = rel_path[n];
		n++;
	}
	dst[n] = '\0';
	if ( n > 3u )
	{
		/* strip a trailing ".ir" (case-insensitive) */
		char c1 = dst[n - 2u], c2 = dst[n - 1u];
		if ( (dst[n - 3u] == '.') &&
				((c1 == 'i') || (c1 == 'I')) &&
				((c2 == 'r') || (c2 == 'R')) )
			dst[n - 3u] = '\0';
	}
} // static void m1_irb_row_label(...)

/*============================================================================*/
/**
  * @brief  Strict-skip notice aggregation (PR-4 notice-hook payload):
  *         count the skipped block and remember the first offending
  *         protocol name.
  */
/*============================================================================*/
static void m1_irb_skip_add(S_M1_IRB_SkipAcc_t *acc, const char *protocol_name)
{
	uint8_t i = 0;

	if ( acc == NULL )
		return;
	acc->skipped++;
	if ( (acc->first_name[0] == '\0') && (protocol_name != NULL) )
	{
		while ( (i < M1_IR_FILE_PROTO_LEN_MAX) && (protocol_name[i] != '\0') )
		{
			acc->first_name[i] = protocol_name[i];
			i++;
		}
		acc->first_name[i] = '\0';
	}
} // static void m1_irb_skip_add(...)

/*============================================================================*/
/**
  * @brief  Bounded join of "dir/name" (empty dir -> bare name). Used for
  *          SD paths and row labels; truncation is the normal display
  *          case, so the copy is manual -- snprintf patterns with two
  *          known-size sources are something GCC cannot prove safe under
  *          -Werror (same rationale as settings_str_clip).
  */
/*============================================================================*/
static void m1_irb_join_path(char *dst, uint8_t dstsz, const char *dir, const char *name)
{
	uint8_t n = 0;
	uint8_t i = 0;

	if ( (dst == NULL) || (dstsz == 0u) )
		return;
	if ( (dir != NULL) && (dir[0] != '\0') )
	{
		while ( (n < (uint8_t)(dstsz - 1u)) && (dir[n] != '\0') )
		{
			dst[n] = dir[n];
			n++;
		}
		if ( n < (uint8_t)(dstsz - 1u) )
			dst[n++] = '/';
	}
	if ( name != NULL )
	{
		while ( (n < (uint8_t)(dstsz - 1u)) && (name[i] != '\0') )
		{
			dst[n] = name[i];
			n++;
			i++;
		}
	}
	dst[n] = '\0';
} // static void m1_irb_join_path(...)

/*============================================================================*/
/**
  * @brief  Decimal text of a 16-bit counter (bounded, no sprintf).
  */
/*============================================================================*/
static void m1_irb_u16_text(char *dst, uint8_t dstsz, uint16_t value)
{
	char tmp[5];
	uint8_t k = 0, n = 0;
	uint16_t v = value;

	if ( (dst == NULL) || (dstsz == 0u) )
		return;
	do
	{
		tmp[k++] = (char)('0' + (v % 10u));
		v /= 10u;
	} while ( (v != 0u) && (k < sizeof(tmp)) );
	while ( (k > 0u) && (n < (uint8_t)(dstsz - 1u)) )
	{
		k--;
		dst[n++] = tmp[k];
	}
	dst[n] = '\0';
} // static void m1_irb_u16_text(...)

/*============================================================================*/
/**
  * @brief  Aggregated strict-skip notice lines (one notice per file, never
  *         one per block): l1 = "N skipped" (count clamped at 9999 so the
  *         line fits beside the notice icon, see irb_notice), l2 = the
  *         first offending protocol name ("" when unknown).
  * @retval 1 when the lines are valid (something was skipped), 0 when the
  *         caller must suppress the notice.
  */
/*============================================================================*/
static uint8_t m1_irb_skip_lines(const S_M1_IRB_SkipAcc_t *acc, char *l1, uint8_t l1sz,
		char *l2, uint8_t l2sz)
{
	static const char suffix[] = " skipped";
	char num[6];
	uint8_t n = 0, i = 0;
	uint16_t shown;

	if ( (acc == NULL) || (acc->skipped == 0u) )
		return 0;
	if ( (l1 == NULL) || (l1sz == 0u) )
		return 0;
	/* line 1 renders beside the 48-px notice icon from x=54: "9999
	 * skipped" (12 chars) is the longest count that fits the 128-px
	 * display, so bigger aggregates clamp the displayed count. */
	shown = (acc->skipped > 9999u) ? 9999u : acc->skipped;
	m1_irb_u16_text(num, (uint8_t)sizeof(num), shown);
	while ( (n < (uint8_t)(l1sz - 1u)) && (num[n] != '\0') )
	{
		l1[n] = num[n];
		n++;
	}
	while ( (n < (uint8_t)(l1sz - 1u)) && (suffix[i] != '\0') )
	{
		l1[n++] = suffix[i++];
	}
	l1[n] = '\0';
	if ( (l2 != NULL) && (l2sz > 0u) )
	{
		i = 0;
		while ( (i < (uint8_t)(l2sz - 1u)) && (acc->first_name[i] != '\0') )
		{
			l2[i] = acc->first_name[i];
			i++;
		}
		l2[i] = '\0';
	}
	return 1;
} // static uint8_t m1_irb_skip_lines(...)
/*============================================================================*/
/**
  * @brief  Is a raw tick count transmittable? The OTA raw path keys its
  *         mark/space polarity off an ODD count that starts with the
  *         leading pause, so a real signal is odd AND longer than the
  *         lone pause. The loader's empty/zero-sample data-line results
  *         (raw_count == 0, the bare leading pause, or an even count --
  *         issue #74) must never reach the transmitter: they would play
  *         a bare pause or flip the polarity of every following frame.
  */
/*============================================================================*/
#define M1_IRB_TX_ERR_EMPTY_RAW	0x80u /* browser-local code, outside the
										* S_M1_IR_File_Error_t range */

static uint8_t m1_irb_raw_tx_ok(uint16_t raw_count)
{
	return (uint8_t)((raw_count >= 3u) && ((raw_count & 0x01u) != 0u));
} // static uint8_t m1_irb_raw_tx_ok(uint16_t raw_count)

/*============================================================================*/
/**
  * @brief  Favorites/recent store line handling (PR-6). Trim one store
  *         line in place: leading blanks skipped, trailing CR/LF/tab/
  *         space removed; returns the trimmed pointer.
  */
/*============================================================================*/
static char *m1_irb_list_trim(char *s)
{
	char *end;

	if ( s == NULL )
		return NULL;
	while ( (*s == ' ') || (*s == '\t') || (*s == '\r') || (*s == '\n') )
		s++;
	if ( *s == '\0' )
		return s;
	end = s;
	while ( end[1] != '\0' )
		end++;
	while ( (end >= s) && ((*end == ' ') || (*end == '\t') || (*end == '\r') || (*end == '\n')) )
		*end-- = '\0';
	return s;
} // static char *m1_irb_list_trim(char *s)

/*============================================================================*/
/**
  * @brief  Tolerant-load rule for one trimmed store line: keep it only
  *         when non-empty and strictly shorter than a bounded workspace
  *         row (NUL included). Blank/corrupt and over-long lines are
  *         skipped, never stored truncated.
  */
/*============================================================================*/
static uint8_t m1_irb_list_line_ok(const char *p)
{
	size_t len = 0;

	if ( (p == NULL) || (p[0] == '\0') )
		return 0;
	while ( p[len] != '\0' )
		len++;
	return (uint8_t)(len < (size_t)M1_IR_BROWSER_PATH_LEN_MAX);
} // static uint8_t m1_irb_list_line_ok(const char *p)

/*============================================================================*/
/**
  * @brief  Linear membership test on the bounded store workspace.
  */
/*============================================================================*/
static uint8_t m1_irb_list_contains(const char (*paths)[M1_IR_BROWSER_PATH_LEN_MAX],
		uint16_t count, const char *path)
{
	uint16_t i;

	if ( (paths == NULL) || (path == NULL) )
		return 0;
	for ( i = 0; i < count; i++ )
	{
		if ( strcmp(paths[i], path) == 0 )
			return 1;
	}
	return 0;
} // static uint8_t m1_irb_list_contains(...)

/*============================================================================*/
/**
  * @brief  Remove one record (full-path match); the rows below close the
  *         gap. @retval 1 when a row was removed.
  */
/*============================================================================*/
static uint8_t m1_irb_list_remove(char (*paths)[M1_IR_BROWSER_PATH_LEN_MAX],
		uint16_t *count, const char *path)
{
	uint16_t i, j;

	if ( (paths == NULL) || (count == NULL) || (path == NULL) )
		return 0;
	for ( i = 0; i < *count; i++ )
	{
		if ( strcmp(paths[i], path) == 0 )
		{
			for ( j = (uint16_t)(i + 1u); j < *count; j++ )
				m1_irb_join_path(paths[j - 1u], M1_IR_BROWSER_PATH_LEN_MAX, "", paths[j]);
			(*count)--;
			return 1;
		}
	}
	return 0;
} // static uint8_t m1_irb_list_remove(...)

/*============================================================================*/
/**
  * @brief  LRU insert at the front of the bounded store: an equal row is
  *         removed first (dedup), the rows shift down, the least recent
  *         row beyond the cap is dropped. *count never exceeds cap.
  */
/*============================================================================*/
static void m1_irb_list_add_front(char (*paths)[M1_IR_BROWSER_PATH_LEN_MAX],
		uint16_t *count, uint16_t cap, const char *path)
{
	uint16_t i;

	if ( (paths == NULL) || (count == NULL) || (path == NULL) || (cap == 0u) )
		return;
	(void)m1_irb_list_remove(paths, count, path);
	if ( *count >= cap )
		*count = (uint16_t)(cap - 1u); /* dropped row = the LRU tail */
	for ( i = *count; i > 0u; i-- )
		m1_irb_join_path(paths[i], M1_IR_BROWSER_PATH_LEN_MAX, "", paths[i - 1u]);
	m1_irb_join_path(paths[0], M1_IR_BROWSER_PATH_LEN_MAX, "", path);
	(*count)++;
} // static void m1_irb_list_add_front(...)

/*============================================================================*/
/**
  * @brief  Legacy list-row label of a stored device path: the basename
  *         (text after the last '/'), trailing ".ir" stripped, truncated
  *         to the row buffer (fork ir_list_from_file intent).
  */
/*============================================================================*/
static void m1_irb_basename_label(char *dst, uint8_t dstsz, const char *path)
{
	const char *base = path;
	const char *p;

	if ( (dst == NULL) || (dstsz == 0u) )
		return;
	if ( path != NULL )
	{
		for ( p = path; *p != '\0'; p++ )
		{
			if ( *p == '/' )
				base = p + 1u;
		}
	}
	m1_irb_row_label(dst, dstsz, base);
} // static void m1_irb_basename_label(...)

/*============================================================================*/
/**
  * @brief  Button-view title of one device: the legacy "* " marker when
  *         the device is a favorite, then the device label, truncated.
  */
/*============================================================================*/
static void m1_irb_dev_title(char *dst, uint8_t dstsz, const char *title, uint8_t fav)
{
	uint8_t n = 0;

	if ( (dst == NULL) || (dstsz == 0u) )
		return;
	if ( (fav != 0u) && (dstsz > 2u) )
	{
		dst[n++] = '*';
		dst[n++] = ' ';
	}
	if ( title != NULL )
	{
		while ( (n < (uint8_t)(dstsz - 1u)) && (*title != '\0') )
			dst[n++] = *title++;
	}
	dst[n] = '\0';
} // static void m1_irb_dev_title(...)
/* ====================== M1_IR_BROWSER_PURE_CORE_END ====================== */

/**************************** V A R I A B L E S ******************************/

/* Module-static workspaces (UI task only, no run-time allocation). */
static char irb_cat_names[M1_IR_BROWSER_CATS_MAX][M1_IR_BROWSER_NAME_LEN_MAX];
static char irb_dev_names[M1_IR_BROWSER_DEVICES_MAX][M1_IR_BROWSER_NAME_LEN_MAX];
static char irb_dev_paths[M1_IR_BROWSER_DEVICES_MAX][M1_IR_BROWSER_PATH_LEN_MAX];
static S_M1_IR_File_Entry_t irb_entries[M1_IR_BROWSER_BUTTONS_MAX];
static uint16_t irb_raw_ticks[M1_IR_BROWSER_RAW_TICKS_MAX];
/* Durable protocol-name copy for the parsed TX publish: ir_universal_payload
 * is an upstream global that outlives irb_transmit's frame, so pprotocol
 * must never point at the stack-local signal (mirrors the "raw" literal). */
static char irb_tx_proto_name[M1_IR_FILE_PROTO_LEN_MAX + 1u];
static char irb_query[M1_IR_BROWSER_NAME_LEN_MAX];
static uint16_t irb_cat_count;
static uint16_t irb_dev_count;
static S_M1_IRB_SkipAcc_t irb_skip_acc;
static S_M1_IR_File_Stats_t irb_file_stats;

/* Favorites/recent workspace (PR-6): one shared bounded buffer serves
 * both stores (the recent cap is below the favorites cap) and their
 * screens; loads rewrite it whole. UI task only, no run-time allocation. */
static char irb_list_paths[M1_IR_BROWSER_FAVORITES_MAX][M1_IR_BROWSER_PATH_LEN_MAX];
static char irb_list_names[M1_IR_BROWSER_FAVORITES_MAX][M1_IR_BROWSER_NAME_LEN_MAX];

static const char *irb_dash_rows[4] = {"Favorites", "Recent", "Browse files", "Search"};

/* Per-depth scan state: the recursive walk never puts a DIR/FILINFO on the
 * task stack, depth is capped by M1_IR_BROWSER_DEPTH_MAX. */
static DIR irb_scan_dirs[M1_IR_BROWSER_DEPTH_MAX + 1u];
static FILINFO irb_scan_fis[M1_IR_BROWSER_DEPTH_MAX + 1u];
static char irb_scan_paths[M1_IR_BROWSER_DEPTH_MAX + 1u][M1_IR_BROWSER_PATH_LEN_MAX];

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void irb_draw_list(const char *title, uint16_t count, const S_M1_IRB_Win_t *w,
		const char *(*row_get)(uint16_t idx), uint8_t truncated);
static void irb_notice(const char *line1, const char *line2, uint32_t delay_ms);
static void irb_draw_empty_library(void);
static void irb_draw_hourglass(void);
static void irb_draw_sending(const char *name);
static const char *irb_row_cat(uint16_t idx);
static const char *irb_row_dev(uint16_t idx);
static const char *irb_row_entry(uint16_t idx);
static const char *irb_row_dash(uint16_t idx);
static const char *irb_row_stored(uint16_t idx);
static S_M1_IRB_Scan_Status_t irb_scan_categories(uint8_t *truncated);
static S_M1_IRB_Scan_Status_t irb_scan_rec(const char *dir_path, const char *rel,
		uint8_t depth, uint8_t *truncated, uint8_t *io_error);
static S_M1_IRB_Scan_Status_t irb_scan_devices(const char *cat, uint8_t *truncated);
static S_M1_IRB_Scan_Status_t irb_scan_search_rec(const char *dir_path, const char *rel,
		const char *lower_query, uint8_t depth, uint8_t *truncated, uint8_t *io_error);
static S_M1_IRB_Scan_Status_t irb_scan_search(const char *lower_query, uint8_t *truncated);
static void irb_notice_hook(const char *protocol_name, uint32_t file_offset);
static int32_t irb_index_device(const char *path);
static uint8_t irb_transmit(const char *path, uint16_t entry_idx);
static uint16_t irb_store_load(const char *file, uint16_t cap);
static void irb_store_save(const char *file, uint16_t count);
static void irb_recent_add(const char *path);
static uint8_t irb_fav_is(const char *path);
static uint8_t irb_fav_toggle(const char *path);
static void irb_screen_dashboard(void);
static void irb_screen_categories(void);
static void irb_screen_devices(const char *cat);
static void irb_screen_stored_list(const char *title, const char *file, uint16_t cap);
static uint8_t irb_screen_buttons(const char *path, const char *title);
static void irb_screen_search(void);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief  Draw a 6-row paginated list: inverted title bar, selected-row
  *         highlight, scrollbar when the list overflows, a trailing "+"
  *         in the title when the scan hit its result cap.
  */
/*============================================================================*/
static void irb_draw_list(const char *title, uint16_t count, const S_M1_IRB_Win_t *w,
		const char *(*row_get)(uint16_t idx), uint8_t truncated)
{
	char line[M1_IRB_COL_MAX + 1u];
	char head[M1_IRB_COL_MAX + 1u];
	const char *text;
	uint16_t row;
	uint32_t bar_h, span, bar_y;
	uint8_t disp_row, y;

	m1_u8g2_firstpage();
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);

	/* Title bar (inverted) */
	(void)snprintf(head, sizeof(head), "%s%s", (title != NULL) ? title : "", truncated ? " +" : "");
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_DrawBox(&m1_u8g2, 0, 0, 128, M1_IRB_TITLE_H - 1u);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
	u8g2_DrawStr(&m1_u8g2, 1, 8, head);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

	/* Rows */
	disp_row = 0;
	for ( row = w->top; (row < count) && (disp_row < M1_IR_BROWSER_ROWS); row++, disp_row++ )
	{
		text = row_get(row);
		y = (uint8_t)(M1_IRB_TITLE_H + (uint16_t)disp_row * M1_IRB_ROW_H);
		if ( row == w->sel )
		{
			u8g2_DrawBox(&m1_u8g2, 0, (int)(y - 1), 124, M1_IRB_ROW_H);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
		}
		(void)snprintf(line, sizeof(line), "%s", (text != NULL) ? text : "");
		u8g2_DrawStr(&m1_u8g2, 1, (int)(y + 7), line);
		if ( row == w->sel )
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	}

	/* Scrollbar when the list overflows the visible rows */
	if ( count > (uint16_t)M1_IR_BROWSER_ROWS )
	{
		bar_h = ((uint32_t)M1_IR_BROWSER_ROWS * 54u) / count;
		if ( bar_h < 4u )
			bar_h = 4u;
		span = 54u - bar_h;
		bar_y = 10u + ((uint32_t)w->top * span) / (uint32_t)(count - M1_IR_BROWSER_ROWS);
		if ( bar_y > 63u )
			bar_y = 63u;
		u8g2_DrawFrame(&m1_u8g2, 125, 10, 3, 54);
		u8g2_DrawBox(&m1_u8g2, 126, (int)bar_y, 1, (int)bar_h);
	}
	m1_u8g2_nextpage(); // Update display RAM
} // static void irb_draw_list(...)

/*============================================================================*/
/**
  * @brief  Brief two-line notice screen with the remote icon (legacy
  *         ir_ui_show_notice intent: short, non-modal feedback).
  */
/*============================================================================*/
static void irb_notice(const char *line1, const char *line2, uint32_t delay_ms)
{
	m1_u8g2_firstpage();
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	u8g2_DrawXBMP(&m1_u8g2, 2, 2, 48, 25, remote_48x25);
	u8g2_DrawStr(&m1_u8g2, 54, 15, (line1 != NULL) ? line1 : "");
	u8g2_DrawStr(&m1_u8g2, 2, 40, (line2 != NULL) ? line2 : "");
	m1_u8g2_nextpage(); // Update display RAM

	if ( delay_ms > 0u )
		vTaskDelay(pdMS_TO_TICKS(delay_ms));
} // static void irb_notice(...)

/*============================================================================*/
/**
  * @brief  Empty-library hint: 0:/IR is missing or holds no categories.
  *         Actionable text (what to copy, where), dismissed with BACK.
  */
/*============================================================================*/
static void irb_draw_empty_library(void)
{
	m1_u8g2_firstpage();
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	u8g2_DrawXBMP(&m1_u8g2, 2, 2, 48, 25, remote_48x25);
	u8g2_DrawStr(&m1_u8g2, 54, 15, "No IR");
	u8g2_DrawStr(&m1_u8g2, 54, 25, "database");
	u8g2_DrawStr(&m1_u8g2, 2, 40, "Copy Flipper IRDB");
	u8g2_DrawStr(&m1_u8g2, 2, 50, "to 0:/IR on SD");
	u8g2_DrawStr(&m1_u8g2, 2, 60, "Then reopen browser");
	m1_u8g2_nextpage(); // Update display RAM
	m1_wait_back_to_exit();
} // static void irb_draw_empty_library(void)

/*============================================================================*/
/**
  * @brief  Hourglass screen shown while SD scans / indexing run.
  */
/*============================================================================*/
static void irb_draw_hourglass(void)
{
	m1_u8g2_firstpage();
	u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH / 2 - 9, M1_LCD_DISPLAY_HEIGHT / 2 - 16, 18, 32, hourglass_18x32);
	m1_u8g2_nextpage(); // Update display RAM
} // static void irb_draw_hourglass(void)

/*============================================================================*/
/**
  * @brief  "Sending" screen while a selected button transmits.
  */
/*============================================================================*/
static void irb_draw_sending(const char *name)
{
	char line[M1_IRB_COL_MAX + 1u];

	m1_u8g2_firstpage();
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	u8g2_DrawXBMP(&m1_u8g2, 2, 2, 48, 25, remote_48x25);
	u8g2_DrawStr(&m1_u8g2, 54, 15, "Sending...");
	(void)snprintf(line, sizeof(line), "%s", (name != NULL) ? name : "");
	u8g2_DrawStr(&m1_u8g2, 2, 40, line);
	m1_u8g2_nextpage(); // Update display RAM
} // static void irb_draw_sending(const char *name)

/*============================================================================*/
/**
  * @brief  Row providers for irb_draw_list (module-static workspaces).
  */
/*============================================================================*/
static const char *irb_row_cat(uint16_t idx)
{
	return (idx < M1_IR_BROWSER_CATS_MAX) ? irb_cat_names[idx] : "";
} // static const char *irb_row_cat(uint16_t idx)

static const char *irb_row_dev(uint16_t idx)
{
	return (idx < M1_IR_BROWSER_DEVICES_MAX) ? irb_dev_names[idx] : "";
} // static const char *irb_row_dev(uint16_t idx)

static const char *irb_row_entry(uint16_t idx)
{
	return (idx < M1_IR_BROWSER_BUTTONS_MAX) ? irb_entries[idx].name : "";
} // static const char *irb_row_entry(uint16_t idx)

static const char *irb_row_dash(uint16_t idx)
{
	return (idx < 4u) ? irb_dash_rows[idx] : "";
} // static const char *irb_row_dash(uint16_t idx)

static const char *irb_row_stored(uint16_t idx)
{
	return (idx < M1_IR_BROWSER_FAVORITES_MAX) ? irb_list_names[idx] : "";
} // static const char *irb_row_stored(uint16_t idx)

/*============================================================================*/
/**
  * @brief  Scan the category level: plain sub-directories of 0:/IR (the
  *         Flipper IRDB top level). Bounded to M1_IR_BROWSER_CATS_MAX;
  *         *truncated is set when the directory holds more.
  */
/*============================================================================*/
static S_M1_IRB_Scan_Status_t irb_scan_categories(uint8_t *truncated)
{
	DIR *dir = &irb_scan_dirs[0];
	FILINFO *fi = &irb_scan_fis[0];
	FRESULT fret;

	irb_cat_count = 0;
	if ( truncated != NULL )
		*truncated = 0;

	fret = m1_fb_open_dir(dir, M1_IRB_SD_ROOT);
	if ( fret != FR_OK )
		return M1_IRB_SCAN_IO;

	while ( 1 )
	{
		fret = f_readdir(dir, fi);
		if ( fret != FR_OK )
		{
			f_closedir(dir);
			return M1_IRB_SCAN_IO;
		}
		if ( fi->fname[0] == '\0' )
			break; /* end of directory */
		if ( (fi->fname[0] == '.') || (fi->fattrib & (AM_HID | AM_SYS)) )
			continue;
		if ( !(fi->fattrib & AM_DIR) )
			continue; /* category rows are directories only */
		if ( !m1_irb_cat_name_ok(fi->fname) )
			continue; /* over-long category name: skipped during the scan
						* (a truncated label would build an unopenable
						* scan root), see docs/ir-provisioning.md */

		if ( irb_cat_count >= M1_IR_BROWSER_CATS_MAX )
		{
			if ( truncated != NULL )
				*truncated = 1;
			break;
		}
		m1_irb_row_label(irb_cat_names[irb_cat_count], M1_IR_BROWSER_NAME_LEN_MAX, fi->fname);
		irb_cat_count++;
	}
	f_closedir(dir);

	return (irb_cat_count > 0u) ? M1_IRB_SCAN_DATA : M1_IRB_SCAN_EMPTY;
} // static S_M1_IRB_Scan_Status_t irb_scan_categories(uint8_t *truncated)

/*============================================================================*/
/**
  * @brief  Recursive device scan under one category: .ir files at any
  *         depth up to M1_IR_BROWSER_DEPTH_MAX (the Flipper IRDB layout is
  *         Brand/Device.ir; deeper trees are tolerated). Row labels keep
  *         the path relative to the category (rel), so "Brand/Device"
  *         stays distinguishable from same-named files of other brands;
  *         the full SD path of each device is kept in irb_dev_paths.
  */
/*============================================================================*/
static S_M1_IRB_Scan_Status_t irb_scan_rec(const char *dir_path, const char *rel,
		uint8_t depth, uint8_t *truncated, uint8_t *io_error)
{
	DIR *dir;
	FILINFO *fi;
	char *child_path;
	char child_rel[M1_IR_BROWSER_NAME_LEN_MAX];
	FRESULT fret;

	if ( (depth > M1_IR_BROWSER_DEPTH_MAX) || (irb_dev_count >= M1_IR_BROWSER_DEVICES_MAX) )
		return M1_IRB_SCAN_EMPTY;

	dir = &irb_scan_dirs[depth];
	fi = &irb_scan_fis[depth];
	child_path = irb_scan_paths[depth];

	fret = m1_fb_open_dir(dir, dir_path);
	if ( fret != FR_OK )
	{
		*io_error = 1;
		return M1_IRB_SCAN_IO;
	}

	while ( irb_dev_count < M1_IR_BROWSER_DEVICES_MAX )
	{
		fret = f_readdir(dir, fi);
		if ( fret != FR_OK )
		{
			*io_error = 1;
			f_closedir(dir);
			return M1_IRB_SCAN_IO;
		}
		if ( fi->fname[0] == '\0' )
			break; /* end of directory */
		if ( (fi->fname[0] == '.') || (fi->fattrib & (AM_HID | AM_SYS)) )
			continue;
		if ( strlen(dir_path) + 1u + strlen(fi->fname) >= (size_t)M1_IR_BROWSER_PATH_LEN_MAX )
			continue; /* path beyond the bounded buffer: skipped */

		if ( fi->fattrib & AM_DIR )
		{
			m1_irb_join_path(child_path, M1_IR_BROWSER_PATH_LEN_MAX, dir_path, fi->fname);
			m1_irb_join_path(child_rel, (uint8_t)sizeof(child_rel), rel, fi->fname);
			(void)irb_scan_rec(child_path, child_rel, (uint8_t)(depth + 1u), truncated, io_error);
		}
		else if ( m1_irb_is_ir_file(fi->fname) )
		{
			m1_irb_join_path(irb_dev_paths[irb_dev_count], M1_IR_BROWSER_PATH_LEN_MAX, dir_path, fi->fname);
			m1_irb_join_path(irb_dev_names[irb_dev_count], M1_IR_BROWSER_NAME_LEN_MAX, rel, fi->fname);
			m1_irb_row_label(irb_dev_names[irb_dev_count], M1_IR_BROWSER_NAME_LEN_MAX, irb_dev_names[irb_dev_count]);
			irb_dev_count++;
		}
	}

	if ( irb_dev_count >= M1_IR_BROWSER_DEVICES_MAX )
	{
		/* look ahead: anything left in this directory means truncation */
		fret = f_readdir(dir, fi);
		if ( (fret == FR_OK) && (fi->fname[0] != '\0') )
		{
			if ( truncated != NULL )
				*truncated = 1;
		}
	}
	f_closedir(dir);
	return M1_IRB_SCAN_EMPTY;
} // static S_M1_IRB_Scan_Status_t irb_scan_rec(...)

/*============================================================================*/
/**
  * @brief  Build the device list of one category (see irb_scan_rec).
  */
/*============================================================================*/
static S_M1_IRB_Scan_Status_t irb_scan_devices(const char *cat, uint8_t *truncated)
{
	char root[M1_IR_BROWSER_PATH_LEN_MAX];
	uint8_t io_error = 0;

	irb_dev_count = 0;
	if ( truncated != NULL )
		*truncated = 0;

	m1_irb_join_path(root, (uint8_t)sizeof(root), M1_IRB_SD_ROOT, cat);
	(void)irb_scan_rec(root, "", 0u, truncated, &io_error);

	if ( (irb_dev_count == 0u) && io_error )
		return M1_IRB_SCAN_IO;
	return (irb_dev_count > 0u) ? M1_IRB_SCAN_DATA : M1_IRB_SCAN_EMPTY;
} // static S_M1_IRB_Scan_Status_t irb_scan_devices(...)

/*============================================================================*/
/**
  * @brief  Recursive search of the whole 0:/IR tree for .ir file names
  *         matching the pre-lowercased query. Row labels keep the path
  *         relative to 0:/IR (rel starts at the category directory), so
  *         results remain identifiable when several categories match.
  */
/*============================================================================*/
static S_M1_IRB_Scan_Status_t irb_scan_search_rec(const char *dir_path, const char *rel,
		const char *lower_query, uint8_t depth, uint8_t *truncated, uint8_t *io_error)
{
	DIR *dir;
	FILINFO *fi;
	char *child_path;
	char child_rel[M1_IR_BROWSER_NAME_LEN_MAX];
	FRESULT fret;

	if ( (depth > M1_IR_BROWSER_DEPTH_MAX) || (irb_dev_count >= M1_IR_BROWSER_DEVICES_MAX) )
		return M1_IRB_SCAN_EMPTY;

	dir = &irb_scan_dirs[depth];
	fi = &irb_scan_fis[depth];
	child_path = irb_scan_paths[depth];

	fret = m1_fb_open_dir(dir, dir_path);
	if ( fret != FR_OK )
	{
		*io_error = 1;
		return M1_IRB_SCAN_IO;
	}

	while ( irb_dev_count < M1_IR_BROWSER_DEVICES_MAX )
	{
		fret = f_readdir(dir, fi);
		if ( fret != FR_OK )
		{
			*io_error = 1;
			f_closedir(dir);
			return M1_IRB_SCAN_IO;
		}
		if ( fi->fname[0] == '\0' )
			break;
		if ( (fi->fname[0] == '.') || (fi->fattrib & (AM_HID | AM_SYS)) )
			continue;
		if ( strlen(dir_path) + 1u + strlen(fi->fname) >= (size_t)M1_IR_BROWSER_PATH_LEN_MAX )
			continue;

		if ( fi->fattrib & AM_DIR )
		{
			m1_irb_join_path(child_path, M1_IR_BROWSER_PATH_LEN_MAX, dir_path, fi->fname);
			m1_irb_join_path(child_rel, (uint8_t)sizeof(child_rel), rel, fi->fname);
			(void)irb_scan_search_rec(child_path, child_rel, lower_query,
					(uint8_t)(depth + 1u), truncated, io_error);
		}
		else if ( m1_irb_is_ir_file(fi->fname) && m1_irb_search_match(fi->fname, lower_query) )
		{
			m1_irb_join_path(irb_dev_paths[irb_dev_count], M1_IR_BROWSER_PATH_LEN_MAX, dir_path, fi->fname);
			m1_irb_join_path(irb_dev_names[irb_dev_count], M1_IR_BROWSER_NAME_LEN_MAX, rel, fi->fname);
			m1_irb_row_label(irb_dev_names[irb_dev_count], M1_IR_BROWSER_NAME_LEN_MAX, irb_dev_names[irb_dev_count]);
			irb_dev_count++;
		}
	}

	if ( irb_dev_count >= M1_IR_BROWSER_DEVICES_MAX )
	{
		fret = f_readdir(dir, fi);
		if ( (fret == FR_OK) && (fi->fname[0] != '\0') )
		{
			if ( truncated != NULL )
				*truncated = 1;
		}
	}
	f_closedir(dir);
	return M1_IRB_SCAN_EMPTY;
} // static S_M1_IRB_Scan_Status_t irb_scan_search_rec(...)

/*============================================================================*/
/**
  * @brief  Build the search result list over the whole database.
  */
/*============================================================================*/
static S_M1_IRB_Scan_Status_t irb_scan_search(const char *lower_query, uint8_t *truncated)
{
	uint8_t io_error = 0;

	irb_dev_count = 0;
	if ( truncated != NULL )
		*truncated = 0;

	(void)irb_scan_search_rec(M1_IRB_SD_ROOT, "", lower_query, 0u, truncated, &io_error);

	if ( (irb_dev_count == 0u) && io_error )
		return M1_IRB_SCAN_IO;
	return (irb_dev_count > 0u) ? M1_IRB_SCAN_DATA : M1_IRB_SCAN_EMPTY;
} // static S_M1_IRB_Scan_Status_t irb_scan_search(...)

/*============================================================================*/
/**
  * @brief  Tolerant load of one favorites/recent store into the shared
  *         bounded workspace (paths + derived row labels). A missing or
  *         unreadable file, blank/corrupt lines, over-long lines and
  *         everything past the cap are skipped: the caller renders an
  *         empty list, never an error path (issue #43 part 3).
  * @retval stored record count (0 .. cap).
  */
/*============================================================================*/
static uint16_t irb_store_load(const char *file, uint16_t cap)
{
	char line[M1_IR_BROWSER_PATH_LEN_MAX + 4];
	char *p;
	uint16_t count = 0;
	FIL f;

	if ( cap > M1_IR_BROWSER_FAVORITES_MAX )
		cap = M1_IR_BROWSER_FAVORITES_MAX; /* the workspace bound */

	if ( f_open(&f, file, FA_READ) != FR_OK )
		return 0; /* missing or unreadable: empty list, no error screen */

	while ( count < cap )
	{
		if ( f_gets(line, (int)sizeof(line), &f) == NULL )
			break; /* end of file (or read error: keep what loaded) */
		if ( (strchr(line, '\n') == NULL) && !f_eof(&f) )
		{
			/* over-long line: drain the rest of the record and skip it
			 * (a truncated path could never be re-opened) */
			do
			{
				if ( f_gets(line, (int)sizeof(line), &f) == NULL )
					break;
			} while ( strchr(line, '\n') == NULL );
			continue;
		}
		p = m1_irb_list_trim(line);
		if ( !m1_irb_list_line_ok(p) )
			continue; /* blank, corrupt or over-long: skipped */
		m1_irb_join_path(irb_list_paths[count], M1_IR_BROWSER_PATH_LEN_MAX, "", p);
		m1_irb_basename_label(irb_list_names[count], M1_IR_BROWSER_NAME_LEN_MAX,
				irb_list_paths[count]);
		count++;
	}
	f_close(&f);
	return count;
} // static uint16_t irb_store_load(...)

/*============================================================================*/
/**
  * @brief  Bounded whole-file rewrite of one store: at most `count` rows,
  *         one full device path per line (legacy fork format). 0:/System
  *         is ensured lazily here (the settings store's folder policy).
  *         A failed write is silent-safe: the lists keep working for the
  *         rest of the session and the next toggle/transmit rewrites the
  *         file. Filesystem-only -- called between transmits, never from
  *         inside the transmit path.
  */
/*============================================================================*/
static void irb_store_save(const char *file, uint16_t count)
{
	FIL f;
	uint16_t i;

	(void)f_mkdir(M1_SD_DIR_SYSTEM); /* lazy ensure: may already exist or
										* fail on a read-only card */
	if ( count > M1_IR_BROWSER_FAVORITES_MAX )
		count = M1_IR_BROWSER_FAVORITES_MAX;
	if ( f_open(&f, file, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK )
	{
		M1_LOG_E(M1_LOGDB_TAG, "store write failed: %s\r\n", file);
		return;
	}
	for ( i = 0; i < count; i++ )
		(void)f_printf(&f, "%s\n", irb_list_paths[i]);
	f_close(&f);
} // static void irb_store_save(...)

/*============================================================================*/
/**
  * @brief  Record a transmit in the recent store (LRU: newest at the
  *         front, dedup, cap M1_IR_BROWSER_RECENT_MAX).
  */
/*============================================================================*/
static void irb_recent_add(const char *path)
{
	uint16_t n;

	if ( (path == NULL) || (path[0] == '\0') )
		return;
	n = irb_store_load(M1_IRB_RECENT_FILE, M1_IR_BROWSER_RECENT_MAX);
	m1_irb_list_add_front(irb_list_paths, &n, M1_IR_BROWSER_RECENT_MAX, path);
	irb_store_save(M1_IRB_RECENT_FILE, n);
} // static void irb_recent_add(const char *path)

/*============================================================================*/
/**
  * @brief  Is this device path in the favorites store?
  */
/*============================================================================*/
static uint8_t irb_fav_is(const char *path)
{
	uint16_t n;

	if ( (path == NULL) || (path[0] == '\0') )
		return 0;
	n = irb_store_load(M1_IRB_FAV_FILE, M1_IR_BROWSER_FAVORITES_MAX);
	return m1_irb_list_contains(irb_list_paths, n, path);
} // static uint8_t irb_fav_is(const char *path)

/*============================================================================*/
/**
  * @brief  Toggle one device in the favorites store (bounded rewrite)
  *         with the legacy short feedback notice.
  * @retval 1 when the device is a favorite now, 0 when it was removed.
  */
/*============================================================================*/
static uint8_t irb_fav_toggle(const char *path)
{
	uint16_t n;
	uint8_t fav;

	if ( (path == NULL) || (path[0] == '\0') )
		return 0;
	n = irb_store_load(M1_IRB_FAV_FILE, M1_IR_BROWSER_FAVORITES_MAX);
	if ( m1_irb_list_contains(irb_list_paths, n, path) )
	{
		(void)m1_irb_list_remove(irb_list_paths, &n, path);
		fav = 0;
	}
	else
	{
		m1_irb_list_add_front(irb_list_paths, &n, M1_IR_BROWSER_FAVORITES_MAX, path);
		fav = 1;
	}
	irb_store_save(M1_IRB_FAV_FILE, n);
	irb_notice("Favorite", (fav != 0u) ? "added" : "removed", M1_IRB_NOTICE_MS_SHORT);
	return fav;
} // static uint8_t irb_fav_toggle(const char *path)

/*============================================================================*/
/**
  * @brief  PR-4 strict-skip notice hook: aggregate into irb_skip_acc.
  */
/*============================================================================*/
static void irb_notice_hook(const char *protocol_name, uint32_t file_offset)
{
	(void)file_offset; /* the aggregate notice names protocols, not offsets */
	m1_irb_skip_add(&irb_skip_acc, protocol_name);
} // static void irb_notice_hook(...)

/*============================================================================*/
/**
  * @brief  Index one device file through the PR-4 streaming loader (this
  *         is the button list). Returns the entry count, or a negative
  *         S_M1_IR_File_Error_t when the file cannot be read at all.
  */
/*============================================================================*/
static int32_t irb_index_device(const char *path)
{
	memset(&irb_file_stats, 0, sizeof(irb_file_stats));
	memset(&irb_skip_acc, 0, sizeof(irb_skip_acc));
	return m1_ir_file_index(path, irb_entries, M1_IR_BROWSER_BUTTONS_MAX,
			&irb_file_stats, irb_notice_hook);
} // static int32_t irb_index_device(const char *path)

/*============================================================================*/
/**
  * @brief  Transmit one indexed button through the upstream play path:
  *         on-demand load via m1_ir_file_signal_load, publish into
  *         ir_universal_payload exactly like ir_remote_function_play,
  *         then irsnd_generate_tx_data + infrared_transmit.
  * @retval 0 when the frame was started, otherwise the (positive)
  *         S_M1_IR_File_Error_t of the failed load (the caller maps it
  *         to a plain notice).
  */
/*============================================================================*/
static uint8_t irb_transmit(const char *path, uint16_t entry_idx)
{
	S_M1_IR_File_Signal_t sig;
	IRMP_DATA ir_data;
	uint8_t proto_no;
	uint8_t i;
	int32_t got;

	got = m1_ir_file_signal_load(path, &irb_entries[entry_idx], &sig,
			irb_raw_ticks, M1_IR_BROWSER_RAW_TICKS_MAX);
	if ( got != 0 )
		return (uint8_t)(-got);

	memset(&ir_data, 0, sizeof(ir_data));
	memset(&ir_universal_payload, 0, sizeof(ir_universal_payload));
	ir_universal_payload.frequency = sig.frequency;
	ir_universal_payload.duty_cycle = sig.duty_cycle;

	if ( sig.flags & M1_IR_FILE_FLAG_RAW )
	{
		if ( !m1_irb_raw_tx_ok(sig.raw_count) ) /* empty/zero-sample raw block
												* (issue #74): never transmit */
			return M1_IRB_TX_ERR_EMPTY_RAW;
		ir_universal_payload.ir_data_type = IR_DATATYPE_RAW;
		ir_universal_payload.rawdata_counter = sig.raw_count;
		ir_universal_payload.pprawdata = sig.raw_ticks;
		ir_universal_payload.pprotocol = (uint8_t *)"raw";
		ir_data.protocol = IRMP_RAW_PROTOCOL;
	}
	else
	{
		if ( ir_protocol_map_lookup(sig.protocol_name, &proto_no) )
			return (uint8_t)M1_IR_FILE_ERR_UNKNOWN_PROTOCOL; /* strict-skip held at load too */
		if ( proto_no == IRMP_RAW_PROTOCOL )
			return M1_IRB_TX_ERR_EMPTY_RAW; /* "protocol: raw" on a parsed block carries
											* no timings (issue #74): raw_count == 0 */
		ir_universal_payload.ir_data_type = IR_DATATYPE_PARSED;
		/* ir_universal_payload outlives this frame: copy the protocol name
		 * into the module-static buffer instead of pointing pprotocol at
		 * the stack-local sig (same value, storage that never dies) */
		i = 0u;
		while ( (i < M1_IR_FILE_PROTO_LEN_MAX) && (sig.protocol_name[i] != '\0') )
		{
			irb_tx_proto_name[i] = sig.protocol_name[i];
			i++;
		}
		irb_tx_proto_name[i] = '\0';
		ir_universal_payload.pprotocol = (uint8_t *)irb_tx_proto_name;
		ir_universal_payload.address = sig.address;
		ir_universal_payload.command = sig.command;
		ir_data.protocol = proto_no;
		ir_data.address = sig.address;
		ir_data.command = sig.command;
	}
	ir_data.duty_cycle = sig.duty_cycle;
	ir_data.frequency = sig.frequency;
	ir_data.flags = 1; // don't repeat frame

	irsnd_init(&timerhdl_ir_carrier, IR_ENCODE_TIMER_TX_CHANNEL); // Reinit the timer before the new transmission
	irsnd_generate_tx_data(ir_data); // make ota data
	infrared_transmit(1, ir_data.protocol); // initialize the tx
	infrared_transmit(0, 0); // start the first frame
	return 0;
} // static uint8_t irb_transmit(...)

/*============================================================================*/
/**
  * @brief  Dashboard: the browser's top screen (legacy intent). The
  *         favorites/recent rows (PR-6) open the persisted stores below;
  *         "Browse files"/"Search" are the PR-5 flows.
  */
/*============================================================================*/
static void irb_screen_dashboard(void)
{
	S_M1_Buttons_Status bs;
	S_M1_Main_Q_t q_item;
	S_M1_IRB_Win_t w = {0, 0};
	BaseType_t ret;

	irb_draw_list("IR Database", 4u, &w, irb_row_dash, 0u);

	while ( 1 )
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if ( ret != pdTRUE )
			continue;
		if ( q_item.q_evt_type != Q_EVENT_KEYPAD )
			continue;
		ret = xQueueReceive(button_events_q_hdl, &bs, 0);
		if ( ret != pdTRUE )
			continue;

		if ( bs.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK )
		{
			xQueueReset(main_q_hdl);
			return;
		}
		else if ( bs.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, 4u, -1);
			irb_draw_list("IR Database", 4u, &w, irb_row_dash, 0u);
		}
		else if ( bs.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, 4u, 1);
			irb_draw_list("IR Database", 4u, &w, irb_row_dash, 0u);
		}
		else if ( bs.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK )
		{
			if ( w.sel == 0u )
				irb_screen_stored_list("Favorites", M1_IRB_FAV_FILE,
						M1_IR_BROWSER_FAVORITES_MAX);
			else if ( w.sel == 1u )
				irb_screen_stored_list("Recent", M1_IRB_RECENT_FILE,
						M1_IR_BROWSER_RECENT_MAX);
			else if ( w.sel == 2u )
				irb_screen_categories();
			else
				irb_screen_search();
			irb_draw_list("IR Database", 4u, &w, irb_row_dash, 0u);
		}
	}
} // static void irb_screen_dashboard(void)

/*============================================================================*/
/**
  * @brief  Category list screen (level 1 of the browse flow).
  */
/*============================================================================*/
static void irb_screen_categories(void)
{
	S_M1_Buttons_Status bs;
	S_M1_Main_Q_t q_item;
	S_M1_IRB_Win_t w = {0, 0};
	S_M1_IRB_Scan_Status_t st;
	BaseType_t ret;
	uint8_t truncated = 0;

	irb_draw_hourglass();
	st = irb_scan_categories(&truncated);

	if ( st == M1_IRB_SCAN_IO )
	{
		irb_notice("Scan failed", "Check SD card", M1_IRB_NOTICE_MS_DEFAULT);
		return;
	}
	if ( st == M1_IRB_SCAN_EMPTY )
	{
		irb_draw_empty_library(); /* actionable hint, waits for BACK */
		return;
	}

	irb_draw_list("Categories", irb_cat_count, &w, irb_row_cat, truncated);

	while ( 1 )
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if ( ret != pdTRUE )
			continue;
		if ( q_item.q_evt_type != Q_EVENT_KEYPAD )
			continue;
		ret = xQueueReceive(button_events_q_hdl, &bs, 0);
		if ( ret != pdTRUE )
			continue;

		if ( bs.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK )
		{
			xQueueReset(main_q_hdl);
			return;
		}
		else if ( bs.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, irb_cat_count, -1);
			irb_draw_list("Categories", irb_cat_count, &w, irb_row_cat, truncated);
		}
		else if ( bs.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, irb_cat_count, 1);
			irb_draw_list("Categories", irb_cat_count, &w, irb_row_cat, truncated);
		}
		else if ( bs.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, irb_cat_count, -M1_IR_BROWSER_ROWS);
			irb_draw_list("Categories", irb_cat_count, &w, irb_row_cat, truncated);
		}
		else if ( bs.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, irb_cat_count, M1_IR_BROWSER_ROWS);
			irb_draw_list("Categories", irb_cat_count, &w, irb_row_cat, truncated);
		}
		else if ( bs.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK )
		{
			irb_screen_devices(irb_cat_names[w.sel]);
			irb_draw_list("Categories", irb_cat_count, &w, irb_row_cat, truncated);
		}
	}
} // static void irb_screen_categories(void)

/*============================================================================*/
/**
  * @brief  Device list screen of one category (level 2).
  */
/*============================================================================*/
static void irb_screen_devices(const char *cat)
{
	S_M1_Buttons_Status bs;
	S_M1_Main_Q_t q_item;
	S_M1_IRB_Win_t w = {0, 0};
	S_M1_IRB_Scan_Status_t st;
	BaseType_t ret;
	uint8_t truncated = 0;

	irb_draw_hourglass();
	st = irb_scan_devices(cat, &truncated);

	if ( st == M1_IRB_SCAN_IO )
	{
		irb_notice("Scan failed", "Check SD card", M1_IRB_NOTICE_MS_DEFAULT);
		return;
	}
	if ( st == M1_IRB_SCAN_EMPTY )
	{
		irb_notice("No .ir files", "in this category", M1_IRB_NOTICE_MS_DEFAULT);
		return;
	}

	irb_draw_list(cat, irb_dev_count, &w, irb_row_dev, truncated);

	while ( 1 )
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if ( ret != pdTRUE )
			continue;
		if ( q_item.q_evt_type != Q_EVENT_KEYPAD )
			continue;
		ret = xQueueReceive(button_events_q_hdl, &bs, 0);
		if ( ret != pdTRUE )
			continue;

		if ( bs.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK )
		{
			xQueueReset(main_q_hdl);
			return;
		}
		else if ( bs.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, irb_dev_count, -1);
			irb_draw_list(cat, irb_dev_count, &w, irb_row_dev, truncated);
		}
		else if ( bs.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, irb_dev_count, 1);
			irb_draw_list(cat, irb_dev_count, &w, irb_row_dev, truncated);
		}
		else if ( bs.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, irb_dev_count, -M1_IR_BROWSER_ROWS);
			irb_draw_list(cat, irb_dev_count, &w, irb_row_dev, truncated);
		}
		else if ( bs.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, irb_dev_count, M1_IR_BROWSER_ROWS);
			irb_draw_list(cat, irb_dev_count, &w, irb_row_dev, truncated);
		}
		else if ( bs.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK )
		{
			(void)irb_screen_buttons(irb_dev_paths[w.sel], irb_dev_names[w.sel]);
			irb_draw_list(cat, irb_dev_count, &w, irb_row_dev, truncated);
		}
	}
} // static void irb_screen_devices(const char *cat)

/*============================================================================*/
/**
  * @brief  Favorites/recent list screen (dashboard rows 0/1): the stored
  *         device paths with legacy basename labels, 6-row pagination,
  *         OK opens the same device/button flow as every other list.
  *         The store is reloaded after returning from the button view: a
  *         transmit (recent) or a favorite toggle may have changed it,
  *         and the shared workspace is reused by those operations.
  */
/*============================================================================*/
static void irb_screen_stored_list(const char *title, const char *file, uint16_t cap)
{
	S_M1_Buttons_Status bs;
	S_M1_Main_Q_t q_item;
	S_M1_IRB_Win_t w = {0, 0};
	BaseType_t ret;
	uint16_t count;

	irb_draw_hourglass();
	count = irb_store_load(file, cap);

	if ( count == 0u )
	{
		irb_notice(title, "list is empty", M1_IRB_NOTICE_MS_DEFAULT);
		return;
	}

	irb_draw_list(title, count, &w, irb_row_stored, 0u);

	while ( 1 )
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if ( ret != pdTRUE )
			continue;
		if ( q_item.q_evt_type != Q_EVENT_KEYPAD )
			continue;
		ret = xQueueReceive(button_events_q_hdl, &bs, 0);
		if ( ret != pdTRUE )
			continue;

		if ( bs.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK )
		{
			xQueueReset(main_q_hdl);
			return;
		}
		else if ( bs.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, count, -1);
			irb_draw_list(title, count, &w, irb_row_stored, 0u);
		}
		else if ( bs.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, count, 1);
			irb_draw_list(title, count, &w, irb_row_stored, 0u);
		}
		else if ( bs.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, count, -M1_IR_BROWSER_ROWS);
			irb_draw_list(title, count, &w, irb_row_stored, 0u);
		}
		else if ( bs.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, count, M1_IR_BROWSER_ROWS);
			irb_draw_list(title, count, &w, irb_row_stored, 0u);
		}
		else if ( bs.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK )
		{
			char sel_path[M1_IR_BROWSER_PATH_LEN_MAX];
			char sel_label[M1_IR_BROWSER_NAME_LEN_MAX];

			/* copy out of the shared workspace: the button view's
			 * favorites/recent operations reuse it */
			m1_irb_join_path(sel_path, (uint8_t)sizeof(sel_path), "",
					irb_list_paths[w.sel]);
			m1_irb_join_path(sel_label, (uint8_t)sizeof(sel_label), "",
					irb_list_names[w.sel]);
			(void)irb_screen_buttons(sel_path, sel_label);
			count = irb_store_load(file, cap); /* toggle/transmit changed it */
			if ( (count == 0u) || (w.sel >= count) )
			{
				w.sel = 0;
				w.top = 0;
			}
			if ( count == 0u )
				return; /* nothing left: back to the dashboard */
			irb_draw_list(title, count, &w, irb_row_stored, 0u);
		}
	}
} // static void irb_screen_stored_list(...)

/*============================================================================*/
/**
  * @brief  Button list screen of one device: the PR-4 index entries with
  *         6-row pagination; OK transmits the selected button through the
  *         upstream play path (a successful transmit is recorded in the
  *         recent store); OK held down toggles the device in the
  *         favorites store (legacy "* " title marker); BACK returns to
  *         the calling list.
  * @retval always 1 (BACK), kept explicit for the legacy call shape.
  */
/*============================================================================*/
static uint8_t irb_screen_buttons(const char *path, const char *title)
{
	S_M1_Buttons_Status bs;
	S_M1_Main_Q_t q_item;
	S_M1_IRB_Win_t w = {0, 0};
	BaseType_t ret;
	int32_t indexed;
	uint16_t count;
	uint8_t truncated, tx_err, fav, tx_active = 0;
	char dev_path[M1_IR_BROWSER_PATH_LEN_MAX];
	char dev_title[M1_IR_BROWSER_NAME_LEN_MAX + 2u]; /* "* " + label */

	/* The caller's buffers may alias the shared list workspaces (the
	 * favorites/recent rows point into them, and this screen's store
	 * operations reuse those workspaces) -- work on local copies. */
	m1_irb_join_path(dev_path, (uint8_t)sizeof(dev_path), "", (path != NULL) ? path : "");
	fav = irb_fav_is(dev_path);
	m1_irb_dev_title(dev_title, (uint8_t)sizeof(dev_title), title, fav);

	irb_draw_hourglass();
	indexed = irb_index_device(dev_path);

	if ( indexed < 0 )
	{
		/* map the loader error to a plain notice */
		switch ( (S_M1_IR_File_Error_t)(-indexed) )
		{
			case M1_IR_FILE_ERR_IO:
				irb_notice("Read failed", "Check SD card", M1_IRB_NOTICE_MS_DEFAULT);
				break;
			case M1_IR_FILE_ERR_HEADER:
				irb_notice("Not .ir file", dev_path, M1_IRB_NOTICE_MS_DEFAULT);
				break;
			default:
				irb_notice("File error", dev_path, M1_IRB_NOTICE_MS_DEFAULT);
				break;
		}
		return 1;
	}
	count = (uint16_t)indexed;
	M1_LOG_I(M1_LOGDB_TAG, "%s: %u buttons, %u skipped\r\n", dev_path,
			(unsigned int)count, (unsigned int)irb_file_stats.skipped_unknown_protocol);

	/* one aggregated strict-skip notice per file, never one per block.
	 * When the file indexed zero usable buttons the aggregated lines
	 * REPLACE the bare "No usable" notice: every block strict-skipped is
	 * exactly why there is nothing to list, and the count + first
	 * protocol is the explanation docs/ir-provisioning.md promises. */
	if ( irb_skip_acc.skipped > 0u )
	{
		char skip_l1[M1_IRB_COL_MAX + 1u];
		char skip_l2[M1_IR_FILE_PROTO_LEN_MAX + 1u];
		if ( m1_irb_skip_lines(&irb_skip_acc, skip_l1, (uint8_t)sizeof(skip_l1),
				skip_l2, (uint8_t)sizeof(skip_l2)) )
			irb_notice(skip_l1, skip_l2, M1_IRB_NOTICE_MS_DEFAULT);
	}

	if ( count == 0u )
	{
		if ( irb_skip_acc.skipped == 0u )
			irb_notice("No usable", "buttons in file", M1_IRB_NOTICE_MS_DEFAULT);
		return 1;
	}
	truncated = (irb_file_stats.index_full > 0u) ? 1u : 0u;

	infrared_encode_sys_init();
	irb_draw_list(dev_title, count, &w, irb_row_entry, truncated);

	while ( 1 )
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if ( ret != pdTRUE )
			continue;

		if ( q_item.q_evt_type == Q_EVENT_IRRED_TX )
		{
			if ( tx_active && (infrared_transmit(0, 0) == IR_TX_COMPLETED) )
			{
				m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
				tx_active = 0;
				irb_draw_list(dev_title, count, &w, irb_row_entry, truncated);
			}
			continue;
		}
		if ( q_item.q_evt_type != Q_EVENT_KEYPAD )
			continue;
		ret = xQueueReceive(button_events_q_hdl, &bs, 0);
		if ( ret != pdTRUE )
			continue;

		if ( tx_active )
			(void)infrared_transmit(0, 0); /* drive the state machine on key activity */

		if ( bs.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK )
		{
			if ( tx_active && ir_ota_data_tx_active )
				m1_ir_ota_frame_repeat_handler(IRMP_UNKNOWN_PROTOCOL); // Reset
			m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
			infrared_encode_sys_deinit();
			xQueueReset(main_q_hdl);
			return 1;
		}
		else if ( bs.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, count, -1);
			irb_draw_list(dev_title, count, &w, irb_row_entry, truncated);
		}
		else if ( bs.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, count, 1);
			irb_draw_list(dev_title, count, &w, irb_row_entry, truncated);
		}
		else if ( bs.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, count, -M1_IR_BROWSER_ROWS);
			irb_draw_list(dev_title, count, &w, irb_row_entry, truncated);
		}
		else if ( bs.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, count, M1_IR_BROWSER_ROWS);
			irb_draw_list(dev_title, count, &w, irb_row_entry, truncated);
		}
		else if ( bs.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_LCLICK )
		{
			if ( tx_active )
				continue; /* no store writes while a frame runs */

			/* OK held down: toggle this device in the favorites store
			 * (legacy fork action, now on the long press so LEFT/RIGHT
			 * keep their page-step duty) and refresh the title marker */
			fav = irb_fav_toggle(dev_path);
			m1_irb_dev_title(dev_title, (uint8_t)sizeof(dev_title), title, fav);
			irb_draw_list(dev_title, count, &w, irb_row_entry, truncated);
		}
		else if ( bs.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK )
		{
			if ( tx_active )
				continue; /* ignore OK while transmitting */

			irb_draw_sending(irb_entries[w.sel].name);
			m1_buzzer_notification();
			m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M);
			tx_err = irb_transmit(dev_path, w.sel);
			if ( tx_err )
			{
				m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
				/* switch on the raw code: it mixes S_M1_IR_File_Error_t
				 * values with the browser-local M1_IRB_TX_ERR_EMPTY_RAW */
				switch ( tx_err )
				{
					case M1_IR_FILE_ERR_IO:
						irb_notice("Read failed", "Check SD card", M1_IRB_NOTICE_MS_DEFAULT);
						break;
					case M1_IR_FILE_ERR_MALFORMED:
						irb_notice("File changed", "Reopen device", M1_IRB_NOTICE_MS_DEFAULT);
						break;
					case M1_IR_FILE_ERR_RAW_OVERFLOW:
						irb_notice("Raw signal", "too long", M1_IRB_NOTICE_MS_DEFAULT);
						break;
					case M1_IRB_TX_ERR_EMPTY_RAW:
						irb_notice("Empty raw", "Nothing to send", M1_IRB_NOTICE_MS_DEFAULT);
						break;
					case M1_IR_FILE_ERR_UNKNOWN_PROTOCOL:
						irb_notice("Unsupported", "protocol skipped", M1_IRB_NOTICE_MS_DEFAULT);
						break;
					default:
						irb_notice("Send failed", "Unsupported signal", M1_IRB_NOTICE_MS_DEFAULT);
						break;
				}
				irb_draw_list(dev_title, count, &w, irb_row_entry, truncated);
			}
			else
			{
				tx_active = 1;
				irb_recent_add(dev_path); /* recent updated on transmit;
											* filesystem-only, between
											* frames -- never inside the
											* transmit path */
			}
		}
	}
} // static uint8_t irb_screen_buttons(...)

/*============================================================================*/
/**
  * @brief  Search screen: virtual-keyboard query, bounded scan, result
  *         list; OK opens the device's button list (legacy intent).
  */
/*============================================================================*/
static void irb_screen_search(void)
{
	S_M1_Buttons_Status bs;
	S_M1_Main_Q_t q_item;
	S_M1_IRB_Win_t w = {0, 0};
	S_M1_IRB_Scan_Status_t st;
	BaseType_t ret;
	uint8_t truncated = 0;

	irb_query[0] = '\0';
	if ( !m1_vkb_get_filename("Search IR", "", irb_query, 0) || (irb_query[0] == '\0') )
		return;

	irb_draw_hourglass();
	(void)m1_irb_query_lower(irb_query);
	st = irb_scan_search(irb_query, &truncated);

	if ( st == M1_IRB_SCAN_IO )
	{
		irb_notice("Search error", "Check SD card", M1_IRB_NOTICE_MS_DEFAULT);
		return;
	}
	if ( st == M1_IRB_SCAN_EMPTY )
	{
		irb_notice("No matches", irb_query, M1_IRB_NOTICE_MS_DEFAULT);
		return;
	}

	irb_draw_list("Results", irb_dev_count, &w, irb_row_dev, truncated);

	while ( 1 )
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if ( ret != pdTRUE )
			continue;
		if ( q_item.q_evt_type != Q_EVENT_KEYPAD )
			continue;
		ret = xQueueReceive(button_events_q_hdl, &bs, 0);
		if ( ret != pdTRUE )
			continue;

		if ( bs.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK )
		{
			xQueueReset(main_q_hdl);
			return;
		}
		else if ( bs.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, irb_dev_count, -1);
			irb_draw_list("Results", irb_dev_count, &w, irb_row_dev, truncated);
		}
		else if ( bs.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, irb_dev_count, 1);
			irb_draw_list("Results", irb_dev_count, &w, irb_row_dev, truncated);
		}
		else if ( bs.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, irb_dev_count, -M1_IR_BROWSER_ROWS);
			irb_draw_list("Results", irb_dev_count, &w, irb_row_dev, truncated);
		}
		else if ( bs.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, irb_dev_count, M1_IR_BROWSER_ROWS);
			irb_draw_list("Results", irb_dev_count, &w, irb_row_dev, truncated);
		}
		else if ( bs.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK )
		{
			(void)irb_screen_buttons(irb_dev_paths[w.sel], irb_dev_names[w.sel]);
			irb_draw_list("Results", irb_dev_count, &w, irb_row_dev, truncated);
		}
	}
} // static void irb_screen_search(void)

/*============================================================================*/
/**
  * @brief  Browser entry point (Infrared menu).
  */
/*============================================================================*/
void m1_ir_browser_run(void)
{
	if ( m1_sdcard_get_status() != SD_access_OK )
	{
		m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_access_error_message);
		m1_wait_back_to_exit();
		return;
	}

	if ( !m1_ir_subsys_acquire() ) /* single-owner IRMP rule */
	{
		irb_notice("IR busy", "Try again later", M1_IRB_NOTICE_MS_DEFAULT);
		return;
	} // if ( !m1_ir_subsys_acquire() )

	irb_screen_dashboard();
	xQueueReset(main_q_hdl);
	m1_ir_subsys_release(); // single-owner IRMP rule
} // void m1_ir_browser_run(void)
