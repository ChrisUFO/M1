/* See COPYING.txt for license details. */

#include <string.h>
#include <stdlib.h>
#include "stm32h5xx_hal.h"
#include "ff.h"
#include "m1_wifi_cred.h"

#define KEY_SIZE 16

static wifi_cred_db_t cred_db;
static bool cred_db_loaded = false;

void wifi_cred_get_key(uint8_t* key, uint8_t len)
{
    const uint32_t* uid = (uint32_t*)UID_BASE;
    for (uint8_t i = 0; i < len; i++) {
        key[i] = ((uid[0] >> (i * 2)) ^ (uid[1] >> (i * 2 + 1)) ^ (uid[2] >> (i * 2 + 2))) & 0xFF;
        key[i] ^= 0x4D ^ 0x49 ^ 0x32 ^ 0x35;
    }
}

void wifi_cred_encrypt(const uint8_t* input, uint8_t* output, uint8_t len)
{
    uint8_t key[KEY_SIZE];
    wifi_cred_get_key(key, KEY_SIZE);
    for (uint8_t i = 0; i < len; i++) {
        output[i] = input[i] ^ key[i % KEY_SIZE];
    }
}

void wifi_cred_decrypt(const uint8_t* input, uint8_t* output, uint8_t len)
{
    wifi_cred_encrypt(input, output, len);
}

static bool cred_db_load(void)
{
    FIL file;
    FRESULT res;
    UINT bytes_read;
    wifi_cred_header_t header;
    uint8_t i;
    
    cred_db.num_networks = 0;
    cred_db_loaded = true;
    
    res = f_open(&file, WIFI_CRED_FILE_PATH, FA_READ);
    if (res != FR_OK) return true;
    
    res = f_read(&file, &header, sizeof(header), &bytes_read);
    if (res != FR_OK || bytes_read != sizeof(header)) {
        f_close(&file);
        return true;
    }
    
    if (header.magic != WIFI_CRED_MAGIC || header.version != WIFI_CRED_VERSION) {
        f_close(&file);
        return true;
    }
    
    uint8_t count = header.count;
    if (count > WIFI_MAX_SAVED_NETWORKS) count = WIFI_MAX_SAVED_NETWORKS;
    
    for (i = 0; i < count; i++) {
        res = f_read(&file, &cred_db.networks[i], sizeof(wifi_credential_t), &bytes_read);
        if (res != FR_OK || bytes_read != sizeof(wifi_credential_t)) break;
    }
    
    cred_db.num_networks = i;
    f_close(&file);
    return true;
}

static bool cred_db_save(void)
{
    FIL file;
    FRESULT res;
    UINT bytes_written;
    wifi_cred_header_t header;
    
    f_mkdir("WIFI");
    
    res = f_open(&file, WIFI_CRED_FILE_PATH, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) return false;
    
    header.magic = WIFI_CRED_MAGIC;
    header.version = WIFI_CRED_VERSION;
    header.count = cred_db.num_networks;
    header.reserved = 0;
    
    res = f_write(&file, &header, sizeof(header), &bytes_written);
    if (res != FR_OK || bytes_written != sizeof(header)) {
        f_close(&file);
        return false;
    }
    
    for (uint8_t i = 0; i < cred_db.num_networks; i++) {
        res = f_write(&file, &cred_db.networks[i], sizeof(wifi_credential_t), &bytes_written);
        if (res != FR_OK || bytes_written != sizeof(wifi_credential_t)) {
            f_close(&file);
            return false;
        }
    }
    
    f_close(&file);
    return true;
}

void wifi_cred_init(void)
{
    cred_db.num_networks = 0;
    cred_db_loaded = false;
    cred_db_load();
}

bool wifi_cred_save(const char* ssid, const char* password, int auth_mode)
{
    if (!ssid || !password) return false;
    if (!cred_db_loaded) cred_db_load();
    
    for (uint8_t i = 0; i < cred_db.num_networks; i++) {
        if (strcmp(cred_db.networks[i].ssid, ssid) == 0) {
            wifi_credential_t* cred = &cred_db.networks[i];
            uint8_t password_len = strlen(password);
            if (password_len > WIFI_MAX_PASSWORD_LEN) password_len = WIFI_MAX_PASSWORD_LEN;
            cred->password_len = password_len;
            wifi_cred_encrypt((const uint8_t*)password, cred->encrypted_password, password_len);
            cred->auth_mode = auth_mode;
            cred->last_connected = HAL_GetTick();
            return cred_db_save();
        }
    }
    
    if (cred_db.num_networks >= WIFI_MAX_SAVED_NETWORKS) return false;
    
    wifi_credential_t* cred = &cred_db.networks[cred_db.num_networks];
    strncpy(cred->ssid, ssid, WIFI_MAX_SSID_LEN);
    cred->ssid[WIFI_MAX_SSID_LEN] = 0;
    
    uint8_t password_len = strlen(password);
    if (password_len > WIFI_MAX_PASSWORD_LEN) password_len = WIFI_MAX_PASSWORD_LEN;
    cred->password_len = password_len;
    wifi_cred_encrypt((const uint8_t*)password, cred->encrypted_password, password_len);
    
    cred->auth_mode = auth_mode;
    cred->flags = 0x01;
    cred->last_connected = HAL_GetTick();
    cred_db.num_networks++;
    
    return cred_db_save();
}

bool wifi_cred_load(const char* ssid, wifi_credential_t* cred)
{
    if (!ssid || !cred) return false;
    if (!cred_db_loaded) cred_db_load();
    
    for (uint8_t i = 0; i < cred_db.num_networks; i++) {
        if (strcmp(cred_db.networks[i].ssid, ssid) == 0) {
            memcpy(cred, &cred_db.networks[i], sizeof(wifi_credential_t));
            return true;
        }
    }
    return false;
}

bool wifi_cred_delete(const char* ssid)
{
    if (!ssid) return false;
    if (!cred_db_loaded) cred_db_load();
    
    for (uint8_t i = 0; i < cred_db.num_networks; i++) {
        if (strcmp(cred_db.networks[i].ssid, ssid) == 0) {
            for (uint8_t j = i; j < cred_db.num_networks - 1; j++) {
                memcpy(&cred_db.networks[j], &cred_db.networks[j + 1], sizeof(wifi_credential_t));
            }
            cred_db.num_networks--;
            return cred_db_save();
        }
    }
    return false;
}

uint8_t wifi_cred_list(wifi_credential_t* out_networks, uint8_t max_count)
{
    if (!out_networks || max_count == 0) return 0;
    if (!cred_db_loaded) cred_db_load();
    
    uint8_t count = (cred_db.num_networks < max_count) ? cred_db.num_networks : max_count;
    for (uint8_t i = 0; i < count; i++) {
        memcpy(&out_networks[i], &cred_db.networks[i], sizeof(wifi_credential_t));
    }
    return count;
}

bool wifi_cred_get_auto_connect(wifi_credential_t* cred)
{
    if (!cred) return false;
    if (!cred_db_loaded) cred_db_load();
    
    uint32_t newest_time = 0;
    int8_t newest_idx = -1;
    
    for (uint8_t i = 0; i < cred_db.num_networks; i++) {
        if ((cred_db.networks[i].flags & 0x01) && cred_db.networks[i].last_connected > newest_time) {
            newest_time = cred_db.networks[i].last_connected;
            newest_idx = i;
        }
    }
    
    if (newest_idx >= 0) {
        memcpy(cred, &cred_db.networks[newest_idx], sizeof(wifi_credential_t));
        return true;
    }
    return false;
}

bool wifi_cred_set_auto_connect(const char* ssid, bool auto_connect)
{
    if (!ssid) return false;
    if (!cred_db_loaded) cred_db_load();
    
    for (uint8_t i = 0; i < cred_db.num_networks; i++) {
        if (strcmp(cred_db.networks[i].ssid, ssid) == 0) {
            if (auto_connect) {
                cred_db.networks[i].flags |= 0x01;
            } else {
                cred_db.networks[i].flags &= ~0x01;
            }
            return cred_db_save();
        }
    }
    return false;
}
