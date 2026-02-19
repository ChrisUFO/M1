/* See COPYING.txt for license details. */

/*
*
* m1_crypto.h
*
* AES-256-CBC cryptography helpers
*
* M1 Project
*
*/

#ifndef M1_CRYPTO_H_
#define M1_CRYPTO_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ######################################### Definitions ############################################ */

#define M1_CRYPTO_KEY_SIZE      32  // AES-256 key size
#define M1_CRYPTO_IV_SIZE       16  // AES block size
#define M1_CRYPTO_BLOCK_SIZE    16  // AES block size

/* ##################################### Function Prototypes ###################################### */

// Initialize hardware crypto peripheral
bool m1_crypto_init(void);

// Derive encryption key from device UID
void m1_crypto_derive_key(uint8_t* key, uint8_t len);

// Encrypt data (adds IV prefix to output)
// Output buffer must be input_len + 16 (for IV)
bool m1_crypto_encrypt(const uint8_t* input, uint8_t* output, uint8_t input_len, uint8_t* output_len);

// Decrypt data (expects IV prefix in input)
// Output buffer should be input_len - 16
bool m1_crypto_decrypt(const uint8_t* input, uint8_t* output, uint8_t input_len, uint8_t* output_len);

// Generate cryptographically secure random IV
bool m1_crypto_generate_iv(uint8_t* iv);

#endif /* M1_CRYPTO_H_ */
