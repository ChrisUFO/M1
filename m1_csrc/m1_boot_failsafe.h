/* See COPYING.txt for license details. */

/*
*
* m1_boot_failsafe.h
*
* Boot-time firmware integrity failsafe (issue #41, part A): pure, host-testable
* helpers.
*
* Everything in this header is plain integer logic with no HAL, flash or RTOS
* dependency, so tools/m1cp_host_test/test_m1cp_failsafe.c can exercise every
* decision branch natively. The device side (flash reads + CRC peripheral) lives
* in bl_crc_validate_active_bank() (m1_fw_update_bl.c) and the recovery state
* machine in startup_boot_failsafe() (m1_system.c).
*
* M1 Project
*
*/

#ifndef M1_BOOT_FAILSAFE_H_
#define M1_BOOT_FAILSAFE_H_

#include <stdint.h>
#include <stdbool.h>

/* Application image the CRC emitter signs and the boot check validates:
 * [bank_start, bank_start + M1_BOOT_CRC_CROP_END_BYTES). Must stay equal to the
 * build's hard-coded crop end (build.sh CROP_END_BYTES, CMakeLists.txt
 * "-crop 0x0 0x000FFC14"); m1_fw_update_bl.h static-asserts the derived flash
 * addresses against this constant so the two can never drift apart silently. */
#define M1_BOOT_CRC_CROP_END_BYTES	0x000FFC14U

/* Maximum bank swaps allowed between two CRC-validated boots. A swap counts
 * against the budget until a boot validates the image it swapped in; a healthy
 * boot clears it (startup_boot_failsafe()). This bounds option-byte churn and
 * stops a device that keeps landing on an unvalidated bank from being swapped
 * back and forth forever. */
#define M1_BOOT_SWAP_ATTEMPTS_MAX	1U

/* Why the running bank failed validation. Stored in the backup register as a
 * single byte (BK_REGS_SELECT_BOOT_FAULT_SIG) AND shown in the log, so a
 * service engineer can tell an erased metadata block (magic/header) from a
 * genuinely corrupted image (CRC). */
typedef enum {
	M1_BOOT_FAULT_NONE = 0,			/* image validates */
	M1_BOOT_FAULT_MAGIC_MISMATCH,	/* S_M1_FW_CONFIG_t magic_number_1 wrong */
	M1_BOOT_FAULT_HEADER_MISMATCH,	/* magic_number_2 / metadata tail wrong */
	M1_BOOT_FAULT_CRC_MISMATCH,		/* metadata intact, image bytes differ */
	M1_BOOT_FAULT_UNKNOWN
} S_M1_BOOT_FAULT_t;

/* Span covered by the stored CRC word, by address arithmetic only -- no
 * magic-number scan and no image-size field (both are false-positive risks;
 * see docs/protocols/firmware-update.md). Returns 0 for a degenerate range. */
static inline uint32_t m1_boot_crc_span_bytes(uint32_t bank_start, uint32_t crc_address)
{
	return (crc_address > bank_start) ? (uint32_t)(crc_address - bank_start) : 0U;
}

/* Pure CRCs-equal decision: the whole point of the failsafe is a full-span
 * comparison, so this is deliberately exact equality (no "allow N bytes
 * different" tolerance). */
static inline bool m1_boot_crc_matches(uint32_t computed_crc, uint32_t stored_crc)
{
	return computed_crc == stored_crc;
}

/* Classify a failed/successful validation. Order matters: metadata faults are
 * reported in preference to a CRC mismatch, because a wrong magic explains the
 * mismatch (erased/foreign image) and is the actionable signal. */
static inline S_M1_BOOT_FAULT_t m1_boot_classify_fault(uint32_t magic_1, uint32_t magic_1_expected,
                                                       uint32_t magic_2, uint32_t magic_2_expected,
                                                       uint32_t computed_crc, uint32_t stored_crc)
{
	if ( magic_1 != magic_1_expected )
	{
		return M1_BOOT_FAULT_MAGIC_MISMATCH;
	}
	if ( magic_2 != magic_2_expected )
	{
		return M1_BOOT_FAULT_HEADER_MISMATCH;
	}
	if ( !m1_boot_crc_matches(computed_crc, stored_crc) )
	{
		return M1_BOOT_FAULT_CRC_MISMATCH;
	}
	return M1_BOOT_FAULT_NONE;
} // m1_boot_classify_fault()

/* Saturating 8-bit counter increment: boot/swap counters live in single bytes
 * and must never wrap (a wrap would look like a healthy reset). */
static inline uint8_t m1_boot_next_counter(uint8_t current)
{
	return (current < UINT8_MAX) ? (uint8_t)(current + 1U) : (uint8_t)UINT8_MAX;
}

/* Rollback loop guard: is another bank swap permitted? */
static inline bool m1_boot_swap_allowed(uint8_t swap_attempts)
{
	return swap_attempts < (uint8_t)M1_BOOT_SWAP_ATTEMPTS_MAX;
}

#endif /* M1_BOOT_FAILSAFE_H_ */
