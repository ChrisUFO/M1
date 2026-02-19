/* See COPYING.txt for license details. */

/*
*
* m1_wifi_cred.h
*
* WiFi credential storage and encryption
*
* M1 Project
*
*/

#ifndef M1_WIFI_CRED_H_
#define M1_WIFI_CRED_H_

#include <stdint.h>
#include <stdbool.h>

/* ######################################### Definitions ############################################ */

#define WIFI_MAX_SAVED_NETWORKS     10
#define WIFI_MAX_SSID_LEN           32
#define WIFI_MAX_PASSWORD_LEN       64
#define WIFI_CRED_FILE_PATH         "WIFI/networks.bin"
#define WIFI_CRED_MAGIC             0x57494649  // "WIFI"
#define WIFI_CRED_VERSION           1
#define WIFI_CRED_FLAG_AUTO_CONNECT 0x01

/* ######################################### Data Types ############################################# */

// Use wifi_auth_mode_e from ctrl_api.h

typedef struct {
    char ssid[WIFI_MAX_SSID_LEN + 1];
    uint8_t encrypted_password[WIFI_MAX_PASSWORD_LEN + 32];  // +16 for IV, +16 for padding
    uint8_t encrypted_len;      // Length of encrypted data (including IV)
    int auth_mode;
    uint8_t flags;              // bit 0: auto-connect
    uint32_t last_connected;    // timestamp
} wifi_credential_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    uint32_t checksum;  // CRC32 of header + credentials
    uint32_t reserved;
} wifi_cred_header_t;

typedef struct {
    wifi_credential_t networks[WIFI_MAX_SAVED_NETWORKS];
    uint8_t num_networks;
} wifi_cred_db_t;

/* ##################################### Function Prototypes ###################################### */

// Initialize credential storage
void wifi_cred_init(void);

// Save credential to SD card
bool wifi_cred_save(const char* ssid, const char* password, int auth_mode);

// Load credential by SSID
bool wifi_cred_load(const char* ssid, wifi_credential_t* cred);

// Delete credential by SSID
bool wifi_cred_delete(const char* ssid);

// List all saved credentials
uint8_t wifi_cred_list(wifi_credential_t* out_networks, uint8_t max_count);

// Find credential for auto-connect
bool wifi_cred_get_auto_connect(wifi_credential_t* cred);

// Set auto-connect flag for a network
bool wifi_cred_set_auto_connect(const char* ssid, bool auto_connect);

// Encrypt/decrypt password using AES-256-CBC
void wifi_cred_encrypt(const uint8_t* input, uint8_t* output, uint8_t len, uint8_t* encrypted_len);
void wifi_cred_decrypt(const uint8_t* input, uint8_t* output, uint8_t encrypted_len);

// Get device-specific encryption key
void wifi_cred_get_key(uint8_t* key, uint8_t len);

#endif /* M1_WIFI_CRED_H_ */
