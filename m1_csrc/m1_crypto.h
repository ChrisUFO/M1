/* See COPYING.txt for license details. */

/*
*
* m1_crypto.h
*
* Software AES-256 / CMAC / KDF primitives for WiFi credential
* protection (issue #42, PR-8: crypto + HW RNG foundation).
*
* Pure core: no HAL, no FreeRTOS, no dynamic allocation, no file I/O --
* compiles unchanged in the firmware and in the host test harness. All
* hardware coupling (the HW RNG that produces IVs) lives in m1_rng.c;
* callers compose the two (generate IV via m1_rng_bytes(), then
* encrypt/MAC via this module). PR-9 wires that composition into the
 * saved-networks CSV; this unit ships only the primitive layer.
*
* Conformance
* -----------
* - AES block cipher: FIPS-197 (Rijndael, 128-bit block, 256-bit keys;
*   the 128-bit key path exists only for CMAC conformance testing).
* - AES-256-CBC: NIST SP 800-38A (F.2.5/F.2.6 known-answer vectors in
*   the host suite).
* - AES-CMAC: RFC 4493 / NIST SP 800-38B (all four RFC 4493 vectors
*   plus NIST CAVP CMACGenAES256 cases in the host suite).
* - Key derivation: NIST SP 800-108 KDF in CMAC mode (see
*   m1_crypto_derive_wifi_key below).
*
* Threat model -- READ THIS BEFORE RELYING ON THESE PRIMITIVES
* ------------------------------------------------------------
* The derived key hides credentials from REMOVED-SD-CARD casual reading
* ONLY (someone picks up the SD card and reads the file). It is NOT
* secrecy against a physical attacker: the 96-bit device UID is
* readable by any SWD debugger and by arbitrary code execution on the
* device, so the key is derivable by anyone with device access. No
* secure element, no TLS, no SPI-link encryption, no side-channel
* hardening (the soft AES is a straight-table implementation; it makes
* no claim of constant-time key handling beyond the tag compare).
* docs/security.md carries the full statement.
*
* Buffer-size contracts
* ---------------------
* - m1_aes256_encrypt_block / m1_aes256_decrypt_block:
*     key points at exactly M1_AES256_KEY_LEN (32) readable bytes;
*     in  points at exactly M1_AES_BLOCK_LEN  (16) readable bytes;
*     out receives exactly M1_AES_BLOCK_LEN   (16) bytes.
*     in and out may alias (in-place) or be disjoint; no partial
*     overlap.
* - m1_aes256_cbc_encrypt / m1_aes256_cbc_decrypt:
*     key: 32 bytes; iv: iv_len bytes (must equal M1_AES_BLOCK_LEN);
*     in: len bytes; out receives len bytes; len must be a non-zero
*     multiple of M1_AES_BLOCK_LEN (CBC here makes NO padding
*     assumption -- the caller pads or frames; PR-9 stores only whole
*     blocks). in and out may be the same buffer (in-place, either
*     direction) or disjoint; no partial overlap.
* - m1_aes_cmac:
*     key: key_len bytes (16 or 32); msg: msg_len bytes (msg may be
*     NULL only when msg_len == 0); tag receives exactly
*     M1_AES_CMAC_TAG_LEN (16) bytes.
* - m1_crypto_derive_wifi_key:
*     uid points at exactly M1_UID_LEN (12) bytes; out_key receives
*     exactly M1_AES256_KEY_LEN (32) bytes.
*
* All entry points return bool and validate their arguments (NULL,
* lengths, alignment rules above); on false the output buffer contents
* are unspecified-but-zeroized where a buffer was already written.
*
* M1 Project
*
*/

#ifndef M1_CRYPTO_H_
#define M1_CRYPTO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*--- Sizes (single source of truth for every caller) -----------------------*/

#define M1_AES256_KEY_LEN   32u  /* AES-256 key bytes                        */
#define M1_AES128_KEY_LEN   16u  /* CMAC conformance path only (RFC 4493)    */
#define M1_AES_BLOCK_LEN    16u  /* AES block bytes (always 16, FIPS-197)    */
#define M1_AES_CMAC_TAG_LEN 16u  /* full RFC 4493 / SP 800-38B tag           */
#define M1_UID_LEN          12u  /* STM32H5 96-bit unique device ID bytes   */

/*--- AES-256 block primitives (FIPS-197) ------------------------------------*/

/* Encrypt/decrypt one 16-byte block with a 256-bit key. */
bool m1_aes256_encrypt_block(const uint8_t *key,
                             const uint8_t *in, uint8_t *out);
bool m1_aes256_decrypt_block(const uint8_t *key,
                             const uint8_t *in, uint8_t *out);

/*--- AES-256-CBC (NIST SP 800-38A, no padding) -------------------------------*/

/* CBC encrypt/decrypt of len bytes (multiple of 16) with explicit IV.
 * No padding is applied or assumed; the IV length must be exactly one
 * block. Decryption of a tampered ciphertext yields garbage plaintext
 * by design -- callers authenticate with CMAC (see PR-9's field
 * layout: MAC covers iv||ct). */
bool m1_aes256_cbc_encrypt(const uint8_t *key, size_t key_len,
                           const uint8_t *iv, size_t iv_len,
                           const uint8_t *in, size_t len, uint8_t *out);
bool m1_aes256_cbc_decrypt(const uint8_t *key, size_t key_len,
                           const uint8_t *iv, size_t iv_len,
                           const uint8_t *in, size_t len, uint8_t *out);

/*--- AES-CMAC (RFC 4493 / SP 800-38B) ----------------------------------------*/

/* Compute the 16-byte CMAC tag of msg. key_len accepts 16 (RFC 4493
 * conformance vectors) or 32 (the firmware's AES-256 use); the
 * algorithm is identical, only the block-cipher key schedule differs.
 * key_len 16 exists for test-vector conformance; production callers
 * pass 32. */
bool m1_aes_cmac(const uint8_t *key, size_t key_len,
                 const uint8_t *msg, size_t msg_len, uint8_t *tag);

/* Constant-time equality (no early exit on the first differing byte).
 * For CMAC tag verification only; a == b == NULL with len 0 is true. */
bool m1_ct_equal(const uint8_t *a, const uint8_t *b, size_t len);

/*--- UID-based key derivation (SP 800-108 KDF in CMAC mode) -----------------*/

/* Derive the 256-bit WiFi-credential encryption key from the 96-bit
 * STM32H5 UID (read via UID_BASE on target; passed in on host).
 *
 * Construction (documented, auditable, single-purpose):
 *   PRK = UID || 0^20                    (96-bit UID zero-padded to a
 *                                         256-bit CMAC key -- the UID is
 *                                         the ONLY input entropy)
 *   K1   = AES-CMAC(PRK, 0x01 || "m1-wifi-cred-v1" || 0x00 || 0x00000100)
 *   K2   = AES-CMAC(PRK, 0x02 || "m1-wifi-cred-v1" || 0x00 || 0x00000100)
 *   key  = K1 || K2                       (256-bit output)
 * i.e. NIST SP 800-108 KDF in CMAC mode, counter width r=8, L=0x100
 * (256), empty context, and the label "m1-wifi-cred-v1" as domain
 * separation so this key can never be confused with material derived
 * for any other purpose on any other device line.
 *
 * HONESTY NOTE: this is key HIDING, not secrecy against a physical
 * attacker -- the UID is readable via SWD or code execution, so an
 * attacker with the device (or its SD card AND the device) recovers
 * the key. See the threat-model block at the top of this header and
 * docs/security.md. */
bool m1_crypto_derive_wifi_key(const uint8_t *uid /* M1_UID_LEN bytes */,
                               uint8_t *out_key   /* M1_AES256_KEY_LEN bytes */);

/*--- Zeroization -------------------------------------------------------------*/

/* Overwrite len bytes at p with zeroes, with volatile stores plus a
 * compiler read/write memory barrier so the writes cannot be elided as
 * dead stores "as far as host-testable" (see the host suite's
 * zeroization test for what is and is not provable on a host).
 * Contract: p NULL or len 0 is a tolerated no-op; otherwise p must
 * address len writable bytes. Use on every plaintext PSK buffer after
 * encrypt/decrypt/send (applied at the PR-9/PR-11 call sites). */
void m1_secure_zero(void *p, size_t len);

#endif /* M1_CRYPTO_H_ */
