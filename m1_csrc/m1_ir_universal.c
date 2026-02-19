/* See COPYING.txt for license details. */

/*
 * m1_ir_universal.c
 *
 * Universal Remote feature for M1 firmware.
 * Parses Flipper Zero-compatible .ir files from SD card and transmits IR.
 *
 * SD card layout: 0:/IR/<Category>/<Brand>/<Device>.ir
 * Database source: https://github.com/Lucaslhm/Flipper-IRDB (MIT License)
 *
 * M1 Project
 */

#include "m1_ir_universal.h"
#include "ff.h"
#include "irsnd.h"
#include "m1_buzzer.h"
#include "m1_display.h"
#include "m1_infrared.h"
#include "m1_led_indicator.h"
#include "m1_sdcard.h"
#include "m1_system.h"
#include "m1_tasks.h"
#include "m1_virtual_kb.h"
#include "main.h"
#include "stm32h5xx_hal.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Internal constants
 * ---------------------------------------------------------------------- */

#define IR_LIST_MAX_ENTRIES                                                    \
  16 /* max dirs/files shown per level; limited for RAM headroom (see issue    \
        #14) */
#define IR_NAME_BUF_LEN (FF_MAX_LFN + 1)
#define IR_LINE_BUF_LEN 128 /* max line length in a .ir file      */
#define IR_DISP_ROWS 6      /* visible rows on 128x64 with 5x8 font */
#define IR_DISP_COL_MAX 24  /* chars per row (128px / 5px font)   */

#define IR_RECENT_FILE "0:/System/ir_recent.txt"
#define IR_FAVORITES_FILE "0:/System/ir_favorites.txt"
#define IR_MAX_RECENT 10
#define IR_MAX_FAVORITES 20
#define IR_SEARCH_MAX_RESULTS 32
#define IR_MAX_RECURSION_DEPTH 5

/* -------------------------------------------------------------------------
 * Protocol name -> IRMP ID mapping table
 * ---------------------------------------------------------------------- */

typedef struct {
  const char *name;
  uint8_t id;
} S_IR_ProtoMap_t;

static const S_IR_ProtoMap_t ir_proto_map[] = {
    {"NEC", IRMP_NEC_PROTOCOL},
    {"NECext", IRMP_NEC_PROTOCOL},
    {"NEC42", IRMP_NEC42_PROTOCOL},
    {"NEC42ext", IRMP_NEC42_PROTOCOL},
    {"RC5", IRMP_RC5_PROTOCOL},
    {"RC5X", IRMP_RC5_PROTOCOL},
    {"RC6", IRMP_RC6_PROTOCOL},
    {"RC6A", IRMP_RC6A_PROTOCOL},
    {"Samsung32", IRMP_SAMSUNG32_PROTOCOL},
    {"Samsung", IRMP_SAMSUNG_PROTOCOL},
    {"SIRC", IRMP_SIRCS_PROTOCOL},
    {"SIRC15", IRMP_SIRCS_PROTOCOL},
    {"SIRC20", IRMP_SIRCS_PROTOCOL},
    {"Kaseikyo", IRMP_KASEIKYO_PROTOCOL},
    {"Denon", IRMP_DENON_PROTOCOL},
    {"Sharp", IRMP_DENON_PROTOCOL},
    {"JVC", IRMP_JVC_PROTOCOL},
    {"Panasonic", IRMP_PANASONIC_PROTOCOL},
    {"NEC16", IRMP_NEC16_PROTOCOL},
    {"LGAIR", IRMP_LGAIR_PROTOCOL},
    {"Samsung48", IRMP_SAMSUNG48_PROTOCOL},
};

#define IR_PROTO_MAP_LEN (sizeof(ir_proto_map) / sizeof(ir_proto_map[0]))

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

/* Trim leading/trailing whitespace in-place, return pointer to trimmed str */
static char *ir_trim(char *s) {
  char *end;
  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
    s++;
  if (*s == '\0')
    return s;
  end = s + strlen(s) - 1;
  while (end > s &&
         (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
    end--;
  *(end + 1) = '\0';
  return s;
}

/* Parse a hex byte string like "07 00 00 00" -> return first two bytes as
 * uint16 */
static uint16_t ir_parse_hex_field(const char *s) {
  char *endptr;
  unsigned long b0 = strtoul(s, &endptr, 16);
  if (s == endptr) {
    return 0; /* Conversion failed */
  }
  unsigned long b1 = strtoul(endptr, NULL, 16);
  return (uint16_t)(b0 | (b1 << 8));
}

/* -------------------------------------------------------------------------
 * Public: ir_universal_proto_to_id
 * ---------------------------------------------------------------------- */

uint8_t ir_universal_proto_to_id(const char *name) {
  uint8_t i;
  for (i = 0; i < IR_PROTO_MAP_LEN; i++) {
    if (strcmp(ir_proto_map[i].name, name) == 0)
      return ir_proto_map[i].id;
  }
  return IRMP_UNKNOWN_PROTOCOL;
}

/* -------------------------------------------------------------------------
 * Public: ir_universal_parse_file
 * ---------------------------------------------------------------------- */

/* Helper to save current block to output array if valid */
static void ir_save_block(S_IR_Device_t *out, S_IR_Cmd_t *cur,
                          uint8_t in_block) {
  if (in_block && cur->valid && out->count < IR_UNIVERSAL_CMDS_MAX) {
    memcpy(&out->cmds[out->count], cur, sizeof(S_IR_Cmd_t));
    out->count++;
  }
}

uint8_t ir_universal_parse_file(const char *path, S_IR_Device_t *out) {
  FIL f;
  FRESULT fr;
  char line[IR_LINE_BUF_LEN];
  char *p, *eq;
  char *key_str, *val_str;
  uint8_t in_block;
  S_IR_Cmd_t cur;

  if (!out)
    return 0;

  memset(out, 0, sizeof(S_IR_Device_t));

  fr = f_open(&f, path, FA_OPEN_EXISTING | FA_READ);
  if (fr != FR_OK)
    return 0;

  in_block = 0;
  memset(&cur, 0, sizeof(cur));

  while (f_gets(line, sizeof(line), &f)) {
    p = ir_trim(line);

    /* Skip comments and empty lines */
    if (p[0] == '#' || p[0] == '\0')
      continue;

    /* Split "key: value" */
    eq = strchr(p, ':');
    if (!eq)
      continue;

    *eq = '\0';
    key_str = ir_trim(p);
    val_str = ir_trim(eq + 1);

    /* Detect start of a new button block */
    if (strcmp(key_str, "name") == 0) {
      ir_save_block(out, &cur, in_block);
      memset(&cur, 0, sizeof(cur));
      strncpy(cur.name, val_str, IR_UNIVERSAL_NAME_LEN_MAX - 1);
      cur.name[IR_UNIVERSAL_NAME_LEN_MAX - 1] = '\0';
      in_block = 1;
      continue;
    }

    if (!in_block)
      continue;

    if (strcmp(key_str, "type") == 0) {
      /* Only support parsed (not raw) signals */
      if (strcmp(val_str, "parsed") != 0)
        in_block = 0; /* skip this block */
      continue;
    }

    if (strcmp(key_str, "protocol") == 0) {
      cur.irmp.protocol = ir_universal_proto_to_id(val_str);
      if (cur.irmp.protocol == IRMP_UNKNOWN_PROTOCOL)
        in_block = 0; /* unsupported protocol, skip */
      continue;
    }

    if (strcmp(key_str, "address") == 0) {
      cur.irmp.address = ir_parse_hex_field(val_str);
      continue;
    }

    if (strcmp(key_str, "command") == 0) {
      cur.irmp.command = ir_parse_hex_field(val_str);
      cur.irmp.flags = 0;
      cur.valid = true;
      continue;
    }
  }

  /* Save last block */
  ir_save_block(out, &cur, in_block);

  f_close(&f);
  return out->count;
}

/* -------------------------------------------------------------------------
 * Internal UI helpers
 * ---------------------------------------------------------------------- */

/*
 * Draw a scrollable list on the 128x64 display.
 *   title      - top header string (truncated to fit)
 *   entries    - array of string pointers
 *   count      - total number of entries
 *   sel        - currently selected index (0-based)
 *   row_offset - first visible row index
 */
static void ir_ui_draw_list(const char *title, const char *entries[],
                            uint8_t count, uint8_t sel, uint8_t row_offset) {
  uint8_t row, disp_row;
  char trunc[IR_DISP_COL_MAX + 1];
  uint8_t y;

  m1_u8g2_firstpage();
  u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
  u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);

  /* Title bar */
  u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
  u8g2_DrawBox(&m1_u8g2, 0, 0, 128, 9);
  u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
  strncpy(trunc, title, IR_DISP_COL_MAX);
  trunc[IR_DISP_COL_MAX] = '\0';
  u8g2_DrawStr(&m1_u8g2, 1, 8, trunc);
  u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

  /* List rows */
  disp_row = 0;
  for (row = row_offset; row < count && disp_row < IR_DISP_ROWS;
       row++, disp_row++) {
    y = 10 + disp_row * 9;

    if (row == sel) {
      /* Highlight selected row */
      u8g2_DrawBox(&m1_u8g2, 0, y - 1, 124, 9);
      u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
    }

    strncpy(trunc, entries[row], IR_DISP_COL_MAX);
    trunc[IR_DISP_COL_MAX] = '\0';
    u8g2_DrawStr(&m1_u8g2, 1, y + 7, trunc);

    if (row == sel)
      u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
  }

  /* Scroll indicator if list overflows */
  if (count > IR_DISP_ROWS) {
    uint8_t bar_h = (uint8_t)((uint16_t)IR_DISP_ROWS * 54 / count);
    if (bar_h < 4)
      bar_h = 4;
    uint8_t bar_y = (uint8_t)(10 + (uint16_t)row_offset * (54 - bar_h) /
                                       (count - IR_DISP_ROWS));
    u8g2_DrawFrame(&m1_u8g2, 125, 10, 3, 54);
    u8g2_DrawBox(&m1_u8g2, 126, bar_y, 1, bar_h);
  }

  m1_u8g2_nextpage();
}

/* Show a simple error message */
static void ir_ui_show_error(const char *msg) {
  m1_u8g2_firstpage();
  u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
  u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
  u8g2_DrawXBMP(&m1_u8g2, 2, 2, 48, 25, remote_48x25);
  u8g2_DrawStr(&m1_u8g2, 55, 15, "IR Error:");
  u8g2_DrawStr(&m1_u8g2, 2, 30, msg);
  m1_u8g2_nextpage();
}

/* Show "Sending..." overlay */
static void ir_ui_show_sending(const char *cmd_name) {
  char buf[IR_DISP_COL_MAX + 1];
  m1_u8g2_firstpage();
  u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
  u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
  u8g2_DrawXBMP(&m1_u8g2, 2, 2, 48, 25, remote_48x25);
  u8g2_DrawStr(&m1_u8g2, 55, 15, "Sending...");
  strncpy(buf, cmd_name, IR_DISP_COL_MAX);
  buf[IR_DISP_COL_MAX] = '\0';
  u8g2_DrawStr(&m1_u8g2, 2, 40, buf);
  m1_u8g2_nextpage();
}

/* Show "Scanning..." overlay for search */
static void ir_ui_show_scanning(void) {
  m1_u8g2_firstpage();
  u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
  u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
  u8g2_DrawXBMP(&m1_u8g2, 2, 2, 48, 25, remote_48x25);
  u8g2_DrawStr(&m1_u8g2, 55, 15, "Scanning...");
  u8g2_DrawStr(&m1_u8g2, 55, 25, "Please wait");
  m1_u8g2_nextpage();
}

/* -------------------------------------------------------------------------
 * Recent & Favorites Logic
 * ---------------------------------------------------------------------- */

static void ir_add_recent(const char *path) {
  char (*lines)[IR_UNIVERSAL_PATH_LEN_MAX];
  uint8_t count = 0;
  FIL f;
  FRESULT fr;

  lines = pvPortMalloc(IR_MAX_RECENT * IR_UNIVERSAL_PATH_LEN_MAX);
  if (!lines)
    return;

  /* Ensure directory exists - removed redundant f_mkdir("0:/System") */

  /* Read current list */
  fr = f_open(&f, IR_RECENT_FILE, FA_READ | FA_OPEN_ALWAYS);
  if (fr == FR_OK) {
    while (count < IR_MAX_RECENT &&
           f_gets(lines[count], IR_UNIVERSAL_PATH_LEN_MAX, &f)) {
      char *p = ir_trim(lines[count]);
      if (p[0] != '\0' && strcmp(p, path) != 0) {
        memmove(lines[count], p, strlen(p) + 1);
        count++;
      }
    }
    f_close(&f);
  }

  /* Shift and prepend new path */
  fr = f_open(&f, IR_RECENT_FILE, FA_WRITE | FA_CREATE_ALWAYS);
  if (fr == FR_OK) {
    f_printf(&f, "%s\n", path);
    for (uint8_t i = 0; i < count && i < (IR_MAX_RECENT - 1); i++) {
      f_printf(&f, "%s\n", lines[i]);
    }
    f_close(&f);
  }

  vPortFree(lines);
}

static bool ir_is_favorite(const char *path) {
  FIL f;
  char line[IR_UNIVERSAL_PATH_LEN_MAX];
  bool found = false;

  if (f_open(&f, IR_FAVORITES_FILE, FA_READ) != FR_OK)
    return false;

  while (f_gets(line, sizeof(line), &f)) {
    char *p = ir_trim(line);
    if (p[0] != '\0' && strcmp(p, path) == 0) {
      found = true;
      break;
    }
  }
  f_close(&f);
  return found;
}

static void ir_toggle_favorite(const char *path) {
  char (*lines)[IR_UNIVERSAL_PATH_LEN_MAX];
  uint8_t count = 0;
  bool already_fav = false;
  FIL f;
  FRESULT fr;

  lines = pvPortMalloc(IR_MAX_FAVORITES * IR_UNIVERSAL_PATH_LEN_MAX);
  if (!lines)
    return;

  /* Read current list */
  fr = f_open(&f, IR_FAVORITES_FILE, FA_READ | FA_OPEN_ALWAYS);
  if (fr == FR_OK) {
    while (count < IR_MAX_FAVORITES &&
           f_gets(lines[count], IR_UNIVERSAL_PATH_LEN_MAX, &f)) {
      char *p = ir_trim(lines[count]);
      if (p[0] != '\0') {
        if (strcmp(p, path) == 0) {
          already_fav = true;
          /* skip adding to the list we will rewrite (removing it) */
        } else {
          memmove(lines[count], p, strlen(p) + 1);
          count++;
        }
      }
    }
    f_close(&f);
  }

  /* Rewrite the file */
  fr = f_open(&f, IR_FAVORITES_FILE, FA_WRITE | FA_CREATE_ALWAYS);
  if (fr == FR_OK) {
    if (!already_fav) {
      /* Add to top */
      f_printf(&f, "%s\n", path);
    }
    for (uint8_t i = 0;
         i < count && i < (IR_MAX_FAVORITES - (already_fav ? 0 : 1)); i++) {
      f_printf(&f, "%s\n", lines[i]);
    }
    f_close(&f);

    /* Feedback */
    ir_ui_show_error(already_fav ? "Removed Favorite" : "Added Favorite");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  vPortFree(lines);
}

static uint8_t ir_list_from_file(const char *file_path,
                                 char names[][IR_NAME_BUF_LEN],
                                 char paths[][IR_UNIVERSAL_PATH_LEN_MAX],
                                 uint8_t max_entries) {
  FIL f;
  uint8_t count = 0;
  char line[IR_UNIVERSAL_PATH_LEN_MAX];

  if (f_open(&f, file_path, FA_READ) != FR_OK)
    return 0;

  while (count < max_entries && f_gets(line, sizeof(line), &f)) {
    char *p = ir_trim(line);
    if (p[0] == '\0')
      continue;

    strncpy(paths[count], p, IR_UNIVERSAL_PATH_LEN_MAX - 1);
    paths[count][IR_UNIVERSAL_PATH_LEN_MAX - 1] = '\0';

    /* Name is the basename of the path */
    char *base = strrchr(p, '/');
    if (base)
      base++;
    else
      base = p;
    strncpy(names[count], base, IR_NAME_BUF_LEN - 1);
    names[count][IR_NAME_BUF_LEN - 1] = '\0';

    /* Strip .ir */
    char *dot = strrchr(names[count], '.');
    if (dot)
      *dot = '\0';

    count++;
  }
  f_close(&f);
  return count;
}

/* -------------------------------------------------------------------------
 * Search Logic (Recursive Scan with Hardening)
 * ---------------------------------------------------------------------- */

static void ir_search_recursive(const char *path, const char *lower_query,
                                char names[][IR_NAME_BUF_LEN],
                                char paths[][IR_UNIVERSAL_PATH_LEN_MAX],
                                uint8_t *count, uint8_t max_entries,
                                uint8_t depth) {
  DIR *dir = NULL;
  FILINFO *fi = NULL;
  char *full_path = NULL;
  char *lower_name = NULL;

  /* Hardening: recursion depth limit and result limit */
  if (depth > IR_MAX_RECURSION_DEPTH || *count >= max_entries)
    return;

  /* Allocate large buffers on the heap to avoid stack overflow */
  dir = pvPortMalloc(sizeof(DIR));
  fi = pvPortMalloc(sizeof(FILINFO));
  full_path = pvPortMalloc(IR_UNIVERSAL_PATH_LEN_MAX);
  lower_name = pvPortMalloc(IR_NAME_BUF_LEN);

  if (!dir || !fi || !full_path || !lower_name)
    goto cleanup;

  if (f_opendir(dir, path) != FR_OK)
    goto cleanup;

  while (f_readdir(dir, fi) == FR_OK && fi->fname[0] != '\0' &&
         *count < max_entries) {
    /* Skip . and .. entries and hidden/system files */
    if (fi->fname[0] == '.')
      continue;
    if (fi->fattrib & (AM_HID | AM_SYS))
      continue;

    if (fi->fattrib & AM_DIR) {
      snprintf(full_path, IR_UNIVERSAL_PATH_LEN_MAX, "%s/%s", path, fi->fname);
      ir_search_recursive(full_path, lower_query, names, paths, count,
                          max_entries, depth + 1);
    } else {
      /* Case-insensitive substr search using pre-lowercased query */
      strncpy(lower_name, fi->fname, IR_NAME_BUF_LEN - 1);
      lower_name[IR_NAME_BUF_LEN - 1] = '\0';
      for (int i = 0; lower_name[i]; i++)
        lower_name[i] = tolower((unsigned char)lower_name[i]);

      if (strstr(lower_name, lower_query)) {
        strncpy(names[*count], fi->fname, IR_NAME_BUF_LEN - 1);
        names[*count][IR_NAME_BUF_LEN - 1] = '\0';
        snprintf(paths[*count], IR_UNIVERSAL_PATH_LEN_MAX, "%s/%s", path,
                 fi->fname);
        (*count)++;
      }
    }
  }
  f_closedir(dir);

cleanup:
  if (dir)
    vPortFree(dir);
  if (fi)
    vPortFree(fi);
  if (full_path)
    vPortFree(full_path);
  if (lower_name)
    vPortFree(lower_name);
}

/* -------------------------------------------------------------------------
 * Internal: list a directory, return count of entries
 * entries[] must point to a caller-allocated array of IR_LIST_MAX_ENTRIES
 * char[IR_NAME_BUF_LEN] buffers.
 * Returns number of entries found (dirs first, then files).
 * ---------------------------------------------------------------------- */

static uint8_t ir_list_dir(const char *path, char names[][IR_NAME_BUF_LEN],
                           uint8_t max_entries, bool dirs_only,
                           bool ir_files_only, uint16_t skip_count,
                           bool *more_items) {
  DIR dir;
  FILINFO fi;
  FRESULT fr;
  uint8_t count = 0;
  uint16_t skipped = 0;

  if (more_items)
    *more_items = false;

  fr = f_opendir(&dir, path);
  if (fr != FR_OK)
    return 0;

  while (1) {
    fr = f_readdir(&dir, &fi);
    /* Skip . and .. entries and hidden/system entries */
    if (fi.fname[0] == '.')
      continue;
    if (fi.fattrib & (AM_HID | AM_SYS))
      continue;

    if (dirs_only && !(fi.fattrib & AM_DIR))
      continue;

    if (ir_files_only) {
      /* Accept only .ir files */
      size_t len = strlen(fi.fname);
      if (len < 4)
        continue;

      /* Check for .ir extension (case-insensitive) using strcasestr or manual
       * logic if not available */
      const char *ext = strrchr(fi.fname, '.');
      if (!ext || strcasecmp(ext, ".ir") != 0) {
        continue;
      }
    }

    /* Pagination skip */
    if (skipped < skip_count) {
      skipped++;
      continue;
    }

    if (count < max_entries) {
      snprintf(names[count], IR_NAME_BUF_LEN, "%s", fi.fname);
      count++;
    } else {
      if (more_items)
        *more_items = true;
      break;
    }
  }

  f_closedir(&dir);
  return count;
}

/* -------------------------------------------------------------------------
 * Internal: command browse + transmit loop
 * Shows the list of buttons in a device .ir file.
 * Returns true if user pressed BACK (go up), false on fatal error.
 * ---------------------------------------------------------------------- */

static bool ir_run_command_ui(const char *file_path, const char *device_name) {
  S_IR_Device_t *dev;
  const char *ptrs[IR_UNIVERSAL_CMDS_MAX];
  S_M1_Buttons_Status btn;
  S_M1_Main_Q_t q_item;
  BaseType_t ret;
  uint8_t sel = 0;
  uint8_t row_offset = 0;
  uint8_t i;
  bool tx_active = false;

  /* Allocate device on heap to avoid large stack frame */
  dev = (S_IR_Device_t *)pvPortMalloc(sizeof(S_IR_Device_t));
  if (!dev) {
    ir_ui_show_error("No memory");
    vTaskDelay(pdMS_TO_TICKS(1500));
    return true;
  }

  if (ir_universal_parse_file(file_path, dev) == 0) {
    ir_ui_show_error("Parse failed");
    vTaskDelay(pdMS_TO_TICKS(1500));
    vPortFree(dev);
    return true;
  }

  /* Build pointer array for the list UI */
  for (i = 0; i < dev->count; i++)
    ptrs[i] = dev->cmds[i].name;

  /* Initial draw */
  char dtitle[64];
  if (ir_is_favorite(file_path))
    snprintf(dtitle, sizeof(dtitle), "* %s", device_name);
  else
    snprintf(dtitle, sizeof(dtitle), "  %s", device_name);

  ir_ui_draw_list(dtitle, ptrs, dev->count, sel, row_offset);

  infrared_encode_sys_init();

  while (1) {
    ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
    if (ret != pdTRUE)
      continue;

    if (q_item.q_evt_type == Q_EVENT_IRRED_TX) {
      /* TX complete notification */
      if (tx_active && infrared_transmit(0) == IR_TX_COMPLETED) {
        m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF,
                          LED_FASTBLINK_ONTIME_OFF);
        infrared_encode_sys_deinit();
        infrared_encode_sys_init();
        tx_active = false;
        /* Redraw list after send */
        ir_ui_draw_list(dtitle, ptrs, dev->count, sel, row_offset);
      }
      continue;
    }

    if (q_item.q_evt_type != Q_EVENT_KEYPAD)
      continue;

    ret = xQueueReceive(button_events_q_hdl, &btn, 0);
    if (ret != pdTRUE)
      continue;

    /* Drive TX state machine while processing keys */
    if (tx_active)
      infrared_transmit(0);

    /* RIGHT key toggles Favorite status */
    if (btn.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK) {
      ir_toggle_favorite(file_path);
      /* Redraw with updated title */
      if (ir_is_favorite(file_path))
        snprintf(dtitle, sizeof(dtitle), "* %s", device_name);
      else
        snprintf(dtitle, sizeof(dtitle), "  %s", device_name);
      ir_ui_draw_list(dtitle, ptrs, dev->count, sel, row_offset);
      continue;
    }

    if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
      if (tx_active) {
        if (ir_ota_data_tx_active)
          m1_ir_ota_frame_post_process(0xFF);
        m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF,
                          LED_FASTBLINK_ONTIME_OFF);
      }
      infrared_encode_sys_deinit();
      xQueueReset(main_q_hdl);
      vPortFree(dev);
      return true;
    }

    if (btn.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) {
      if (sel > 0) {
        sel--;
        if (sel < row_offset)
          row_offset = sel;
      }
      ir_ui_draw_list(device_name, ptrs, dev->count, sel, row_offset);
      continue;
    }

    if (btn.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK) {
      if (sel < dev->count - 1) {
        sel++;
        if (sel >= row_offset + IR_DISP_ROWS)
          row_offset = sel - IR_DISP_ROWS + 1;
      }
      ir_ui_draw_list(device_name, ptrs, dev->count, sel, row_offset);
      continue;
    }

    if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
      if (tx_active)
        continue; /* ignore OK while transmitting */

      /* Transmit selected command */
      ir_ui_show_sending(dev->cmds[sel].name);
      m1_buzzer_notification();
      m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_M,
                        LED_FASTBLINK_ONTIME_M);

      irsnd_generate_tx_data(dev->cmds[sel].irmp);
      infrared_transmit(1);
      infrared_transmit(0); /* Start the first frame */
      tx_active = true;
      continue;
    }
  }
}

/* -------------------------------------------------------------------------
 * Internal: one level of the 3-level browser
 * level 0 = Category, level 1 = Brand, level 2 = Device (.ir file)
 * base_path is the directory to list.
 * Returns true to go back up, false on fatal error.
 *
 * NOTE: This function uses recursion (max depth = 3 levels).
 * Task stack must be sufficient for 3 nested calls.
 * Current implementation is safe for the 3-level IR database structure.
 * ---------------------------------------------------------------------- */

static bool ir_browse_level(const char *base_path, const char *title,
                            uint8_t level) {
  uint16_t skip_count = 0;
  uint8_t items_per_page = IR_LIST_MAX_ENTRIES - 2; /* reserve for next/prev */

  while (1) {
    char (*names)[IR_NAME_BUF_LEN] =
        pvPortMalloc(IR_LIST_MAX_ENTRIES * IR_NAME_BUF_LEN);
    if (!names) {
      ir_ui_show_error("No memory");
      vTaskDelay(pdMS_TO_TICKS(1500));
      return true;
    }
    const char *ptrs[IR_LIST_MAX_ENTRIES];
    S_M1_Buttons_Status btn;
    S_M1_Main_Q_t q_item;
    BaseType_t ret;
    uint8_t count;
    uint8_t sel = 0;
    uint8_t row_offset = 0;
    uint8_t i;
    bool more_items = false;
    char child_path[IR_UNIVERSAL_PATH_LEN_MAX];
    char child_title[IR_UNIVERSAL_NAME_LEN_MAX];

    /* List directory contents with pagination */
    bool dirs_only = (level < 2);
    bool ir_files_only = (level >= 2);
    count = ir_list_dir(base_path, names, items_per_page, dirs_only,
                        ir_files_only, skip_count, &more_items);

    if (count == 0 && skip_count == 0) {
      ir_ui_show_error("Empty folder");
      vTaskDelay(pdMS_TO_TICKS(1500));
      vPortFree(names);
      return true;
    }

    /* Add Previous Page virtual item */
    if (skip_count > 0) {
      /* Shift items down */
      memmove(&names[1], &names[0], count * sizeof(names[0]));
      strcpy(names[0], "[Previous Page]");
      count++;
    }

    /* Add Next Page virtual item */
    if (more_items) {
      strcpy(names[count], "[Next Page]");
      count++;
    }

    for (i = 0; i < count; i++)
      ptrs[i] = names[i];

    /* Breadcrumb hint */
    char btitle[64];
    if (level == 0)
      snprintf(btitle, sizeof(btitle), "Root:%s", title);
    else if (level == 1)
      snprintf(btitle, sizeof(btitle), "Cat:%s", title);
    else
      snprintf(btitle, sizeof(btitle), "Dev:%s", title);

    ir_ui_draw_list(btitle, ptrs, count, sel, row_offset);

    bool reload = false;
    while (!reload) {
      /* Hardening: Check SD status periodically */
      if (m1_sdcard_get_status() != SD_access_OK) {
        ir_ui_show_error("SD Removed");
        vTaskDelay(pdMS_TO_TICKS(1500));
        vPortFree(names);
        return false; /* Exit completely */
      }

      ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
      if (ret != pdTRUE || q_item.q_evt_type != Q_EVENT_KEYPAD)
        continue;

      ret = xQueueReceive(button_events_q_hdl, &btn, 0);
      if (ret != pdTRUE)
        continue;

      if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
        xQueueReset(main_q_hdl);
        vPortFree(names);
        return true;
      }

      if (btn.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) {
        if (sel > 0) {
          sel--;
          if (sel < row_offset)
            row_offset = sel;
        }
        ir_ui_draw_list(title, ptrs, count, sel, row_offset);
        continue;
      }

      if (btn.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK) {
        if (sel < count - 1) {
          sel++;
          if (sel >= row_offset + IR_DISP_ROWS)
            row_offset = sel - IR_DISP_ROWS + 1;
        }
        ir_ui_draw_list(title, ptrs, count, sel, row_offset);
        continue;
      }

      if (btn.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK) {
        if (skip_count >= items_per_page) {
          skip_count -= items_per_page;
          reload = true;
        } else if (skip_count > 0) {
          skip_count = 0;
          reload = true;
        }
        if (reload) {
          /* Page indicator overlay */
          u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
          u8g2_DrawBox(&m1_u8g2, 30, 20, 68, 20);
          u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
          u8g2_DrawFrame(&m1_u8g2, 30, 20, 68, 20);
          char pbuf[16];
          snprintf(pbuf, sizeof(pbuf), "Page %d",
                   (skip_count / items_per_page) + 1);
          u8g2_DrawStr(&m1_u8g2, 40, 34, pbuf);
          m1_u8g2_nextpage();
          vTaskDelay(pdMS_TO_TICKS(500));
        }
        continue;
      }

      if (btn.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK) {
        if (more_items) {
          skip_count += items_per_page;
          reload = true;
        }
        if (reload) {
          /* Page indicator overlay */
          u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
          u8g2_DrawBox(&m1_u8g2, 30, 20, 68, 20);
          u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
          u8g2_DrawFrame(&m1_u8g2, 30, 20, 68, 20);
          char pbuf[16];
          snprintf(pbuf, sizeof(pbuf), "Page %d",
                   (skip_count / items_per_page) + 1);
          u8g2_DrawStr(&m1_u8g2, 40, 34, pbuf);
          m1_u8g2_nextpage();
          vTaskDelay(pdMS_TO_TICKS(500));
        }
        continue;
      }

      if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
        if (strcmp(names[sel], "[Previous Page]") == 0) {
          skip_count -= items_per_page;
          reload = true;
          continue;
        }
        if (strcmp(names[sel], "[Next Page]") == 0) {
          skip_count += items_per_page;
          reload = true;
          continue;
        }

        if (strstr(names[sel], "..") || strchr(names[sel], '/') ||
            strchr(names[sel], '\\')) {
          continue;
        }

        snprintf(child_path, sizeof(child_path), "%s/%s", base_path,
                 names[sel]);

        if (level < 2) {
          strncpy(child_title, names[sel], sizeof(child_title) - 1);
          child_title[sizeof(child_title) - 1] = '\0';
          ir_browse_level(child_path, child_title, level + 1);
        } else {
          strncpy(child_title, names[sel], sizeof(child_title) - 1);
          child_title[sizeof(child_title) - 1] = '\0';
          char *dot = strrchr(child_title, '.');
          if (dot)
            *dot = '\0';

          ir_add_recent(child_path);
          ir_run_command_ui(child_path, child_title);
        }

        ir_ui_draw_list(title, ptrs, count, sel, row_offset);
        continue;
      }
    }
    vPortFree(names);
  }
}

/* -------------------------------------------------------------------------
 * List UI for specific lists (Recent, Search)
 * ---------------------------------------------------------------------- */

static void ir_show_fixed_list(const char *title, char names[][IR_NAME_BUF_LEN],
                               char paths[][IR_UNIVERSAL_PATH_LEN_MAX],
                               uint8_t count) {
  const char *ptrs[IR_SEARCH_MAX_RESULTS];
  uint8_t sel = 0, row_offset = 0;
  S_M1_Buttons_Status btn;
  S_M1_Main_Q_t q_item;

  if (count == 0) {
    ir_ui_show_error("List empty");
    vTaskDelay(pdMS_TO_TICKS(1500));
    return;
  }

  for (uint8_t i = 0; i < count; i++)
    ptrs[i] = names[i];

  ir_ui_draw_list(title, ptrs, count, sel, row_offset);

  while (1) {
    if (xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY) != pdTRUE)
      continue;
    if (q_item.q_evt_type != Q_EVENT_KEYPAD)
      continue;
    if (xQueueReceive(button_events_q_hdl, &btn, 0) != pdTRUE)
      continue;

    if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
      return;

    if (btn.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) {
      if (sel > 0) {
        sel--;
        if (sel < row_offset)
          row_offset = sel;
      }
      ir_ui_draw_list(title, ptrs, count, sel, row_offset);
    }
    if (btn.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK) {
      if (sel < count - 1) {
        sel++;
        if (sel >= row_offset + IR_DISP_ROWS)
          row_offset = sel - IR_DISP_ROWS + 1;
      }
      ir_ui_draw_list(title, ptrs, count, sel, row_offset);
    }
    if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
      ir_add_recent(paths[sel]);
      ir_run_command_ui(paths[sel], names[sel]);
      ir_ui_draw_list(title, ptrs, count, sel, row_offset);
    }
  }
}

/* -------------------------------------------------------------------------
 * Dashboard & Main Loop
 * ---------------------------------------------------------------------- */

static void ir_dashboard(void) {
  const char *menu[] = {"Favorites", "Recent", "Search", "Browse Database"};
  uint8_t count = 4;
  uint8_t sel = 0;
  S_M1_Buttons_Status btn;
  S_M1_Main_Q_t q_item;

  ir_ui_draw_list("Universal Remote", menu, count, sel, 0);

  while (1) {
    if (xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY) != pdTRUE)
      continue;
    if (q_item.q_evt_type != Q_EVENT_KEYPAD)
      continue;
    if (xQueueReceive(button_events_q_hdl, &btn, 0) != pdTRUE)
      continue;

    if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
      return;

    if (btn.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) {
      if (sel > 0)
        sel--;
      ir_ui_draw_list("Universal Remote", menu, count, sel, 0);
    }
    if (btn.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK) {
      if (sel < count - 1)
        sel++;
      ir_ui_draw_list("Universal Remote", menu, count, sel, 0);
    }
    if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
      if (sel == 0) { /* Favorites */
        char (*names)[IR_NAME_BUF_LEN] =
            pvPortMalloc(IR_MAX_FAVORITES * IR_NAME_BUF_LEN);
        char (*paths)[IR_UNIVERSAL_PATH_LEN_MAX] =
            pvPortMalloc(IR_MAX_FAVORITES * IR_UNIVERSAL_PATH_LEN_MAX);

        if (names && paths) {
          uint8_t c = ir_list_from_file(IR_FAVORITES_FILE, names, paths,
                                        IR_MAX_FAVORITES);
          ir_show_fixed_list("Favorites", names, paths, c);
        } else {
          ir_ui_show_error("No memory");
          vTaskDelay(pdMS_TO_TICKS(1500));
        }
        if (names)
          vPortFree(names);
        if (paths)
          vPortFree(paths);
      } else if (sel == 1) { /* Recent */
        char (*names)[IR_NAME_BUF_LEN] =
            pvPortMalloc(IR_MAX_RECENT * IR_NAME_BUF_LEN);
        char (*paths)[IR_UNIVERSAL_PATH_LEN_MAX] =
            pvPortMalloc(IR_MAX_RECENT * IR_UNIVERSAL_PATH_LEN_MAX);

        if (names && paths) {
          uint8_t c =
              ir_list_from_file(IR_RECENT_FILE, names, paths, IR_MAX_RECENT);
          ir_show_fixed_list("Recent", names, paths, c);
        } else {
          ir_ui_show_error("No memory");
          vTaskDelay(pdMS_TO_TICKS(1500));
        }
        if (names)
          vPortFree(names);
        if (paths)
          vPortFree(paths);
      } else if (sel == 2) { /* Search */
        char query[32] = "";
        if (m1_vkb_get_filename("Search IR", "", query) > 0) {
          ir_ui_show_scanning();
          /* Lowercase query once for efficiency */
          for (int i = 0; query[i]; i++)
            query[i] = tolower((unsigned char)query[i]);

          char (*names)[IR_NAME_BUF_LEN] =
              pvPortMalloc(IR_SEARCH_MAX_RESULTS * IR_NAME_BUF_LEN);
          char (*paths)[IR_UNIVERSAL_PATH_LEN_MAX] =
              pvPortMalloc(IR_SEARCH_MAX_RESULTS * IR_UNIVERSAL_PATH_LEN_MAX);

          if (names && paths) {
            uint8_t c = 0;
            /* Scan database root */
            ir_search_recursive(IR_UNIVERSAL_SD_ROOT, query, names, paths, &c,
                                IR_SEARCH_MAX_RESULTS, 0);
            if (c > 0) {
              ir_show_fixed_list("Search Results", names, paths, c);
            } else {
              ir_ui_show_error("No matches found");
              vTaskDelay(pdMS_TO_TICKS(1500));
            }
          } else {
            ir_ui_show_error("No memory");
            vTaskDelay(pdMS_TO_TICKS(1500));
          }
          if (names)
            vPortFree(names);
          if (paths)
            vPortFree(paths);
        }
      } else if (sel == 3) { /* Browse */
        ir_browse_level(IR_UNIVERSAL_SD_ROOT, "Database", 0);
      }
      ir_ui_draw_list("Universal Remote", menu, count, sel, 0);
    }
  }
}

/* -------------------------------------------------------------------------
 * Public: ir_universal_run
 * ---------------------------------------------------------------------- */

void ir_universal_run(void) {
  /* Check SD card is available */
  if (m1_sdcard_get_status() != SD_access_OK) {
    ir_ui_show_error("No SD card");
    /* Wait for BACK to exit */
    S_M1_Buttons_Status btn;
    S_M1_Main_Q_t q_item;
    BaseType_t ret;
    while (1) {
      ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
      if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD) {
        xQueueReceive(button_events_q_hdl, &btn, 0);
        if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
          break;
      }
    }
    xQueueReset(main_q_hdl);
    return;
  }

  /* Ensure System directory exists for history/favorites */
  f_mkdir("0:/System");

  /* Enter the IR Dashboard */
  ir_dashboard();

  xQueueReset(main_q_hdl);
}
