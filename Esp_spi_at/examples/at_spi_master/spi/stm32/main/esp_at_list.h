/*
*
* esp_at_list.h
*
* AT commands list
*
* B.N @ MonstaTek, Jul 2025
*
*/

#ifndef ESP32C6_AT_LIST_H_
#define ESP32C6_AT_LIST_H_

#define CONCAT_CMD_PARAM(cmd, param)		cmd param ESP32C6_AT_REQ_CRLF

#define ESP32C6_WIFI_MODE_NULL		"0" // Null mode. Wi-Fi RF will be disabled.
#define ESP32C6_WIFI_MODE_STA		"1" // Station mode.
#define ESP32C6_WIFI_MODE_AP		"2" // SoftAP mode.
#define ESP32C6_WIFI_MODE_APSTA		"3" // SoftAP+Station mode.

#define ESP32C6_BLE_MODE_NULL		"0" // deinit Bluetooth LE
#define ESP32C6_BLE_MODE_CLI		"1" // client role
#define ESP32C6_BLE_MODE_SER		"2" // server role

// Syntax: AT+CWMODE=<mode>[,<auto_connect>]
// Response; OK
#define ESP32C6_AT_REQ_WIFI_MODE		"AT+CWMODE="

#define ESP32C6_AT_REQ_LIST_AP			"AT+CWLAP"
#define ESP32C6_AT_RES_LIST_AP_KEY		"+CWLAP:"

#define ESP32C6_AT_REQ_CRLF				"\r\n"

#define ESP32C6_AT_RES_OK				"OK"
#define ESP32C6_AT_RES_READY			"ready"

#define ESP32C6_AT_REQ_BLE_MODE			"AT+BLEINIT="
#define ESP32C6_AT_REQ_BLE_SCAN			"AT+BLESCAN=1," // AT+BLESCAN=<enable>[,<duration>][,<filter_type>,<filter_param>]
#define ESP32C6_AT_RES_BLE_SCAN_KEY		"+BLESCAN:"

#define ESP32C6_AT_REQ_ADVERTISE		"AT+BLEADVDATAEX=" // AT+BLEADVDATAEX=<dev_name>,<uuid>,<manufacturer_data>,<include_power>
#define ESP32C6_AT_REQ_ADV_DATA			"\"MONSTATEK-M1\",\"A000\",\"1A2B3C4D5E\",1"

#define ESP32C6_AT_RESET				"AT+RST"

/* Station join (PR-11, issue #42): AT+CWJAP=<ssid>,<pwd>.
 * Responses: "WIFI CONNECTED" / "WIFI GOT IP" / "OK" on success, or
 * "+CWJAP:<error_code>" (1 timeout, 2 wrong password, 3 AP not found,
 * 4 connection failed) followed by "FAIL". The wifi_connect_ap()
 * handler maps those onto the CTRL_ERR_* resp_event_status codes the
 * CTRL_RESP_CONNECT_AP reply carries (see ctrl_api.h). */
#define ESP32C6_AT_REQ_CONNECT_AP		"AT+CWJAP="
#define ESP32C6_AT_RES_CONN_AP_FAIL		"+CWJAP:"
#define ESP32C6_AT_RES_CONN_GOT_IP		"WIFI GOT IP"
#define ESP32C6_AT_RES_FAIL				"FAIL"

/* Paired-firmware version (PR-11). The ESP application built from this
 * tree reports this exact line for the read-only console `version` verb
 * -- the shared ESPFW handshake contract the STM32's live feature gate
 * parses (m1_csrc/m1_esp_feature_gate.h). ESP_FEATURE_WIFI_JOIN's
 * minimum is {major,minor,patch,rc} BELOW, so an image that does not
 * answer (older/foreign firmware) is refused and the join handler is
 * only reached on a version-paired image. m1_csrc/test/
 * m1_wifi_join_test.py pins these constants together. */
#define ESP32C6_FW_VERSION_MAJOR		1
#define ESP32C6_FW_VERSION_MINOR		1
#define ESP32C6_FW_VERSION_PATCH		0
#define ESP32C6_FW_VERSION_RC		0
#define ESP32C6_FW_VERSION_LINE		"ESPFW 1.1.0.0"

#endif /* ESP32C6_AT_LIST_H_ */
