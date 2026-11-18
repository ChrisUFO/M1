/* See COPYING.txt for license details. */

/*
*
* m1_rng.h
*
* STM32H5 hardware RNG wrapper (issue #42, PR-8: crypto + HW RNG
* foundation).
*
* Contracts
* ---------
* 1. MANDATORY hardware RNG. The only entropy source in this firmware is
*    the STM32H5 RNG peripheral (kernel clock HSI48, see
*    HAL_RNG_MspInit() in Core/Src/stm32h5xx_hal_msp.c). There is NO
*    software, time-based or counter-based fallback: if m1_rng_init()
*    fails, or any generate call fails, m1_rng_bytes() returns false --
*    forever (failure is sticky) -- and the caller's crypto path must
*    refuse. Never seed an IV or key from HAL_GetTick(), a counter, RAM
*    scraps or any other deterministic source.
*
* 2. Init ordering. m1_rng_init() is called once from
*    m1_system_init_task() (m1_csrc/m1_sys_init.c), after the SD card /
*    settings load and before m1_tasks_init() -- deterministically before
*    any task that may consume randomness. It is idempotent; safe to
*    re-call.
*
* 3. Buffer contract. m1_rng_buf[0 .. len-1] is fully written on
*    success. On failure the partial output is zeroized (m1_secure_zero)
*    so no half-random buffer can be mistaken for entropy.
*
* 4. Context. Task context only (the HAL generate call blocks on DRDY
*    with a timeout). Not ISR-safe. Callers must serialize access from
*    multiple tasks (the WiFi credential path is single-consumer in the
*    UI task by design).
*
* Host test seam: build with -DM1_RNG_HOST_TEST to replace the two HAL
* calls (init + generate one 32-bit word) with harness-provided
* functions; the policy logic compiled on host and target is identical.
* See m1_csrc/test/m1_crypto_test.py.
*
* M1 Project
*
*/

#ifndef M1_RNG_H_
#define M1_RNG_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Initialize the hardware RNG. Returns true when the peripheral is
 * usable. On any HAL init failure, or when the post-init generate probe
 * fails, the wrapper is marked unusable and stays unusable: every later
 * m1_rng_init()/m1_rng_bytes() call fails. No fallback exists. */
bool m1_rng_init(void);

/* True iff the RNG initialized successfully and has not failed since.
 * Crypto paths can gate on this to refuse work up front. */
bool m1_rng_ok(void);

/* Fill buf[0 .. len-1] with hardware-random bytes. Returns false (and
 * leaves the buffer zeroized) when the wrapper is not initialized, when
 * buf is NULL, or when any word generation fails; a generate failure is
 * sticky -- subsequent calls fail too. */
bool m1_rng_bytes(uint8_t *buf, size_t len);

#endif /* M1_RNG_H_ */
