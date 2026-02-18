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

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "ff.h"
#include "m1_ir_universal.h"
#include "m1_infrared.h"
#include "m1_display.h"
#include "m1_sdcard.h"
#include "m1_buzzer.h"
#include "m1_led_indicator.h"
#include "m1_tasks.h"
#include "m1_system.h"
#include "irsnd.h"

/* -------------------------------------------------------------------------
 * Internal constants
 * ---------------------------------------------------------------------- */

#define IR_LIST_MAX_ENTRIES     64   /* max dirs/files shown per level     */
#define IR_NAME_BUF_LEN         (FF_MAX_LFN + 1)
#define IR_LINE_BUF_LEN         128  /* max line length in a .ir file      */
#define IR_DISP_ROWS            6    /* visible rows on 128x64 with 5x8 font */
#define IR_DISP_COL_MAX         24   /* chars per row (128px / 5px font)   */

/* -------------------------------------------------------------------------
 * Protocol name -> IRMP ID mapping table
 * ---------------------------------------------------------------------- */

typedef struct {
    const char *name;
    uint8_t     id;
} S_IR_ProtoMap_t;

static const S_IR_ProtoMap_t ir_proto_map[] = {
    { "NEC",        IRMP_NEC_PROTOCOL       },
    { "NECext",     IRMP_NEC_PROTOCOL       },
    { "NEC42",      IRMP_NEC42_PROTOCOL     },
    { "NEC42ext",   IRMP_NEC42_PROTOCOL     },
    { "RC5",        IRMP_RC5_PROTOCOL       },
    { "RC5X",       IRMP_RC5_PROTOCOL       },
    { "RC6",        IRMP_RC6_PROTOCOL       },
    { "RC6A",       IRMP_RC6A_PROTOCOL      },
    { "Samsung32",  IRMP_SAMSUNG32_PROTOCOL },
    { "Samsung",    IRMP_SAMSUNG_PROTOCOL   },
    { "SIRC",       IRMP_SIRCS_PROTOCOL     },
    { "SIRC15",     IRMP_SIRCS_PROTOCOL     },
    { "SIRC20",     IRMP_SIRCS_PROTOCOL     },
    { "Kaseikyo",   IRMP_KASEIKYO_PROTOCOL  },
    { "Denon",      IRMP_DENON_PROTOCOL     },
    { "Sharp",      IRMP_DENON_PROTOCOL     },
    { "JVC",        IRMP_JVC_PROTOCOL       },
    { "Panasonic",  IRMP_PANASONIC_PROTOCOL },
    { "NEC16",      IRMP_NEC16_PROTOCOL     },
    { "LGAIR",      IRMP_LGAIR_PROTOCOL     },
    { "Samsung48",  IRMP_SAMSUNG48_PROTOCOL },
};

#define IR_PROTO_MAP_LEN  (sizeof(ir_proto_map) / sizeof(ir_proto_map[0]))

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

/* Trim leading/trailing whitespace in-place, return pointer to trimmed str */
static char *ir_trim(char *s)
{
    char *end;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        s++;
    if (*s == '\0')
        return s;
    end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
        end--;
    *(end + 1) = '\0';
    return s;
}

/* Parse a hex byte string like "07 00 00 00" -> return first two bytes as uint16 */
static uint16_t ir_parse_hex_field(const char *s)
{
    unsigned int b0 = 0, b1 = 0;
    sscanf(s, "%x %x", &b0, &b1);
    return (uint16_t)(b0 | (b1 << 8));
}

/* -------------------------------------------------------------------------
 * Public: ir_universal_proto_to_id
 * ---------------------------------------------------------------------- */

uint8_t ir_universal_proto_to_id(const char *name)
{
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
static void ir_save_block(S_IR_Device_t *out, S_IR_Cmd_t *cur, uint8_t in_block)
{
    if (in_block && cur->valid && out->count < IR_UNIVERSAL_CMDS_MAX) {
        memcpy(&out->cmds[out->count], cur, sizeof(S_IR_Cmd_t));
        out->count++;
    }
}

uint8_t ir_universal_parse_file(const char *path, S_IR_Device_t *out)
{
    FIL     f;
    FRESULT fr;
    char    line[IR_LINE_BUF_LEN];
    char    key[32], val[96];
    char   *p, *eq;
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
        strncpy(key, ir_trim(p),      sizeof(key) - 1);
        strncpy(val, ir_trim(eq + 1), sizeof(val) - 1);
        key[sizeof(key)-1] = '\0';
        val[sizeof(val)-1] = '\0';

        /* Detect start of a new button block */
        if (strcmp(key, "name") == 0) {
            ir_save_block(out, &cur, in_block);
            memset(&cur, 0, sizeof(cur));
            strncpy(cur.name, val, IR_UNIVERSAL_NAME_LEN_MAX - 1);
            in_block = 1;
            continue;
        }

        if (!in_block)
            continue;

        if (strcmp(key, "type") == 0) {
            /* Only support parsed (not raw) signals */
            if (strcmp(val, "parsed") != 0)
                in_block = 0; /* skip this block */
            continue;
        }

        if (strcmp(key, "protocol") == 0) {
            cur.irmp.protocol = ir_universal_proto_to_id(val);
            if (cur.irmp.protocol == IRMP_UNKNOWN_PROTOCOL)
                in_block = 0; /* unsupported protocol, skip */
            continue;
        }

        if (strcmp(key, "address") == 0) {
            cur.irmp.address = ir_parse_hex_field(val);
            continue;
        }

        if (strcmp(key, "command") == 0) {
            cur.irmp.command = ir_parse_hex_field(val);
            cur.irmp.flags   = 0;
            cur.valid        = true;
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
static void ir_ui_draw_list(const char *title,
                             const char *entries[],
                             uint8_t     count,
                             uint8_t     sel,
                             uint8_t     row_offset)
{
    uint8_t row, disp_row;
    char    trunc[IR_DISP_COL_MAX + 1];
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
    for (row = row_offset; row < count && disp_row < IR_DISP_ROWS; row++, disp_row++) {
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
        if (bar_h < 4) bar_h = 4;
        uint8_t bar_y = (uint8_t)(10 + (uint16_t)row_offset * (54 - bar_h) / (count - IR_DISP_ROWS));
        u8g2_DrawFrame(&m1_u8g2, 125, 10, 3, 54);
        u8g2_DrawBox(&m1_u8g2,   126, bar_y, 1, bar_h);
    }

    m1_u8g2_nextpage();
}

/* Show a simple error message */
static void ir_ui_show_error(const char *msg)
{
    m1_u8g2_firstpage();
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
    u8g2_DrawXBMP(&m1_u8g2, 2, 2, 48, 25, remote_48x25);
    u8g2_DrawStr(&m1_u8g2, 55, 15, "IR Error:");
    u8g2_DrawStr(&m1_u8g2, 2,  30, msg);
    m1_u8g2_nextpage();
}

/* Show "Sending..." overlay */
static void ir_ui_show_sending(const char *cmd_name)
{
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

/* -------------------------------------------------------------------------
 * Internal: list a directory, return count of entries
 * entries[] must point to a caller-allocated array of IR_LIST_MAX_ENTRIES
 * char[IR_NAME_BUF_LEN] buffers.
 * Returns number of entries found (dirs first, then files).
 * ---------------------------------------------------------------------- */

static uint8_t ir_list_dir(const char *path,
                            char        names[][IR_NAME_BUF_LEN],
                            uint8_t     max_entries,
                            bool        dirs_only,
                            bool        ir_files_only)
{
    DIR     dir;
    FILINFO fi;
    FRESULT fr;
    uint8_t count = 0;

    fr = f_opendir(&dir, path);
    if (fr != FR_OK)
        return 0;

    while (count < max_entries) {
        fr = f_readdir(&dir, &fi);
        if (fr != FR_OK || fi.fname[0] == '\0')
            break;

        /* Skip hidden/system entries */
        if (fi.fattrib & (AM_HID | AM_SYS))
            continue;

        if (dirs_only && !(fi.fattrib & AM_DIR))
            continue;

        if (ir_files_only) {
            /* Accept only .ir files */
            size_t len = strlen(fi.fname);
            if (len < 3)
                continue;
            if (fi.fname[len-3] != '.' ||
                (fi.fname[len-2] != 'i' && fi.fname[len-2] != 'I') ||
                (fi.fname[len-1] != 'r' && fi.fname[len-1] != 'R'))
                continue;
        }

        strncpy(names[count], fi.fname, IR_NAME_BUF_LEN - 1);
        names[count][IR_NAME_BUF_LEN - 1] = '\0';
        count++;
    }

    f_closedir(&dir);
    return count;
}

/* -------------------------------------------------------------------------
 * Internal: command browse + transmit loop
 * Shows the list of buttons in a device .ir file.
 * Returns true if user pressed BACK (go up), false on fatal error.
 * ---------------------------------------------------------------------- */

static bool ir_run_command_ui(const char *file_path, const char *device_name)
{
    S_IR_Device_t   *dev;
    const char      *ptrs[IR_UNIVERSAL_CMDS_MAX];
    S_M1_Buttons_Status btn;
    S_M1_Main_Q_t   q_item;
    BaseType_t      ret;
    uint8_t         sel        = 0;
    uint8_t         row_offset = 0;
    uint8_t         i;
    bool            tx_active  = false;

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
    ir_ui_draw_list(device_name, ptrs, dev->count, sel, row_offset);

    infrared_encode_sys_init();

    while (1) {
        ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
        if (ret != pdTRUE)
            continue;

        if (q_item.q_evt_type == Q_EVENT_IRRED_TX) {
            /* TX complete notification */
            if (tx_active) {
                m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
                infrared_encode_sys_deinit();
                infrared_encode_sys_init();
                tx_active = false;
                /* Redraw list after send */
                ir_ui_draw_list(device_name, ptrs, dev->count, sel, row_offset);
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

        if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
            if (tx_active) {
                if (ir_ota_data_tx_active)
                    m1_ir_ota_frame_post_process(0xFF);
                m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
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
            m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M);

            irsnd_generate_tx_data(dev->cmds[sel].irmp);
            infrared_transmit(1);
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
 * ---------------------------------------------------------------------- */

static bool ir_browse_level(const char *base_path,
                             const char *title,
                             uint8_t     level)
{
    /* Static storage for names to avoid large stack allocations */
    static char names[IR_LIST_MAX_ENTRIES][IR_NAME_BUF_LEN];
    const char *ptrs[IR_LIST_MAX_ENTRIES];
    S_M1_Buttons_Status btn;
    S_M1_Main_Q_t   q_item;
    BaseType_t      ret;
    uint8_t         count;
    uint8_t         sel        = 0;
    uint8_t         row_offset = 0;
    uint8_t         i;
    char            child_path[IR_UNIVERSAL_PATH_LEN_MAX];
    char            child_title[IR_UNIVERSAL_NAME_LEN_MAX];

    /* List directory contents */
    if (level < 2) {
        /* Category and Brand levels: directories only */
        count = ir_list_dir(base_path, names, IR_LIST_MAX_ENTRIES, true, false);
    } else {
        /* Device level: .ir files only */
        count = ir_list_dir(base_path, names, IR_LIST_MAX_ENTRIES, false, true);
    }

    if (count == 0) {
        ir_ui_show_error("Empty folder");
        vTaskDelay(pdMS_TO_TICKS(1500));
        return true;
    }

    for (i = 0; i < count; i++)
        ptrs[i] = names[i];

    ir_ui_draw_list(title, ptrs, count, sel, row_offset);

    while (1) {
        ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
        if (ret != pdTRUE)
            continue;

        if (q_item.q_evt_type != Q_EVENT_KEYPAD)
            continue;

        ret = xQueueReceive(button_events_q_hdl, &btn, 0);
        if (ret != pdTRUE)
            continue;

        if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
            xQueueReset(main_q_hdl);
            return true; /* go up one level */
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

        if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
            /* Build child path */
            snprintf(child_path, sizeof(child_path), "%s/%s", base_path, names[sel]);

            if (level < 2) {
                /* Descend into sub-directory */
                strncpy(child_title, names[sel], sizeof(child_title) - 1);
                child_title[sizeof(child_title)-1] = '\0';
                /* Recurse into next level */
                ir_browse_level(child_path, child_title, level + 1);
            } else {
                /* Level 2: open the .ir file */
                /* Strip .ir extension for display title */
                strncpy(child_title, names[sel], sizeof(child_title) - 1);
                child_title[sizeof(child_title)-1] = '\0';
                char* dot = strrchr(child_title, '.');
                if (dot && (dot[1] == 'i' || dot[1] == 'I') &&
                    (dot[2] == 'r' || dot[2] == 'R') && dot[3] == '\0') {
                    *dot = '\0';
                }

                ir_run_command_ui(child_path, child_title);
            }

            /* Redraw this level after returning from child */
            ir_ui_draw_list(title, ptrs, count, sel, row_offset);
            continue;
        }
    }
}

/* -------------------------------------------------------------------------
 * Public: ir_universal_run
 * ---------------------------------------------------------------------- */

void ir_universal_run(void)
{
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

    /* Enter the 3-level browser starting at the IR root */
    ir_browse_level(IR_UNIVERSAL_SD_ROOT, "Universal Remote", 0);

    xQueueReset(main_q_hdl);
}
