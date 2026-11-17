/* See COPYING.txt for license details. */

/*
*
*  m1_settings.c
*
*  M1 settings screens
*
*  Modal settings-screen idiom (introduced by the LCD & Notifications and
*  System screens; copy it for future settings screens such as Sub-GHz
*  Radio Settings or Bluetooth):
*   - one full-screen modal function dispatched as a menu leaf's sub_func,
*     running in subfunc_handler_task (the settings store's single writer);
*   - draw with u8g2_FirstPage()/m1_u8g2_nextpage() page pairs; rows are
*     label/value lines, the selected row gets an inverted bar and the
*     bold font (same look as the submenu renderer);
*   - UP/DOWN move the selection (wrapping), LEFT/RIGHT move the selected
*     row's value toward its minimum/maximum, OK activates a row action
*     (edit text, confirm), BACK exits;
*   - edits update the live settings image (settings_set_*()) and apply
*     to the consuming subsystem immediately (live preview); after each
*     settings_set_*() re-sync the row local from settings_get() so a
*     clamping setter can never make displayed, effective and persisted
*     values diverge; the exit
*     path calls settings_exit_save() so one atomic settings_save()
*     persists the editing session -- skipped entirely when the session
*     changed nothing, so a browse-only visit never touches the card --
*     and a refused save of real edits is shown, not hidden;
*   - the button loop is the standard xQueueReceive(main_q_hdl) +
*     xQueueReceive(button_events_q_hdl, 0) pattern of settings_about(),
*     with the event-receive result checked (pdFALSE skips the event,
*     like m1_vkb_get_filename()) so a stale or uninitialized button
*     struct is never acted on.
*  The Date & Time screen (PR-3) reuses the modal idiom with the button
*  axes the clock-editor layout calls for: LEFT/RIGHT select the field,
*  UP/DOWN step its value, OK applies the whole edited date/time to the
*  RTC (validated by the pure domain first, then re-synced by reading
*  the RTC back -- the idiom's post-setter re-sync with the RTC playing
*  the role of the store), and BACK exits WITHOUT applying: the
*  battery-backed RTC is its own persistence, so this screen never
*  marks a settings session dirty, never calls settings_save(), and
*  time is deliberately NOT a settings.cfg field (Reset defaults
*  leaves the clock alone). The current date/time is shown read-only in
*  the System screen, refreshed on entry.
*  Value stepping/formatting logic lives in the host-testable
*  M1_SETTINGS_SCREENS_PURE_CORE region below.
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_settings.h"
#include "m1_buzzer.h"
#include "m1_fw_update_bl.h"
#include "m1_settings_store.h"
#include "m1_feedback_manager.h"
#include "m1_virtual_kb.h"
#include "m1_sdcard.h"
#include "m1_file_util.h"
#include "m1_power_ctl.h"
#include "m1_rtc.h"

/*************************** D E F I N E S ************************************/

#define SETTING_ABOUT_CHOICES_MAX		2 //5

#define ABOUT_BOX_Y_POS_ROW_1			10
#define ABOUT_BOX_Y_POS_ROW_2			20
#define ABOUT_BOX_Y_POS_ROW_3			30
#define ABOUT_BOX_Y_POS_ROW_4			40
#define ABOUT_BOX_Y_POS_ROW_5			50

/* LCD & Notifications screen layout (128x64): bold title row, three
 * value rows, inverted hint bar at the bottom like settings_about(). */
#define SETTING_LCD_ROW_FIRST_Y			24
#define SETTING_LCD_ROW_SPACE			12
#define SETTING_LCD_ROWS_N				3
#define SETTING_LCD_VALUE_RIGHT_X	(M1_LCD_DISPLAY_WIDTH - 4)

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void menu_settings_init(void);
void menu_settings_exit(void);
void settings_about(void);
static void settings_about_display_choice(uint8_t choice);
void settings_lcd_notifications(void);
static void settings_lcd_gui_update(uint8_t sel_row, uint8_t bright, uint16_t timeout_s, uint8_t led_notify);
static void settings_exit_save(uint8_t session_dirty);
void settings_str_clip(char *dst, size_t dst_size, const char *src);
void settings_system(void);
static void settings_system_gui_update(uint8_t sel_row, const char *dev_name, uint8_t time_ok, const char *time_str, uint8_t storage_ok, uint64_t free_b, uint32_t total_kb);
static uint8_t settings_system_edit_name(char *name_buf, size_t buf_size);
static void settings_system_reset_confirm(void);
void settings_date_time(void);
static void settings_dt_gui_update(uint8_t sel_field, const S_M1_DateTime_t *dt);
static uint8_t settings_dt_draw_seg(uint8_t x, uint8_t y, uint8_t seg_top_off, uint8_t seg_h, const char *txt, uint8_t selected);
static void settings_dt_notice(const uint8_t *icon_10x10, const char *msg, uint16_t show_ms);

/* ================= M1_SETTINGS_SCREENS_PURE_CORE BEGIN ================= */
/* Value stepping and label formatting for the settings screens. Plain
 * C99 over stdint/stddef only -- no HAL, no FreeRTOS, no u8g2 -- so the
 * host unit test (m1_csrc/test/m1_settings_screens_test.py) extracts
 * exactly this region and compiles it standalone. Screens keep their
 * decision logic here, not inlined in the draw/button code.
 * Bounds mirror the settings store's field table: brightness 0..100,
 * screen timeout 0 (Off) / 10..120 s, LED notifications 0/1. Steps are
 * +/-10 (the timeout's Off sits one Left-press below 10 s), clamped at
 * both ends; the store admits off-grid timeout values (its compiled
 * default is 45 s), so stepping is arithmetic, not table-walking; a
 * nonzero sub-10 step result snaps to 10 (the store clamps 1..9 to 10)
 * so stepped values always stay inside the admitted domain. */

uint8_t settings_scr_bright_step(uint8_t cur, int8_t dir)
{
	if ( dir > 0 )
		return (uint8_t)((cur > 100u - 10u) ? 100u : cur + 10u);
	if ( dir < 0 )
		return (uint8_t)((cur < 10u) ? 0u : cur - 10u);
	return cur;
} // uint8_t settings_scr_bright_step(uint8_t cur, int8_t dir)

uint16_t settings_scr_timeout_step(uint16_t cur, int8_t dir)
{
	uint16_t next;

	if ( dir > 0 )
		return (uint16_t)((cur > 120u - 10u) ? 120u : cur + 10u);
	if ( dir < 0 )
	{
		next = (uint16_t)((cur < 10u) ? 0u : cur - 10u); /* 10 -> 0 = Off */
		if ( (next!=0u) && (next<10u) )
		{
			/* Off-grid cur in 11..19 lands in 1..9, which the store
			 * clamps to 10: snap so displayed, effective and persisted
			 * values cannot diverge (off-grid values >=20, e.g. the
			 * compiled 45 s default, keep stepping arithmetically). */
			next = 10u;
		}
		return next;
	}
	return cur;
} // uint16_t settings_scr_timeout_step(uint16_t cur, int8_t dir)

/* "Off" for 0, else "<n>s". buf must hold at least 6 bytes. */
const char *settings_scr_timeout_label(uint16_t timeout_s, char *buf, size_t bufsize)
{
	if ( timeout_s==0u )
		return "Off";
	snprintf(buf, bufsize, "%us", (unsigned)timeout_s);
	return buf;
} // const char *settings_scr_timeout_label(...)

/* "On"/"Off"; Right-press means toward On, Left toward Off. */
const char *settings_scr_onoff_label(uint8_t on)
{
	return on ? "On" : "Off";
} // const char *settings_scr_onoff_label(uint8_t on)

/* Bounded copy (src may be longer than dst: display clipping and the
 * store's fixed field bound make that a normal case, so this never
 * invokes strncpy/snprintf patterns GCC cannot prove safe). */
void settings_str_clip(char *dst, size_t dst_size, const char *src)
{
	size_t len;

	if ( dst_size==0u )
		return;
	len = strlen(src);
	if ( len >= dst_size )
		len = dst_size - 1u;
	memcpy(dst, src, len);
	dst[len] = '\0';
} // void settings_str_clip(char *dst, size_t dst_size, const char *src)

/* ================= M1_SETTINGS_SCREENS_PURE_CORE END =================== */

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void menu_settings_init(void)
{
	;
} // void menu_settings_init(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void menu_settings_exit(void)
{
	;
} // void menu_settings_exit(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void settings_buzzer(void)
{
	//buzzer_demo_play();
} // void settings_sound(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void settings_power(void)
{
	;
} // void settings_power(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void settings_about(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t choice;

	/* Graphic work starts here */
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12); // Draw an inverted bar at the bottom to display options
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // Write text in inverted color
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	u8g2_DrawXBMP(&m1_u8g2, 1, 53, 8, 8, arrowleft_8x8); // draw arrowleft icon
	u8g2_DrawStr(&m1_u8g2, 11, 61, "Prev.");
	u8g2_DrawXBMP(&m1_u8g2, 119, 53, 8, 8, arrowright_8x8); // draw arrowright icon
	u8g2_DrawStr(&m1_u8g2, 97, 61, "Next");
	m1_u8g2_nextpage(); // Update display RAM

	choice = 0;
	settings_about_display_choice(choice);

	while (1 ) // Main loop of this task
	{
		;
		; // Do other parts of this task here
		;

		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE)
		{
			if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			{
				// Notification is only sent to this task when there's any button activity,
				// so it doesn't need to wait when reading the event from the queue
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( ret!=pdTRUE )
				{
					continue; // No event behind the notification: never act on a stale struct
				} // if ( ret!=pdTRUE )
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
				{
					; // Do extra tasks here if needed
					xQueueReset(main_q_hdl); // Reset main q before return
					break; // Exit and return to the calling task (subfunc_handler_task)
				} // if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK ) // Previous?
				{
					choice--;
					if ( choice > SETTING_ABOUT_CHOICES_MAX )
						choice = SETTING_ABOUT_CHOICES_MAX;
					settings_about_display_choice(choice);
				} // else if ( this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK ) // Next?
				{
					choice++;
					if ( choice > SETTING_ABOUT_CHOICES_MAX )
						choice = 0;
					settings_about_display_choice(choice);
				} // else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			else
			{
				; // Do other things for this task
			}
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task

} // void settings_about(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void settings_about_display_choice(uint8_t choice)
{
	char prn_name[20];

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // Set background color
	u8g2_DrawBox(&m1_u8g2, 0, 0, M1_LCD_DISPLAY_WIDTH, ABOUT_BOX_Y_POS_ROW_5 + 1); // Clear old content
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // Set text color

	switch (choice)
	{
		case 0: // FW info
			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B); // Set bold font
			u8g2_DrawStr(&m1_u8g2, 0, ABOUT_BOX_Y_POS_ROW_1, "FW version info:");
			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N); // Set normal font
			sprintf(prn_name, "%d.%d.%d.%d", m1_device_stat.config.fw_version_major, m1_device_stat.config.fw_version_minor, m1_device_stat.config.fw_version_build, m1_device_stat.config.fw_version_rc);
			u8g2_DrawStr(&m1_u8g2, 0, ABOUT_BOX_Y_POS_ROW_2, prn_name);
			sprintf(prn_name, "Active bank: %d", (m1_device_stat.active_bank==BANK1_ACTIVE)?1:2);
			u8g2_DrawStr(&m1_u8g2, 0, ABOUT_BOX_Y_POS_ROW_3, prn_name);
			u8g2_DrawStr(&m1_u8g2, 0, ABOUT_BOX_Y_POS_ROW_4, FW_BUILD_LABEL); // Build label
			/* Device name (System screen setting) on the free fifth row;
			 * prn_name bounds the copy to what this page can render. */
			u8g2_DrawStr(&m1_u8g2, 0, ABOUT_BOX_Y_POS_ROW_5, "Name:");
			settings_str_clip(prn_name, sizeof(prn_name), settings_get()->device_name);
			u8g2_DrawStr(&m1_u8g2, 25, ABOUT_BOX_Y_POS_ROW_5, prn_name);
			break;
/*
		case 1: // HW info
			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B); // Set bold font
			u8g2_DrawStr(&m1_u8g2, 0, ABOUT_BOX_Y_POS_ROW_1, "HW version info:");
			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N); // Set normal font
			sprintf(prn_name, "Rev2.7R-09252025");
			u8g2_DrawStr(&m1_u8g2, 0, ABOUT_BOX_Y_POS_ROW_2, prn_name);
			u8g2_DrawStr(&m1_u8g2, 0, ABOUT_BOX_Y_POS_ROW_3, "Serial number:");
			sprintf(prn_name, "xxx-xxx-xxx");
			u8g2_DrawStr(&m1_u8g2, 0, ABOUT_BOX_Y_POS_ROW_4, prn_name);
			break;

		case 2: // Product info
			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B); // Set bold font
			u8g2_DrawStr(&m1_u8g2, 0, ABOUT_BOX_Y_POS_ROW_1, "Product: M1");
			u8g2_DrawStr(&m1_u8g2, 0, ABOUT_BOX_Y_POS_ROW_2, "Model: M1.1");
			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N); // Set normal font
			sprintf(prn_name, "FCC ID: Pending");
			u8g2_DrawStr(&m1_u8g2, 0, ABOUT_BOX_Y_POS_ROW_3, prn_name);
			sprintf(prn_name, "IC: Pending");
			u8g2_DrawStr(&m1_u8g2, 0, ABOUT_BOX_Y_POS_ROW_4, prn_name);
			break;

		case 3: // Certificates info
			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N); // Set small font
			u8g2_DrawStr(&m1_u8g2, 0, ABOUT_BOX_Y_POS_ROW_1, "For all compliance");
			u8g2_DrawStr(&m1_u8g2, 0, ABOUT_BOX_Y_POS_ROW_2, "certificates, please visit:");
			u8g2_DrawStr(&m1_u8g2, 0, ABOUT_BOX_Y_POS_ROW_3, "www.monstatek.com/compliance");
			break;
*/
		case 1: // Company info
			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N); // Set small font
			u8g2_DrawStr(&m1_u8g2, 0, ABOUT_BOX_Y_POS_ROW_1, "MonstaTek Inc.");
			u8g2_DrawStr(&m1_u8g2, 0, ABOUT_BOX_Y_POS_ROW_2, "San Jose, CA, USA");
			break;

		default:
			u8g2_DrawXBMP(&m1_u8g2, 23, 1, 82, 36, m1_device_82x36);
			break;
	} // switch (choice)

	m1_u8g2_nextpage(); // Update display RAM
} // static void settings_about_display_choice(uint8_t choice)


/*============================================================================*/
/**
  * @brief Shared exit path for settings screens: persist the editing
  *        session when it changed something, then reset the queue for
  *        the return to the menu.
  *
  * One atomic settings_save() per edited screen exit (the store's single
  * writer is this UI task; save runs alone, never concurrent with another
  * FatFs user). A session that edited nothing skips the save entirely:
  * no SD churn for a browse-only visit, and no misleading "Save failed"
  * notice when the boot load was refused but nothing was changed. A
  * refused/failed save of real edits is already logged by the store --
  * here it is also shown to the user for ~1.5 s, never silently
  * swallowed, and the in-RAM/live-applied edits stay in effect for this
  * power cycle.
  */
/*============================================================================*/
static void settings_exit_save(uint8_t session_dirty)
{
	if ( session_dirty && !settings_save() )
	{
		u8g2_FirstPage(&m1_u8g2);
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 13, 28, "Save failed - changes");
		u8g2_DrawStr(&m1_u8g2, 13, 38, "apply until reboot");
		m1_u8g2_nextpage(); // Update display RAM
		vTaskDelay(pdMS_TO_TICKS(1500));
	} // if ( session_dirty && !settings_save() )

	xQueueReset(main_q_hdl); // Reset main q before return
} // static void settings_exit_save(uint8_t session_dirty)



/*============================================================================*/
/**
  * @brief Draw the LCD & Notifications screen (see settings_lcd_notifications).
  */
/*============================================================================*/
static void settings_lcd_gui_update(uint8_t sel_row, uint8_t bright, uint16_t timeout_s, uint8_t led_notify)
{
	char val_str[8];
	uint8_t i, row_y;
	const char *val;

	/* Graphic work starts here */
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, 0, 9, "LCD & Notifications");

	for (i=0; i<SETTING_LCD_ROWS_N; i++)
	{
		row_y = SETTING_LCD_ROW_FIRST_Y + i*SETTING_LCD_ROW_SPACE;
		switch (i)
		{
			case 0: // Brightness
				snprintf(val_str, sizeof(val_str), "%u%%", (unsigned)bright);
				val = val_str;
				break;
			case 1: // Screen timeout
				val = settings_scr_timeout_label(timeout_s, val_str, sizeof(val_str));
				break;
			default: // LED notifications
				val = settings_scr_onoff_label(led_notify);
				break;
		} // switch (i)

		if ( i==sel_row )
		{
			// Inverted selection bar with the bold font, like the submenu renderer
			u8g2_DrawBox(&m1_u8g2, 0, row_y - 9, M1_LCD_DISPLAY_WIDTH, 11);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
		} // if ( i==sel_row )
		else
		{
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
		} // else

		u8g2_DrawStr(&m1_u8g2, 4, row_y, (i==0) ? "Brightness" : ((i==1) ? "Timeout" : "LED Notify"));
		u8g2_DrawStr(&m1_u8g2, SETTING_LCD_VALUE_RIGHT_X - u8g2_GetStrWidth(&m1_u8g2, val), row_y, val);
	} // for (i=0; i<SETTING_LCD_ROWS_N; i++)

	// Inverted hint bar: LEFT moves the value down/toward Off, RIGHT up/toward On
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_DrawBox(&m1_u8g2, 0, 52, M1_LCD_DISPLAY_WIDTH, 12);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // Write text in inverted color
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	u8g2_DrawXBMP(&m1_u8g2, 1, 53, 8, 8, arrowleft_8x8); // draw arrowleft icon
	u8g2_DrawStr(&m1_u8g2, 11, 61, "-");
	u8g2_DrawStr(&m1_u8g2, 117, 61, "+");
	u8g2_DrawXBMP(&m1_u8g2, 119, 53, 8, 8, arrowright_8x8); // draw arrowright icon
	m1_u8g2_nextpage(); // Update display RAM
} // static void settings_lcd_gui_update(...)



/*============================================================================*/
/**
  * @brief LCD & Notifications settings screen (issue #4).
  *
  * Rows: Brightness 0..100%, Screen timeout Off/10..120 s, LED
  * notifications On/Off. Edits apply live (brightness through
  * fb_set_brightness_pct() so the change is visible immediately; the LED
  * gate through fb_set_led_notifications_enabled(); the timeout is read
  * by lcd_saver_update() on its next periodic check) and are persisted by
  * one atomic settings_save() when the user leaves the screen with BACK,
  * skipped entirely when the session edited nothing.
  */
/*============================================================================*/
void settings_lcd_notifications(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t sel_row, bright, led_notify, session_dirty;
	uint16_t timeout_s;
	int8_t dir;

	bright = settings_get()->brightness_pct;
	timeout_s = settings_get()->screen_timeout_s;
	led_notify = settings_get()->led_notifications;

	sel_row = 0;
	session_dirty = 0u; // Set by any value edit; BACK saves only when set
	settings_lcd_gui_update(sel_row, bright, timeout_s, led_notify);

	while (1 ) // Main loop of this task
	{
		;
		; // Do other parts of this task here
		;

		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE)
		{
			if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			{
				// Notification is only sent to this task when there's any button activity,
				// so it doesn't need to wait when reading the event from the queue
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( ret!=pdTRUE )
				{
					continue; // No event behind the notification: never act on a stale struct
				} // if ( ret!=pdTRUE )
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
				{
					settings_exit_save(session_dirty); // Persist this session atomically
					break; // Exit and return to the calling task (subfunc_handler_task)
				} // if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK ) // Previous row?
				{
					sel_row = (uint8_t)((sel_row + SETTING_LCD_ROWS_N - 1) % SETTING_LCD_ROWS_N);
					settings_lcd_gui_update(sel_row, bright, timeout_s, led_notify);
				} // else if ( this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK ) // Next row?
				{
					sel_row = (uint8_t)((sel_row + 1) % SETTING_LCD_ROWS_N);
					settings_lcd_gui_update(sel_row, bright, timeout_s, led_notify);
				} // else if ( this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK ||
					this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK ) // Adjust value?
				{
					dir = (this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK) ? 1 : -1;
					session_dirty = 1u; // A value edit happened: persist it on exit
					switch (sel_row)
					{
						case 0: // Brightness: live preview through the feedback manager
							bright = settings_scr_bright_step(bright, dir);
							settings_set_brightness_pct(bright);
							bright = settings_get()->brightness_pct; // re-sync after the clamping setter
							fb_set_brightness_pct(bright);
							break;
						case 1: // Screen timeout: consumer reads it on its next check
							timeout_s = settings_scr_timeout_step(timeout_s, dir);
							settings_set_screen_timeout_s(timeout_s);
							timeout_s = settings_get()->screen_timeout_s; // re-sync after the clamping setter
							break;
						default: // LED notifications: Left = Off, Right = On (directional)
							led_notify = (dir > 0) ? 1u : 0u;
							settings_set_led_notifications(led_notify);
							led_notify = settings_get()->led_notifications; // re-sync after the clamping setter
							fb_set_led_notifications_enabled(led_notify != 0u);
							break;
					} // switch (sel_row)
					settings_lcd_gui_update(sel_row, bright, timeout_s, led_notify);
				} // else if ( LEFT/RIGHT == BUTTON_EVENT_CLICK )
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			else
			{
				; // Do other things for this task
			}
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task

} // void settings_lcd_notifications(void)


/*============================================================================*/
/**
  * @brief System settings screen (issue #7, non-RTC part + PR-3 time row).
  *
  * Rows: current date/time (read-only, from the RTC on entry -- the
  * RTC is the timebase, no settings-store copy exists), device Name
  * (OK opens the virtual keyboard; persisted with the settings store,
  * 32-char store bound -- the keyboard itself caps entry at its own
  * 20-char buffer), SD storage totals (informational), and Reset
  * defaults (OK opens a confirmation; executing it restores the
  * compiled defaults, persists them over the stored file, and reboots
  * through the standard power_reboot() path). The flash metadata
  * sector is never touched -- user settings live only on the SD card,
  * and the clock lives only in the RTC.
  */
/*============================================================================*/
#define SETTING_SYS_TIME_ROW_Y			18
#define SETTING_SYS_NAME_ROW_Y			28
#define SETTING_SYS_STORAGE_ROW_Y		38
#define SETTING_SYS_RESET_ROW_Y			48

void settings_system(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t sel_row, storage_ok, session_dirty, time_ok;
	uint64_t free_b;
	uint32_t total_kb;
	char name_buf[33]; // store bound (32) + NUL
	char time_str[M1_RTC_DATETIME_STR_LEN];
	S_M1_DateTime_t rtc_now;

	settings_str_clip(name_buf, sizeof(name_buf), settings_get()->device_name);

	/* Time snapshot, taken once on entry (informational like the storage
	 * row; the Date & Time leaf is where the clock is edited). */
	time_str[0] = '\0';
	time_ok = (uint8_t)m1_rtc_get_datetime(&rtc_now);
	if ( time_ok )
		m1_rtc_format_datetime(&rtc_now, time_str, sizeof(time_str));

	/* Storage snapshot, taken once on entry (same sources as the
	 * Settings > Storage > About SD Card screen: free bytes via
	 * fs_get_free_space(), total via the SD driver's mount-time scan). */
	storage_ok = 0;
	free_b = 0;
	total_kb = 0;
	if ( m1_sdcard_get_status()==SD_access_OK )
	{
		S_M1_SDCard_Info *info = m1_sdcard_get_info();

		if ( info!=NULL && fs_get_free_space(&free_b)==FR_OK )
		{
			total_kb = info->total_cap_kb;
			storage_ok = 1;
		}
	} // if ( m1_sdcard_get_status()==SD_access_OK )

	sel_row = 0; // Only the Name and Reset rows are actionable
	session_dirty = 0u; // Set when the name edit changes it; BACK saves only when set
	settings_system_gui_update(sel_row, name_buf, time_ok, time_str, storage_ok, free_b, total_kb);

	while (1 ) // Main loop of this task
	{
		;
		; // Do other parts of this task here
		;

		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE)
		{
			if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			{
				// Notification is only sent to this task when there's any button activity,
				// so it doesn't need to wait when reading the event from the queue
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( ret!=pdTRUE )
				{
					continue; // No event behind the notification: never act on a stale struct
				} // if ( ret!=pdTRUE )
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
				{
					settings_exit_save(session_dirty); // Persist this session atomically
					break; // Exit and return to the calling task (subfunc_handler_task)
				} // if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK ||
					this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK ) // Switch row?
				{
					sel_row = (sel_row==0) ? 1 : 0; // Two actionable rows
					settings_system_gui_update(sel_row, name_buf, time_ok, time_str, storage_ok, free_b, total_kb);
				} // else if ( UP/DOWN == BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK ) // Row action?
				{
					if ( sel_row==0 ) // Name: open the virtual keyboard
					{
						session_dirty |= settings_system_edit_name(name_buf, sizeof(name_buf));
						settings_system_gui_update(sel_row, name_buf, time_ok, time_str, storage_ok, free_b, total_kb);
					} // if ( sel_row==0 )
					else // Reset: ask for confirmation
					{
						settings_system_reset_confirm();
						/* Cancel returns here; the confirmed path reboots the device. */
						settings_system_gui_update(sel_row, name_buf, time_ok, time_str, storage_ok, free_b, total_kb);
					} // else
				} // else if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			else
			{
				; // Do other things for this task
			}
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task

} // void settings_system(void)



/*============================================================================*/
/**
  * @brief Draw the System screen.
  */
/*============================================================================*/
static void settings_system_gui_update(uint8_t sel_row, const char *dev_name, uint8_t time_ok, const char *time_str, uint8_t storage_ok, uint64_t free_b, uint32_t total_kb)
{
	char info_str[48];
	uint8_t row_y;
	const char *label;

	/* Graphic work starts here */
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, 0, 9, "System");

	/* Date/time row (informational, never selected): RTC read on entry;
	 * "Time not set" after a backup-domain loss until the clock is set. */
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
	label = time_ok ? time_str : "Time not set";
	u8g2_DrawStr(&m1_u8g2, (M1_LCD_DISPLAY_WIDTH - u8g2_GetStrWidth(&m1_u8g2, label))/2, SETTING_SYS_TIME_ROW_Y, label);

	/* Row 0: device name (actionable) */
	row_y = SETTING_SYS_NAME_ROW_Y;
	label = "Name";
	if ( sel_row==0 )
	{
		u8g2_DrawBox(&m1_u8g2, 0, row_y - 9, M1_LCD_DISPLAY_WIDTH, 11);
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
		u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
	} // if ( sel_row==0 )
	else
	{
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
	} // else
	u8g2_DrawStr(&m1_u8g2, 4, row_y, label);
	/* Right-aligned value, already bounded by the store's 32-char field */
	settings_str_clip(info_str, sizeof(info_str), dev_name);
	u8g2_DrawStr(&m1_u8g2, SETTING_LCD_VALUE_RIGHT_X - u8g2_GetStrWidth(&m1_u8g2, info_str), row_y, info_str);

	/* Storage info row (informational, never selected): free bytes via
	 * fs_get_free_space(), total from the SD driver's capacity scan --
	 * integer GB, exactly like Settings > Storage > About SD Card. */
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
	if ( storage_ok )
	{
		snprintf(info_str, sizeof(info_str), "Total %luGB  Free %luGB",
			(unsigned long)(total_kb/(1024u*1024u)),
			(unsigned long)(free_b/(1024ull*1024ull*1024ull)));
		u8g2_DrawStr(&m1_u8g2, (M1_LCD_DISPLAY_WIDTH - u8g2_GetStrWidth(&m1_u8g2, info_str))/2, SETTING_SYS_STORAGE_ROW_Y, info_str);
	} // if ( storage_ok )
	else
	{
		label = "SD card unavailable";
		u8g2_DrawStr(&m1_u8g2, (M1_LCD_DISPLAY_WIDTH - u8g2_GetStrWidth(&m1_u8g2, label))/2, SETTING_SYS_STORAGE_ROW_Y, label);
	} // else

	/* Row 1: reset defaults (actionable) */
	row_y = SETTING_SYS_RESET_ROW_Y;
	label = "Reset defaults";
	if ( sel_row==1 )
	{
		u8g2_DrawBox(&m1_u8g2, 0, row_y - 9, M1_LCD_DISPLAY_WIDTH, 11);
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
		u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
	} // if ( sel_row==1 )
	else
	{
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
	} // else
	u8g2_DrawStr(&m1_u8g2, 4, row_y, label);

	// Inverted hint bar: OK activates the selected row, BACK exits
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_DrawBox(&m1_u8g2, 0, 52, M1_LCD_DISPLAY_WIDTH, 12);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // Write text in inverted color
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	u8g2_DrawXBMP(&m1_u8g2, 1, 53, 10, 10, target_10x10); // draw CENTER/OK button icon
	u8g2_DrawStr(&m1_u8g2, 13, 61, "Select");
	u8g2_DrawStr(&m1_u8g2, 97, 61, "Back");
	u8g2_DrawXBMP(&m1_u8g2, 119, 53, 8, 8, arrowleft_8x8); // draw arrowleft icon
	m1_u8g2_nextpage(); // Update display RAM
} // static void settings_system_gui_update(...)



/*============================================================================*/
/**
  * @brief Edit the device name with the virtual keyboard (System screen).
  *
  * The keyboard seeds from the current name (the compiled default "M1"
  * is passed as generated text, so the first typed character replaces
  * it wholesale) and writes back through the shared buffer on Enter.
  * Cancellation (BACK) leaves the current name untouched, and so does
  * confirming an unchanged name. The store setter enforces the 32-char
  * field bound; the keyboard's own editor is 20 chars, so every
  * reachable result already fits both. Returns 1 only when the store
  * image was actually changed (the session has something to persist).
  */
/*============================================================================*/
static uint8_t settings_system_edit_name(char *name_buf, size_t buf_size)
{
	char new_name[33]; // store bound (32) + NUL
	uint8_t generated;

	settings_str_clip(new_name, sizeof(new_name), name_buf);
	generated = (uint8_t)(strcmp(new_name, "M1")==0); // compiled default: generated text

	if ( m1_vkb_get_filename("Device name", new_name, new_name, generated) )
	{
		if ( strcmp(new_name, name_buf)!=0 ) // Enter on an unchanged name edits nothing
		{
			settings_set_device_name(new_name); // clamps to the store bound
			settings_str_clip(name_buf, buf_size, settings_get()->device_name);
			return 1u; // Name changed: mark the editing session
		} // if ( strcmp(new_name, name_buf)!=0 )
	} // if ( m1_vkb_get_filename(...) )
	return 0u;
} // static uint8_t settings_system_edit_name(...)



/*============================================================================*/
/**
  * @brief Reset-to-defaults confirmation and execution (System screen).
  *
  * RIGHT executes (same confirm-on-RIGHT convention as Format SD Card),
  * BACK/LEFT cancels. Executing: restore the compiled defaults in RAM,
  * re-apply the live feedback settings so the pre-reboot state matches,
  * persist the defaults over the stored file (the SD settings store IS
  * the configuration being cleared -- the boot-signed flash metadata is
  * never read or written here), then reboot through power_reboot(), which
  * does not return. A failed persist is reported, not hidden, and the
  * reboot still happens: the user asked for a reset, and whatever the
  * card serves on the next boot is then authoritative.
  */
/*============================================================================*/
static void settings_system_reset_confirm(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;

	/* Graphic work starts here */
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, 22, 20, "Reset settings");
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	u8g2_DrawStr(&m1_u8g2, 18, 30, "All stored settings");
	u8g2_DrawStr(&m1_u8g2, 27, 40, "return to defaults");

	u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12); // Draw an inverted bar at the bottom to display options
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // Write text in inverted color
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	u8g2_DrawXBMP(&m1_u8g2, 1, 53, 8, 8, arrowleft_8x8); // draw arrowleft icon
	u8g2_DrawStr(&m1_u8g2, 11, 61, "Cancel");
	u8g2_DrawXBMP(&m1_u8g2, 119, 53, 8, 8, arrowright_8x8); // draw arrowright icon
	u8g2_DrawStr(&m1_u8g2, 92, 61, "Reset");
	m1_u8g2_nextpage(); // Update display RAM

	while (1 ) // Main loop of this task
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE)
		{
			if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			{
				// Notification is only sent to this task when there's any button activity,
				// so it doesn't need to wait when reading the event from the queue
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( ret!=pdTRUE )
				{
					continue; // No event behind the notification: never act on a stale struct
				} // if ( ret!=pdTRUE )
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ||
					this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK ) // Cancel?
				{
					xQueueReset(main_q_hdl); // Reset main q before return
					return; // Back to the System screen
				} // if ( BACK/LEFT == BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK ) // Reset?
				{
					settings_reset_defaults(); // RAM image: compiled defaults
					fb_set_brightness_pct(settings_get()->brightness_pct); // live re-apply (100)
					fb_set_led_notifications_enabled(settings_get()->led_notifications != 0u);
					if ( !settings_save() )
					{
						u8g2_FirstPage(&m1_u8g2);
						u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
						u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
						u8g2_DrawStr(&m1_u8g2, 16, 30, "Reset: SD write failed");
						m1_u8g2_nextpage(); // Update display RAM
						vTaskDelay(pdMS_TO_TICKS(1500));
					} // if ( !settings_save() )
					xQueueReset(main_q_hdl); // Reset main q before the view switch
					power_reboot(); // Existing reboot path; does not return
				} // else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			else
			{
				; // Do other things for this task
			}
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task

} // static void settings_system_reset_confirm(void)



/*============================================================================*/
/**
  * @brief Date & Time editor screen (issue #7 completion, PR-3).
  *
  * Two-line clock editor: "YYYY-MM-DD" and "HH:MM" in the large font,
  * the selected field marked by an inverted segment. Buttons follow the
  * clock-editor axes (documented deviation from the LCD screen's
  * UP/DOWN-row axes, per the approved plan): LEFT/RIGHT select the
  * field (wrapping), UP/DOWN step the selected value through the pure
  * domain (wrapping month/day/hour/minute, clamped year, day kept
  * possible across month/year steps), OK applies the whole edited
  * date/time to the RTC -- m1_rtc_set_datetime() validates first, and
  * success re-syncs the edit copy from a read-back (displayed ==
  * effective), the idiom's post-setter re-sync with the RTC as the
  * store -- and BACK exits WITHOUT applying. No session-dirty state
  * and no settings_save() call exist here on purpose: the RTC is its
  * own battery-backed persistence and time is not a settings.cfg
  * field, so the SD card is never touched by this screen.
  */
/*============================================================================*/
#define SETTING_DT_DATE_Y				28 // Baseline of the "YYYY-MM-DD" line
#define SETTING_DT_TIME_Y				46 // Baseline of the "HH:MM" line

void settings_date_time(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	S_M1_DateTime_t edit;
	uint8_t sel_field;

	/* Seed from the RTC; a not-set clock (backup-domain loss) starts at
	 * the window base so the first visit edits a full, valid date. */
	if ( !m1_rtc_get_datetime(&edit) )
	{
		edit.year = (uint16_t)M1_RTC_YEAR_MIN;
		edit.month = 1u;
		edit.day = 1u;
		edit.hour = 0u;
		edit.minute = 0u;
	} // if ( !m1_rtc_get_datetime(&edit) )

	sel_field = 0u;
	settings_dt_gui_update(sel_field, &edit);

	while (1 ) // Main loop of this task
	{
		;
		; // Do other parts of this task here
		;

		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE)
		{
			if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			{
				// Notification is only sent to this task when there's any button activity,
				// so it doesn't need to wait when reading the event from the queue
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( ret!=pdTRUE )
				{
					continue; // No event behind the notification: never act on a stale struct
				} // if ( ret!=pdTRUE )
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // Exit without applying?
				{
					xQueueReset(main_q_hdl); // Reset main q before return (nothing to save)
					break; // Exit and return to the calling task (subfunc_handler_task)
				} // if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK ) // Previous field?
				{
					sel_field = (uint8_t)((sel_field + M1_RTC_FIELD_COUNT - 1u) % M1_RTC_FIELD_COUNT);
					settings_dt_gui_update(sel_field, &edit);
				} // else if ( this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK ) // Next field?
				{
					sel_field = (uint8_t)((sel_field + 1u) % M1_RTC_FIELD_COUNT);
					settings_dt_gui_update(sel_field, &edit);
				} // else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK ||
					this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK ) // Step the field value?
				{
					m1_rtc_datetime_step(&edit, (E_M1_RtcField_t)sel_field,
						(this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK) ? 1 : -1);
					settings_dt_gui_update(sel_field, &edit);
				} // else if ( UP/DOWN == BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK ) // Apply to the RTC?
				{
					if ( m1_rtc_set_datetime(&edit) ) // Validates before any register is touched
					{
						(void)m1_rtc_get_datetime(&edit); // Post-setter re-sync from the timebase
						settings_dt_notice(check_10x10, "Set", 700u);
					} // if ( m1_rtc_set_datetime(&edit) )
					else
					{
						settings_dt_notice(error_10x10, "Set failed", 1500u);
					} // else
					settings_dt_gui_update(sel_field, &edit);
				} // else if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			else
			{
				; // Do other things for this task
			}
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task

} // void settings_date_time(void)



/*============================================================================*/
/**
  * @brief Draw one field segment of the Date & Time editor (see
  *        settings_dt_gui_update); returns the advance to the next
  *        segment so a proportional font cannot desync the layout.
  */
/*============================================================================*/
static uint8_t settings_dt_draw_seg(uint8_t x, uint8_t y, uint8_t seg_top_off, uint8_t seg_h, const char *txt, uint8_t selected)
{
	uint8_t w = u8g2_GetStrWidth(&m1_u8g2, txt);

	if ( selected )
	{
		// Inverted segment bar over the selected field, like the row bars
		u8g2_DrawBox(&m1_u8g2, x, (uint8_t)(y - seg_top_off), (uint8_t)(w + 2u), seg_h);
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
	} // if ( selected )
	else
	{
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	} // else
	u8g2_DrawStr(&m1_u8g2, (uint8_t)(x + 1u), y, txt);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	return (uint8_t)(w + 2u);
} // static uint8_t settings_dt_draw_seg(...)



/*============================================================================*/
/**
  * @brief Draw the Date & Time editor (see settings_date_time).
  */
/*============================================================================*/
static void settings_dt_gui_update(uint8_t sel_field, const S_M1_DateTime_t *dt)
{
	char seg[16]; // Wide enough that GCC can prove no %u truncation
	char line[24];
	uint8_t x, seg_top_off, seg_h, w;
	int16_t asc, desc;

	/* Graphic work starts here */
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, 0, 9, "Date & Time");

	/* Large font for both value lines (profont17: mono digits, already
	 * linked for the Sub-GHz frequency display). The segment bar is
	 * derived from the font's real ascent/descent, no magic numbers. */
	u8g2_SetFont(&m1_u8g2, M1_DISP_LARGE_FONT_1B);
	asc = u8g2_GetAscent(&m1_u8g2);
	desc = u8g2_GetDescent(&m1_u8g2); // <= 0
	seg_top_off = (uint8_t)(asc + 1);
	seg_h = (uint8_t)(asc - desc + 2);

	/* Date line "YYYY-MM-DD": centered as a whole, drawn per field so
	 * the selected field can carry the inverted segment bar. */
	snprintf(line, sizeof(line), "%04u-%02u-%02u",
		(unsigned)dt->year, (unsigned)dt->month, (unsigned)dt->day);
	x = (uint8_t)((M1_LCD_DISPLAY_WIDTH - u8g2_GetStrWidth(&m1_u8g2, line)) / 2u);
	snprintf(seg, sizeof(seg), "%04u", (unsigned)dt->year);
	x = (uint8_t)(x + settings_dt_draw_seg(x, SETTING_DT_DATE_Y, seg_top_off, seg_h, seg, (uint8_t)(sel_field==M1_RTC_FIELD_YEAR)));
	x = (uint8_t)(x + settings_dt_draw_seg(x, SETTING_DT_DATE_Y, seg_top_off, seg_h, "-", 0u));
	snprintf(seg, sizeof(seg), "%02u", (unsigned)dt->month);
	x = (uint8_t)(x + settings_dt_draw_seg(x, SETTING_DT_DATE_Y, seg_top_off, seg_h, seg, (uint8_t)(sel_field==M1_RTC_FIELD_MONTH)));
	x = (uint8_t)(x + settings_dt_draw_seg(x, SETTING_DT_DATE_Y, seg_top_off, seg_h, "-", 0u));
	snprintf(seg, sizeof(seg), "%02u", (unsigned)dt->day);
	(void)settings_dt_draw_seg(x, SETTING_DT_DATE_Y, seg_top_off, seg_h, seg, (uint8_t)(sel_field==M1_RTC_FIELD_DAY));

	/* Time line "HH:MM": centered as a whole, drawn per field. */
	snprintf(line, sizeof(line), "%02u:%02u", (unsigned)dt->hour, (unsigned)dt->minute);
	x = (uint8_t)((M1_LCD_DISPLAY_WIDTH - u8g2_GetStrWidth(&m1_u8g2, line)) / 2u);
	snprintf(seg, sizeof(seg), "%02u", (unsigned)dt->hour);
	x = (uint8_t)(x + settings_dt_draw_seg(x, SETTING_DT_TIME_Y, seg_top_off, seg_h, seg, (uint8_t)(sel_field==M1_RTC_FIELD_HOUR)));
	x = (uint8_t)(x + settings_dt_draw_seg(x, SETTING_DT_TIME_Y, seg_top_off, seg_h, ":", 0u));
	snprintf(seg, sizeof(seg), "%02u", (unsigned)dt->minute);
	(void)settings_dt_draw_seg(x, SETTING_DT_TIME_Y, seg_top_off, seg_h, seg, (uint8_t)(sel_field==M1_RTC_FIELD_MINUTE));

	// Inverted hint bar: LEFT/RIGHT select the field, UP/DOWN step (+/-), OK sets
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_DrawBox(&m1_u8g2, 0, 52, M1_LCD_DISPLAY_WIDTH, 12);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // Write text in inverted color
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	u8g2_DrawXBMP(&m1_u8g2, 1, 53, 8, 8, arrowleft_8x8); // draw arrowleft icon
	u8g2_DrawStr(&m1_u8g2, 11, 61, "Field");
	w = u8g2_GetStrWidth(&m1_u8g2, "+/-");
	u8g2_DrawStr(&m1_u8g2, (uint8_t)((M1_LCD_DISPLAY_WIDTH - w)/2u), 61, "+/-");
	w = u8g2_GetStrWidth(&m1_u8g2, "Set OK");
	u8g2_DrawStr(&m1_u8g2, (uint8_t)(117u - w), 61, "Set OK");
	u8g2_DrawXBMP(&m1_u8g2, 119, 53, 8, 8, arrowright_8x8); // draw arrowright icon
	m1_u8g2_nextpage(); // Update display RAM
} // static void settings_dt_gui_update(...)



/*============================================================================*/
/**
  * @brief Brief full-frame result notice for the Date & Time apply path
  *        (check icon on success, error icon on refusal). A refused Set
  *        is shown, never silently swallowed.
  */
/*============================================================================*/
static void settings_dt_notice(const uint8_t *icon_10x10, const char *msg, uint16_t show_ms)
{
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_DrawXBMP(&m1_u8g2, 20, 27, 10, 10, icon_10x10);
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	u8g2_DrawStr(&m1_u8g2, 34, 36, msg);
	m1_u8g2_nextpage(); // Update display RAM
	vTaskDelay(pdMS_TO_TICKS(show_ms));
} // static void settings_dt_notice(...)
