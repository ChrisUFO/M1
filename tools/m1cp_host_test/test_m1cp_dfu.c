/* Host test for USB DFU entry (issue #41-B, PR-DFU).
 *
 * Covers, with no hardware:
 *   - the pure decisions in m1_csrc/m1_dfu_entry.h: hardware strap polarity,
 *     ROM initial-SP sanity, the 32 s IWDG budget pinned to the REGISTER
 *     encoding (IWDG_PR == 6, /256 -> 32.768 s) and its keys, the
 *     software-request transition (raise -> recognised -> cleared with every
 *     other status left alone), and the bounded, edge-triggered recovery policy
 *     (a released -> pressed press is honoured, a stuck key never is, expiry can
 *     never return WAIT again),
 *   - source seams on the device-only code (HAL/CMSIS, cannot be host-linked):
 *       * bl_jump_to_dfu(), the teardown ORDER:
 *           interrupts off -> SysTick -> HAL_RCC_DeInit -> NVIC ICER/ICPR ->
 *           VTOR -> barriers -> ICACHE/DCACHE off -> IWDG start -> unlock ->
 *           PR/RLR -> feed -> interrupts on -> MSP -> jump,
 *         bounded flag spins, plus no option-byte programming inside it,
 *       * Core/Src/main.c: the hardware strap is evaluated before HAL_Init()
 *         and before MX_IWDG_Init(), drives PD12/PD13 + the speaker tick, and
 *         the jump call is CONFINED to the strap condition; the strap index is
 *         statically tied to BUTTON_UP_Pin; the file programs no option bytes,
 *       * m1_system.c: the request is recognised/cleared through the tested
 *         pure transition, the recovery hook uses the edge-triggered policy,
 *         the banner+offer screen draws both lines, the indication is asserted
 *         only on ENTER, and the watchdog is kicked inside the bounded window,
 *       * m1_fw_update.c: firmware_update_enter_usb_dfu() raises the request
 *         through the same transition before resetting, and never touches the
 *         TAMP backup registers,
 *       * m1_system.h: the op-status append preserved the existing values and
 *         the 8-byte backup-register contract is intact.
 *
 * Run either from the repository root or from this directory:
 *   ./tools/m1cp_host_test/test_m1cp_dfu
 *   make -C tools/m1cp_host_test test
 */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m1_dfu_entry.h"

static int count = 0, fails = 0;
static void ok(int cond, const char *what) {
    count++;
    if (cond) { printf("  ok   %s\n", what); }
    else       { printf("  FAIL %s\n", what); fails++; }
}

/* Persisted op-status values restated here so the enum's numeric contract is
 * pinned independently of m1_system.h (which pulls in FreeRTOS headers and is
 * therefore not host-includable). */
#define OP_NO_OP              0u
#define OP_FW_UPDATE_ACTIVE   1u
#define OP_FW_UPDATE_COMPLETE 2u
#define OP_FW_ROLLBACK_DONE   3u
#define OP_REBOOT             4u
#define OP_USB_DFU_REQUEST    5u
#define OP_UNKNOWN            6u

static char *read_all(const char *path) {
    FILE *fp = fopen(path, "rb");
    long n;
    char *s;

    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    n = ftell(fp);
    if (n < 0 || fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }
    s = malloc((size_t)n + 1U);
    if (s == NULL) { fclose(fp); return NULL; }
    if (fread(s, 1U, (size_t)n, fp) != (size_t)n) { free(s); fclose(fp); return NULL; }
    s[n] = '\0';
    fclose(fp);
    return s;
}

/* Repo-root-relative read, from either the repository root or this directory. */
static char *read_repo(const char *path) {
    char alt[512];
    char *s = read_all(path);
    if (s != NULL) return s;
    snprintf(alt, sizeof(alt), "../../%s", path);
    return read_all(alt);
}

/* Brace-matched body bounding, same technique as test_update_intake_size.c:
 * locate `sig`, skip prototypes, then copy the block through its matching '}'. */
static char *bound_block(const char *src, const char *sig) {
    const char *search_from = src;
    for (;;) {
        const char *start = strstr(search_from, sig);
        const char *after, *open_brace, *p, *end = NULL;
        int depth = 0;
        size_t len;
        char *body;

        if (start == NULL) return NULL;
        after = start + strlen(sig);
        while (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n') after++;
        if (*after == ';') { search_from = start + strlen(sig); continue; }
        open_brace = strchr(start, '{');
        if (open_brace == NULL) return NULL;
        for (p = open_brace; *p != '\0'; p++) {
            if (*p == '{') depth++;
            else if (*p == '}') { depth--; if (depth == 0) { end = p; break; } }
        }
        if (end == NULL) return NULL;
        len = (size_t)(end - start) + 1U;
        body = malloc(len + 1U);
        if (body == NULL) return NULL;
        memcpy(body, start, len);
        body[len] = '\0';
        return body;
    }
}

/* All needles present and in strictly ascending source order. */
static int in_order(const char *src, const char *const *needles, size_t n) {
    const char *p = src;
    size_t i;

    for (i = 0; i < n; i++) {
        const char *hit = strstr(p, needles[i]);
        if (hit == NULL) {
            printf("       (order: missing \"%s\")\n", needles[i]);
            return 0;
        }
        p = hit + strlen(needles[i]);
    }
    return 1;
}

int main(void) {
    printf("USB DFU entry (issue #41-B)\n");

    /* ---- hardware strap --------------------------------------------------- */
    ok(m1_dfu_strap_active(0U), "strap: PE11 low (button held) selects DFU");
    ok(!m1_dfu_strap_active(1U), "strap: PE11 high (released) does not");
    ok(M1_DFU_STRAP_PIN_INDEX == 11U, "strap pin is PE11 (the UP button)");

    /* ---- ROM jump SP sanity ---------------------------------------------- */
    ok(m1_dfu_sp_plausible(0x20000000U), "SP at SRAM base is plausible");
    ok(m1_dfu_sp_plausible(0x2000FFF0U), "SP at the top of H5 SRAM is plausible");
    ok(!m1_dfu_sp_plausible(0xFFFFFFFFU), "erased system memory (SP ~ all ones) is rejected");
    ok(!m1_dfu_sp_plausible(0x00000000U), "zero SP is rejected");
    ok(!m1_dfu_sp_plausible(0x08000000U), "flash address as SP is rejected");
    ok(!m1_dfu_sp_plausible(0x30000000U), "SP outside the SRAM prefix is rejected");

    /* ---- 32 s watchdog safety net ----------------------------------------
     * The REGISTER VALUE is what is pinned here, not the helper constant: on
     * STM32H5 the IWDG divider is 4 << PR and IWDG_PRESCALER_256 is 6. The old
     * bug (PR = 7, i.e. /512 -> 65.536 s) passed a test that only checked the
     * helper against itself, so the expectations below are computed from the
     * register encoding independently of m1_dfu_iwdg_timeout_ms(). */
    ok(M1_DFU_IWDG_PRESCALER == 6U,
       "IWDG PR register encoding is 6 (IWDG_PRESCALER_256), not 7 (/512)");
    ok(M1_DFU_IWDG_DIVIDER == (4U << M1_DFU_IWDG_PRESCALER),
       "IWDG divider is derived from the register encoding (4 << PR)");
    ok(M1_DFU_IWDG_DIVIDER == 256U,
       "PR 6 means LSI/256");
    ok((4096U * 512U * 1000U) / M1_DFU_LSI_NOMINAL_HZ == 65536U,
       "PR 7 (/512) really would be 65.536 s -- the shipped bug is not 32 s");
    ok(m1_dfu_iwdg_timeout_ms(M1_DFU_LSI_NOMINAL_HZ) ==
       (uint32_t)(((uint64_t)(M1_DFU_IWDG_RELOAD + 1U) * (uint64_t)(4U << M1_DFU_IWDG_PRESCALER) * 1000U) /
                  (uint64_t)M1_DFU_LSI_NOMINAL_HZ),
       "timeout helper is the arithmetic of the register encoding");
    ok(m1_dfu_iwdg_timeout_ms(M1_DFU_LSI_NOMINAL_HZ) == 32768U,
       "watchdog net is 32.768 s at the nominal 32 kHz LSI");
    ok(m1_dfu_iwdg_timeout_ms(0U) == 0U, "zero LSI frequency cannot divide by zero");
    ok(m1_dfu_iwdg_timeout_ms(32000U) > 1000U * 30U,
       "watchdog net exceeds 30 s (well past the 4 s application period)");
    ok(M1_DFU_IWDG_RELOAD == 0xFFFU, "IWDG reload is the maximum 4095");
    ok(M1_DFU_IWDG_KEY_UNLOCK == 0x5555U && M1_DFU_IWDG_KEY_RELOAD == 0xAAAAU &&
       M1_DFU_IWDG_KEY_START == 0xCCCCU,
       "IWDG PR/RLR unlock, reload and start keys are the STM32 keys");
    ok(M1_DFU_TEARDOWN_SPIN_LIMIT >= 1000U,
       "teardown flag spins have a finite, generous iteration cap");

    /* ---- software request state machine ----------------------------------
     * Both shipped call sites use these transitions (raise before the reset in
     * firmware_update_enter_usb_dfu(), consume in startup_config_handler()), so
     * what is pinned here is the state machine the device actually runs. */
    ok(m1_dfu_request_is_pending(OP_USB_DFU_REQUEST, OP_USB_DFU_REQUEST),
       "request: pending when device_op_status is the request value");
    ok(!m1_dfu_request_is_pending(OP_NO_OP, OP_USB_DFU_REQUEST),
       "request: idle status is not pending");
    ok(m1_dfu_request_next(OP_USB_DFU_REQUEST, false, OP_USB_DFU_REQUEST, OP_NO_OP) == OP_NO_OP,
       "request: honoured request is consumed to NO_OP");
    ok(m1_dfu_request_next(OP_NO_OP, true, OP_USB_DFU_REQUEST, OP_NO_OP) == OP_USB_DFU_REQUEST,
       "request: raising from idle stores the request");
    ok(m1_dfu_request_next(OP_FW_UPDATE_ACTIVE, true, OP_USB_DFU_REQUEST, OP_NO_OP) == OP_USB_DFU_REQUEST,
       "request: an explicit dfu wins over an active-update status");
    {
        static const uint8_t others[] = {
            OP_NO_OP, OP_FW_UPDATE_ACTIVE, OP_FW_UPDATE_COMPLETE,
            OP_FW_ROLLBACK_DONE, OP_REBOOT, OP_UNKNOWN
        };
        size_t i;
        int preserved = 1;
        for (i = 0; i < sizeof(others); i++) {
            if (m1_dfu_request_next(others[i], false, OP_USB_DFU_REQUEST, OP_NO_OP) != others[i]) {
                preserved = 0;
            }
        }
        ok(preserved, "request: every update-state value is left untouched by the consume step");
    }
    /* request -> recognised -> cleared is single-shot: consuming twice ends at
     * NO_OP and the second consume cannot re-recognise the request. */
    {
        uint8_t st = m1_dfu_request_next(OP_USB_DFU_REQUEST, false, OP_USB_DFU_REQUEST, OP_NO_OP);
        ok(!m1_dfu_request_is_pending(st, OP_USB_DFU_REQUEST) &&
           m1_dfu_request_next(st, false, OP_USB_DFU_REQUEST, OP_NO_OP) == OP_NO_OP,
           "request: consumed once, it can never re-fire (no DFU boot loop)");
    }

    /* ---- bounded recovery policy (edge-triggered) ------------------------- */
    ok(M1_DFU_RECOVERY_OFFER_WINDOW_MS == 5000U,
       "recovery offer window is the documented 5 s");
    {
        /* Honest press: released at offer start, then two pressed polls. */
        S_M1_DFU_RECOVERY_POLL_t st = m1_dfu_recovery_poll_start(false);
        ok(m1_dfu_recovery_poll(&st, false, 0U) == M1_DFU_RECOVERY_WAIT,
           "recovery: released key keeps waiting");
        ok(m1_dfu_recovery_poll(&st, true, 50U) == M1_DFU_RECOVERY_WAIT,
           "recovery: one pressed poll is not yet a press");
        ok(m1_dfu_recovery_poll(&st, true, 100U) == M1_DFU_RECOVERY_ENTER,
           "recovery: a released->pressed press enters DFU on the second poll");
    }
    {
        /* A press that lands on the last (deadline) poll is still honoured. */
        S_M1_DFU_RECOVERY_POLL_t st = m1_dfu_recovery_poll_start(false);
        uint32_t t;
        S_M1_DFU_RECOVERY_ACTION_t a = M1_DFU_RECOVERY_WAIT;
        for (t = 0U; t + 50U < M1_DFU_RECOVERY_OFFER_WINDOW_MS; t += 50U) {
            a = m1_dfu_recovery_poll(&st, false, t);
        }
        ok(a == M1_DFU_RECOVERY_WAIT, "recovery: no input inside the window keeps waiting");
        ok(m1_dfu_recovery_poll(&st, true, M1_DFU_RECOVERY_OFFER_WINDOW_MS) == M1_DFU_RECOVERY_ENTER,
           "recovery: a press that lands on the deadline poll is still honoured");
    }
    {
        /* STUCK KEY: held from before the offer opens to well past the window.
         * A level-triggered policy would enter DFU on the second poll (~50 ms)
         * and cycle failsafe -> DFU -> failsafe forever; the edge-triggered one
         * must never ENTER and must fall through at the window. */
        S_M1_DFU_RECOVERY_POLL_t st = m1_dfu_recovery_poll_start(true);
        uint32_t t;
        S_M1_DFU_RECOVERY_ACTION_t a = M1_DFU_RECOVERY_WAIT;
        int ever_entered = 0;
        for (t = 0U; t <= M1_DFU_RECOVERY_OFFER_WINDOW_MS + 10000U; t += 50U) {
            a = m1_dfu_recovery_poll(&st, true, t);
            if (a == M1_DFU_RECOVERY_ENTER) ever_entered = 1;
            if (a == M1_DFU_RECOVERY_FALL_THROUGH) break;
        }
        ok(!ever_entered, "recovery: a stuck OK key can never select DFU");
        ok(a == M1_DFU_RECOVERY_FALL_THROUGH &&
           t == M1_DFU_RECOVERY_OFFER_WINDOW_MS,
           "recovery: a stuck OK key falls through at the 5 s window (no DFU cycle)");
    }
    {
        S_M1_DFU_RECOVERY_POLL_t st = m1_dfu_recovery_poll_start(false);
        ok(m1_dfu_recovery_poll(&st, false, M1_DFU_RECOVERY_OFFER_WINDOW_MS - 1U) == M1_DFU_RECOVERY_WAIT,
           "recovery: no input one millisecond before the deadline keeps waiting");
        ok(m1_dfu_recovery_poll(&st, false, M1_DFU_RECOVERY_OFFER_WINDOW_MS) == M1_DFU_RECOVERY_FALL_THROUGH,
           "recovery: no input at the deadline falls through");
        ok(m1_dfu_recovery_poll(&st, false, UINT32_MAX) == M1_DFU_RECOVERY_FALL_THROUGH,
           "recovery: no input long after the deadline falls through");
    }
    {
        /* boundedness theorem at the policy level: past the window the decision
         * is never WAIT, so any poll loop that stops on non-WAIT terminates. */
        uint32_t t;
        int never_waits = 1;
        for (t = M1_DFU_RECOVERY_OFFER_WINDOW_MS; t < M1_DFU_RECOVERY_OFFER_WINDOW_MS + 4000U; t++) {
            S_M1_DFU_RECOVERY_POLL_t st = m1_dfu_recovery_poll_start(true);
            if (m1_dfu_recovery_poll(&st, true, t) == M1_DFU_RECOVERY_WAIT) never_waits = 0;
        }
        ok(never_waits, "recovery: policy can never WAIT once the window has elapsed");
    }

    /* ---- seam: teardown order in bl_jump_to_dfu() ------------------------- */
    {
        char *src = read_repo("m1_csrc/m1_fw_update_bl.c");
        ok(src != NULL, "read m1_csrc/m1_fw_update_bl.c");
        if (src != NULL) {
            char *body = bound_block(src, "void bl_jump_to_dfu(void)");
            ok(body != NULL, "bl_jump_to_dfu() is defined");
            if (body != NULL) {
                static const char *const order[] = {
                    "__disable_irq();",
                    "SysTick->CTRL = 0U;",
                    "HAL_RCC_DeInit()",
                    "NVIC->ICER[i] = 0xFFFFFFFFU;",
                    "NVIC->ICPR[i] = 0xFFFFFFFFU;",
                    "SCB->VTOR = M1_SYSTEM_MEMORY_BASE;",
                    "__DSB();",
                    "__ISB();",
                    "ICACHE->CR &= ~ICACHE_CR_EN;",
                    "DCACHE1->CR &= ~DCACHE_CR_EN;",
                    /* Start-then-configure: 0xCCCC must precede the unlock and
                     * the PR/RLR writes, or a cold-boot strap entry programs a
                     * stopped IWDG (no LSI -> update flags never arrive). */
                    "IWDG->KR  = M1_DFU_IWDG_KEY_START;",
                    "IWDG->KR  = M1_DFU_IWDG_KEY_UNLOCK;",
                    "IWDG->PR  = M1_DFU_IWDG_PRESCALER;",
                    "IWDG->RLR = M1_DFU_IWDG_RELOAD;",
                    "IWDG->KR = M1_DFU_IWDG_KEY_RELOAD;",
                    "__enable_irq();",
                    "__set_MSP(sp_val);",
                    "jump_to_boot();"
                };
                ok(in_order(body, order, sizeof(order) / sizeof(order[0])),
                   "teardown order: irq off -> SysTick -> RCC -> NVIC -> VTOR -> barriers -> caches -> IWDG start/unlock/PR/RLR/feed -> irq on -> MSP -> jump");
                ok(strstr(body, "m1_dfu_sp_plausible") != NULL,
                   "jump validates the ROM initial SP before using it");
                ok(strstr(body, "NVIC_SystemReset") != NULL,
                   "implausible ROM vector resets instead of branching");
                ok(strstr(body, "HAL_FLASHEx_OBProgram(") == NULL &&
                   strstr(body, "HAL_FLASH_OB_Launch(") == NULL &&
                   strstr(body, "HAL_FLASH_Unlock(") == NULL,
                   "jump programs no option bytes and unlocks no flash");
                /* Every hardware-flag spin is bounded: a stuck flag with
                 * PRIMASK set and no net armed must fall through to the jump. */
                {
                    const char *p = body;
                    int bounded = 0;
                    while ((p = strstr(p, "M1_DFU_TEARDOWN_SPIN_LIMIT")) != NULL) {
                        bounded++;
                        p += 26;
                    }
                    ok(bounded >= 3,
                       "all three teardown spins (ICACHE/DCACHE/IWDG SR) are iteration-capped");
                }
                ok(strstr(body, "(IWDG->SR != 0U) && (i < M1_DFU_TEARDOWN_SPIN_LIMIT)") != NULL,
                   "the watchdog register-update wait is bounded, not an infinite spin");
                ok(strstr(body, "while ( IWDG->SR != 0U )") == NULL,
                   "no unbounded IWDG->SR spin remains in the jump");
            }
            free(body);
            ok(strstr(src, "#ifndef M1CP_HOST_TEST") != NULL,
               "CMSIS-touching jump is guarded out of host builds");
        }
        free(src);
    }

    /* ---- seam: Core/Src/main.c early strap -------------------------------- */
    {
        char *src = read_repo("Core/Src/main.c");
        ok(src != NULL, "read Core/Src/main.c");
        if (src != NULL) {
            const char *strap = strstr(src, "m1_dfu_strap_active");
            const char *hal_init = strstr(src, "HAL_Init();");
            const char *iwdg_init = strstr(src, "MX_IWDG_Init();");
            ok(strap != NULL, "main.c evaluates the hardware strap decision");
            ok(strap != NULL && hal_init != NULL && strap < hal_init,
               "strap is evaluated before HAL_Init()");
            ok(strap != NULL && iwdg_init != NULL && strap < iwdg_init,
               "strap is evaluated before the application watchdog is started");
            ok(strstr(src, "GPIOC->BSRR = (1U << 7U)") != NULL &&
               strstr(src, "GPIOC->BSRR = (1U << (7U + 16U))") != NULL,
               "strap gives the SPK_CTRL (PC7) speaker tick");
            /* Brace-bound containment (VA1): a call moved outside the condition
             * would make every boot enter DFU, and only a containment assertion
             * can catch that. */
            {
                char *strap_block = bound_block(src, "if ( dfu_hardware_strap )");
                ok(strap_block != NULL, "the strap condition is a braced block");
                if (strap_block != NULL) {
                    ok(strstr(strap_block, "bl_jump_to_dfu();") != NULL,
                       "the jump lives INSIDE if ( dfu_hardware_strap ) (outside it every boot would enter DFU)");
                    ok(strstr(strap_block, "GPIOD->BSRR = (1U << 12U) | (1U << 13U)") != NULL,
                       "strap drives PD12/PD13 (external connector pins 6-7)");
                }
                free(strap_block);
            }
            ok(strstr(src, "_Static_assert((1U << M1_DFU_STRAP_PIN_INDEX) == (uint32_t)BUTTON_UP_Pin") != NULL,
               "strap pin index is statically tied to BUTTON_UP_Pin (PE11 on GPIOE)");
            ok(strstr(src, "HAL_FLASHEx_OBProgram(") == NULL &&
               strstr(src, "HAL_FLASH_OB_Launch(") == NULL &&
               strstr(src, "HAL_FLASH_Unlock(") == NULL,
               "main.c programs no option bytes (legacy per-boot rewrite not ported)");
        }
        free(src);
    }

    /* ---- seam: startup_config_handler request branch ---------------------- */
    {
        char *src = read_repo("m1_csrc/m1_system.c");
        ok(src != NULL, "read m1_csrc/m1_system.c");
        if (src != NULL) {
            char *handler = bound_block(src, "void startup_config_handler(void)");
            ok(handler != NULL, "startup_config_handler() is defined");
            if (handler != NULL) {
                const char *req = strstr(handler,
                    "m1_dfu_request_is_pending(m1_device_stat.bu_regs.device_op_status");
                const char *active = strstr(handler, "device_op_status==DEV_OP_STATUS_FW_UPDATE_ACTIVE");
                const char *failsafe = strstr(handler, "startup_boot_failsafe();");
                ok(req != NULL, "handler recognises the request through the tested pure helper");
                ok(req != NULL && active != NULL && req < active,
                   "request branch is the FIRST op-status branch (honoured early)");
                ok(req != NULL && failsafe != NULL && req < failsafe,
                   "request branch precedes the boot failsafe");
                if (req != NULL) {
                    const char *clear = strstr(req, "DEV_OP_STATUS_NO_OP");
                    const char *jump = strstr(req, "bl_jump_to_dfu();");
                    ok(clear != NULL && jump != NULL && clear < jump,
                       "request is cleared to NO_OP BEFORE jumping (single-shot)");
                    ok(strstr(req, "m1_dfu_request_next(m1_device_stat.bu_regs.device_op_status, false") != NULL,
                       "request is cleared through the same transition the host test covers");
                }
                ok(strstr(req != NULL ? req : handler, "startup_dfu_indicate()") != NULL,
                   "request path gives the PD12/PD13 indication");
            }
            free(handler);

            char *hook = bound_block(src, "static void startup_boot_recovery_hook(uint8_t boot_attempts)");
            ok(hook != NULL, "startup_boot_recovery_hook() is filled in (not a log-only stub)");
            if (hook != NULL) {
                ok(strstr(hook, "m1_dfu_recovery_poll_start(") != NULL,
                   "recovery hook seeds the edge trigger from the level at offer start");
                ok(strstr(hook, "m1_dfu_recovery_poll(") != NULL,
                   "recovery hook uses the pure edge-triggered policy helper");
                ok(strstr(hook, "startup_recovery_offer_display()") != NULL,
                   "recovery hook renders the banner+offer screen in one page");
                ok(strstr(hook, "firmware_update_enter_usb_dfu()") != NULL,
                   "recovery hook enters DFU through the single public API");
                {
                    /* D3: the indication must only be asserted on ENTER, right
                     * before the reset -- never while merely offering. */
                    const char *enter = strstr(hook, "action==M1_DFU_RECOVERY_ENTER");
                    const char *ind = strstr(hook, "startup_dfu_indicate()");
                    const char *api = strstr(hook, "firmware_update_enter_usb_dfu()");
                    ok(enter != NULL && ind != NULL && api != NULL && enter < ind && ind < api,
                       "PD12/PD13 are asserted only on ENTER, immediately before the DFU API");
                }
                ok(strstr(hook, "M1_DFU_RECOVERY_FALL_THROUGH") != NULL &&
                   strstr(hook, "break;") != NULL,
                   "recovery hook falls through when the window expires");
                ok(strstr(hook, "m1_wdt_kick()") != NULL,
                   "recovery hook keeps the IWDG fed inside its bounded window");
                ok(strstr(hook, "vTaskDelay") != NULL,
                   "recovery wait is a delayed poll, not a busy loop");
                ok(strstr(hook, "HAL_FLASHEx_OBProgram(") == NULL &&
                   strstr(hook, "HAL_FLASH_OB_Launch(") == NULL,
                   "recovery hook programs no option bytes");
                ok(strstr(hook, "\"FW CORRUPT!\"") != NULL,
                   "timeout restores the diagnostic banner (no silent healthy boot)");
            }
            free(hook);

            /* G4: the offer screen must draw the diagnosis AND the offer, since
             * u8g2_FirstPage() clears whatever the banner previously drew. */
            {
                char *offer = bound_block(src, "static void startup_recovery_offer_display(void)");
                ok(offer != NULL, "startup_recovery_offer_display() is defined");
                if (offer != NULL) {
                    ok(strstr(offer, "\"FW CORRUPT!\"") != NULL,
                       "offer screen still shows the FW CORRUPT! banner");
                    ok(strstr(offer, "\"OK=USB DFU %us\"") != NULL,
                       "offer screen shows the DFU offer line");
                    ok(strstr(offer, "startup_info_screen_display(") == NULL,
                       "offer screen does not re-enter the clearing one-line status screen");
                }
                free(offer);
            }

            char *failsafe = bound_block(src, "static void startup_boot_failsafe(void)");
            ok(failsafe != NULL, "startup_boot_failsafe() is present");
            if (failsafe != NULL) {
                ok(strstr(failsafe, "startup_boot_recovery_hook(attempts)") != NULL,
                   "failsafe still hands control to the recovery hook");
                ok(strstr(failsafe, "HAL_FLASHEx_OBProgram(") == NULL &&
                   strstr(failsafe, "HAL_FLASH_OB_Launch(") == NULL,
                   "failsafe itself programs no option bytes");
            }
            free(failsafe);

            ok(strstr(src, "TAMP" "->" "BKP") == NULL,
               "m1_system.c never touches the TAMP backup registers directly (accessor-only)");
        }
        free(src);
    }

    /* ---- seam: firmware_update_enter_usb_dfu() --------------------------- */
    {
        char *src = read_repo("m1_csrc/m1_fw_update.c");
        ok(src != NULL, "read m1_csrc/m1_fw_update.c");
        if (src != NULL) {
            char *body = bound_block(src, "void firmware_update_enter_usb_dfu(void)");
            ok(body != NULL, "firmware_update_enter_usb_dfu() is defined");
            if (body != NULL) {
                const char *write = strstr(body,
                    "m1_dfu_request_next(m1_device_stat.bu_regs.device_op_status, true");
                const char *reset = strstr(body, "NVIC_SystemReset();");
                ok(write != NULL,
                   "public API raises the request through the tested pure transition");
                ok(strstr(body, "startup_config_write(BK_REGS_SELECT_DEV_OP_STAT") != NULL,
                   "public API persists through the bu_regs accessor");
                ok(write != NULL && reset != NULL && write < reset,
                   "request is persisted before the reset");
                ok(strstr(body, "TAMP" "->" "BKP") == NULL,
                   "public API writes no raw backup register");
                ok(strstr(body, "HAL_FLASHEx_OBProgram(") == NULL &&
                   strstr(body, "HAL_FLASH_OB_Launch(") == NULL,
                   "public API programs no option bytes");
            }
            free(body);
        }
        free(src);
    }

    /* ---- seam: persisted values + backup-register contract --------------- */
    {
        char *src = read_repo("m1_csrc/m1_system.h");
        ok(src != NULL, "read m1_csrc/m1_system.h");
        if (src != NULL) {
            const char *req = strstr(src, "DEV_OP_STATUS_USB_DFU_REQUEST,");
            const char *unknown = strstr(src, "DEV_OP_STATUS_UNKNOWN");
            ok(req != NULL && unknown != NULL && req < unknown,
               "USB DFU request is appended before UNKNOWN (existing values keep their numbers)");
            ok(strstr(src, "_Static_assert(DEV_OP_STATUS_REBOOT == 4") != NULL &&
               strstr(src, "_Static_assert(DEV_OP_STATUS_USB_DFU_REQUEST == 5") != NULL,
               "op-status numeric contract is statically pinned");
            ok(strstr(src,
                      "_Static_assert(sizeof(S_M1_BK_REGS_t) == 8, \"BK regs must stay 8 bytes") != NULL,
               "backup-register struct is still statically 8 bytes (DR0/DR1 only)");
        }
        free(src);
    }

    /* ---- seam: op-status enum order in the header ------------------------ */
    {
        char *sysh = read_repo("m1_csrc/m1_system.h");
        if (sysh != NULL) {
            ok(strstr(sysh, "DEV_OP_STATUS_USB_DFU_REQUEST") != NULL &&
               strstr(sysh, "BK_REGS_SELECT_DEV_OP_STAT = 1") != NULL,
               "accessor selectors unchanged (DEV_OP_STAT stays 1)");
        }
        free(sysh);
    }

    printf("\n%s (%d/%d)\n", fails == 0 ? "ALL PASS" : "FAILURES", count - fails, count);
    fflush(stdout);
    return fails ? 1 : 0;
}
