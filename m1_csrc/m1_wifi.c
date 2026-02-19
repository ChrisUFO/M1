/* See COPYING.txt for license details. */

/*
*
* m1_wifi.h
*
* Library for M1 Wifi
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_wifi.h"
#include "m1_wifi_cred.h"
#include "m1_esp32_hal.h"
#include "m1_virtual_kb.h"
//#include "control.h"
#include "ctrl_api.h"
#include "esp_app_main.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"Wifi"

#define M1_WIFI_AP_SCANNING_TIME	30 // seconds

#define M1_GUI_ROW_SPACING			1

#ifndef WIFI_MAX_PASSWORD_LEN
#define WIFI_MAX_PASSWORD_LEN		64
#endif

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/


/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void menu_wifi_init(void);
void menu_wifi_exit(void);

void menu_wifi_init(void);
void wifi_scan_ap(void);
void wifi_config(void);

static uint16_t wifi_ap_list_print(ctrl_cmd_t *app_resp, bool up_dir);
static uint8_t wifi_ap_list_validation(ctrl_cmd_t *app_resp);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void menu_wifi_init(void)
{
	;
} // void menu_wifi_init(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void  menu_wifi_exit(void)
{
	;
} // void  menu_wifi_exit(void)



/*============================================================================*/
/**
  * @brief Scans for wifi access point list
  * @param
  * @retval
  */
/*============================================================================*/
void wifi_scan_ap(void)
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
		u8g2_DrawStr(&m1_u8g2, 6, 15, "Scanning AP...");
		u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32, hourglass_18x32);
		m1_u8g2_nextpage();

		// implemented synchronous
		app_req.cmd_timeout_sec = M1_WIFI_AP_SCANNING_TIME; //DEFAULT_CTRL_RESP_TIMEOUT //30 sec
		app_req.msg_id = CTRL_RESP_GET_AP_SCAN_LIST;
		ret = wifi_ap_scan_list(&app_req);
		ret = wifi_ap_list_validation(&app_req);
		if ( ret )
		{
			list_count = wifi_ap_list_print(&app_req, true);
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
					wifi_ap_list_print(NULL, false);

					xQueueReset(main_q_hdl); // Reset main q before return
					m1_esp32_deinit();
					break; // Exit and return to the calling task (subfunc_handler_task)
				} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK ) // go up?
				{
					if ( list_count )
						wifi_ap_list_print(&app_req, true);
				}
				else if ( this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK ) // go down?
				{
					if ( list_count )
						wifi_ap_list_print(&app_req, false);
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

} // void wifi_scan_ap(void)



/*============================================================================*/
/**
  * @brief Displays all scanned AP list.
  * @param
  * @retval
  */
/*============================================================================*/
static uint16_t wifi_ap_list_print(ctrl_cmd_t *app_resp, bool up_dir)
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
			strcpy(prn_msg, "No AP found!");
			M1_LOG_I(M1_LOGDB_TAG, "No AP found\n\r");
			init_done = false;
		}
		else if (!list)
		{
			strcpy(prn_msg, "Try again!");
			M1_LOG_I(M1_LOGDB_TAG, "Failed to get scanned AP list\n\r");
			init_done = false;
		}
		else
		{
			M1_LOG_I(M1_LOGDB_TAG, "Number of available APs is %d\n\r", w_scan_p->count);
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
	u8g2_DrawStr(&m1_u8g2, 2, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, "Total AP:");

	sprintf(prn_msg, "%d", w_scan_p->count);
	u8g2_DrawStr(&m1_u8g2, 2 + strlen("Total AP: ")*M1_GUI_FONT_WIDTH + 2, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, prn_msg);

	sprintf(prn_msg, "%d/%d", i + 1, w_scan_p->count); // Current AP
	u8g2_DrawStr(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 6*M1_GUI_FONT_WIDTH, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, prn_msg);

	y_offset = 14 + M1_GUI_FONT_HEIGHT - 1;
	// Draw text
	if ( list[i].ssid[0]==0x00 ) // Hidden SSID?
		strcpy(prn_msg, "*hidden*");
	else
		strncpy(prn_msg, list[i].ssid, M1_LCD_DISPLAY_WIDTH/M1_GUI_FONT_WIDTH);
	u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);
	y_offset += M1_GUI_FONT_HEIGHT;
	u8g2_DrawStr(&m1_u8g2, 2, y_offset, list[i].bssid);
	y_offset += M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING;
	sprintf(prn_msg, "RSSI: %ddBm", list[i].rssi);
	u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);
	y_offset += M1_GUI_FONT_HEIGHT;
	sprintf(prn_msg, "Channel: %d", list[i].channel);
	u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);
	y_offset += M1_GUI_FONT_HEIGHT;
	sprintf(prn_msg, "Auth mode: %d", list[i].encryption_mode);
	u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);

	m1_u8g2_nextpage(); // Update display RAM

	M1_LOG_D(M1_LOGDB_TAG, "%d) ssid \"%s\" bssid \"%s\" rssi \"%d\" channel \"%d\" auth mode \"%d\" \n\r",\
						i, list[i].ssid, list[i].bssid, list[i].rssi,
						list[i].channel, list[i].encryption_mode);

	return w_scan_p->count;
} // static uint16_t wifi_ap_list_print(ctrl_cmd_t *app_resp, bool up_dir)



/*============================================================================*/
/**
  * @brief Validates the AP list.
  * @param
  * @retval
  */
/*============================================================================*/
static uint8_t wifi_ap_list_validation(ctrl_cmd_t *app_resp)
{
	if (!app_resp || (app_resp->msg_type != CTRL_RESP))
	{
		if (app_resp)
			M1_LOG_I(M1_LOGDB_TAG, "Msg type is not response[%u]\n\r", app_resp->msg_type);
		return false;
	}
	if (app_resp->resp_event_status != SUCCESS)
	{
		//process_failed_responses(app_resp);
		return false;
	}
	if (app_resp->msg_id != CTRL_RESP_GET_AP_SCAN_LIST)
	{
		M1_LOG_I(M1_LOGDB_TAG, "Invalid Response[%u] to parse\n\r", app_resp->msg_id);
		return false;
	}

	return true;
} // static uint8_t wifi_ap_list_validation(ctrl_cmd_t *app_resp)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void wifi_config(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t menu_sel = 0;
	bool run_menu = true;

	// Initialize ESP32 if not already done
	if ( !m1_esp32_get_init_status() )
	{
		m1_esp32_init();
		if ( !get_esp32_main_init_status() )
			esp32_main_init();
	}

	// Initialize credential storage
	wifi_cred_init();

	// Show menu
	while (run_menu)
	{
		// Display menu
		m1_u8g2_firstpage();
		u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 6, 15, "WiFi Config:");
		
		// Menu items
		const char* menu_items[] = {
			"Join Network",
			"Saved Networks",
			"Connection Status"
		};
		const uint8_t num_items = sizeof(menu_items) / sizeof(menu_items[0]);
		
		// Draw menu items with selection indicator - tighter spacing to fit all 3 items
		for (uint8_t i = 0; i < num_items; i++) {
			uint8_t y_pos = 32 + (i * 14); // Reduced from 16 to 14 pixel spacing
			if (i == menu_sel) {
				u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
				u8g2_DrawBox(&m1_u8g2, 0, y_pos - 7, 128, 11); // Slightly smaller box
				u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
			}
			u8g2_DrawStr(&m1_u8g2, 6, y_pos, menu_items[i]);
			if (i == menu_sel) {
				u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
			}
		}
		
		u8g2_DrawStr(&m1_u8g2, 6, 120, "Back: Exit");
		m1_u8g2_nextpage();

		// Wait for button event
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD)
		{
			ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
			
			if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
			{
				xQueueReset(main_q_hdl);
				run_menu = false;
				break;
			}
			else if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
			{
				if (menu_sel > 0) menu_sel--;
			}
			else if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
			{
				if (menu_sel < num_items - 1) menu_sel++;
			}
			else if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
			{
				switch (menu_sel)
				{
					case 0: // Join Network
						wifi_join_network();
						break;
					case 1: // Saved Networks
						wifi_show_saved_networks();
						break;
					case 2: // Connection Status
						wifi_show_connection_status();
						break;
				}
			}
		}
	}
}


// WiFi Join Network - Scan, select AP, enter password, connect
void wifi_join_network(void)
{
    S_M1_Buttons_Status this_button_status;
    S_M1_Main_Q_t q_item;
    BaseType_t ret;
    ctrl_cmd_t app_req = CTRL_CMD_DEFAULT_REQ();
    uint16_t list_count = 0;
    static wifi_ap_scan_list_t *w_scan_p = NULL;
    static wifi_scanlist_t *list = NULL;
    static uint16_t selected_ap = 0;
    bool selecting = false;
    bool connecting = false;
    char password_buffer[WIFI_MAX_PASSWORD_LEN + 1] = {0};
    
    // Initialize ESP32 if needed
    if (!m1_esp32_get_init_status()) {
        m1_esp32_init();
        if (!get_esp32_main_init_status())
            esp32_main_init();
    }
    
    // Initialize credential storage
    wifi_cred_init();
    
    // Scanning phase
    m1_u8g2_firstpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
    u8g2_DrawStr(&m1_u8g2, 6, 15, "Scanning AP...");
    u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32, hourglass_18x32);
    m1_u8g2_nextpage();
    
    if (get_esp32_main_init_status()) {
        app_req.cmd_timeout_sec = M1_WIFI_AP_SCANNING_TIME;
        app_req.msg_id = CTRL_RESP_GET_AP_SCAN_LIST;
        ret = wifi_ap_scan_list(&app_req);
        ret = wifi_ap_list_validation(&app_req);
        
        if (ret) {
            w_scan_p = &app_req.u.wifi_ap_scan;
            list = w_scan_p->out_list;
            list_count = w_scan_p->count;
            selecting = true;
        } else {
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
            u8g2_DrawBox(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
            u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 32/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 32, 32, wifi_error_32x32);
            u8g2_DrawStr(&m1_u8g2, 6, 15 + M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, "Scan failed!");
            m1_u8g2_nextpage();
            m1_hard_delay(2000);
        }
    }
    
    // Selection phase
    if (selecting && list_count > 0) {
        selected_ap = 0;
        bool selection_made = false;
        
        while (!selection_made) {
            // Display current AP
            m1_u8g2_firstpage();
            u8g2_DrawXBMP(&m1_u8g2, 0, 0, 128, 14, m1_frame_128_14);
            u8g2_DrawStr(&m1_u8g2, 2, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, "Select AP:");
            
            char prn_msg[25];
            sprintf(prn_msg, "%d/%d", selected_ap + 1, list_count);
            u8g2_DrawStr(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 6*M1_GUI_FONT_WIDTH, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, prn_msg);
            
            uint8_t y_offset = 14 + M1_GUI_FONT_HEIGHT - 1;
            
            // SSID
            if (list[selected_ap].ssid[0] == 0x00)
                strcpy(prn_msg, "*hidden*");
            else
                strncpy(prn_msg, (char*)list[selected_ap].ssid, M1_LCD_DISPLAY_WIDTH/M1_GUI_FONT_WIDTH);
            u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);
            y_offset += M1_GUI_FONT_HEIGHT;
            
            // Security icon
            const char* sec_str = "Open";
            if (list[selected_ap].encryption_mode == 1) sec_str = "WEP";
            else if (list[selected_ap].encryption_mode == 2) sec_str = "WPA";
            else if (list[selected_ap].encryption_mode == 3) sec_str = "WPA2";
            else if (list[selected_ap].encryption_mode == 4) sec_str = "WPA3";
            sprintf(prn_msg, "Auth: %s", sec_str);
            u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);
            y_offset += M1_GUI_FONT_HEIGHT;
            
            // RSSI
            sprintf(prn_msg, "RSSI: %ddBm", list[selected_ap].rssi);
            u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);
            
            u8g2_DrawStr(&m1_u8g2, 6, 120, "UP/DN:Scroll OK:Sel");
            m1_u8g2_nextpage();
            
            // Wait for button press
            ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
            if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD) {
                ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
                
                if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
                    selection_made = true; // Cancel
                    break;
                } else if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) {
                    if (selected_ap > 0) selected_ap--;
                    else selected_ap = list_count - 1;
                } else if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK) {
                    selected_ap++;
                    if (selected_ap >= list_count) selected_ap = 0;
                } else if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
                    selection_made = true;
                    connecting = true;
                }
            }
        }
    }
    
    // Connection phase
    if (connecting) {
        bool need_password = (list[selected_ap].encryption_mode != 0);
        
        // Get password if needed
        if (need_password) {
            // Use virtual keyboard for password entry
            // TODO: Check if credential exists in saved networks
            wifi_credential_t cred;
            if (wifi_cred_load((char*)list[selected_ap].ssid, &cred)) {
                // Decrypt saved password
                char decrypted_pwd[WIFI_MAX_PASSWORD_LEN + 1];
                wifi_cred_decrypt(cred.encrypted_password, (uint8_t*)decrypted_pwd, cred.password_len);
                decrypted_pwd[cred.password_len] = '\0';
                strncpy(password_buffer, decrypted_pwd, WIFI_MAX_PASSWORD_LEN);
                password_buffer[WIFI_MAX_PASSWORD_LEN] = '\0';
            } else {
                // Prompt for password using full alphanumeric virtual keyboard
                uint8_t pwd_len = m1_vkb_get_password("Enter password:", password_buffer, WIFI_MAX_PASSWORD_LEN);
                if (pwd_len == 0) {
                    // Cancelled or empty
                    connecting = false;
                }
            }
        }
        
        // Connect to AP
        if (connecting) {
            m1_u8g2_firstpage();
            u8g2_DrawStr(&m1_u8g2, 6, 15, "Connecting...");
            u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32, hourglass_18x32);
            m1_u8g2_nextpage();
            
            // Prepare connect request
            ctrl_cmd_t connect_req = CTRL_CMD_DEFAULT_REQ();
            connect_req.cmd_timeout_sec = 15; // 15 seconds timeout
            connect_req.msg_id = CTRL_REQ_CONNECT_AP;
            strncpy((char*)connect_req.u.wifi_ap_config.ssid, (char*)list[selected_ap].ssid, SSID_LENGTH);
            
            if (need_password) {
                strncpy((char*)connect_req.u.wifi_ap_config.pwd, password_buffer, PASSWORD_LENGTH);
            }
            
            // Send connect command and wait for response
            ret = wifi_connect_ap(&connect_req);
            
            if (ret == SUCCESS) {
                if (need_password && strlen(password_buffer) > 0) {
                    // Save credential
                    int enc_mode = list[selected_ap].encryption_mode;
                    int auth_mode = WIFI_AUTH_OPEN;
                    if (enc_mode == 1) auth_mode = WIFI_AUTH_WEP;
                    else if (enc_mode == 2) auth_mode = WIFI_AUTH_WPA_PSK;
                    else if (enc_mode == 3) auth_mode = WIFI_AUTH_WPA2_PSK;
                    else if (enc_mode == 4) auth_mode = WIFI_AUTH_WPA3_PSK;
                    
                    wifi_cred_save((char*)list[selected_ap].ssid, password_buffer, auth_mode);
                }
                
                m1_u8g2_firstpage();
                u8g2_DrawStr(&m1_u8g2, 6, 15, "Connected!");
                u8g2_DrawStr(&m1_u8g2, 6, 35, (char*)list[selected_ap].ssid);
                m1_u8g2_nextpage();
                m1_hard_delay(2000);
            } else {
                m1_u8g2_firstpage();
                u8g2_DrawStr(&m1_u8g2, 6, 15, "Connect failed!");
                u8g2_DrawStr(&m1_u8g2, 6, 35, "Check password");
                m1_u8g2_nextpage();
                m1_hard_delay(2000);
            }
        }
    }
    
    // Cleanup
    if (app_req.u.wifi_ap_scan.out_list != NULL) {
        free(app_req.u.wifi_ap_scan.out_list);
    }
    xQueueReset(main_q_hdl);
}

void wifi_show_saved_networks(void)
{
    // TODO: Implement saved networks display
    m1_gui_let_update_fw();
}

void wifi_show_connection_status(void)
{
    // TODO: Implement connection status display
    m1_gui_let_update_fw();
}
