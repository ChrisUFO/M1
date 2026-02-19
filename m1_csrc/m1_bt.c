/*
*
* m1_bt.c
*
* Source for M1 bluetooth
*
* M1 Project
*
*/
/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

/********************************************************************************
*
* This file is for gatt server. It can send adv data, and get connected by client.
*
*********************************************************************************/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_bt.h"
#include "m1_esp32_hal.h"
#include "spi_master.h"
#include "esp_app_main.h"
#include "esp_at_list.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG				"BLE"

#define M1_GUI_ROW_SPACING			1

#define M1_BLE_SCANNING_TIME		10 // seconds


//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/


/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void menu_bluetooth_init(void);
void bluetooth_config(void);
void bluetooth_scan(void);
void bluetooth_advertise(void);
static uint8_t ble_scan_list_validation(ctrl_cmd_t *app_resp);
static uint16_t ble_scan_list_print(ctrl_cmd_t *app_resp, bool up_dir);
//extern void ble_app_main(void);
extern void  esp32_main_init(void);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/*
 * This function initializes display for this sub-menu item.
 */
/*============================================================================*/
void menu_bluetooth_init(void)
{
	;
} // void menu_bluetooth_init(void)



/*============================================================================*/
/*
 * This function scans for devices.
 */
/*============================================================================*/
void bluetooth_scan(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	ctrl_cmd_t app_req = CTRL_CMD_DEFAULT_REQ();
	uint16_t list_count;

    /* Graphic work starts here */
	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
	if ( !m1_esp32_get_init_status() )
	{
		m1_esp32_init();
		if ( !get_esp32_main_init_status() )
			esp32_main_init();
		m1_u8g2_firstpage();
		u8g2_DrawStr(&m1_u8g2, 6, 15, "Initializing...");
		u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32, hourglass_18x32);
		m1_u8g2_nextpage();
	}

	list_count = 0;

	m1_u8g2_firstpage();
	if ( get_esp32_main_init_status() )
	{
		u8g2_DrawStr(&m1_u8g2, 6, 15, "Scanning BLE...");
		u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32, hourglass_18x32);
		m1_u8g2_nextpage();

		// implemented synchronous
		app_req.cmd_timeout_sec = M1_BLE_SCANNING_TIME; //DEFAULT_CTRL_RESP_TIMEOUT //30 sec
		app_req.msg_id = CTRL_RESP_GET_BLE_SCAN_LIST;
		ret = ble_scan_list(&app_req);
		ret = ble_scan_list_validation(&app_req);
		if ( ret )
		{
			list_count = ble_scan_list_print(&app_req, true);
		} // if ( ret )
		else
		{
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
			u8g2_DrawBox(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32); // Clear old image
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
			u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 32/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 32, 32, wifi_error_32x32);
			u8g2_DrawStr(&m1_u8g2, 6, 15 + M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, "Failed. Let retry!");
			m1_u8g2_nextpage();
			// Reset the ESP module
			esp32_disable();
			m1_hard_delay(1);
			esp32_enable();
			/* stop spi transactions short time to avoid slave sync issues */
			m1_hard_delay(200);
		}
	} // if ( get_esp32_main_init_status() )
	else
	{
		u8g2_DrawStr(&m1_u8g2, 6, 15 + M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, "ESP32 not ready!");
		m1_u8g2_nextpage();
	}
	// mode declared in control.c should be set to MODE_STATION after M1 has been connected to a Wifi network
	//mode |= MODE_STATION;

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
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
				{
					; // Do extra tasks here if needed
					if (app_req.u.wifi_ap_scan.out_list != NULL)
					{
						free(app_req.u.wifi_ap_scan.out_list);
					}
					ble_scan_list_print(NULL, false);

					xQueueReset(main_q_hdl); // Reset main q before return
					m1_esp32_deinit();
					break; // Exit and return to the calling task (subfunc_handler_task)
				} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK ) // go up?
				{
					if ( list_count )
						ble_scan_list_print(&app_req, true);
				}
				else if ( this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK ) // go down?
				{
					if ( list_count )
						ble_scan_list_print(&app_req, false);
				}
				else if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK ) // Select?
				{
					; // Do other things for this task, if needed
				}
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			else
			{
				; // Do other things for this task
			}
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task
} // void bluetooth_scan(void)



/*============================================================================*/
/*
 * This function initializes the server mode and starts the advertising mode.
 */
/*============================================================================*/
void bluetooth_advertise(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	ctrl_cmd_t app_req = CTRL_CMD_DEFAULT_REQ();
	char *index, *start_index, *next_index;
	char prn_buffer[25], prn_cnt;
	uint8_t cp_len;

    /* Graphic work starts here */
	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
	if ( !m1_esp32_get_init_status() )
	{
		m1_esp32_init();
		if ( !get_esp32_main_init_status() )
			esp32_main_init();
		m1_u8g2_firstpage();
		u8g2_DrawStr(&m1_u8g2, 6, 15, "Initializing...");
		u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32, hourglass_18x32);
		m1_u8g2_nextpage();
	}

	m1_u8g2_firstpage();
	if ( get_esp32_main_init_status() )
	{
		u8g2_DrawStr(&m1_u8g2, 6, 15, "Advertising...");
		u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32, hourglass_18x32);
		m1_u8g2_nextpage();

		// implemented synchronous
		app_req.cmd_timeout_sec = M1_BLE_SCANNING_TIME; //DEFAULT_CTRL_RESP_TIMEOUT //30 sec
		app_req.msg_id = CTRL_RESP_SET_BLE_RESET;
		ret = esp_dev_reset(&app_req);
		app_req.msg_id = CTRL_RESP_SET_BLE_ADVERTISE;
		ret = ble_advertise(&app_req);

		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
		u8g2_DrawBox(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32); // Clear old image
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

		if ( !ret )
		{
			u8g2_DrawStr(&m1_u8g2, 100, 15, "Done");
			//"AT+BLEADVDATAEX="MONSTATEK-M1","A000","1A2B3C4D5E",1" // AT+BLEADVDATAEX=<dev_name>,<uuid>,<manufacturer_data>,<include_power>
			index = ESP32C6_AT_REQ_ADV_DATA;
			if ( index )
			{
				prn_cnt = 0;
				while (true)
				{
					start_index = strstr(index, "\"");
					if ( !start_index )
						break;
					next_index = strstr(&start_index[1], "\"");
					cp_len = next_index - start_index - 1;
					strncpy(prn_buffer, &start_index[1], cp_len);
					prn_buffer[cp_len] = '\0'; // Add end of string
					u8g2_DrawStr(&m1_u8g2, 2, 35 + prn_cnt*10, prn_buffer);
					index = &next_index[1];
					prn_cnt++;
					if ( prn_cnt >= 3)
						break;
				} // while (true)
			} // if ( index )
			else
			{
				u8g2_DrawStr(&m1_u8g2, 6, 25, "Done");
				m1_u8g2_nextpage();
			} // else
			m1_u8g2_nextpage();
		} // if ( ret )
		else
		{
			u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 32/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 32, 32, wifi_error_32x32);
			u8g2_DrawStr(&m1_u8g2, 6, 15 + M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, "Failed. Let retry!");
			m1_u8g2_nextpage();
			// Reset the ESP module
			esp32_disable();
			m1_hard_delay(1);
			esp32_enable();
			/* stop spi transactions short time to avoid slave sync issues */
			m1_hard_delay(200);
		}
	} // if ( get_esp32_main_init_status() )
	else
	{
		u8g2_DrawStr(&m1_u8g2, 6, 15 + M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, "ESP32 not ready!");
		m1_u8g2_nextpage();
	}
	// mode declared in control.c should be set to MODE_STATION after M1 has been connected to a Wifi network
	//mode |= MODE_STATION;

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
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
				{
					; // Do extra tasks here if needed
					u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 18*2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32, hourglass_18x32);
					m1_u8g2_nextpage();

					app_req.cmd_timeout_sec = M1_BLE_SCANNING_TIME; //DEFAULT_CTRL_RESP_TIMEOUT //30 sec
					app_req.msg_id = CTRL_RESP_SET_BLE_RESET;
					ret = esp_dev_reset(&app_req);

					xQueueReset(main_q_hdl); // Reset main q before return
					m1_esp32_deinit();
					break; // Exit and return to the calling task (subfunc_handler_task)
				} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			else
			{
				; // Do other things for this task
			}
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task
} // void bluetooth_advertise(void)



/*============================================================================*/
/*
 * This function
 */
/*============================================================================*/
void bluetooth_config(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;

	m1_gui_let_update_fw();

	while (1 ) // Main loop of this task
	{
		;
		; // Do other parts of this task here
		;

		// Wait for the notification from button_event_handler_task to subfunc_handler_task.
		// This task is the sub-task of subfunc_handler_task.
		// The notification is given in the form of an item in the main queue.
		// So let read the main queue.
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE)
		{
			if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			{
				// Notification is only sent to this task when there's any button activity,
				// so it doesn't need to wait when reading the event from the queue
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
				{
					; // Do extra tasks here if needed

					xQueueReset(main_q_hdl); // Reset main q before return
					break; // Exit and return to the calling task (subfunc_handler_task)
				} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				else
				{
					; // Do other things for this task, if needed
				}
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			else
			{
				; // Do other things for this task
			}
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task

} // void bluetooth_config(void)



/*============================================================================*/
/*
 * This function validates the scan list.
 */
/*============================================================================*/
static uint8_t ble_scan_list_validation(ctrl_cmd_t *app_resp)
{
	if (!app_resp || (app_resp->msg_type != CTRL_RESP))
	{
		if (app_resp)
			M1_LOG_D(M1_LOGDB_TAG, "Msg type is not response[%u]\n\r", app_resp->msg_type);
		return false;
	}
	if (app_resp->resp_event_status != SUCCESS)
	{
		//process_failed_responses(app_resp);
		return false;
	}
	if (app_resp->msg_id != CTRL_RESP_GET_BLE_SCAN_LIST)
	{
		M1_LOG_D(M1_LOGDB_TAG, "Invalid Response[%u] to parse\n\r", app_resp->msg_id);
		return false;
	}

	return true;
} // static uint8_t ble_scan_list_validation(ctrl_cmd_t *app_resp)




/*============================================================================*/
/*
 * This function displays all scanned device list.
 * Return: number of devices found
 */
/*============================================================================*/
static uint16_t ble_scan_list_print(ctrl_cmd_t *app_resp, bool up_dir)
{
	static uint16_t i;
	static wifi_ap_scan_list_t *w_scan_p;
	static wifi_scanlist_t *list;
	static bool init_done = false;
	char prn_msg[25];
	uint8_t y_offset;

	if ( !app_resp && !up_dir ) // reset condition?
	{
		init_done = false;
		return 0;
	} // if ( !app_resp && !up_dir )

	if ( !init_done )
	{
		init_done = true;
		w_scan_p = &app_resp->u.wifi_ap_scan;
		list = w_scan_p->out_list;

		if (!w_scan_p->count)
		{
			strcpy(prn_msg, "No device found!");
			M1_LOG_D(M1_LOGDB_TAG, "No device found\n\r");
			init_done = false;
		}
		else if (!list)
		{
			strcpy(prn_msg, "Try again!");
			M1_LOG_D(M1_LOGDB_TAG, "Failed to get scanned device list\n\r");
			init_done = false;
		}
		else
		{
			M1_LOG_D(M1_LOGDB_TAG, "Number of available devices is %d\n\r", w_scan_p->count);
		}

		if ( !init_done )
		{
			u8g2_DrawStr(&m1_u8g2, 6, 25 + M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, prn_msg);
			m1_u8g2_nextpage(); // Update display RAM
			return 0;
		}
		// Display first AP in the list
		i = 1;
		up_dir = true; // Overwrite the up_dir for the AP to be displayed for the first time
	} // if ( !init_done )

	if ( up_dir )
	{
		if ( i )
			i--;
		else
			i = w_scan_p->count-1; // roll over
	}
	else
	{
		i++;
		if ( i >= w_scan_p->count )
			i = 0; // roll over
	}

	m1_u8g2_firstpage();
	u8g2_DrawXBMP(&m1_u8g2, 0, 0, 128, 14, m1_frame_128_14);
	u8g2_DrawStr(&m1_u8g2, 2, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, "Total Dev:");

	sprintf(prn_msg, "%d", w_scan_p->count);
	u8g2_DrawStr(&m1_u8g2, 2 + strlen("Total Dev: ")*M1_GUI_FONT_WIDTH + 2, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, prn_msg);

	sprintf(prn_msg, "%d/%d", i + 1, w_scan_p->count); // Current AP
	u8g2_DrawStr(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 6*M1_GUI_FONT_WIDTH, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, prn_msg);

	y_offset = 14 + M1_GUI_FONT_HEIGHT - 1;
	// Draw text
	u8g2_DrawStr(&m1_u8g2, 2, y_offset, (char *)list[i].bssid);
	y_offset += M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING;
	sprintf(prn_msg, "RSSI: %ddBm", list[i].rssi);
	u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);
	y_offset += M1_GUI_FONT_HEIGHT;
	sprintf(prn_msg, "Address type: %d", list[i].encryption_mode);
	u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);

	m1_u8g2_nextpage(); // Update display RAM

	M1_LOG_D(M1_LOGDB_TAG, "%d) bssid \"%s\" rssi \"%d\" address type \"%d\" \n\r",\
						i, list[i].bssid, list[i].rssi, list[i].encryption_mode);

	return w_scan_p->count;
} // static uint16_t ble_scan_list_print(ctrl_cmd_t *app_resp, bool up_dir)
