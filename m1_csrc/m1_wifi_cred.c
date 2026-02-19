/* See COPYING.txt for license details. */

#include <string.h>
#include <stdlib.h>
#include "stm32h5xx_hal.h"
#include "ff.h"
#include "m1_wifi_cred.h"
#include "m1_crypto.h"

static wifi_cred_db_t cred_db;
static bool cred_db_loaded = false;

static uint32_t cred_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static uint32_t cred_db_checksum(uint16_t count)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint8_t meta[8];

    memcpy(&meta[0], &((uint32_t){WIFI_CRED_MAGIC}), sizeof(uint32_t));
    memcpy(&meta[4], &((uint16_t){WIFI_CRED_VERSION}), sizeof(uint16_t));
    memcpy(&meta[6], &count, sizeof(uint16_t));

    uint32_t meta_crc = cred_crc32(meta, sizeof(meta));
    crc ^= meta_crc;

    uint32_t data_len = (uint32_t)count * (uint32_t)sizeof(wifi_credential_t);
    uint32_t data_crc = cred_crc32((const uint8_t *)cred_db.networks, data_len);
    crc ^= data_crc;

    return crc;
}

void wifi_cred_get_key(uint8_t* key, uint8_t len)
{
    m1_crypto_derive_key(key, len);
}

void wifi_cred_encrypt(const uint8_t* input, uint8_t* output, uint8_t len, uint8_t* encrypted_len)
{
    uint8_t encrypted[WIFI_MAX_PASSWORD_LEN + 32];  // Max password + IV + padding
    uint8_t out_len = 0;
    
    if (m1_crypto_encrypt(input, encrypted, len, &out_len)) {
        *encrypted_len = out_len;
        memcpy(output, encrypted, out_len);
    } else {
        *encrypted_len = 0;
    }
}

void wifi_cred_decrypt(const uint8_t* input, uint8_t* output, uint8_t encrypted_len)
{
    uint8_t decrypted[WIFI_MAX_PASSWORD_LEN + 32];
    uint8_t out_len = 0;
    
    if (m1_crypto_decrypt(input, decrypted, encrypted_len, &out_len)) {
        memcpy(output, decrypted, out_len);
        output[out_len] = '\0';  // Null terminate
    } else {
        output[0] = '\0';
    }
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

    // Validate checksum over all credential content that was read
    uint32_t calc_checksum = cred_db_checksum(i);
    if (calc_checksum != header.checksum) {
        f_close(&file);
        cred_db.num_networks = 0;
        memset(cred_db.networks, 0, sizeof(cred_db.networks));
        return true;  // Corrupted file, start fresh
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
    
    // CRC32 checksum over metadata + full credential records
    header.checksum = cred_db_checksum(cred_db.num_networks);
    
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
    
    m1_crypto_init();  // Ensure crypto is initialized
    
    for (uint8_t i = 0; i < cred_db.num_networks; i++) {
        if (strcmp(cred_db.networks[i].ssid, ssid) == 0) {
            wifi_credential_t* cred = &cred_db.networks[i];
            uint8_t password_len = strlen(password);
            if (password_len > WIFI_MAX_PASSWORD_LEN) password_len = WIFI_MAX_PASSWORD_LEN;
            wifi_cred_encrypt((const uint8_t*)password, cred->encrypted_password, password_len, &cred->encrypted_len);
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
    wifi_cred_encrypt((const uint8_t*)password, cred->encrypted_password, password_len, &cred->encrypted_len);
    
    cred->auth_mode = auth_mode;
    cred->flags = WIFI_CRED_FLAG_AUTO_CONNECT;
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
            memset(&cred_db.networks[cred_db.num_networks], 0, sizeof(wifi_credential_t));
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

    // Most recently used first
    for (uint8_t i = 0; i < count; i++) {
        for (uint8_t j = i + 1; j < count; j++) {
            if (out_networks[j].last_connected > out_networks[i].last_connected) {
                wifi_credential_t tmp;
                memcpy(&tmp, &out_networks[i], sizeof(wifi_credential_t));
                memcpy(&out_networks[i], &out_networks[j], sizeof(wifi_credential_t));
                memcpy(&out_networks[j], &tmp, sizeof(wifi_credential_t));
            }
        }
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
        if ((cred_db.networks[i].flags & WIFI_CRED_FLAG_AUTO_CONNECT) && cred_db.networks[i].last_connected > newest_time) {
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
                cred_db.networks[i].flags |= WIFI_CRED_FLAG_AUTO_CONNECT;
            } else {
                cred_db.networks[i].flags &= ~WIFI_CRED_FLAG_AUTO_CONNECT;
            }
            return cred_db_save();
        }
    }
    return false;
}
