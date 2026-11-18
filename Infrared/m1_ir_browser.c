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
* m1_ir_file_signal_load(); nothing here allocates at run time.
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

#define M1_IRB_ROW_H				9 /* pixel, 6 rows below the 10-px title bar */
#define M1_IRB_TITLE_H				10 /* pixel */
#define M1_IRB_COL_MAX				20 /* visible chars per row (5x8 font) */

#define M1_IRB_NOTICE_MS_DEFAULT	1500

/* ===================== M1_IR_BROWSER_PURE_CORE_BEGIN ===================== */
/*
* Pure view/model core: no FatFs, no HAL, no FreeRTOS, no allocation. The
* host test m1_csrc/test/m1_ir_browser_test.py extracts this region
* verbatim and exercises pagination, search matching, entry classification
* (category dir vs .ir device), display-name derivation and the
* strict-skip notice aggregation.
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
  *         one per block): l1 = "N skipped", l2 = the first offending
  *         protocol name ("" when unknown).
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

	if ( (acc == NULL) || (acc->skipped == 0u) )
		return 0;
	if ( (l1 == NULL) || (l1sz == 0u) )
		return 0;
	m1_irb_u16_text(num, (uint8_t)sizeof(num), acc->skipped);
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
/* ====================== M1_IR_BROWSER_PURE_CORE_END ====================== */

/**************************** V A R I A B L E S ******************************/

/* Module-static workspaces (UI task only, no run-time allocation). */
static char irb_cat_names[M1_IR_BROWSER_CATS_MAX][M1_IR_BROWSER_NAME_LEN_MAX];
static char irb_dev_names[M1_IR_BROWSER_DEVICES_MAX][M1_IR_BROWSER_NAME_LEN_MAX];
static char irb_dev_paths[M1_IR_BROWSER_DEVICES_MAX][M1_IR_BROWSER_PATH_LEN_MAX];
static S_M1_IR_File_Entry_t irb_entries[M1_IR_BROWSER_BUTTONS_MAX];
static uint16_t irb_raw_ticks[M1_IR_BROWSER_RAW_TICKS_MAX];
static char irb_query[M1_IR_BROWSER_NAME_LEN_MAX];
static uint16_t irb_cat_count;
static uint16_t irb_dev_count;
static S_M1_IRB_SkipAcc_t irb_skip_acc;
static S_M1_IR_File_Stats_t irb_file_stats;

static const char *irb_dash_rows[2] = {"Browse files", "Search"};

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
static void irb_screen_dashboard(void);
static void irb_screen_categories(void);
static void irb_screen_devices(const char *cat);
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
	return (idx < 2u) ? irb_dash_rows[idx] : "";
} // static const char *irb_row_dash(uint16_t idx)

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
		ir_universal_payload.ir_data_type = IR_DATATYPE_PARSED;
		ir_universal_payload.pprotocol = (uint8_t *)sig.protocol_name;
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
  * @brief  Dashboard: the browser's top screen (legacy intent; the
  *         favorites/recent rows land together with their persistence in
  *         PR-6 and are deliberately not stubbed here).
  */
/*============================================================================*/
static void irb_screen_dashboard(void)
{
	S_M1_Buttons_Status bs;
	S_M1_Main_Q_t q_item;
	S_M1_IRB_Win_t w = {0, 0};
	BaseType_t ret;

	irb_draw_list("IR Database", 2u, &w, irb_row_dash, 0u);

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
			m1_irb_win_move(&w, 2u, -1);
			irb_draw_list("IR Database", 2u, &w, irb_row_dash, 0u);
		}
		else if ( bs.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, 2u, 1);
			irb_draw_list("IR Database", 2u, &w, irb_row_dash, 0u);
		}
		else if ( bs.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK )
		{
			if ( w.sel == 0u )
				irb_screen_categories();
			else
				irb_screen_search();
			irb_draw_list("IR Database", 2u, &w, irb_row_dash, 0u);
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
  * @brief  Button list screen of one device: the PR-4 index entries with
  *         6-row pagination; OK transmits the selected button through the
  *         upstream play path; BACK returns to the device list.
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
	uint8_t truncated, tx_err, tx_active = 0;

	irb_draw_hourglass();
	indexed = irb_index_device(path);

	if ( indexed < 0 )
	{
		/* map the loader error to a plain notice */
		switch ( (S_M1_IR_File_Error_t)(-indexed) )
		{
			case M1_IR_FILE_ERR_IO:
				irb_notice("Can't read file", "Check SD card", M1_IRB_NOTICE_MS_DEFAULT);
				break;
			case M1_IR_FILE_ERR_HEADER:
				irb_notice("Not an .ir file", path, M1_IRB_NOTICE_MS_DEFAULT);
				break;
			default:
				irb_notice("Unreadable file", path, M1_IRB_NOTICE_MS_DEFAULT);
				break;
		}
		return 1;
	}
	count = (uint16_t)indexed;
	M1_LOG_I(M1_LOGDB_TAG, "%s: %u buttons, %u skipped\r\n", path,
			(unsigned int)count, (unsigned int)irb_file_stats.skipped_unknown_protocol);
	if ( count == 0u )
	{
		irb_notice("No usable", "buttons in file", M1_IRB_NOTICE_MS_DEFAULT);
		return 1;
	}
	truncated = (irb_file_stats.index_full > 0u) ? 1u : 0u;

	/* one aggregated strict-skip notice per file, never one per block */
	if ( irb_skip_acc.skipped > 0u )
	{
		char skip_l1[M1_IRB_COL_MAX + 1u];
		char skip_l2[M1_IR_FILE_PROTO_LEN_MAX + 1u];
		if ( m1_irb_skip_lines(&irb_skip_acc, skip_l1, (uint8_t)sizeof(skip_l1),
				skip_l2, (uint8_t)sizeof(skip_l2)) )
			irb_notice(skip_l1, skip_l2, M1_IRB_NOTICE_MS_DEFAULT);
	}

	infrared_encode_sys_init();
	irb_draw_list(title, count, &w, irb_row_entry, truncated);

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
				irb_draw_list(title, count, &w, irb_row_entry, truncated);
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
			irb_draw_list(title, count, &w, irb_row_entry, truncated);
		}
		else if ( bs.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, count, 1);
			irb_draw_list(title, count, &w, irb_row_entry, truncated);
		}
		else if ( bs.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, count, -M1_IR_BROWSER_ROWS);
			irb_draw_list(title, count, &w, irb_row_entry, truncated);
		}
		else if ( bs.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK )
		{
			m1_irb_win_move(&w, count, M1_IR_BROWSER_ROWS);
			irb_draw_list(title, count, &w, irb_row_entry, truncated);
		}
		else if ( bs.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK )
		{
			if ( tx_active )
				continue; /* ignore OK while transmitting */

			irb_draw_sending(irb_entries[w.sel].name);
			m1_buzzer_notification();
			m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M);
			tx_err = irb_transmit(path, w.sel);
			if ( tx_err )
			{
				m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
				switch ( (S_M1_IR_File_Error_t)tx_err )
				{
					case M1_IR_FILE_ERR_IO:
						irb_notice("Can't read file", "Check SD card", M1_IRB_NOTICE_MS_DEFAULT);
						break;
					case M1_IR_FILE_ERR_MALFORMED:
						irb_notice("File changed", "Reopen device", M1_IRB_NOTICE_MS_DEFAULT);
						break;
					case M1_IR_FILE_ERR_RAW_OVERFLOW:
						irb_notice("Raw signal", "too long", M1_IRB_NOTICE_MS_DEFAULT);
						break;
					case M1_IR_FILE_ERR_UNKNOWN_PROTOCOL:
						irb_notice("Unsupported", "protocol skipped", M1_IRB_NOTICE_MS_DEFAULT);
						break;
					default:
						irb_notice("Send failed", "Unsupported signal", M1_IRB_NOTICE_MS_DEFAULT);
						break;
				}
				irb_draw_list(title, count, &w, irb_row_entry, truncated);
			}
			else
			{
				tx_active = 1;
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
		irb_notice("Search failed", "Check SD card", M1_IRB_NOTICE_MS_DEFAULT);
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
