/* See COPYING.txt for license details. */

/*
*
* m1_dfu_entry.h
*
* USB DFU entry decisions (issue #41, part B): pure, host-testable helpers.
*
* Nothing here touches a register: every function is plain integer logic over
* values the caller has already read, so tools/m1cp_host_test/test_m1cp_dfu.c
* can exercise each decision natively (no CMSIS, no HAL, no RTOS). The device
* side is main.c's pre-HAL hardware-strap block, bl_jump_to_dfu()
* (m1_fw_update_bl.c) and the recovery offer in startup_boot_recovery_hook()
* (m1_system.c).
*
* M1 Project
*
*/

#ifndef M1_DFU_ENTRY_H_
#define M1_DFU_ENTRY_H_

#include <stdint.h>
#include <stdbool.h>

/* STM32H573 system memory (ROM bootloader). The boot vector is the first two
 * words: initial SP at +0, reset vector at +4. */
#define M1_SYSTEM_MEMORY_BASE	((uint32_t)0x0BF90000U)

/* ---- Hardware strap ------------------------------------------------------ */

/* The UP button (PE11) held at reset is the hardware DFU strap; it is wired
 * active-low (pressed pulls the pin to ground). The pre-HAL check in main.c
 * configures PE11 as input/pull-up with raw register access because it runs
 * before HAL_Init(), then asks this helper for the decision so the polarity
 * lives in one place and is host-tested. */
#define M1_DFU_STRAP_PIN_INDEX	11U

static inline bool m1_dfu_strap_active(uint32_t strap_pin_level)
{
	return strap_pin_level == 0U;
} // m1_dfu_strap_active()

/* ---- Initial-SP sanity for the ROM jump ---------------------------------- */

/* A jump is only attempted when the word at M1_SYSTEM_MEMORY_BASE looks like a
 * stack pointer into H5 SRAM (0x20000000-0x200FFFFF; a coarse mask is enough
 * for boot-vector sanity). A blank/foreign system memory region then resets
 * instead of branching into garbage. */
#define M1_DFU_SP_SRAM_MASK		((uint32_t)0xFF000000U)
#define M1_DFU_SP_SRAM_VALUE	((uint32_t)0x20000000U)

static inline bool m1_dfu_sp_plausible(uint32_t sp)
{
	return (sp & M1_DFU_SP_SRAM_MASK) == M1_DFU_SP_SRAM_VALUE;
} // m1_dfu_sp_plausible()

/* ---- 32 s watchdog safety net ------------------------------------------- */

/* The IWDG start key (0xCCCC) survives a software reset, so the watchdog keeps
 * counting through the ST ROM bootloader, which has no interrupt handler to
 * feed it. Before jumping, the counter is re-armed at its slowest setting:
 * LSI / 256 with the maximum reload value, i.e.
 *
 *     (0xFFF + 1) * 256 / 32000 Hz = 32.768 s
 *
 * That budget lets the ROM enumerate over USB, or lets the user unplug the
 * device to trigger a real power-on reset, instead of the ~4 s application
 * watchdog resetting the unit mid-enumeration.
 *
 * IWDG_PR is a 4-bit field on STM32H5 and the LSI divider is 4 << PR
 * (IWDG_PRESCALER_256 == 0b110 == 6, stm32h5xx_hal_iwdg.h). The divider below
 * is derived FROM the encoding, so the timeout arithmetic and the value written
 * to IWDG->PR cannot diverge; PR == 7 would be /512 (65.536 s), which is what
 * shipped before this was centralised. */
#define M1_DFU_IWDG_PRESCALER	((uint32_t)0x06U)	/* IWDG_PR = 0b110 -> /256 */
#define M1_DFU_IWDG_DIVIDER		((uint32_t)(4U << M1_DFU_IWDG_PRESCALER))
#define M1_DFU_IWDG_RELOAD		((uint32_t)0x0FFFU)	/* 4095 */

_Static_assert(M1_DFU_IWDG_PRESCALER == 6U,
               "IWDG_PRESCALER_256 is 0b110 on STM32H5; PR 7 is /512 (65.5 s)");
_Static_assert(M1_DFU_IWDG_DIVIDER == 256U,
               "the DFU watchdog net must be LSI/256 (32.768 s at 32 kHz)");

#define M1_DFU_IWDG_KEY_UNLOCK	((uint32_t)0x5555U)	/* PR/RLR access */
#define M1_DFU_IWDG_KEY_RELOAD	((uint32_t)0xAAAAU)	/* feed */
#define M1_DFU_IWDG_KEY_START	((uint32_t)0xCCCCU)	/* start if stopped */

/* Fixed iteration cap for every hardware-flag spin in the jump teardown
 * (IWDG->SR update flags, ICACHE->SR.BUSYF, DCACHE1->SR.BUSYF). The teardown
 * runs with interrupts disabled and -- on the cold-boot strap route -- before
 * the watchdog net is armed, so an unbounded spin on a stuck hardware flag
 * would hang the unit until power is removed. Hitting the cap falls through to
 * the jump (best effort): the ROM either enumerates or the unit resets, which
 * is strictly better than a silent hang. ~100 000 NOP-free polls is orders of
 * magnitude longer than the handful of LSI/cache cycles each flag needs. */
#define M1_DFU_TEARDOWN_SPIN_LIMIT	((uint32_t)100000U)

/* LSI is nominally 32 kHz; the exact value is only used for the log line and
 * this helper's window assertion, never for the reload value itself. */
#define M1_DFU_LSI_NOMINAL_HZ	((uint32_t)32000U)

static inline uint32_t m1_dfu_iwdg_timeout_ms(uint32_t lsi_hz)
{
	uint32_t counts;

	if ( lsi_hz == 0U )
	{
		return 0U;
	}
	counts = (M1_DFU_IWDG_RELOAD + 1U) * M1_DFU_IWDG_DIVIDER;
	return (uint32_t)(((uint64_t)counts * 1000U) / (uint64_t)lsi_hz);
} // m1_dfu_iwdg_timeout_ms()

/* ---- Software DFU request (device_op_status) ----------------------------- */

/* DEV_OP_STATUS_USB_DFU_REQUEST is honoured once, at boot: the request is
 * cleared BEFORE the jump so a power loss during DFU/after the jump cannot
 * re-request DFU forever (an honoured request that reset the device again
 * would be a boot loop with no path back to the application).
 *
 * The status values are passed in instead of including m1_system.h, which
 * pulls in FreeRTOS headers: this keeps the header host-buildable and there is
 * still exactly one definition of each value (the caller passes the enum). */
static inline bool m1_dfu_request_is_pending(uint8_t device_op_status, uint8_t request_status)
{
	return device_op_status == request_status;
} // m1_dfu_request_is_pending()

/* Next persisted status for one step of the request state machine. Both real
 * transitions go through this one function, so the host-tested state machine is
 * the shipped one:
 *   - raise_request == true  (firmware_update_enter_usb_dfu(), before the
 *     reset): the request wins over whatever status is currently stored.
 *   - raise_request == false (startup_config_handler(), before the jump):
 *     consume -- NO_OP when the observed status is the request, otherwise the
 *     status is left untouched (the update state machine's own transitions own
 *     every other value). */
static inline uint8_t m1_dfu_request_next(uint8_t device_op_status, bool raise_request,
                                          uint8_t request_status, uint8_t idle_status)
{
	if ( raise_request )
	{
		return request_status;
	}
	return (device_op_status == request_status) ? idle_status : device_op_status;
} // m1_dfu_request_next()

/* ---- Bounded recovery offer -------------------------------------------- */

/* A failed boot validation offers DFU entry, but never waits indefinitely: a
 * flat battery or a stuck key must not turn the offer into a hang. The offer
 * is bounded by this wall-clock window and falls through to the pre-existing
 * behaviour (FW CORRUPT! banner, normal UI reachable) when it expires. Five
 * seconds is long enough to read one line and press OK, short enough that the
 * unit is never perceived as frozen; the poll loop kicks the application
 * watchdog inside it (normal IWDG period is 4 s). */
#define M1_DFU_RECOVERY_OFFER_WINDOW_MS	((uint32_t)5000U)

/* Two consecutive pressed polls (~100 ms at a 50 ms poll interval) are a
 * deliberate press rather than one noisy sample. */
#define M1_DFU_RECOVERY_PRESS_POLLS	((uint8_t)2U)

typedef enum {
	M1_DFU_RECOVERY_WAIT = 0,	/* keep showing the offer            */
	M1_DFU_RECOVERY_ENTER,		/* user asked for DFU: request+reset */
	M1_DFU_RECOVERY_FALL_THROUGH	/* window expired: normal behaviour  */
} S_M1_DFU_RECOVERY_ACTION_t;

/* Poll-loop state for the recovery offer. */
typedef struct {
	uint8_t pressed_polls;	/* consecutive pressed polls               */
	bool seen_release;	/* the key has been seen released since the
				 * offer opened (the edge trigger)         */
} S_M1_DFU_RECOVERY_POLL_t;

/* Seed the poll state from the key level at offer start. A key that is already
 * down when the offer appears was not pressed for it: a held/shorted/stuck OK
 * key must fall through at the window instead of selecting DFU on every boot
 * (which would cycle failsafe -> DFU -> failsafe forever). */
static inline S_M1_DFU_RECOVERY_POLL_t m1_dfu_recovery_poll_start(bool dfu_button_down)
{
	S_M1_DFU_RECOVERY_POLL_t st;

	st.pressed_polls = dfu_button_down ? 1U : 0U;
	st.seen_release = !dfu_button_down;
	return st;
} // m1_dfu_recovery_poll_start()

/* Pure decision for one poll of the recovery offer. Acceptance is
 * EDGE-triggered: ENTER requires a released -> pressed transition, so only a
 * press made after the offer appeared counts. Order still matters for the
 * honest case: a press that lands on the final (deadline) poll is honoured
 * even though it has only been observed pressed once, and no poll past the
 * deadline can return WAIT (that is the boundedness property the host test
 * pins). Updates *st in place; touches no register. */
static inline S_M1_DFU_RECOVERY_ACTION_t m1_dfu_recovery_poll(S_M1_DFU_RECOVERY_POLL_t *st,
                                                              bool dfu_button_down,
                                                              uint32_t elapsed_ms)
{
	if ( dfu_button_down )
	{
		if ( st->pressed_polls < M1_DFU_RECOVERY_PRESS_POLLS )
		{
			st->pressed_polls++;
		}
	}
	else
	{
		st->pressed_polls = 0U;
		st->seen_release = true;
	}

	if ( st->seen_release &&
	     ((st->pressed_polls >= M1_DFU_RECOVERY_PRESS_POLLS) ||
	      ((elapsed_ms >= M1_DFU_RECOVERY_OFFER_WINDOW_MS) && (st->pressed_polls > 0U))) )
	{
		return M1_DFU_RECOVERY_ENTER;
	}
	if ( elapsed_ms >= M1_DFU_RECOVERY_OFFER_WINDOW_MS )
	{
		return M1_DFU_RECOVERY_FALL_THROUGH;
	}
	return M1_DFU_RECOVERY_WAIT;
} // m1_dfu_recovery_poll()

_Static_assert(M1_DFU_RECOVERY_OFFER_WINDOW_MS > 0U,
               "the recovery offer must have a non-zero, finite window");

#endif /* M1_DFU_ENTRY_H_ */
