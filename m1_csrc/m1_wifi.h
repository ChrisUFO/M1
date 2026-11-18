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

#include <stddef.h>

#include "m1_system.h"
#include "ctrl_api.h"

void menu_wifi_init(void);
void menu_wifi_exit(void);

void menu_wifi_init(void);
uint8_t wifi_handshake(void);
/* wifi_deauth() (the low-level single-client deauth loop) is internal to
 * m1_wifi.c -- called only from wifi_deauth_client(). */
void wifi_beacon(void);
void wifi_scan_ap(void);
void wifi_survey(void);
void wifi_deauth_client(void);
void wifi_handshake_flow(void);
void wifi_scan_networks(void);
void wifi_saved_browser(void);
void wifi_config(void);
uint8_t wifi_ap_sta_scan_list(ctrl_cmd_t *app_req, uint8_t target);

/* Encrypted PSK credentials on the saved-networks CSV (issue #42 part 2).
 * Field: base64(iv||ct)":"base64(cmac); AES-256-CBC over a fixed 64-byte
 * framed plaintext, AES-CMAC over iv||ct, key derived from the device UID
 * (m1_crypto.h), IV from the hardware RNG (refusal, never a fallback).
 * See docs/security/wifi-credential-threat-model.md for the honest scope.
 *
 * wifi_cred_set_psk: seal, store, and persist the PSK for the saved
 * network with the given canonical uppercase BSSID. psk is ABSORBED
 * (zeroized on every return once sealing is attempted). NULL/"" clears
 * the stored credential. Returns 0 ok, 1 no such network, 2 save failed
 * (state rolled back), 3 crypto refused (RNG/KDF; nothing changed).
 *
 * wifi_cred_get_psk: decrypt at join time (re-verifies; never trusts the
 * load-time state alone). psk_out needs room for 65 bytes (the 64-char
 * hex-PMK form plus NUL). On any failure — absent credential, structural
 * damage, truncation, wrong CMAC, or a field sealed under a different
 * device's UID — the entry is marked unusable, skipped by the join flow,
 * and psk_out is returned zeroized. On success the CALLER must
 * m1_secure_zero() psk_out after use (the PR-11 join-flow contract).
 * Returns true iff psk_out holds the decrypted PSK. */
uint8_t wifi_cred_set_psk(const char *bssid_upper, char *psk);
bool wifi_cred_get_psk(uint16_t idx, char *psk_out, size_t outsz);

/* Shared Wi-Fi presentation used by the standard Wi-Fi tools and
 * MonstaShark. These functions draw only; they do not scan, select a radio
 * mode, send a command, or alter transport state. */
void wifi_ui_draw_scanning(const char *msg);
void wifi_ui_draw_ap_card(const char *heading, uint16_t sel, uint16_t count,
                          const char *ssid, const char *bssid, int rssi,
                          int channel, const char *security,
                          const char *status, const char *ok_label,
                          const char *refresh_hint);

#endif /* M1_WIFI_H_ */
