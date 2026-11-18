/* See COPYING.txt for license details. */

/*
*
* m1_crypto.c
*
* Software AES-256 / AES-CMAC / UID-KDF primitives. See m1_crypto.h
* for the contracts, conformance references and the threat model.
*
* Implementation notes
* --------------------
* - Straight-table byte-oriented AES (FIPS-197): small, auditable, no
*  dynamic allocation. Speed is irrelevant here (credential records
*  are tens of bytes). NOT side-channel hardened beyond the branchless
*  GF(2^8) multiply and the constant-time tag compare -- documented
*  threat-model limit.
* - One key-expansion routine serves Nk=4 (AES-128, CMAC conformance
*  vectors) and Nk=8 (AES-256, everything production).
* - Zeroization on every error path that had already touched a key
*  schedule or partial output.
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "m1_crypto.h"

/**************************** D E F I N E S ***********************************/

#define AES_NR_128	10u /* rounds, Nk=4 */
#define AES_NR_256	14u /* rounds, Nk=8 */
#define AES_WORDS_128	44u /* 4*(AES_NR_128+1) */
#define AES_WORDS_256	60u /* 4*(AES_NR_256+1) */

/* CMAC subkey doubling constant (RFC 4493 §2.3, GF(2^8) poly Rb). */
#define CMAC_RB		0x87u

/*--- UID KDF domain separation (SP 800-108 label; see header) ---------------*/

static const uint8_t M1_KDF_LABEL[] = "m1-wifi-cred-v1";

/***************************** V A R I A B L E S ******************************/

/*--- AES S-box (FIPS-197 §5.1.1, Fig.7) ---*/
static const uint8_t SBOX[256] = {
	0x63u,0x7cu,0x77u,0x7bu,0xf2u,0x6bu,0x6fu,0xc5u,
	0x30u,0x01u,0x67u,0x2bu,0xfeu,0xd7u,0xabu,0x76u,
	0xcau,0x82u,0xc9u,0x7du,0xfau,0x59u,0x47u,0xf0u,
	0xadu,0xd4u,0xa2u,0xafu,0x9cu,0xa4u,0x72u,0xc0u,
	0xb7u,0xfdu,0x93u,0x26u,0x36u,0x3fu,0xf7u,0xccu,
	0x34u,0xa5u,0xe5u,0xf1u,0x71u,0xd8u,0x31u,0x15u,
	0x04u,0xc7u,0x23u,0xc3u,0x18u,0x96u,0x05u,0x9au,
	0x07u,0x12u,0x80u,0xe2u,0xebu,0x27u,0xb2u,0x75u,
	0x09u,0x83u,0x2cu,0x1au,0x1bu,0x6eu,0x5au,0xa0u,
	0x52u,0x3bu,0xd6u,0xb3u,0x29u,0xe3u,0x2fu,0x84u,
	0x53u,0xd1u,0x00u,0xedu,0x20u,0xfcu,0xb1u,0x5bu,
	0x6au,0xcbu,0xbeu,0x39u,0x4au,0x4cu,0x58u,0xcfu,
	0xd0u,0xefu,0xaau,0xfbu,0x43u,0x4du,0x33u,0x85u,
	0x45u,0xf9u,0x02u,0x7fu,0x50u,0x3cu,0x9fu,0xa8u,
	0x51u,0xa3u,0x40u,0x8fu,0x92u,0x9du,0x38u,0xf5u,
	0xbcu,0xb6u,0xdau,0x21u,0x10u,0xffu,0xf3u,0xd2u,
	0xcdu,0x0cu,0x13u,0xecu,0x5fu,0x97u,0x44u,0x17u,
	0xc4u,0xa7u,0x7eu,0x3du,0x64u,0x5du,0x19u,0x73u,
	0x60u,0x81u,0x4fu,0xdcu,0x22u,0x2au,0x90u,0x88u,
	0x46u,0xeeu,0xb8u,0x14u,0xdeu,0x5eu,0x0bu,0xdbu,
	0xe0u,0x32u,0x3au,0x0au,0x49u,0x06u,0x24u,0x5cu,
	0xc2u,0xd3u,0xacu,0x62u,0x91u,0x95u,0xe4u,0x79u,
	0xe7u,0xc8u,0x37u,0x6du,0x8du,0xd5u,0x4eu,0xa9u,
	0x6cu,0x56u,0xf4u,0xeau,0x65u,0x7au,0xaeu,0x08u,
	0xbau,0x78u,0x25u,0x2eu,0x1cu,0xa6u,0xb4u,0xc6u,
	0xe8u,0xddu,0x74u,0x1fu,0x4bu,0xbdu,0x8bu,0x8au,
	0x70u,0x3eu,0xb5u,0x66u,0x48u,0x03u,0xf6u,0x0eu,
	0x61u,0x35u,0x57u,0xb9u,0x86u,0xc1u,0x1du,0x9eu,
	0xe1u,0xf8u,0x98u,0x11u,0x69u,0xd9u,0x8eu,0x94u,
	0x9bu,0x1eu,0x87u,0xe9u,0xceu,0x55u,0x28u,0xdfu,
	0x8cu,0xa1u,0x89u,0x0du,0xbfu,0xe6u,0x42u,0x68u,
	0x41u,0x99u,0x2du,0x0fu,0xb0u,0x54u,0xbbu,0x16u
};

/*--- AES inverse S-box (FIPS-197 §5.3.2, Fig.14) ---*/
static const uint8_t INV_SBOX[256] = {
	0x52u,0x09u,0x6au,0xd5u,0x30u,0x36u,0xa5u,0x38u,
	0xbfu,0x40u,0xa3u,0x9eu,0x81u,0xf3u,0xd7u,0xfbu,
	0x7cu,0xe3u,0x39u,0x82u,0x9bu,0x2fu,0xffu,0x87u,
	0x34u,0x8eu,0x43u,0x44u,0xc4u,0xdeu,0xe9u,0xcbu,
	0x54u,0x7bu,0x94u,0x32u,0xa6u,0xc2u,0x23u,0x3du,
	0xeeu,0x4cu,0x95u,0x0bu,0x42u,0xfau,0xc3u,0x4eu,
	0x08u,0x2eu,0xa1u,0x66u,0x28u,0xd9u,0x24u,0xb2u,
	0x76u,0x5bu,0xa2u,0x49u,0x6du,0x8bu,0xd1u,0x25u,
	0x72u,0xf8u,0xf6u,0x64u,0x86u,0x68u,0x98u,0x16u,
	0xd4u,0xa4u,0x5cu,0xccu,0x5du,0x65u,0xb6u,0x92u,
	0x6cu,0x70u,0x48u,0x50u,0xfdu,0xedu,0xb9u,0xdau,
	0x5eu,0x15u,0x46u,0x57u,0xa7u,0x8du,0x9du,0x84u,
	0x90u,0xd8u,0xabu,0x00u,0x8cu,0xbcu,0xd3u,0x0au,
	0xf7u,0xe4u,0x58u,0x05u,0xb8u,0xb3u,0x45u,0x06u,
	0xd0u,0x2cu,0x1eu,0x8fu,0xcau,0x3fu,0x0fu,0x02u,
	0xc1u,0xafu,0xbdu,0x03u,0x01u,0x13u,0x8au,0x6bu,
	0x3au,0x91u,0x11u,0x41u,0x4fu,0x67u,0xdcu,0xeau,
	0x97u,0xf2u,0xcfu,0xceu,0xf0u,0xb4u,0xe6u,0x73u,
	0x96u,0xacu,0x74u,0x22u,0xe7u,0xadu,0x35u,0x85u,
	0xe2u,0xf9u,0x37u,0xe8u,0x1cu,0x75u,0xdfu,0x6eu,
	0x47u,0xf1u,0x1au,0x71u,0x1du,0x29u,0xc5u,0x89u,
	0x6fu,0xb7u,0x62u,0x0eu,0xaau,0x18u,0xbeu,0x1bu,
	0xfcu,0x56u,0x3eu,0x4bu,0xc6u,0xd2u,0x79u,0x20u,
	0x9au,0xdbu,0xc0u,0xfeu,0x78u,0xcdu,0x5au,0xf4u,
	0x1fu,0xddu,0xa8u,0x33u,0x88u,0x07u,0xc7u,0x31u,
	0xb1u,0x12u,0x10u,0x59u,0x27u,0x80u,0xecu,0x5fu,
	0x60u,0x51u,0x7fu,0xa9u,0x19u,0xb5u,0x4au,0x0du,
	0x2du,0xe5u,0x7au,0x9fu,0x93u,0xc9u,0x9cu,0xefu,
	0xa0u,0xe0u,0x3bu,0x4du,0xaeu,0x2au,0xf5u,0xb0u,
	0xc8u,0xebu,0xbbu,0x3cu,0x83u,0x53u,0x99u,0x61u,
	0x17u,0x2bu,0x04u,0x7eu,0xbau,0x77u,0xd6u,0x26u,
	0xe1u,0x69u,0x14u,0x63u,0x55u,0x21u,0x0cu,0x7du
};

/*--- Key-expansion round constants x^(i-1) in GF(2^8) (FIPS-197 Fig.11) ---*/
static const uint8_t RCON[10] = {
	0x01u,0x02u,0x04u,0x08u,0x10u,0x20u,0x40u,0x80u,0x1bu,0x36u
};

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static bool aes_expand_key(const uint8_t *key, size_t key_len,
                           uint32_t *w, size_t w_words);
static void aes_encrypt_block_w(const uint32_t *w, unsigned nr,
                                const uint8_t *in, uint8_t *out);
static void aes_decrypt_block_w(const uint32_t *w, unsigned nr,
                                const uint8_t *in, uint8_t *out);

/*************** F U N C T I O N   I M P L E M A T I O N ****************/

/*============================================================================*/
/**
  * @brief Overwrite memory with zeroes; volatile stores + barrier.
  *
  * Volatile stores keep the writes observable; the compiler memory
  * clobber (__asm__ __volatile__("" ::: "memory") on GCC/Clang, which
  * covers both the ARM target and every host compiler this repo's
  * harnesses use) prevents reordering/elision across the call. What it
  * cannot prevent (documented limit, host-tested as far as possible):
  * a caller that never reads the buffer again and whose buffer is
  * provably dead -- see the zeroization test discussion in
  * m1_csrc/test/m1_crypto_test.py.
  */
/*============================================================================*/
void m1_secure_zero(void *p, size_t len)
{
	volatile uint8_t *v;

	if ((p == NULL) || (len == 0u))
		return;

	v = (volatile uint8_t *)p;
	while (len-- != 0u)
		*v++ = 0u;

#if defined(__GNUC__) || defined(__clang__)
	__asm__ __volatile__("" ::: "memory");
#endif
} /* void m1_secure_zero(void *p, size_t len) */

/*============================================================================*/
/**
  * @brief Constant-time byte-string equality (tag verification).
  */
/*============================================================================*/
bool m1_ct_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
	uint8_t d = 0u;
	size_t  i;

	if ((a == NULL) || (b == NULL))
		return (len == 0u) && (a == b);

	for (i = 0u; i < len; i++)
		d |= (uint8_t)(a[i] ^ b[i]);

	return (d == 0u);
} /* bool m1_ct_equal(const uint8_t *a, const uint8_t *b, size_t len) */

/*============================================================================*/
/**
  * @brief Branchless multiply in GF(2^8) (AES poly 0x11B).
  *
  * No data-dependent branch or memory access: the conditional XORs of
  * the reduction polynomial become masks. b is always a small public
  * constant at every call site (2,3 on encrypt; 9,11,13,14 on
  * decrypt), so the loop is also compile-time friendly.
  */
/*============================================================================*/
static uint8_t gmul(uint8_t a, uint8_t b)
{
	uint8_t p = 0u;
	unsigned i;

	for (i = 0u; i < 8u; i++)
	{
		p	^= (uint8_t)(a & (uint8_t)(0u - (b & 1u)));
		a	= (uint8_t)((uint8_t)(a << 1u) ^
		                 (uint8_t)((uint8_t)(a >> 7u) * 0x1bu));
		b >>= 1u;
	}
	return p;
} /* static uint8_t gmul(uint8_t a, uint8_t b) */

/*============================================================================*/
/**
  * @brief AES key expansion (FIPS-197 §5.2), Nk=4 or Nk=8.
  *
  * w receives 4*(Nr+1) words, big-endian byte order per column: word j
  * of round key r is w[4r+j], whose most significant byte is the first
  * key byte of that column (FIPS-197 input order), so a round key is
  * the 16 bytes of its 4 words MSB-first.
  */
/*============================================================================*/
static bool aes_expand_key(const uint8_t *key, size_t key_len,
                           uint32_t *w, size_t w_words)
{
	unsigned nk;
	unsigned nr;
	unsigned i;
	uint32_t temp;

	if (key == NULL)
		return false;

	if (key_len == M1_AES128_KEY_LEN)
	{
		nk = 4u;
		nr = AES_NR_128;
	}
	else if (key_len == M1_AES256_KEY_LEN)
	{
		nk = 8u;
		nr = AES_NR_256;
	}
	else
		return false;

	if (w_words < (size_t)(4u * (nr + 1u)))
		return false;

	for (i = 0u; i < nk; i++)
		w[i] = ((uint32_t)key[4u * i] << 24) |
		       ((uint32_t)key[4u * i + 1u] << 16) |
		       ((uint32_t)key[4u * i + 2u] << 8) |
		       (uint32_t)key[4u * i + 3u];

	for (i = nk; i < (4u * (nr + 1u)); i++)
	{
		temp = w[i - 1u];

		if ((i % nk) == 0u)
		{
			/* RotWord then SubWord then Rcon */
			uint32_t rot = ((temp << 8) & 0xFFFFFF00u) | (temp >> 24);
			temp = ((uint32_t)SBOX[(rot >> 24) & 0xFFu] << 24) |
			       ((uint32_t)SBOX[(rot >> 16) & 0xFFu] << 16) |
			       ((uint32_t)SBOX[(rot >> 8) & 0xFFu] << 8) |
			       (uint32_t)SBOX[rot & 0xFFu];
			temp ^= (uint32_t)RCON[(i / nk) - 1u] << 24;
		}
		else if ((nk > 6u) && ((i % nk) == 4u))
		{
			/* Extra SubWord for Nk=8 (AES-256) */
			temp = ((uint32_t)SBOX[(temp >> 24) & 0xFFu] << 24) |
			       ((uint32_t)SBOX[(temp >> 16) & 0xFFu] << 16) |
			       ((uint32_t)SBOX[(temp >> 8) & 0xFFu] << 8) |
			       (uint32_t)SBOX[temp & 0xFFu];
		}

		w[i] = w[i - nk] ^ temp;
	}

	return true;
} /* static bool aes_expand_key(...) */

/*============================================================================*/
/**
  * @brief AES round key application: XOR the 16 bytes of round key rk.
  */
/*============================================================================*/
static void add_round_key(uint8_t st[M1_AES_BLOCK_LEN], const uint32_t *rk)
{
	unsigned i;

	for (i = 0u; i < 4u; i++)
	{
		uint32_t k = rk[i];
		st[4u * i + 0u] ^= (uint8_t)(k >> 24);
		st[4u * i + 1u] ^= (uint8_t)(k >> 16);
		st[4u * i + 2u] ^= (uint8_t)(k >> 8);
		st[4u * i + 3u] ^= (uint8_t)k;
	}
} /* static void add_round_key(...) */

/*============================================================================*/
/**
  * @brief AES encrypt one block (FIPS-197 §5.1 cipher).
  *
  * State layout: st[4c + r] is row r, column c -- the FIPS-197 mapping
  * of the input bytes in order onto s[r][c] = in[r + 4c].
  */
/*============================================================================*/
static void aes_encrypt_block_w(const uint32_t *w, unsigned nr,
                                const uint8_t *in, uint8_t *out)
{
	uint8_t  st[M1_AES_BLOCK_LEN];
	unsigned r, c, round, i;

	for (i = 0u; i < M1_AES_BLOCK_LEN; i++)
		st[i] = in[i];

	add_round_key(st, &w[0]);

	for (round = 1u; round < nr; round++)
	{
		/* SubBytes */
		for (i = 0u; i < M1_AES_BLOCK_LEN; i++)
			st[i] = SBOX[st[i]];

		/* ShiftRows: row r rotated left by r columns */
		for (r = 1u; r < 4u; r++)
		{
			uint8_t row[4];
			for (c = 0u; c < 4u; c++)
				row[c] = st[4u * ((c + r) % 4u) + r];
			for (c = 0u; c < 4u; c++)
				st[4u * c + r] = row[c];
		}

		/* MixColumns */
		for (c = 0u; c < 4u; c++)
		{
			uint8_t a0 = st[4u * c + 0u];
			uint8_t a1 = st[4u * c + 1u];
			uint8_t a2 = st[4u * c + 2u];
			uint8_t a3 = st[4u * c + 3u];
			st[4u * c + 0u] = (uint8_t)(gmul(a0, 2u) ^ gmul(a1, 3u) ^ a2 ^ a3);
			st[4u * c + 1u] = (uint8_t)(a0 ^ gmul(a1, 2u) ^ gmul(a2, 3u) ^ a3);
			st[4u * c + 2u] = (uint8_t)(a0 ^ a1 ^ gmul(a2, 2u) ^ gmul(a3, 3u));
			st[4u * c + 3u] = (uint8_t)(gmul(a0, 3u) ^ a1 ^ a2 ^ gmul(a3, 2u));
		}

		add_round_key(st, &w[4u * round]);
	}

	/* Final round: no MixColumns */
	for (i = 0u; i < M1_AES_BLOCK_LEN; i++)
		st[i] = SBOX[st[i]];
	for (r = 1u; r < 4u; r++)
	{
		uint8_t row[4];
		for (c = 0u; c < 4u; c++)
			row[c] = st[4u * ((c + r) % 4u) + r];
		for (c = 0u; c < 4u; c++)
			st[4u * c + r] = row[c];
	}
	add_round_key(st, &w[4u * nr]);

	for (i = 0u; i < M1_AES_BLOCK_LEN; i++)
		out[i] = st[i];

	m1_secure_zero(st, sizeof(st));
} /* static void aes_encrypt_block_w(...) */

/*============================================================================*/
/**
  * @brief AES decrypt one block (FIPS-197 §5.3 straight inverse cipher).
  */
/*============================================================================*/
static void aes_decrypt_block_w(const uint32_t *w, unsigned nr,
                                const uint8_t *in, uint8_t *out)
{
	uint8_t  st[M1_AES_BLOCK_LEN];
	unsigned r, c, round, i;

	for (i = 0u; i < M1_AES_BLOCK_LEN; i++)
		st[i] = in[i];

	add_round_key(st, &w[4u * nr]);

	for (round = nr; round > 1u; round--)
	{
		/* InvShiftRows: row r rotated right by r columns */
		for (r = 1u; r < 4u; r++)
		{
			uint8_t row[4];
			for (c = 0u; c < 4u; c++)
				row[c] = st[4u * ((c + (4u - r)) % 4u) + r];
			for (c = 0u; c < 4u; c++)
				st[4u * c + r] = row[c];
		}

		/* InvSubBytes */
		for (i = 0u; i < M1_AES_BLOCK_LEN; i++)
			st[i] = INV_SBOX[st[i]];

		add_round_key(st, &w[4u * (round - 1u)]);

		/* InvMixColumns */
		for (c = 0u; c < 4u; c++)
		{
			uint8_t a0 = st[4u * c + 0u];
			uint8_t a1 = st[4u * c + 1u];
			uint8_t a2 = st[4u * c + 2u];
			uint8_t a3 = st[4u * c + 3u];
			st[4u * c + 0u] = (uint8_t)(gmul(a0, 14u) ^ gmul(a1, 11u) ^
			                            gmul(a2, 13u) ^ gmul(a3, 9u));
			st[4u * c + 1u] = (uint8_t)(gmul(a0, 9u) ^ gmul(a1, 14u) ^
			                            gmul(a2, 11u) ^ gmul(a3, 13u));
			st[4u * c + 2u] = (uint8_t)(gmul(a0, 13u) ^ gmul(a1, 9u) ^
			                            gmul(a2, 14u) ^ gmul(a3, 11u));
			st[4u * c + 3u] = (uint8_t)(gmul(a0, 11u) ^ gmul(a1, 13u) ^
			                            gmul(a2, 9u) ^ gmul(a3, 14u));
		}
	}

	/* Last round (round == 1): InvShiftRows + InvSubBytes + AddRoundKey */
	for (r = 1u; r < 4u; r++)
	{
		uint8_t row[4];
		for (c = 0u; c < 4u; c++)
			row[c] = st[4u * ((c + (4u - r)) % 4u) + r];
		for (c = 0u; c < 4u; c++)
			st[4u * c + r] = row[c];
	}
	for (i = 0u; i < M1_AES_BLOCK_LEN; i++)
		st[i] = INV_SBOX[st[i]];
	add_round_key(st, &w[0]);

	for (i = 0u; i < M1_AES_BLOCK_LEN; i++)
		out[i] = st[i];

	m1_secure_zero(st, sizeof(st));
} /* static void aes_decrypt_block_w(...) */

/*============================================================================*/
/**
  * @brief Public AES-256 block encrypt.
  */
/*============================================================================*/
bool m1_aes256_encrypt_block(const uint8_t *key,
                             const uint8_t *in, uint8_t *out)
{
	uint32_t w[AES_WORDS_256];
	bool	 ok;

	if ((in == NULL) || (out == NULL))
		return false;

	ok = aes_expand_key(key, M1_AES256_KEY_LEN, w, AES_WORDS_256);
	if (!ok)
	{
		m1_secure_zero(w, sizeof(w));
		return false;
	}

	aes_encrypt_block_w(w, AES_NR_256, in, out);

	m1_secure_zero(w, sizeof(w));
	return true;
} /* bool m1_aes256_encrypt_block(...) */

/*============================================================================*/
/**
  * @brief Public AES-256 block decrypt.
  */
/*============================================================================*/
bool m1_aes256_decrypt_block(const uint8_t *key,
                             const uint8_t *in, uint8_t *out)
{
	uint32_t w[AES_WORDS_256];
	bool	 ok;

	if ((in == NULL) || (out == NULL))
		return false;

	ok = aes_expand_key(key, M1_AES256_KEY_LEN, w, AES_WORDS_256);
	if (!ok)
	{
		m1_secure_zero(w, sizeof(w));
		return false;
	}

	aes_decrypt_block_w(w, AES_NR_256, in, out);

	m1_secure_zero(w, sizeof(w));
	return true;
} /* bool m1_aes256_decrypt_block(...) */

/*============================================================================*/
/**
  * @brief AES-256-CBC encrypt (SP 800-38A §6.2, no padding).
  */
/*============================================================================*/
bool m1_aes256_cbc_encrypt(const uint8_t *key, size_t key_len,
                           const uint8_t *iv, size_t iv_len,
                           const uint8_t *in, size_t len, uint8_t *out)
{
	uint32_t w[AES_WORDS_256];
	uint8_t  prev[M1_AES_BLOCK_LEN];
	uint8_t  cur[M1_AES_BLOCK_LEN];
	size_t	 off;
	bool	 ok;

	if ((key == NULL) || (iv == NULL) || (in == NULL) || (out == NULL))
		return false;
	if (key_len != M1_AES256_KEY_LEN)
		return false;
	if (iv_len != M1_AES_BLOCK_LEN)
		return false;
	if ((len == 0u) || ((len % M1_AES_BLOCK_LEN) != 0u))
		return false; /* No padding contract: whole blocks only */

	ok = aes_expand_key(key, key_len, w, AES_WORDS_256);
	if (!ok)
	{
		m1_secure_zero(w, sizeof(w));
		return false;
	}

	for (off = 0u; off < M1_AES_BLOCK_LEN; off++)
		prev[off] = iv[off];

	for (off = 0u; off < len; off += M1_AES_BLOCK_LEN)
	{
		size_t i;

		/* cur = plaintext block XOR previous ciphertext (or IV) */
		for (i = 0u; i < M1_AES_BLOCK_LEN; i++)
			cur[i] = (uint8_t)(in[off + i] ^ prev[i]);

		aes_encrypt_block_w(w, AES_NR_256, cur, out + off);

		/* Previous ciphertext for the next block: read before the
		 * next iteration's plaintext XOR -- in == out is safe because
		 * out[off..] was just written and is only read here. */
		for (i = 0u; i < M1_AES_BLOCK_LEN; i++)
			prev[i] = out[off + i];
	}

	m1_secure_zero(w, sizeof(w));
	m1_secure_zero(prev, sizeof(prev));
	m1_secure_zero(cur, sizeof(cur));
	return true;
} /* bool m1_aes256_cbc_encrypt(...) */

/*============================================================================*/
/**
  * @brief AES-256-CBC decrypt (SP 800-38A §6.2, no padding).
  *
  * In-place (in == out) is supported: each ciphertext block is saved
  * into prev BEFORE the output overwrites it.
  */
/*============================================================================*/
bool m1_aes256_cbc_decrypt(const uint8_t *key, size_t key_len,
                           const uint8_t *iv, size_t iv_len,
                           const uint8_t *in, size_t len, uint8_t *out)
{
	uint32_t w[AES_WORDS_256];
	uint8_t  prev[M1_AES_BLOCK_LEN];
	uint8_t  cur[M1_AES_BLOCK_LEN];
	uint8_t  dec[M1_AES_BLOCK_LEN];
	size_t	 off;
	bool	 ok;

	if ((key == NULL) || (iv == NULL) || (in == NULL) || (out == NULL))
		return false;
	if (key_len != M1_AES256_KEY_LEN)
		return false;
	if (iv_len != M1_AES_BLOCK_LEN)
		return false;
	if ((len == 0u) || ((len % M1_AES_BLOCK_LEN) != 0u))
		return false;

	ok = aes_expand_key(key, key_len, w, AES_WORDS_256);
	if (!ok)
	{
		m1_secure_zero(w, sizeof(w));
		return false;
	}

	for (off = 0u; off < M1_AES_BLOCK_LEN; off++)
		prev[off] = iv[off];

	for (off = 0u; off < len; off += M1_AES_BLOCK_LEN)
	{
		size_t i;

		for (i = 0u; i < M1_AES_BLOCK_LEN; i++)
			cur[i] = in[off + i]; /* Save ct before any in-place write */

		aes_decrypt_block_w(w, AES_NR_256, cur, dec);

		for (i = 0u; i < M1_AES_BLOCK_LEN; i++)
			out[off + i] = (uint8_t)(dec[i] ^ prev[i]);

		for (i = 0u; i < M1_AES_BLOCK_LEN; i++)
			prev[i] = cur[i];
	}

	m1_secure_zero(w, sizeof(w));
	m1_secure_zero(prev, sizeof(prev));
	m1_secure_zero(cur, sizeof(cur));
	m1_secure_zero(dec, sizeof(dec));
	return true;
} /* bool m1_aes256_cbc_decrypt(...) */

/*============================================================================*/
/**
  * @brief AES-CMAC (RFC 4493 / SP 800-38B).
  *
  * AES-128 keys are accepted solely so the host suite can run the
  * RFC 4493 vectors; the firmware's own path is AES-256.
  */
/*============================================================================*/
bool m1_aes_cmac(const uint8_t *key, size_t key_len,
                 const uint8_t *msg, size_t msg_len, uint8_t *tag)
{
	uint32_t w[AES_WORDS_256];
	uint8_t  l[M1_AES_BLOCK_LEN];
	uint8_t  k1[M1_AES_BLOCK_LEN];
	uint8_t  k2[M1_AES_BLOCK_LEN];
	uint8_t  c[M1_AES_BLOCK_LEN];
	uint8_t  m_last[M1_AES_BLOCK_LEN];
	size_t	 nblocks;
	size_t	 blk;
	unsigned nr;
	bool	 complete;

	if ((key == NULL) || (tag == NULL))
		return false;
	if ((msg == NULL) && (msg_len != 0u))
		return false;

	if (key_len == M1_AES128_KEY_LEN)
		nr = AES_NR_128;
	else if (key_len == M1_AES256_KEY_LEN)
		nr = AES_NR_256;
	else
		return false;

	if (!aes_expand_key(key, key_len, w, AES_WORDS_256))
	{
		m1_secure_zero(w, sizeof(w));
		return false;
	}

	/* Subkey generation (RFC 4493 §2.3): L = CIPH_K(0^128); K1 = L<<1
	 * (mod Rb); K2 = K1<<1 (mod Rb). Branchless on secret data: the
	 * top-bit carry becomes a 0x00/0xFF mask XORed into the last byte. */
	{
		uint8_t zero[M1_AES_BLOCK_LEN] = {0};
		uint8_t rb;
		unsigned i;

		aes_encrypt_block_w(w, nr, zero, l);

		rb = (uint8_t)(0u - ((l[0] >> 7) & 1u)) & CMAC_RB;
		for (i = 0u; i < M1_AES_BLOCK_LEN; i++)
		{
			uint8_t nxt = (i + 1u < M1_AES_BLOCK_LEN)
			                  ? (uint8_t)(l[i + 1u] >> 7)
			                  : 0u;
			k1[i] = (uint8_t)((uint8_t)(l[i] << 1) | nxt);
		}
		k1[M1_AES_BLOCK_LEN - 1u] ^= rb;

		rb = (uint8_t)(0u - ((k1[0] >> 7) & 1u)) & CMAC_RB;
		for (i = 0u; i < M1_AES_BLOCK_LEN; i++)
		{
			uint8_t nxt = (i + 1u < M1_AES_BLOCK_LEN)
			                  ? (uint8_t)(k1[i + 1u] >> 7)
			                  : 0u;
			k2[i] = (uint8_t)((uint8_t)(k1[i] << 1) | nxt);
		}
		k2[M1_AES_BLOCK_LEN - 1u] ^= rb;
	}

	/* Number of blocks (RFC 4493 §2.1): a zero-length message is one
	 * all-zero padded block (the K2 path); otherwise ceil(msg_len/16),
	 * with the last block either complete (K1) or padded 10* (K2). */
	nblocks = (msg_len + M1_AES_BLOCK_LEN - 1u) / M1_AES_BLOCK_LEN;
	if (nblocks == 0u)
		nblocks = 1u;
	complete = (msg_len > 0u) &&
	           ((msg_len % M1_AES_BLOCK_LEN) == 0u);

	if (complete)
	{
		/* Last full block XOR K1 */
		size_t base = (nblocks - 1u) * M1_AES_BLOCK_LEN;
		size_t i;
		for (i = 0u; i < M1_AES_BLOCK_LEN; i++)
			m_last[i] = (uint8_t)(msg[base + i] ^ k1[i]);
	}
	else
	{
		/* 10* padding of the (possibly empty) last block XOR K2 */
		size_t base = (msg_len / M1_AES_BLOCK_LEN) * M1_AES_BLOCK_LEN;
		size_t rem	= msg_len - base;
		size_t i;
		for (i = 0u; i < M1_AES_BLOCK_LEN; i++)
		{
			uint8_t b = (i < rem) ? msg[base + i]
			                      : (uint8_t)(i == rem ? 0x80u : 0x00u);
			m_last[i] = (uint8_t)(b ^ k2[i]);
		}
	}

	for (blk = 0u; blk < M1_AES_BLOCK_LEN; blk++)
		c[blk] = 0u; /* C_0 = 0^128 */

	for (blk = 0u; blk + 1u < nblocks; blk++)
	{
		size_t base = blk * M1_AES_BLOCK_LEN;
		size_t i;
		for (i = 0u; i < M1_AES_BLOCK_LEN; i++)
			c[i] ^= msg[base + i];
		aes_encrypt_block_w(w, nr, c, c); /* in-place: c = CIPH(c) */
	}

	for (blk = 0u; blk < M1_AES_BLOCK_LEN; blk++)
		c[blk] ^= m_last[blk];
	aes_encrypt_block_w(w, nr, c, tag);

	m1_secure_zero(w, sizeof(w));
	m1_secure_zero(l, sizeof(l));
	m1_secure_zero(k1, sizeof(k1));
	m1_secure_zero(k2, sizeof(k2));
	m1_secure_zero(c, sizeof(c));
	m1_secure_zero(m_last, sizeof(m_last));
	return true;
} /* bool m1_aes_cmac(...) */

/*============================================================================*/
/**
  * @brief SP 800-108 KDF-in-CMAC-mode iteration (see header diagram).
  *
  * Input: i || Label || 0x00 || Context(empty) || L, with the counter
  * one byte (r=8) and L the 4-byte big-endian bit length of the KDF
  * output (0x00000100 = 256). 1 + 15 + 1 + 0 + 4 = 21 bytes.
  */
/*============================================================================*/
static bool kdf_cmac_iteration(const uint8_t prk[M1_AES256_KEY_LEN],
                               uint8_t counter,
                               uint8_t out[M1_AES_BLOCK_LEN])
{
	uint8_t buf[21];
	size_t  len = 0u;
	size_t  label_len = sizeof(M1_KDF_LABEL) - 1u; /* sans NUL: 15 */
	size_t  i;

	buf[len++] = counter;
	for (i = 0u; i < label_len; i++)
		buf[len++] = M1_KDF_LABEL[i];
	buf[len++] = 0x00u; /* Label separator */
	buf[len++] = 0x00u; /* L = 0x00000100 */
	buf[len++] = 0x00u;
	buf[len++] = 0x01u;
	buf[len++] = 0x00u;

	if (len != sizeof(buf))
		return false; /* Layout drift guard (label length changed) */

	if (!m1_aes_cmac(prk, M1_AES256_KEY_LEN, buf, len, out))
	{
		m1_secure_zero(buf, sizeof(buf));
		return false;
	}

	m1_secure_zero(buf, sizeof(buf));
	return true;
} /* static bool kdf_cmac_iteration(...) */

/*============================================================================*/
/**
  * @brief Derive the 256-bit WiFi credential key from the device UID.
  * @see m1_crypto.h for the construction and the honesty note.
  */
/*============================================================================*/
bool m1_crypto_derive_wifi_key(const uint8_t *uid, uint8_t *out_key)
{
	uint8_t prk[M1_AES256_KEY_LEN];
	uint8_t k1[M1_AES_BLOCK_LEN];
	uint8_t k2[M1_AES_BLOCK_LEN];
	size_t  i;
	bool    ok;

	if ((uid == NULL) || (out_key == NULL))
		return false;

	/* PRK = UID || 0^20 (the only input entropy; see honesty note) */
	for (i = 0u; i < M1_AES256_KEY_LEN; i++)
		prk[i] = (i < M1_UID_LEN) ? uid[i] : 0u;

	ok = kdf_cmac_iteration(prk, 1u, k1) &&
	     kdf_cmac_iteration(prk, 2u, k2);

	if (ok)
	{
		for (i = 0u; i < M1_AES_BLOCK_LEN; i++)
		{
			out_key[i] = k1[i];
			out_key[M1_AES_BLOCK_LEN + i] = k2[i];
		}
	}
	else
	{
		m1_secure_zero(out_key, M1_AES256_KEY_LEN);
	}

	m1_secure_zero(prk, sizeof(prk));
	m1_secure_zero(k1, sizeof(k1));
	m1_secure_zero(k2, sizeof(k2));
	return ok;
} /* bool m1_crypto_derive_wifi_key(...) */
