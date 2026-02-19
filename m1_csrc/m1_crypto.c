/* See COPYING.txt for license details. */

/*
*
* m1_crypto.c
*
* Software AES-256-CBC implementation for WiFi credential protection
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <string.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_crypto.h"

/*************************** D E F I N E S ************************************/

#define M1_CRYPTO_TAG				"CRYPTO"

// Use software AES if hardware CRYP not available
#define USE_SOFTWARE_AES			1

/***************************** V A R I A B L E S ******************************/

static uint8_t crypto_key[M1_CRYPTO_KEY_SIZE];
static bool crypto_initialized = false;

// AES S-box for software implementation
static const uint8_t sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

// Rcon for key schedule
static const uint8_t Rcon[11] = {
    0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void SubBytes(uint8_t* state);
static void ShiftRows(uint8_t* state);
static void MixColumns(uint8_t* state);
static void InvSubBytes(uint8_t* state);
static void InvShiftRows(uint8_t* state);
static void InvMixColumns(uint8_t* state);
static void AddRoundKey(uint8_t* state, const uint8_t* roundKey);
static void KeyExpansion(const uint8_t* key, uint8_t* expandedKey);
static void XorBlock(uint8_t* out, const uint8_t* a, const uint8_t* b);
static void CopyBlock(uint8_t* out, const uint8_t* in);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
 * @brief Initialize crypto module - derive key from device UID
 * @retval true if initialized successfully
 */
/*============================================================================*/
bool m1_crypto_init(void)
{
	if (crypto_initialized)
		return true;
	
	// Derive key from device UID
	m1_crypto_derive_key(crypto_key, M1_CRYPTO_KEY_SIZE);
	
	// TODO: Initialize STM32H5 CRYP peripheral if available
	// HAL_CRYP_Init(&hcryp);
	
	crypto_initialized = true;
	return true;
}



/*============================================================================*/
/**
 * @brief Derive encryption key from STM32H5 unique device ID
 * @param key: Output buffer for derived key
 * @param len: Key length (should be 32 for AES-256)
 */
/*============================================================================*/
void m1_crypto_derive_key(uint8_t* key, uint8_t len)
{
	// Get 96-bit unique device ID
	uint32_t uid[3];
	uid[0] = HAL_GetUIDw0();
	uid[1] = HAL_GetUIDw1();
	uid[2] = HAL_GetUIDw2();

	// Derive key bytes using splitmix64 expansion from UID seed.
	uint64_t seed = ((uint64_t)uid[0] << 32) ^ (uint64_t)uid[1] ^ ((uint64_t)uid[2] << 13) ^ 0xA5A5A5A5A5A5A5A5ULL;
	for (uint8_t i = 0; i < len; i++) {
		seed += 0x9E3779B97F4A7C15ULL;
		uint64_t z = seed;
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
		z = z ^ (z >> 31);
		key[i] = (uint8_t)(z & 0xFF);
	}
}



/*============================================================================*/
/**
 * @brief Generate random IV using hardware RNG peripheral
 * @param iv: Output buffer (16 bytes)
 * 
 * NOTE: This implementation uses a fallback until hardware RNG is enabled.
 * See GitHub Issue #16 for tracking the proper implementation.
 * 
 * The fallback combines:
 * - HAL_GetTick() for time-based entropy
 * - Device UID for per-device uniqueness
 * - A counter for additional variation
 *============================================================================*/
bool m1_crypto_generate_iv(uint8_t* iv)
{
    // Best-effort PRNG when hardware RNG is unavailable in this build.
    // Seed with runtime state + UID so IVs are unique and hard to predict
    // without device/runtime knowledge.
    static uint64_t s0 = 0;
    static uint64_t s1 = 0;
    static uint32_t init = 0;

    if (!init) {
        uint64_t uid_mix = ((uint64_t)HAL_GetUIDw0() << 32) ^ HAL_GetUIDw1() ^ ((uint64_t)HAL_GetUIDw2() << 13);
        uint64_t t = ((uint64_t)HAL_GetTick() << 32) ^ (uintptr_t)&init;
        s0 = uid_mix ^ 0x9E3779B97F4A7C15ULL;
        s1 = t ^ 0xBF58476D1CE4E5B9ULL;
        init = 1;
    }

    for (uint8_t i = 0; i < M1_CRYPTO_IV_SIZE; i += 8) {
        // xorshift128+
        uint64_t x = s0;
        uint64_t y = s1;
        s0 = y;
        x ^= x << 23;
        s1 = x ^ y ^ (x >> 17) ^ (y >> 26);
        uint64_t r = s1 + y;
        for (uint8_t j = 0; j < 8 && (i + j) < M1_CRYPTO_IV_SIZE; j++) {
            iv[i + j] = (uint8_t)(r >> (j * 8));
        }
    }

    return true;
}



/*============================================================================*/
/**
 * @brief Encrypt data using AES-256-CBC
 * @param input: Plaintext data
 * @param output: Output buffer (must be input_len + 16 for IV)
 * @param input_len: Length of input data
 * @param output_len: Output length written (input_len + 16)
 * @retval true on success
 */
/*============================================================================*/
bool m1_crypto_encrypt(const uint8_t* input, uint8_t* output, uint8_t input_len, uint8_t* output_len)
{
	if (!crypto_initialized) {
		m1_crypto_init();
	}
	
    uint8_t iv[M1_CRYPTO_IV_SIZE];
    if (!m1_crypto_generate_iv(iv)) {
        return false;
    }
	
	// Prepend IV to output
	memcpy(output, iv, M1_CRYPTO_IV_SIZE);
	
	// Encrypt using AES-256-CBC (software implementation)
	uint8_t state[M1_CRYPTO_BLOCK_SIZE];
	uint8_t expandedKey[240];  // 60 words * 4 bytes for AES-256
	
	KeyExpansion(crypto_key, expandedKey);
	
	uint8_t prev_cipher[M1_CRYPTO_BLOCK_SIZE];
	memcpy(prev_cipher, iv, M1_CRYPTO_BLOCK_SIZE);
	
	uint8_t padded_len = ((input_len + 15) / 16) * 16;  // PKCS7 padding
	
	for (uint8_t block = 0; block < padded_len / 16; block++) {
		// Prepare block with PKCS7 padding
		memset(state, 0, M1_CRYPTO_BLOCK_SIZE);
		uint8_t copy_len = (block * 16 < input_len) ? 
			((input_len - block * 16 < 16) ? (input_len - block * 16) : 16) : 0;
		memcpy(state, input + block * 16, copy_len);
		
		// PKCS7 padding
		if (copy_len < 16) {
			memset(state + copy_len, 16 - copy_len, 16 - copy_len);
		} else if (block == (padded_len / 16) - 1 && input_len % 16 == 0) {
			// Add full padding block
			memset(state, 16, 16);
		}
		
		// XOR with previous ciphertext (CBC mode)
		XorBlock(state, state, prev_cipher);
		
		// AES encrypt
		AddRoundKey(state, expandedKey);
		
		for (uint8_t round = 1; round <= 13; round++) {
			SubBytes(state);
			ShiftRows(state);
			MixColumns(state);
			AddRoundKey(state, expandedKey + round * 16);
		}
		
		SubBytes(state);
		ShiftRows(state);
		AddRoundKey(state, expandedKey + 14 * 16);
		
		// Save ciphertext for next block
		memcpy(prev_cipher, state, M1_CRYPTO_BLOCK_SIZE);
		memcpy(output + M1_CRYPTO_IV_SIZE + block * 16, state, M1_CRYPTO_BLOCK_SIZE);
	}
	
	*output_len = M1_CRYPTO_IV_SIZE + padded_len;
	return true;
}



/*============================================================================*/
/**
 * @brief Decrypt data using AES-256-CBC
 * @param input: Ciphertext with IV prefix
 * @param output: Output buffer (should be input_len - 16)
 * @param input_len: Length of input data
 * @param output_len: Output length written
 * @retval true on success
 */
/*============================================================================*/
bool m1_crypto_decrypt(const uint8_t* input, uint8_t* output, uint8_t input_len, uint8_t* output_len)
{
	if (!crypto_initialized) {
		m1_crypto_init();
	}
	
	if (input_len < M1_CRYPTO_IV_SIZE + 16) {
		return false;  // Too short
	}
	
	uint8_t iv[M1_CRYPTO_IV_SIZE];
	memcpy(iv, input, M1_CRYPTO_IV_SIZE);
	
	uint8_t cipher_len = input_len - M1_CRYPTO_IV_SIZE;
	
	// Decrypt using AES-256-CBC
	uint8_t state[M1_CRYPTO_BLOCK_SIZE];
	uint8_t expandedKey[240];
	
	KeyExpansion(crypto_key, expandedKey);
	
	uint8_t prev_cipher[M1_CRYPTO_BLOCK_SIZE];
	memcpy(prev_cipher, iv, M1_CRYPTO_BLOCK_SIZE);
	
	for (uint8_t block = 0; block < cipher_len / 16; block++) {
		memcpy(state, input + M1_CRYPTO_IV_SIZE + block * 16, M1_CRYPTO_BLOCK_SIZE);
		
		uint8_t curr_cipher[M1_CRYPTO_BLOCK_SIZE];
		memcpy(curr_cipher, state, M1_CRYPTO_BLOCK_SIZE);
		
		// AES decrypt (reverse operations)
		// Initial round - just AddRoundKey
		AddRoundKey(state, expandedKey + 14 * 16);
		
		// Main rounds (13 down to 1)
		for (int round = 13; round >= 1; round--) {
			InvShiftRows(state);
			InvSubBytes(state);
			AddRoundKey(state, expandedKey + round * 16);
			InvMixColumns(state);
		}
		
		// Final round (no InvMixColumns)
		InvShiftRows(state);
		InvSubBytes(state);
		AddRoundKey(state, expandedKey);
		
		// XOR with previous ciphertext (CBC mode)
		XorBlock(output + block * 16, state, prev_cipher);
		
		memcpy(prev_cipher, curr_cipher, M1_CRYPTO_BLOCK_SIZE);
	}
	
    // Remove PKCS7 padding in constant-time style.
    uint8_t pad_len = output[cipher_len - 1];
    uint8_t bad = (pad_len == 0) | (pad_len > 16);
    uint8_t check = 0;

    for (uint8_t i = 0; i < 16; i++) {
        uint8_t mask = (uint8_t)((i < pad_len) ? 0xFF : 0x00);
        uint8_t b = output[cipher_len - 1 - i];
        check |= (b ^ pad_len) & mask;
    }

    if (bad || check != 0) {
        return false;
    }

    *output_len = cipher_len - pad_len;
	
	return true;
}



/*============================================================================*/
/**
 * @brief XOR two 16-byte blocks
 */
/*============================================================================*/
static void XorBlock(uint8_t* out, const uint8_t* a, const uint8_t* b)
{
	for (uint8_t i = 0; i < 16; i++) {
		out[i] = a[i] ^ b[i];
	}
}



/*============================================================================*/
/**
 * @brief AES SubBytes transformation
 */
/*============================================================================*/
static void SubBytes(uint8_t* state)
{
	for (uint8_t i = 0; i < 16; i++) {
		state[i] = sbox[state[i]];
	}
}



/*============================================================================*/
/**
 * @brief AES ShiftRows transformation
 */
/*============================================================================*/
static void ShiftRows(uint8_t* state)
{
	uint8_t temp[16];
	
	// Row 0: no shift
	temp[0] = state[0];
	temp[4] = state[4];
	temp[8] = state[8];
	temp[12] = state[12];
	
	// Row 1: shift left by 1
	temp[1] = state[5];
	temp[5] = state[9];
	temp[9] = state[13];
	temp[13] = state[1];
	
	// Row 2: shift left by 2
	temp[2] = state[10];
	temp[6] = state[14];
	temp[10] = state[2];
	temp[14] = state[6];
	
	// Row 3: shift left by 3
	temp[3] = state[15];
	temp[7] = state[3];
	temp[11] = state[7];
	temp[15] = state[11];
	
	memcpy(state, temp, 16);
}



// Galois multiplication in GF(2^8)
static uint8_t gmul(uint8_t a, uint8_t b)
{
	uint8_t p = 0;
	while (b) {
		if (b & 1) p ^= a;
		uint8_t hi = a & 0x80;
		a <<= 1;
		if (hi) a ^= 0x1B;  // Reduce by AES polynomial
		b >>= 1;
	}
	return p;
}

/*============================================================================*/
/**
 * @brief AES MixColumns transformation
 */
/*============================================================================*/
static void MixColumns(uint8_t* state)
{
	for (uint8_t c = 0; c < 4; c++) {
		uint8_t a0 = state[c * 4 + 0];
		uint8_t a1 = state[c * 4 + 1];
		uint8_t a2 = state[c * 4 + 2];
		uint8_t a3 = state[c * 4 + 3];
		
		state[c * 4 + 0] = gmul(a0, 2) ^ gmul(a1, 3) ^ a2 ^ a3;
		state[c * 4 + 1] = a0 ^ gmul(a1, 2) ^ gmul(a2, 3) ^ a3;
		state[c * 4 + 2] = a0 ^ a1 ^ gmul(a2, 2) ^ gmul(a3, 3);
		state[c * 4 + 3] = gmul(a0, 3) ^ a1 ^ a2 ^ gmul(a3, 2);
	}
}

// Inverse S-box for decryption
static const uint8_t inv_sbox[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
};

/*============================================================================*/
/**
 * @brief AES InvSubBytes transformation (inverse of SubBytes)
 */
/*============================================================================*/
static void InvSubBytes(uint8_t* state)
{
	for (uint8_t i = 0; i < 16; i++) {
		state[i] = inv_sbox[state[i]];
	}
}

/*============================================================================*/
/**
 * @brief AES InvShiftRows transformation (inverse of ShiftRows)
 */
/*============================================================================*/
static void InvShiftRows(uint8_t* state)
{
	uint8_t temp[16];
	
	// Row 0: no shift
	temp[0] = state[0];
	temp[4] = state[4];
	temp[8] = state[8];
	temp[12] = state[12];
	
	// Row 1: shift right by 1 (inverse of left by 1)
	temp[1] = state[13];
	temp[5] = state[1];
	temp[9] = state[5];
	temp[13] = state[9];
	
	// Row 2: shift right by 2 (inverse of left by 2)
	temp[2] = state[10];
	temp[6] = state[14];
	temp[10] = state[2];
	temp[14] = state[6];
	
	// Row 3: shift right by 3 (inverse of left by 3)
	temp[3] = state[7];
	temp[7] = state[11];
	temp[11] = state[15];
	temp[15] = state[3];
	
	memcpy(state, temp, 16);
}

/*============================================================================*/
/**
 * @brief AES InvMixColumns transformation (inverse of MixColumns)
 */
/*============================================================================*/
static void InvMixColumns(uint8_t* state)
{
	for (uint8_t c = 0; c < 4; c++) {
		uint8_t a0 = state[c * 4 + 0];
		uint8_t a1 = state[c * 4 + 1];
		uint8_t a2 = state[c * 4 + 2];
		uint8_t a3 = state[c * 4 + 3];
		
		state[c * 4 + 0] = gmul(a0, 0x0e) ^ gmul(a1, 0x0b) ^ gmul(a2, 0x0d) ^ gmul(a3, 0x09);
		state[c * 4 + 1] = gmul(a0, 0x09) ^ gmul(a1, 0x0e) ^ gmul(a2, 0x0b) ^ gmul(a3, 0x0d);
		state[c * 4 + 2] = gmul(a0, 0x0d) ^ gmul(a1, 0x09) ^ gmul(a2, 0x0e) ^ gmul(a3, 0x0b);
		state[c * 4 + 3] = gmul(a0, 0x0b) ^ gmul(a1, 0x0d) ^ gmul(a2, 0x09) ^ gmul(a3, 0x0e);
	}
}

/*============================================================================*/
/**
 * @brief AES AddRoundKey transformation
 */
/*============================================================================*/
static void AddRoundKey(uint8_t* state, const uint8_t* roundKey)
{
	for (uint8_t i = 0; i < 16; i++) {
		state[i] ^= roundKey[i];
	}
}



/*============================================================================*/
/**
 * @brief AES-256 key expansion
 */
/*============================================================================*/
static void KeyExpansion(const uint8_t* key, uint8_t* expandedKey)
{
	// Copy original key
	memcpy(expandedKey, key, 32);
	
	// Generate remaining round keys
	uint8_t temp[4];
	uint8_t i = 32;
	uint8_t rcon_idx = 1;
	
	while (i < 240) {
		// Copy last 4 bytes
		memcpy(temp, expandedKey + i - 4, 4);
		
		// Every 32 bytes (8 words) apply transformation
		if (i % 32 == 0) {
			// RotWord
			uint8_t t = temp[0];
			temp[0] = temp[1];
			temp[1] = temp[2];
			temp[2] = temp[3];
			temp[3] = t;
			
			// SubWord
			for (uint8_t j = 0; j < 4; j++) {
				temp[j] = sbox[temp[j]];
			}
			
			// XOR with Rcon
			temp[0] ^= Rcon[rcon_idx++];
		}
		
		// XOR with word 32 bytes (8 words) back
		for (uint8_t j = 0; j < 4; j++) {
			expandedKey[i] = expandedKey[i - 32] ^ temp[j];
			i++;
		}
	}
}
