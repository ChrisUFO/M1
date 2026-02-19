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

#include "m1_wifi.h"
#include "m1_esp32_hal.h"
#include "m1_virtual_kb.h"
#include "m1_wifi_cred.h"
#include "main.h"
#include "stm32h5xx_hal.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// #include "control.h"
#include "ctrl_api.h"
#include "esp_app_main.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG "Wifi"

#define M1_WIFI_AP_SCANNING_TIME 30 // seconds

#define M1_GUI_ROW_SPACING 1

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
static void wifi_auto_connect(void);
static bool wifi_ensure_initialized(void);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
 * @brief
 * @param
 * @retval
 */
/*============================================================================*/
void menu_wifi_init(void) { ; } // void menu_wifi_init(void)

/*============================================================================*/
/**
 * @brief
 * @param
 * @retval
 */
/*============================================================================*/
void menu_wifi_exit(void) { ; } // void  menu_wifi_exit(void)

/*============================================================================*/
/**
 * @brief Scans for wifi access point list
 * @param
 * @retval
 */
/*============================================================================*/
void wifi_scan_ap(void) {
  S_M1_Buttons_Status this_button_status;
  S_M1_Main_Q_t q_item;
  BaseType_t ret;
  ctrl_cmd_t app_req = CTRL_CMD_DEFAULT_REQ();
  uint16_t list_count;

  /* Graphic work starts here */
  u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
  if (!wifi_ensure_initialized()) {
    m1_u8g2_firstpage();
    u8g2_DrawStr(&m1_u8g2, 6, 15, "Initializing...");
    u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH / 2 - 18 / 2,
                  M1_LCD_DISPLAY_HEIGHT / 2 - 2, 18, 32, hourglass_18x32);
    m1_u8g2_nextpage();
  }

  list_count = 0;

  m1_u8g2_firstpage();
  if (get_esp32_main_init_status()) {
    u8g2_DrawStr(&m1_u8g2, 6, 15, "Scanning AP...");
    u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH / 2 - 18 / 2,
                  M1_LCD_DISPLAY_HEIGHT / 2 - 2, 18, 32, hourglass_18x32);
    m1_u8g2_nextpage();

    // implemented synchronous
    app_req.cmd_timeout_sec =
        M1_WIFI_AP_SCANNING_TIME; // DEFAULT_CTRL_RESP_TIMEOUT //30 sec
    app_req.msg_id = CTRL_RESP_GET_AP_SCAN_LIST;
    ret = wifi_ap_scan_list(&app_req);
    ret = wifi_ap_list_validation(&app_req);
    if (ret) {
      list_count = wifi_ap_list_print(&app_req, true);
    } // if ( ret )
    else {
      u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
      u8g2_DrawBox(&m1_u8g2, M1_LCD_DISPLAY_WIDTH / 2 - 18 / 2,
                   M1_LCD_DISPLAY_HEIGHT / 2 - 2, 18, 32); // Clear old image
      u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
      u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH / 2 - 32 / 2,
                    M1_LCD_DISPLAY_HEIGHT / 2 - 2, 32, 32, wifi_error_32x32);
      u8g2_DrawStr(&m1_u8g2, 6, 15 + M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT,
                   "Failed. Let retry!");
      m1_u8g2_nextpage();
      // Reset the ESP module
      esp32_disable();
      m1_hard_delay(1);
      esp32_enable();
      /* stop spi transactions short time to avoid slave sync issues */
      m1_hard_delay(200);
    }
  } // if ( get_esp32_main_init_status() )
  else {
    u8g2_DrawStr(&m1_u8g2, 6, 15 + M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT,
                 "ESP32 not ready!");
    m1_u8g2_nextpage();
  }
  // mode declared in control.c should be set to MODE_STATION after M1 has been
  // connected to a Wifi network
  // mode |= MODE_STATION;

  while (1) // Main loop of this task
  {
    ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
    if (ret == pdTRUE) {
      if (q_item.q_evt_type == Q_EVENT_KEYPAD) {
        // Notification is only sent to this task when there's any button
        // activity, so it doesn't need to wait when reading the event from the
        // queue
        ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
        if (this_button_status.event[BUTTON_BACK_KP_ID] ==
            BUTTON_EVENT_CLICK) // user wants to exit?
        {
          ; // Do extra tasks here if needed
          if (app_req.u.wifi_ap_scan.out_list != NULL) {
            free(app_req.u.wifi_ap_scan.out_list);
          }
          wifi_ap_list_print(NULL, false);

          xQueueReset(main_q_hdl); // Reset main q before return
          m1_esp32_deinit();
          break; // Exit and return to the calling task (subfunc_handler_task)
        } // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
        else if (this_button_status.event[BUTTON_UP_KP_ID] ==
                 BUTTON_EVENT_CLICK) // go up?
        {
          if (list_count)
            wifi_ap_list_print(&app_req, true);
        } else if (this_button_status.event[BUTTON_DOWN_KP_ID] ==
                   BUTTON_EVENT_CLICK) // go down?
        {
          if (list_count)
            wifi_ap_list_print(&app_req, false);
        } else if (this_button_status.event[BUTTON_OK_KP_ID] ==
                   BUTTON_EVENT_CLICK) // Select?
        {
          ; // Do other things for this task, if needed
        }
      } // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
      else {
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
static uint16_t wifi_ap_list_print(ctrl_cmd_t *app_resp, bool up_dir) {
  static uint16_t i;
  static wifi_ap_scan_list_t *w_scan_p;
  static wifi_scanlist_t *list;
  static bool init_done = false;
  char prn_msg[64];
  uint8_t y_offset;

  if (!app_resp && !up_dir) // reset condition?
  {
    init_done = false;
    return 0;
  } // if ( !app_resp && !up_dir )

  if (!init_done) {
    if (!app_resp) {
      return 0;
    }
    init_done = true;
    w_scan_p = &app_resp->u.wifi_ap_scan;
    list = w_scan_p->out_list;

    if (!w_scan_p->count) {
      strcpy(prn_msg, "No AP found!");
      M1_LOG_I(M1_LOGDB_TAG, "No AP found\n\r");
      init_done = false;
    } else if (!list) {
      strcpy(prn_msg, "Try again!");
      M1_LOG_I(M1_LOGDB_TAG, "Failed to get scanned AP list\n\r");
      init_done = false;
    } else {
      M1_LOG_I(M1_LOGDB_TAG, "Number of available APs is %d\n\r",
               w_scan_p->count);
    }

    if (!init_done) {
      u8g2_DrawStr(&m1_u8g2, 6, 25 + M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT,
                   prn_msg);
      m1_u8g2_nextpage(); // Update display RAM
      return 0;
    }
    // Display first AP in the list
    i = 1;
    up_dir = true; // Overwrite the up_dir for the AP to be displayed for the
                   // first time
  } // if ( !init_done )

  // Defensive re-validation for stale state on re-entry
  if (!w_scan_p || !list || w_scan_p->count == 0 ||
      list != w_scan_p->out_list) {
    init_done = false;
    return 0;
  }

  if (up_dir) {
    if (i)
      i--;
    else
      i = w_scan_p->count - 1; // roll over
  } else {
    i++;
    if (i >= w_scan_p->count)
      i = 0; // roll over
  }

  m1_u8g2_firstpage();
  u8g2_DrawXBMP(&m1_u8g2, 0, 0, 128, 14, m1_frame_128_14);
  u8g2_DrawStr(&m1_u8g2, 2, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT,
               "Total AP:");

  sprintf(prn_msg, "%d", w_scan_p->count);
  u8g2_DrawStr(&m1_u8g2, 2 + strlen("Total AP: ") * M1_GUI_FONT_WIDTH + 2,
               M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, prn_msg);

  sprintf(prn_msg, "%d/%d", i + 1, w_scan_p->count); // Current AP
  u8g2_DrawStr(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 6 * M1_GUI_FONT_WIDTH,
               M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, prn_msg);

  y_offset = 14 + M1_GUI_FONT_HEIGHT - 1;
  // Draw text
  if (list[i].ssid[0] == 0x00) // Hidden SSID?
    strcpy(prn_msg, "*hidden*");
  else
    snprintf(prn_msg, sizeof(prn_msg), "%s",
             (char *)list[i].ssid);
  u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);
  y_offset += M1_GUI_FONT_HEIGHT;
  u8g2_DrawStr(&m1_u8g2, 2, y_offset, (char *)list[i].bssid);
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

  M1_LOG_D(M1_LOGDB_TAG,
           "%d) ssid \"%s\" bssid \"%s\" rssi \"%d\" channel \"%d\" auth mode "
           "\"%d\" \n\r",
           i, list[i].ssid, list[i].bssid, list[i].rssi, list[i].channel,
           list[i].encryption_mode);

  return w_scan_p->count;
} // static uint16_t wifi_ap_list_print(ctrl_cmd_t *app_resp, bool up_dir)

/*============================================================================*/
/**
 * @brief Validates the AP list.
 * @param
 * @retval
 */
/*============================================================================*/
static uint8_t wifi_ap_list_validation(ctrl_cmd_t *app_resp) {
  if (!app_resp || (app_resp->msg_type != CTRL_RESP)) {
    if (app_resp)
      M1_LOG_I(M1_LOGDB_TAG, "Msg type is not response[%u]\n\r",
               app_resp->msg_type);
    return false;
  }
  if (app_resp->resp_event_status != SUCCESS) {
    // process_failed_responses(app_resp);
    return false;
  }
  if (app_resp->msg_id != CTRL_RESP_GET_AP_SCAN_LIST) {
    M1_LOG_I(M1_LOGDB_TAG, "Invalid Response[%u] to parse\n\r",
             app_resp->msg_id);
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
void wifi_config(void) {
  S_M1_Buttons_Status this_button_status;
  S_M1_Main_Q_t q_item;
  BaseType_t ret;
  uint8_t menu_sel = 0;
  bool run_menu = true;

  // Initialize ESP32 if not already done
  (void)wifi_ensure_initialized();

  // Initialize credential storage
  wifi_cred_init();

  // Silent auto-connect on first entry after boot
  static bool auto_connect_tried = false;
  if (!auto_connect_tried) {
    wifi_auto_connect();
    auto_connect_tried = true;
  }

  // Show menu
  while (run_menu) {
    // Display menu
    m1_u8g2_firstpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
    u8g2_DrawStr(&m1_u8g2, 6, 15, "WiFi Config:");

    // Menu items
    const char *menu_items[] = {"Join Network", "Saved Networks",
                                "Connection Status"};
    const uint8_t num_items = sizeof(menu_items) / sizeof(menu_items[0]);

    // Draw menu items with selection indicator - tighter spacing to fit all 3
    // items
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

    u8g2_DrawStr(&m1_u8g2, 2, 62, "Back: Exit");
    m1_u8g2_nextpage();

    // Wait for button event
    ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
    if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD) {
      ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);

      if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
        xQueueReset(main_q_hdl);
        run_menu = false;
        break;
      } else if (this_button_status.event[BUTTON_UP_KP_ID] ==
                 BUTTON_EVENT_CLICK) {
        if (menu_sel > 0)
          menu_sel--;
      } else if (this_button_status.event[BUTTON_DOWN_KP_ID] ==
                 BUTTON_EVENT_CLICK) {
        if (menu_sel < num_items - 1)
          menu_sel++;
      } else if (this_button_status.event[BUTTON_OK_KP_ID] ==
                 BUTTON_EVENT_CLICK) {
        switch (menu_sel) {
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

static void wifi_auto_connect(void) {
  wifi_credential_t cred;
  if (!wifi_cred_get_auto_connect(&cred))
    return;

  if (!get_esp32_main_init_status())
    return;

  ctrl_cmd_t connect_req = CTRL_CMD_DEFAULT_REQ();
  connect_req.cmd_timeout_sec = 10;
  connect_req.msg_id = CTRL_REQ_CONNECT_AP;
  snprintf((char *)connect_req.u.wifi_ap_config.ssid, SSID_LENGTH, "%s",
           cred.ssid);

  if (cred.encrypted_len > 0) {
    char decrypted_pwd[WIFI_MAX_PASSWORD_LEN + 1];
    wifi_cred_decrypt(cred.encrypted_password, (uint8_t *)decrypted_pwd,
                      cred.encrypted_len);
    snprintf((char *)connect_req.u.wifi_ap_config.pwd, PASSWORD_LENGTH, "%s",
             decrypted_pwd);
    memset(decrypted_pwd, 0, sizeof(decrypted_pwd));
  }

  (void)wifi_connect_ap(&connect_req); // silent fail by design
}

static bool wifi_ensure_initialized(void) {
  if (!m1_esp32_get_init_status()) {
    m1_esp32_init();
  }
  if (!get_esp32_main_init_status()) {
    esp32_main_init();
  }
  return get_esp32_main_init_status();
}

// WiFi Join Network - Scan, select AP, enter password, connect
void wifi_join_network(void) {
  S_M1_Buttons_Status this_button_status;
  S_M1_Main_Q_t q_item;
  BaseType_t ret;
  ctrl_cmd_t app_req = CTRL_CMD_DEFAULT_REQ();
  uint16_t list_count = 0;
  wifi_ap_scan_list_t *w_scan_p = NULL;
  wifi_scanlist_t *list = NULL;
  uint16_t selected_ap = 0;
  bool selecting = false;
  bool connecting = false;
  bool manual_entry = false;
  int manual_auth_mode = WIFI_AUTH_WPA2_PSK;
  char password_buffer[WIFI_MAX_PASSWORD_LEN + 1] = {0};

  // Initialize ESP32 if needed
  (void)wifi_ensure_initialized();

  // Initialize credential storage
  wifi_cred_init();

  // Scanning phase
  m1_u8g2_firstpage();
  u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
  u8g2_DrawStr(&m1_u8g2, 6, 15, "Scanning AP...");
  u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH / 2 - 18 / 2,
                M1_LCD_DISPLAY_HEIGHT / 2 - 2, 18, 32, hourglass_18x32);
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
      u8g2_DrawBox(&m1_u8g2, M1_LCD_DISPLAY_WIDTH / 2 - 18 / 2,
                   M1_LCD_DISPLAY_HEIGHT / 2 - 2, 18, 32);
      u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
      u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH / 2 - 32 / 2,
                    M1_LCD_DISPLAY_HEIGHT / 2 - 2, 32, 32, wifi_error_32x32);
      u8g2_DrawStr(&m1_u8g2, 6, 15 + M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT,
                   "Scan failed!");
      m1_u8g2_nextpage();
      m1_hard_delay(2000);
    }
  }

  // Selection phase
  if (selecting && list_count > 0) {
    selected_ap = 0;
    bool selection_made = false;
    uint16_t total_items = list_count + 1; // +1 for manual/hidden network

    while (!selection_made) {
      // Display current AP
      m1_u8g2_firstpage();
      u8g2_DrawXBMP(&m1_u8g2, 0, 0, 128, 14, m1_frame_128_14);
      u8g2_DrawStr(&m1_u8g2, 2, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT,
                   "Select AP:");

      char prn_msg[64];
      sprintf(prn_msg, "%d/%d", selected_ap + 1, total_items);
      u8g2_DrawStr(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 6 * M1_GUI_FONT_WIDTH,
                   M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, prn_msg);

      uint8_t y_offset = 14 + M1_GUI_FONT_HEIGHT - 1;

      if (selected_ap == list_count) {
        strcpy(prn_msg, "*Manual SSID*");
        u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);
        y_offset += M1_GUI_FONT_HEIGHT;
        u8g2_DrawStr(&m1_u8g2, 2, y_offset, "Enter hidden AP");
        y_offset += M1_GUI_FONT_HEIGHT;
        u8g2_DrawStr(&m1_u8g2, 2, y_offset, "Auth: Select later");
      } else {
        // SSID
        if (list[selected_ap].ssid[0] == 0x00)
          strcpy(prn_msg, "*hidden*");
        else {
          snprintf(prn_msg, sizeof(prn_msg), "%s",
                   (char *)list[selected_ap].ssid);
        }
        u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);
        y_offset += M1_GUI_FONT_HEIGHT;

        // Security icon
        const char *sec_str = "Open";
        if (list[selected_ap].encryption_mode == 1)
          sec_str = "WEP";
        else if (list[selected_ap].encryption_mode == 2)
          sec_str = "WPA";
        else if (list[selected_ap].encryption_mode == 3)
          sec_str = "WPA2";
        else if (list[selected_ap].encryption_mode == 4)
          sec_str = "WPA3";
        sprintf(prn_msg, "Auth: %s", sec_str);
        u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);
        y_offset += M1_GUI_FONT_HEIGHT;

        // RSSI with signal bars
        int8_t rssi = list[selected_ap].rssi;
        uint8_t bars = 0;
        if (rssi > -50)
          bars = 4;
        else if (rssi > -60)
          bars = 3;
        else if (rssi > -70)
          bars = 2;
        else if (rssi > -80)
          bars = 1;

        sprintf(prn_msg, "RSSI:");
        u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);

        // Draw signal bars (x=50, y_offset-7)
        for (uint8_t b = 0; b < 4; b++) {
          uint8_t bar_height = 4 + (b * 3);
          if (b < bars) {
            u8g2_DrawBox(&m1_u8g2, 50 + (b * 6), y_offset - bar_height, 4,
                         bar_height);
          } else {
            u8g2_DrawFrame(&m1_u8g2, 50 + (b * 6), y_offset - bar_height, 4,
                           bar_height);
          }
        }
      }

      u8g2_DrawStr(&m1_u8g2, 2, 60, "UP/DN:Scroll OK:Sel");
      m1_u8g2_nextpage();

      // Wait for button press
      ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
      if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD) {
        ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);

        if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
          selection_made = true; // Cancel
          break;
        } else if (this_button_status.event[BUTTON_UP_KP_ID] ==
                   BUTTON_EVENT_CLICK) {
          if (selected_ap > 0)
            selected_ap--;
          else
            selected_ap = total_items - 1;
        } else if (this_button_status.event[BUTTON_DOWN_KP_ID] ==
                   BUTTON_EVENT_CLICK) {
          selected_ap++;
          if (selected_ap >= total_items)
            selected_ap = 0;
        } else if (this_button_status.event[BUTTON_OK_KP_ID] ==
                   BUTTON_EVENT_CLICK) {
          manual_entry = (selected_ap == list_count);
          selection_made = true;
          connecting = true;
        }
      }
    }
  }

  // Connection phase
  if (connecting) {
    bool need_password = false;
    uint8_t target_ssid[SSID_LENGTH] = {0};

    if (manual_entry) {
      char ssid_buf[SSID_LENGTH] = {0};
      if (m1_vkb_get_filename("Enter SSID:", "", ssid_buf) == 0) {
        connecting = false;
      } else {
        size_t s_len = strlen(ssid_buf);
        if (s_len >= SSID_LENGTH) s_len = SSID_LENGTH - 1;
        memcpy(target_ssid, ssid_buf, s_len);
        target_ssid[s_len] = '\0';
      }

      // Security type selection
      const char *auth_options[] = {"Open", "WPA", "WPA2", "WPA3"};
      uint8_t auth_idx = 2;
      bool auth_done = false;
      while (!auth_done && connecting) {
        m1_u8g2_firstpage();
        u8g2_DrawStr(&m1_u8g2, 2, 14, "Security Type:");
        u8g2_DrawStr(&m1_u8g2, 2, 34, auth_options[auth_idx]);
        u8g2_DrawStr(&m1_u8g2, 2, 62, "UP/DN:Change OK:Sel");
        m1_u8g2_nextpage();

        ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
        if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD) {
          ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
          if (this_button_status.event[BUTTON_BACK_KP_ID] ==
              BUTTON_EVENT_CLICK) {
            connecting = false;
            break;
          } else if (this_button_status.event[BUTTON_UP_KP_ID] ==
                     BUTTON_EVENT_CLICK) {
            if (auth_idx > 0)
              auth_idx--;
            else
              auth_idx = 3;
          } else if (this_button_status.event[BUTTON_DOWN_KP_ID] ==
                     BUTTON_EVENT_CLICK) {
            auth_idx = (auth_idx + 1) % 4;
          } else if (this_button_status.event[BUTTON_OK_KP_ID] ==
                     BUTTON_EVENT_CLICK) {
            auth_done = true;
          }
        }
      }

      if (auth_idx == 0)
        manual_auth_mode = WIFI_AUTH_OPEN;
      else if (auth_idx == 1)
        manual_auth_mode = WIFI_AUTH_WPA_PSK;
      else if (auth_idx == 2)
        manual_auth_mode = WIFI_AUTH_WPA2_PSK;
      else
        manual_auth_mode = WIFI_AUTH_WPA3_PSK;

      need_password = (manual_auth_mode != WIFI_AUTH_OPEN);
    } else {
      need_password = (list[selected_ap].encryption_mode != 0);
      size_t s_len = strlen((char *)list[selected_ap].ssid);
      if (s_len >= SSID_LENGTH) s_len = SSID_LENGTH - 1;
      memcpy(target_ssid, list[selected_ap].ssid, s_len);
      target_ssid[s_len] = '\0';
    }

    // Get password if needed
    if (need_password) {
      // Use virtual keyboard for password entry
      wifi_credential_t cred;
      if (!manual_entry && wifi_cred_load((char *)target_ssid, &cred)) {
        // Decrypt saved password
        char decrypted_pwd[WIFI_MAX_PASSWORD_LEN + 1];
        wifi_cred_decrypt(cred.encrypted_password, (uint8_t *)decrypted_pwd,
                          cred.encrypted_len);
        strncpy(password_buffer, decrypted_pwd, WIFI_MAX_PASSWORD_LEN);
        password_buffer[WIFI_MAX_PASSWORD_LEN] = '\0';
      } else {
        // Prompt for password using full alphanumeric virtual keyboard
        uint8_t pwd_len = m1_vkb_get_password(
            "Enter password:", password_buffer, WIFI_MAX_PASSWORD_LEN);
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
      u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH / 2 - 18 / 2,
                    M1_LCD_DISPLAY_HEIGHT / 2 - 2, 18, 32, hourglass_18x32);
      m1_u8g2_nextpage();

      // Prepare connect request
      ctrl_cmd_t connect_req = CTRL_CMD_DEFAULT_REQ();
      connect_req.cmd_timeout_sec = 15; // 15 seconds timeout
      connect_req.msg_id = CTRL_REQ_CONNECT_AP;
      snprintf((char *)connect_req.u.wifi_ap_config.ssid, SSID_LENGTH, "%s",
               (char *)target_ssid);

      if (need_password) {
        strncpy((char *)connect_req.u.wifi_ap_config.pwd, password_buffer,
                PASSWORD_LENGTH);
      }

      // Send connect command and wait for response
      ret = wifi_connect_ap(&connect_req);

      if (ret == SUCCESS) {
        if (need_password && strlen(password_buffer) > 0) {
          // Save credential
          int auth_mode = WIFI_AUTH_OPEN;
          if (manual_entry) {
            auth_mode = manual_auth_mode;
          } else {
            int enc_mode = list[selected_ap].encryption_mode;
            if (enc_mode == 1)
              auth_mode = WIFI_AUTH_WEP;
            else if (enc_mode == 2)
              auth_mode = WIFI_AUTH_WPA_PSK;
            else if (enc_mode == 3)
              auth_mode = WIFI_AUTH_WPA2_PSK;
            else if (enc_mode == 4)
              auth_mode = WIFI_AUTH_WPA3_PSK;
          }

          wifi_cred_save((char *)target_ssid, password_buffer, auth_mode);
        }

        m1_u8g2_firstpage();
        u8g2_DrawStr(&m1_u8g2, 6, 15, "Connected!");
        u8g2_DrawStr(&m1_u8g2, 6, 35, (char *)target_ssid);
        m1_u8g2_nextpage();
        m1_hard_delay(2000);
      } else {
        m1_u8g2_firstpage();
        u8g2_DrawStr(&m1_u8g2, 6, 15, "Connect failed!");
        u8g2_DrawStr(&m1_u8g2, 6, 35, "Check password");
        m1_u8g2_nextpage();
        m1_hard_delay(2000);
      }
      memset(password_buffer, 0, sizeof(password_buffer));
    }
  }

  // Cleanup
  if (app_req.u.wifi_ap_scan.out_list != NULL) {
    free(app_req.u.wifi_ap_scan.out_list);
  }
  xQueueReset(main_q_hdl);
}

void wifi_show_saved_networks(void) {
  S_M1_Buttons_Status this_button_status;
  S_M1_Main_Q_t q_item;
  BaseType_t ret;
  wifi_credential_t networks[WIFI_MAX_SAVED_NETWORKS];
  uint8_t num_networks;
  uint8_t selected = 0;
  uint8_t row_offset = 0;
  bool exit_menu = false;

  // Get list of saved networks
  num_networks = wifi_cred_list(networks, WIFI_MAX_SAVED_NETWORKS);

  while (!exit_menu) {
    m1_u8g2_firstpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

    // Title
    u8g2_DrawStr(&m1_u8g2, 2, 10, "Saved Networks:");

    if (num_networks == 0) {
      u8g2_DrawStr(&m1_u8g2, 6, 30, "No saved networks");
      u8g2_DrawStr(&m1_u8g2, 2, 60, "Back: Return");
    } else {
      // Show up to 3 networks (with spacing for 64px height)
      uint8_t display_count = (num_networks > 3) ? 3 : num_networks;

      for (uint8_t i = 0; i < display_count; i++) {
        uint8_t y_pos = 24 + (i * 14);

        uint8_t idx = row_offset + i;
        if (idx >= num_networks)
          break;

        // Highlight selected
        if (idx == selected) {
          u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
          u8g2_DrawBox(&m1_u8g2, 0, y_pos - 7, 128, 11);
          u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
        }

        // Truncate SSID if too long
        char display_ssid[22];
        strncpy(display_ssid, networks[idx].ssid, 21);
        display_ssid[21] = '\0';
        u8g2_DrawStr(&m1_u8g2, 4, y_pos, display_ssid);

        // Show auto-connect indicator
        if (networks[idx].flags & 0x01) {
          u8g2_DrawStr(&m1_u8g2, 110, y_pos, "*");
        }

        if (idx == selected) {
          u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
        }
      }

      // Instructions
      u8g2_DrawStr(&m1_u8g2, 2, 60, "OK:Del LongOK:Auto");
    }

    m1_u8g2_nextpage();

    // Wait for button
    ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
    if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD) {
      ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);

      if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
        exit_menu = true;
      } else if (num_networks > 0) {
        if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) {
          if (selected > 0)
            selected--;
          else
            selected = num_networks - 1;
          if (selected < row_offset) {
            row_offset = selected;
          }
        } else if (this_button_status.event[BUTTON_DOWN_KP_ID] ==
                   BUTTON_EVENT_CLICK) {
          selected++;
          if (selected >= num_networks)
            selected = 0;
          if (selected >= row_offset + 3) {
            row_offset = selected - 2;
          }
        } else if (this_button_status.event[BUTTON_OK_KP_ID] ==
                   BUTTON_EVENT_CLICK) {
          // Delete confirmation
          m1_u8g2_firstpage();
          u8g2_DrawStr(&m1_u8g2, 2, 10, "Delete network?");
          u8g2_DrawStr(&m1_u8g2, 6, 30, networks[selected].ssid);
          u8g2_DrawStr(&m1_u8g2, 2, 50, "OK:Confirm Back:Cancel");
          m1_u8g2_nextpage();

          // Wait for confirmation
          bool waiting = true;
          while (waiting) {
            ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
            if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD) {
              ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
              if (this_button_status.event[BUTTON_OK_KP_ID] ==
                  BUTTON_EVENT_CLICK) {
                wifi_cred_delete(networks[selected].ssid);
                num_networks =
                    wifi_cred_list(networks, WIFI_MAX_SAVED_NETWORKS);
                if (selected >= num_networks)
                  selected = 0;
                if (row_offset > selected)
                  row_offset = selected;
                waiting = false;
              } else if (this_button_status.event[BUTTON_BACK_KP_ID] ==
                         BUTTON_EVENT_CLICK) {
                waiting = false;
              }
            }
          }
        } else if (this_button_status.event[BUTTON_OK_KP_ID] ==
                   BUTTON_EVENT_LCLICK) {
          bool is_auto = ((networks[selected].flags & 0x01) != 0);
          wifi_cred_set_auto_connect(networks[selected].ssid, !is_auto);
          num_networks = wifi_cred_list(networks, WIFI_MAX_SAVED_NETWORKS);
        }
      }
    }
  }

  xQueueReset(main_q_hdl);
}

void wifi_show_connection_status(void) {
  S_M1_Buttons_Status this_button_status;
  S_M1_Main_Q_t q_item;
  BaseType_t ret;
  bool exit_menu = false;
  char status_ssid[33] = "";
  char status_ip[17] = "Not connected";
  int status_rssi = 0;
  bool is_connected = false;

  // Initialize ESP32 if needed
  (void)wifi_ensure_initialized();

  while (!exit_menu) {
    // Auto-refresh status
    if (get_esp32_main_init_status()) {
      ctrl_cmd_t status_req = CTRL_CMD_DEFAULT_REQ();
      status_req.cmd_timeout_sec = 5;
      status_req.msg_id = CTRL_REQ_GET_AP_CONFIG;

      if (wifi_get_ap_config(&status_req) == SUCCESS &&
          strncmp(status_req.u.wifi_ap_config.status, "CONNECTED",
                  STATUS_LENGTH - 1) == 0) {
        is_connected = true;
        snprintf(status_ssid, sizeof(status_ssid), "%s",
                 (char *)status_req.u.wifi_ap_config.ssid);
        if (status_req.u.wifi_ap_config.out_mac[0]) {
          strncpy(status_ip, status_req.u.wifi_ap_config.out_mac, 16);
          status_ip[16] = '\0';
        } else {
          strcpy(status_ip, "IP: unknown");
        }
        status_rssi = status_req.u.wifi_ap_config.rssi;
      } else {
        is_connected = false;
        status_ssid[0] = 0;
        strcpy(status_ip, "Not connected");
        status_rssi = 0;
      }
    }

    m1_u8g2_firstpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

    // Title
    u8g2_DrawStr(&m1_u8g2, 2, 10, "WiFi Status:");

    if (!get_esp32_main_init_status()) {
      u8g2_DrawStr(&m1_u8g2, 6, 30, "ESP32 not ready");
    } else if (!is_connected) {
      u8g2_DrawStr(&m1_u8g2, 6, 24, "Status: Disconnected");
      u8g2_DrawStr(&m1_u8g2, 6, 36, "No network");
    } else {
      // Display actual connection info
      char display_line[22];

      u8g2_DrawStr(&m1_u8g2, 6, 24, "Status: Connected");

      // SSID (truncate if needed)
      strncpy(display_line, status_ssid, 21);
      display_line[21] = '\0';
      u8g2_DrawStr(&m1_u8g2, 6, 36, display_line);

      // IP Address
      u8g2_DrawStr(&m1_u8g2, 6, 48, status_ip);

      // RSSI unknown until status AT command is implemented in SPI-AT glue
      if (status_rssi == 0) {
        strcpy(display_line, "RSSI: unknown");
      } else {
        sprintf(display_line, "RSSI: %ddBm", status_rssi);
      }
      u8g2_DrawStr(&m1_u8g2, 6, 60, display_line);
    }

    if (is_connected) {
      u8g2_DrawStr(&m1_u8g2, 2, 62, "OK:Disconnect Back:Exit");
    } else {
      u8g2_DrawStr(&m1_u8g2, 2, 62, "Back: Exit");
    }
    m1_u8g2_nextpage();

    // Wait for button
    ret = xQueueReceive(main_q_hdl, &q_item, pdMS_TO_TICKS(5000));
    if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD) {
      ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);

      if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
        exit_menu = true;
      } else if (this_button_status.event[BUTTON_OK_KP_ID] ==
                     BUTTON_EVENT_CLICK &&
                 is_connected) {
        // Disconnect
        m1_u8g2_firstpage();
        u8g2_DrawStr(&m1_u8g2, 6, 30, "Disconnecting...");
        m1_u8g2_nextpage();

        ctrl_cmd_t disc_req = CTRL_CMD_DEFAULT_REQ();
        disc_req.cmd_timeout_sec = 5;
        disc_req.msg_id = CTRL_REQ_DISCONNECT_AP;
        (void)wifi_disconnect_ap(&disc_req);

        is_connected = false;
        strcpy(status_ip, "Not connected");
        m1_hard_delay(1000);
      }
    } else {
      // timeout -> refresh screen
      continue;
    }
  }

  xQueueReset(main_q_hdl);
}
