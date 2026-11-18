/* See COPYING.txt for license details. */

/*
*
* m1_rng.c
*
* STM32H5 hardware RNG wrapper (issue #42, PR-8). See m1_rng.h for the
* contracts (mandatory HW RNG, sticky refusal, no fallback).
*
* The two HAL-touching operations are isolated behind m1_rng_hal_init()
* / m1_rng_hal_generate() so the policy logic (idempotence, stickiness,
* partial-output zeroization) compiles identically on the target and in
* the host harness (-DM1_RNG_HOST_TEST provides fault-injectable
* versions of those two functions).
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "m1_rng.h"
#include "m1_crypto.h"

#ifdef M1_RNG_HOST_TEST
/* Host harness provides both functions (fault injection). Declared here
 * so the target build never sees them; the target build below provides
 * the real HAL versions instead. */
bool m1_rng_hal_init(void);
bool m1_rng_hal_generate(uint32_t *word);
#else /* target build */
#include "stm32h5xx_hal.h"

static RNG_HandleTypeDef g_m1_hrng;

/*============================================================================*/
/**
  * @brief Bring up the RNG peripheral via the HAL.
  *
  * The kernel clock (HSI48) and the peripheral clock are configured by
  * HAL_RNG_MspInit() (Core/Src/stm32h5xx_hal_msp.c). Clock Error
  * Detection stays ENABLED: HSI48 is trimmed by the USB CRS only while
  * USB is active, so the always-on CED is the safe configuration for a
  * free-running HSI48 (RM0481 RNG chapter).
  */
/*============================================================================*/
static bool m1_rng_hal_init(void)
{
	HAL_StatusTypeDef st;

	g_m1_hrng.Instance						= RNG;
	g_m1_hrng.Init.ClockErrorDetection		= RNG_CED_ENABLE;

	st = HAL_RNG_Init(&g_m1_hrng);

	return (st == HAL_OK);
} /* static bool m1_rng_hal_init(void) */

/*============================================================================*/
/**
  * @brief Generate one 32-bit random word (blocking, HAL timeout).
  *
  * Returns false on any HAL error or timeout -- the caller makes that
  * failure sticky. Never retried into a fallback: a failing RNG means
  * no entropy, and no entropy means the crypto path refuses.
  */
/*============================================================================*/
static bool m1_rng_hal_generate(uint32_t *word)
{
	return (HAL_RNG_GenerateRandomNumber(&g_m1_hrng, word) == HAL_OK);
} /* static bool m1_rng_hal_generate(uint32_t *word) */

#endif /* M1_RNG_HOST_TEST */

/***************************** V A R I A B L E S ******************************/

/* Sticky readiness: set only after init + one successful generate
 * probe; cleared by any generate failure.
 *
 * Permanent failure latch: set by ANY init or generate failure and
 * never cleared. It is consulted by m1_rng_init() and m1_rng_bytes(),
 * so a later init cannot resurrect the RNG once anything has failed
 * (m1_rng.h contract 1/2: refusal is sticky and permanent). */
static bool g_m1_rng_ready = false;
static bool g_m1_rng_failed = false;

/*************** F U N C T I O N   I M P L E M A T I O N ****************/

/*============================================================================*/
/**
  * @brief Initialize the hardware RNG (idempotent while healthy,
  *   permanently refusing after any failure).
  * @see m1_rng.h contract 1/2.
  */
/*============================================================================*/
bool m1_rng_init(void)
{
	uint32_t probe;

	if (g_m1_rng_ready)
		return true; /* Already up: idempotent */

	/* Permanent latch: once ANY init or generate failure occurred, no
	 * later init may bring the RNG back -- the crypto path refuses
	 * for the rest of the boot. */
	if (g_m1_rng_failed)
		return false;

	if (!m1_rng_hal_init())
	{
		g_m1_rng_failed = true;
		return false;
	}

	/* Generate probe: a peripheral that initializes but cannot produce
	 * a word is not an entropy source. Failure latches permanently:
	 * g_m1_rng_ready is never set and g_m1_rng_failed blocks every
	 * later m1_rng_init()/m1_rng_bytes() call. */
	if (!m1_rng_hal_generate(&probe))
	{
		g_m1_rng_failed = true;
		return false;
	}

	m1_secure_zero(&probe, sizeof(probe));

	g_m1_rng_ready = true;
	return true;
} /* bool m1_rng_init(void) */

/*============================================================================*/
/**
  * @brief True iff the RNG is usable (initialized, no failure since).
  */
/*============================================================================*/
bool m1_rng_ok(void)
{
	return g_m1_rng_ready;
} /* bool m1_rng_ok(void) */

/*============================================================================*/
/**
  * @brief Fill buf with hardware-random bytes; refuse on any failure.
  * @see m1_rng.h contract 3 (buffer zeroization on failure).
  */
/*============================================================================*/
bool m1_rng_bytes(uint8_t *buf, size_t len)
{
	size_t off;

	if (!g_m1_rng_ready || g_m1_rng_failed || (buf == NULL))
		return false;

	for (off = 0u; off < len; off += sizeof(uint32_t))
	{
		uint32_t word = 0u;
		size_t   n	   = len - off;
		size_t   i;

		if (n > sizeof(uint32_t))
			n = sizeof(uint32_t);

		if (!m1_rng_hal_generate(&word))
		{
			/* Sticky refusal: zero the partial output, mark the
			 * wrapper unusable, latch the failure permanently,
			 * fail. No fallback, no retry loop papering over a
			 * dead peripheral, and no later init resurrection. */
			m1_secure_zero(buf, len);
			g_m1_rng_ready = false;
			g_m1_rng_failed = true;
			return false;
		}

		/* Little-endian word unpack (byte order of RNG output is
		 * irrelevant to the consumer; it is fixed here so the host
		 * seam and the target produce identical packing). */
		for (i = 0u; i < n; i++)
			buf[off + i] = (uint8_t)(word >> (8u * i));

		/* Zero the word copy (partial-tail words leave unused byte
		 * copies in `word` -- do not let them linger on the stack). */
		m1_secure_zero(&word, sizeof(word));
	}

	return true;
} /* bool m1_rng_bytes(uint8_t *buf, size_t len) */
