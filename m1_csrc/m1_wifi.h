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


#ifndef M1_WIFI_H_
#define M1_WIFI_H_

void menu_wifi_init(void);
void menu_wifi_exit(void);

void menu_wifi_init(void);
void wifi_scan_ap(void);
void wifi_config(void);

// WiFi credential functions (stubs for now)
void wifi_cred_init(void);
void wifi_join_network(void);
void wifi_show_saved_networks(void);
void wifi_show_connection_status(void);

#endif /* M1_WIFI_H_ */
