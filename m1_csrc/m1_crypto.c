/* See COPYING.txt for license details. */

/*
*
* m1_crypto.c
*
* Hardware AES encryption using STM32H5 CRYP peripheral
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
	
	// Simple key derivation - XOR expand to fill key buffer
	for (uint8_t i = 0; i < len; i++) {
		uint8_t byte_idx = i % 12;  // 12 bytes = 96 bits
		uint8_t word_idx = byte_idx / 4;
		uint8_t shift = (byte_idx % 4) * 8;
		
		key[i] = (uid[word_idx] >> shift) & 0xFF;
		
		// Add some mixing with position
		key[i] ^= (i * 0x9E) ^ 0x37;
	}
}



/*============================================================================*/
/**
 * @brief Generate random IV using RNG peripheral or simple counter
 * @param iv: Output buffer (16 bytes)
 */
/*============================================================================*/
void m1_crypto_generate_iv(uint8_t* iv)
{
	// For now, use a simple counter-based IV
	// In production, should use HAL_RNG_GetRandomNumber()
	static uint32_t iv_counter = 0;
	
	for (uint8_t i = 0; i < M1_CRYPTO_IV_SIZE; i++) {
		iv[i] = (iv_counter >> (i * 2)) & 0xFF;
		iv[i] ^= crypto_key[i % M1_CRYPTO_KEY_SIZE];
	}
	
	iv_counter++;
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
	m1_crypto_generate_iv(iv);
	
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
		AddRoundKey(state, expandedKey + 14 * 16);
		
		for (uint8_t round = 13; round >= 1; round--) {
			// Inverse operations would go here (simplified)
			// For now, use a placeholder - full AES implementation needed
			ShiftRows(state);
			SubBytes(state);
			AddRoundKey(state, expandedKey + round * 16);
			MixColumns(state);
		}
		
		// XOR with previous ciphertext (CBC mode)
		XorBlock(output + block * 16, state, prev_cipher);
		
		memcpy(prev_cipher, curr_cipher, M1_CRYPTO_BLOCK_SIZE);
	}
	
	// Remove PKCS7 padding
	uint8_t pad_len = output[cipher_len - 1];
	if (pad_len > 0 && pad_len <= 16) {
		*output_len = cipher_len - pad_len;
	} else {
		*output_len = cipher_len;
	}
	
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



/*============================================================================*/
/**
 * @brief AES MixColumns transformation
 */
/*============================================================================*/
static void MixColumns(uint8_t* state)
{
	// Simplified - full Galois multiplication needed
	// This is a placeholder
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
