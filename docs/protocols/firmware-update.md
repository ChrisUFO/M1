# STM32 firmware image and update

## Status

This document describes the implementation visible in `m1_fw_update.c`,
`m1_fw_update_bl.c`, `m1_fw_update_bl.h`, the linker scripts, and
`CMakeLists.txt`. Security and product-policy assertions marked **Needs
validation** require owner approval.

## Flash layout

| Region | Address/size | Purpose |
| --- | --- | --- |
| Bank 1 base | `0x08000000` | Active or inactive application bank |
| Bank size | `0x00100000` (1 MiB) | One firmware image bank |
| Firmware configuration | `0x080FFC00`–`0x080FFFFF` | Reserved 1 KiB linker section |
| Application region | `0x000FFC00` (1,023 KiB) | Linker region below the reserved configuration section |
| Transfer chunk | 1024 bytes | File/update processing chunk |
| CRC | 4 bytes | Stored immediately after `S_M1_FW_CONFIG_t` |

Bank 2 is addressed by adding `M1_FLASH_BANK_SIZE` to the Bank 1 address.

## Firmware metadata

`S_M1_FW_CONFIG_t` is placed in `.FW_CONFIG_SECTION` and contains, in order:

| Field | Type | Meaning |
| --- | --- | --- |
| `magic_number_1` | `uint32_t` | `0x4D493235` |
| `fw_version_rc` | `uint8_t` | Least-significant version component |
| `fw_version_build` | `uint8_t` | Build component |
| `fw_version_minor` | `uint8_t` | Minor component |
| `fw_version_major` | `uint8_t` | Major component |
| `user_option_1` | `uint16_t` | Reserved/user option |
| `User_option_2` | `uint16_t` | Reserved/user option |
| `ism_band_region` | `uint8_t` | Compiled Sub-GHz region |
| reserved fields | mixed | Reserved |
| `magic_number_2` | `uint32_t` | `0x534A1F41` |

The current source constants are major `0`, minor `8`, build `0`, RC `5`.
The byte order in the struct is intentionally RC, build, minor, major.

## Backup-register state

The STM32 keeps its small, power-cycle-persistent state in RTC/TAMP backup
registers. The 8-byte layout is fixed by `S_M1_BK_REGS_t` (`m1_system.h`), which
is statically asserted to stay 8 bytes so the block can never grow into the
registers the ESP32 version record owns:

| Register | Bytes | Field |
| --- | --- | --- |
| DR0 | 0-3 | `magic_number` (`SYS_CONFIG_MAGIC_NUMBER`, `0x534A1F42`) |
| DR1 | 4 | `device_op_status` (update state machine) |
| DR1 | 5 | `boot_attempts` (failed boot CRC validations, saturating at 255) |
| DR1 | 6 | `boot_fault_signature` (fault class, `0` = none) |
| DR1 | 7 | `swap_attempts` (bank swaps since the last CRC-validated boot) |
| DR2 | -- | ESP32 firmware version (owned by `m1_esp_version.c`) |
| DR3 | -- | ESP32 version validity magic (owned by `m1_esp_version.c`) |

All access goes through `startup_config_write()` / `startup_config_read()` with
the `BK_REGS_SELECT_*` selectors, which derive the physical register from the
byte offset and rewrite the whole 32-bit word from the in-RAM shadow (DR1
carries four independent byte fields). No source may dereference the TAMP BKP
registers directly; `scripts/check_no_raw_bkp_writes.sh` enforces that, and the
host test target runs it. `SYS_CONFIG_MAGIC_NUMBER` is bumped whenever this
layout changes, so registers left by an older layout are re-initialised exactly
once.

## CRC generation and validation

The build command crops the binary through `0x000FFC14` and asks SRecord to
write an STM32 little-endian CRC at `0x000FFC14`. Runtime validation uses the
STM32 CRC peripheral with the default polynomial and initial value, no input
inversion, no output inversion, and 32-bit input words.

`0x000FFC14` is correct for the current 20-byte metadata structure beginning at
`0x000FFC00`. A metadata-size change moves the CRC address and therefore requires
coordinated changes to the linker layout, CMake command, update parser, tests,
and documentation. The firmware build static-asserts
`FW_CRC_ADDRESS - FW_START_ADDRESS == 0xFFC14` against the boot failsafe's crop
end, so a metadata change fails the build instead of silently validating a span
nobody signed.

Two different banks are checked, and the offsets differ deliberately:

- **Update verification** (`bl_crc_check()`) validates the *inactive* bank while
  the device runs from the active one, so it reads `FW_START_ADDRESS +
  M1_FLASH_BANK_SIZE` and compares against the CRC stored at `FW_CRC_ADDRESS +
  M1_FLASH_BANK_SIZE`.
- **Boot failsafe** (`bl_crc_validate_active_bank()`) validates the bank the
  device is *executing from*. Because the option-byte bank swap remaps the
  active physical bank to `0x08000000`, that bank always appears at
  `0x08000000..0x080FFFFF` and its CRC word is exactly at `FW_CRC_ADDRESS` --
  no bank-size offset.

Both checks derive the image size from address arithmetic. Neither scans for a
magic number (image data can contain it) nor trusts a size field.

## Update flow

The implementation performs the following high-level sequence:

1. Select a firmware image from storage.
2. Check file access, type, size, version, region, and battery readiness.
3. Read the image in 1024-byte chunks and calculate CRC.
4. Compare the calculated CRC with the CRC stored in the image.
5. Erase and program the inactive flash bank.
6. Validate the programmed bank.
7. Set operation state in backup registers.
8. change the bank-swap option byte and reset into the new bank.
9. Report completion or failure during the next startup.

The exact error enumeration is `S_M1_M1_FW_CODES_t` in
`m1_fw_update_bl.h`.

## Canonical image size (update intake)

Update intake accepts exactly one image size: `0xFFC18` bytes (`1047576`) — the
`0xFFC14`-byte signed span above followed by its 4-byte CRC word. That is the
only shape the build's CRC emitter produces (`<artifact>_wCRC.bin`) and the only
span the boot failsafe validates, so the update path and the boot check cannot
disagree about what a valid image is:

- **SD-card path** (`m1_fw_update.c`): any other size — empty, the plain
  `0xFFC14` image without its CRC word, a foreign or crafted `.bin`, anything
  longer — is rejected with `M1_FW_IMAGE_SIZE_INVALID` (`Invalid image size!`)
  before the pre-flight CRC pass and before anything is erased or programmed.
  The log line reports the actual and the required size.
- **Streamed M1CP path** (`bl_flash_stream_begin()`): the same check rejects the
  client-declared size with `BL_CODE_SIZE_ERROR` before the target bank is
  erased.

The decision is `m1_boot_image_size_is_canonical()` over
`M1_BOOT_IMAGE_FILE_SIZE_CANONICAL` (`m1_boot_failsafe.h`), and
`m1_fw_update_bl.h` static-asserts that constant against
`FW_CRC_ADDRESS - FW_START_ADDRESS + FW_IMAGE_CRC_SIZE`, so the intake size and
the failsafe's span cannot drift apart silently. Host test:
`tools/m1cp_host_test/test_update_intake_size.c`.

Without the gate, a shorter self-consistent file gets past the pre-flight
verifier (which hashes the file over its own length) and the whole target bank is
erased and programmed before the fixed-address post-flash check can object — and
if that check passes by coincidence (the stale word left at `0xFFC14` happens to
equal the short image's CRC), the image is swapped in and every later boot
reports `FW CORRUPT!`.

## Provisioning: which artifact to flash

Only `<artifact>_wCRC.bin` self-validates. It is the `0xFFC14` application image
plus the CRC word at `0x080FFC14`; the plain `.bin`, the `.hex`, and the ELF all
stop at `0x080FFC14`. The boot failsafe reads `0x080FFC14` unconditionally, so a
device provisioned over SWD or by an IDE download from any artifact without the
CRC word reads erased `0xFFFFFFFF` there and shows `FW CORRUPT!` on every boot,
with `boot_attempts` counting up in the backup registers.

The banner is a diagnostic rather than containment — the application still runs
— but on such a device it is a false alarm, and it masks genuine corruption.
Provision with `_wCRC.bin`: download it over SWD/IDE, or copy it to the SD card
and install it through the update menu.

An erased or absent CRC word is reported as `boot_fault_signature` =
`CRC_MISMATCH` (`3`). The fault class separates metadata faults from image
faults; it does not distinguish "never provisioned" from "image corrupted",
because a legitimate image may in principle carry the CRC value `0xFFFFFFFF`.

## Boot failsafe

Every boot validates the RUNNING bank before the boot artwork is drawn
(`startup_boot_failsafe()` in `m1_system.c`; it runs after the update state
machine has been reconciled and before `m1_gui_welcome_scr()`):

1. `bl_crc_validate_active_bank()` computes the hardware CRC32 of the fixed
   span `[FW_START_ADDRESS, FW_CRC_ADDRESS)` (the same `0xFFC14` bytes the build
   signed) and compares it with the 4 bytes at `FW_CRC_ADDRESS`. No magic scan
   and no image-size field are involved.
2. On a match the boot continues and any previous failure accounting is
   cleared (`boot_attempts`, `boot_fault_signature`, `swap_attempts`).
3. On a mismatch the device:
   - increments `boot_attempts` (saturating at 255) and records the fault class
     in `boot_fault_signature` (metadata magic / metadata header / image CRC) in
     the backup registers, so the failure survives a power cycle,
   - shows the `FW CORRUPT!` screen instead of the normal boot artwork,
   - logs the fault class and attempt count, then enters the recovery seam
     (`startup_boot_recovery_hook()`).

**What the failsafe does and does not do.** Detection and accounting are
automatic; containment is not. On a failure the boot stops at `FW CORRUPT!`,
the fault is recorded, and a **bounded USB DFU offer** runs (see "DFU entry"
below):

- the unvalidated image is **not** prevented from running — execution continues
  into the application exactly as on a healthy boot once the offer has
  expired or been declined,
- `FW CORRUPT!` is **not** a lock-out screen: after the offer the normal UI is
  one key press away. The message leaves the display mode in
  `M1_OPERATION_MODE_DISPLAY_ON`, and the first OK press takes `m1_menu.c` into
  `menu_main_init()`, which redraws the main menu over the banner,
- the offer itself never waits indefinitely (5 s window, then fall-through), so
  a flat battery or a stuck key cannot turn recovery into a hang.

Treat `FW CORRUPT!` as a diagnostic banner rather than containment. What it
does give you is durable evidence: `boot_fault_signature` and `boot_attempts`
survive a power cycle in DR1, so a technician can distinguish an erased metadata
block (magic/header fault) from a genuinely corrupted image (CRC fault). The
supported ways out of a failed bank are the recovery-screen DFU offer, the
service DFU strap, and the SD-card update path (plus the interactive rollback
offer below). On-device qualification of the whole corrupt-bank path is still
the issue #41 hardware gate (milestone "Post-merge stabilization").

The manual alternate-bank rollback path is still interactive-only: it runs on
the first power-up after an update reboot when the user presses BACK+DOWN, and
it is unchanged apart from a loop guard. `swap_attempts` is written before
`bl_swap_banks()` and allows at most one swap between two CRC-validated boots;
a healthy boot clears it, so:

- a device that keeps landing on a bank the failsafe cannot validate cannot be
  swapped back and forth indefinitely (option-byte churn is bounded), and
- a swap that dies inside the option-byte launch is still counted, because the
  counter is written first. The counter is preserved across the
  rollback-complete re-initialisation for the same reason.

Automatic failed-boot rollback (swapping without the user keypress) is still not
implemented. On-device qualification of the corrupt-bank boot path -- including
the `FW CORRUPT!` diagnostic banner (not a lock-out) and the DFU offer/entry
-- is tracked as the issue #41 hardware gate (milestone "Post-merge
stabilization"); the host suite covers the span arithmetic, the CRC decision,
every fault class, the counters, the swap guard, the teardown order and the
bounded recovery policy, and signs/injects a synthetic `0xFFC14`-byte bank with
the build's own CRC algorithm (valid, flipped byte, zeroed magic, relocated CRC
word).

The boot path performs a READ-ONLY IWDG option-byte check
(`startup_iwdg_option_byte_check()`) and logs whether the watchdog is
software-started. It reads the OPTSR *value* bit for the watchdog mode
(`FLASH_OPTSR_IWDG_SW`, bit 3, exposed by the HAL as `OB_IWDG_SW`) -- not the
similarly named `OB_USER_IWDG_SW`, which is the `USERType` selector that chooses
which option byte `HAL_FLASHEx_OBProgram` writes and would report `BORH_EN`
(bit 2) as the watchdog mode. The decision lives in
`m1_boot_iwdg_is_hardware()` (`m1_boot_failsafe.h`) and is host-tested. The
legacy fork silently rewrote the IWDG option bytes on every boot; that rewrite is
deliberately not ported — a normal boot writes no option bytes. (The interactive
rollback branch of the same boot path still programs them, via `bl_swap_banks()`,
and so does the offline `m1_pre_power_down()` path: both are explicit
user/power-down actions, not a per-boot rewrite.)

## DFU entry

The device can enter the STM32H573 ROM USB DFU bootloader in system memory
(`0x0BF90000`) three ways. All three end in the same non-returning
`bl_jump_to_dfu()` (`m1_fw_update_bl.c`), so the teardown exists once.

### 1. Hardware strap

Hold the **UP button** (PE11, active low) while powering on. `Core/Src/main.c`
samples it in the first instructions of `main()`, **before `HAL_Init()` and
before `MX_IWDG_Init()`**, using raw register access (no HAL exists yet). This
is the fork's original service entry and the one to use on a unit that no
longer boots far enough to run the application's recovery screen.

On a strap hit the unit:

1. gives a speaker tick (SPK_CTRL = PC7 driven high ~23 ms -- the emitted loop
   is 300 000 iterations at 64 MHz -- then low),
2. drives PD12/PD13 high: these are **user-owned external connector pins 6-7**
   (`docs/hardware.md`, also the GPIO menu's "Pin PD12"/"Pin PD13" via
   `m1_ext_gpio[]`), **not** dedicated DFU LEDs,
3. calls `bl_jump_to_dfu()`.

The strap decision is the pure `m1_dfu_strap_active()` (`m1_dfu_entry.h`),
host-tested; only the pre-HAL GPIO plumbing lives in `main.c`.

### 2. Software request

`firmware_update_enter_usb_dfu()` (`m1_fw_update.c`) is the single public
entry point (used by the future `dfu` CLI command, unit PR-CLI, and by the
recovery offer below). It:

1. persists `DEV_OP_STATUS_USB_DFU_REQUEST` (`5`) in DR1[7:0] through
   `startup_config_write()` -- never a raw `TAMP->BKP` write; DR2/DR3 stay
   ESP-owned,
2. resets the device.

`startup_config_handler()` then honours the request as the **first**
`device_op_status` branch -- before the update state machine and before the
boot failsafe, so an explicit request is never misread as an interrupted
update -- and clears it back to `DEV_OP_STATUS_NO_OP` **before** jumping. The
request is therefore single-shot: a power loss during the ROM session, or a
reset after it, cannot put the unit back into DFU, so there is no DFU boot
loop. Existing enum values keep their persisted numbers
(`REBOOT` = 4, request = 5, `UNKNOWN` = 6; statically asserted).

### 3. Recovery-screen offer

When the boot CRC validation fails, `startup_boot_recovery_hook()`
(`m1_system.c`) shows `FW CORRUPT!` and `OK=USB DFU 5s` **together in one
screen** (`startup_recovery_offer_display()`, one `u8g2` page, so the
diagnostic line is not cleared by drawing the offer) and offers DFU entry for a
**bounded** window:

- the decision each poll is the pure `m1_dfu_recovery_poll()`
  (`m1_dfu_entry.h`). Acceptance is **edge-triggered**: the key level is sampled
  when the offer opens and a press only counts after a released -> pressed
  transition, so a deliberate OK press (~100 ms, i.e. two consecutive 50 ms
  polls, so one noisy sample cannot trigger it) selects DFU while a held,
  shorted or stuck key never does. A press that lands on the last poll of the
  window is still honoured, and once the 5 s window has elapsed the helper can
  never return "keep waiting";
- the poll loop kicks the IWDG (`m1_wdt_kick()`, documented for long bounded
  operations) because 5 s exceeds the ~4 s application watchdog period;
- a press calls `firmware_update_enter_usb_dfu()` (recorded request + reset);
- on timeout the `FW CORRUPT!` banner is restored and the pre-existing
  behaviour resumes (the normal UI stays reachable), with the choice logged.

So a flat battery or a stuck key can delay a failed boot by at most the window
plus one poll interval -- never hang it, and never cycle it into DFU: a stuck
OK key falls through at 5 s every time -- and a failing image still cannot
silently present itself as healthy: the banner is drawn and the failure is
persisted in `boot_attempts` / `boot_fault_signature`. PD12/PD13 (external
connector pins 6-7) are driven only at the moment of ENTER, on the way into the
reset, so offering or timing out never leaves an external circuit asserted.

### Teardown order

`bl_jump_to_dfu()` performs, in this order (host-test-enforced by source seam
in `tools/m1cp_host_test/test_m1cp_dfu.c`):

1. ROM initial-SP sanity (`m1_dfu_sp_plausible()`); implausible -> reset,
2. `__disable_irq()`,
3. SysTick teardown (CTRL/LOAD/VAL = 0) and the pended SysTick/PendSV bits
   cleared (`SCB->ICSR`; `NVIC->ICER`/`ICPR` do not cover core exceptions),
4. application clock tree down to the HSI **reset state** with a
   raw-register, iteration-capped teardown (`bl_rcc_teardown_bounded()`):
   HSI on and selected, prescalers /1, HSE/CSI/HSI48/PLL1-3 off, PLL config
   registers at their reset values, LSI left running for the watchdog net
   below. `HAL_RCC_DeInit()` is deliberately **not** used: its internal waits
   poll `HAL_GetTick()`, whose only tick source (TIM6) step 2 masked, so its
   timeouts never expire and a stuck ready flag would spin forever -- and on
   the strap route (before `HAL_Init()`) it additionally returns `HAL_ERROR`
   at its first `HAL_InitTick()` (no valid `uwTickPrio` yet), i.e. it never
   tears the tree down at all. The helper's post-condition is **verified**, not
   discarded: SYSCLK on HSI and PLL1 off, otherwise the teardown fails loudly
   (step 10's reset) instead of handing the ROM a clock it cannot service,
5. TIM6 (the HAL tick timebase) stopped and ungated,
6. `NVIC->ICER`/`NVIC->ICPR` cleared for every interrupt,
7. `SCB->VTOR = M1_SYSTEM_MEMORY_BASE`,
8. `__DSB()` / `__ISB()`, then ICACHE and DCACHE handled with a **bounded**
   busy-wait (BootROM entry with caches enabled can fail or hang silently on
   H5). Only ICACHE is a real action: it is disabled and the wait covers ST's
   completion criterion `CR.EN == 0` (what `HAL_ICACHE_Disable()` polls) **and**
   `SR.BUSYF` (the automatic invalidation the disable starts). DCACHE1 is never
   enabled in this build -- its `RCC_AHB1ENR.DCACHE1EN` gate is not enabled
   anywhere in the tree and `HAL_DCACHE_MODULE_ENABLED` is off -- so that read
   returns 0 and the branch is not taken; it is retained for legacy-fork parity
   and is confirmed on silicon by checklist item 6,
9. 32 s IWDG safety net (prescaler /256, reload 4095 -> 32.768 s at the
   nominal 32 kHz LSI). The sequence is **start-then-configure** --
   `KR=0xCCCC` (start), `KR=0x5555` (unlock), `PR`, `RLR`, wait for the
   register update (`IWDG->SR`), `KR=0xAAAA` (feed) -- matching
   `stm32h5xx_hal_iwdg.c`. That `0xAAAA` is the **last** feed: neither the
   teardown nor the ROM re-feeds the counter, so the same 32.768 s bounds the
   **entire** ROM residency -- connect, enumerate, download and verify -- and
   not merely an abandoned session. On the cold-boot strap route the IWDG/LSI is
   still stopped (this runs before `MX_IWDG_Init()`), so configuring first would
   leave the update flags waiting on LSI cycles that never come, or drop the
   writes and start at the reset-default ~0.5 s period. That update wait is
   LSI-domain (one
   *prescaled* LSI period = 8 ms at /256, plus LSI startup), so it uses its own
   millisecond-class budget (`M1_DFU_IWDG_SYNC_SPIN_LIMIT`, ~4x
   `HAL_IWDG_DEFAULT_TIMEOUT`) rather than the tens-of-cycles cap the cache
   waits use. It tests only the four **update** flags,
   `PVU|RVU|WVU|EWU` (`M1_DFU_IWDG_SR_UPDATE_FLAGS`, == the HAL's private
   `IWDG_KERNEL_UPDATE_FLAGS` == `LL_IWDG_IsReady()`'s mask), never the whole
   register: `ONF` (bit 8) is the persistent "IWDG is running" status bit that
   the `0xCCCC` above sets, so a whole-register `SR == 0` test can never
   succeed and would send every entry route down the failure path. If the four
   flags still do not clear the teardown **resets** instead of feeding
   `0xAAAA`: reloading before `PVU`/`RVU` clear takes the *previous* `PR`/`RLR`
   (same HAL file), which would silently turn the net back into ~0.5 s on the
   strap route / ~4 s on the application route,
10. `CONTROL.SPSEL` cleared with MSP = the ROM initial SP, then interrupts
   re-enabled, then the branch to the ROM reset vector -- one register-only
   sequence. The SPSEL clear is required, not cosmetic: under FreeRTOS thread
   mode runs on **PSP**, so a bare `__set_MSP()` would leave the ROM executing
   on the abandoned task stack instead of the SP just validated.

Every hardware-flag spin in steps 4, 8 and 9 is capped: the `RCC`
ready/switch flags and the `ICACHE` `CR.EN`/`SR.BUSYF` wait are bounded by
`M1_DFU_TEARDOWN_SPIN_LIMIT` (tens-of-cycles flags), and the `IWDG->SR` update
wait by its own, larger `M1_DFU_IWDG_SYNC_SPIN_LIMIT`. The cache and RCC waits
fall through to the jump (best effort); the two waits whose expiry would leave
the unit in a state *worse* than a reset -- the clock tree post-condition and
the watchdog sync -- fail loudly through `bl_dfu_teardown_failed()`
(`NVIC_SystemReset()`), so the unit reboots instead of jumping into the ROM
with a clock it cannot service or a silently disarmed net. Between step 2 and
step 9 the unit runs with interrupts disabled and, on the strap route, **no**
watchdog net, so an unbounded wait on a stuck flag would be a hang that only
removing power clears.

That failure reset is reached only when a teardown step **genuinely fails**;
every entry route that completes the teardown hands over to the ROM and resets
nothing. When it is reached it is a recovery on the software-request and
recovery-offer routes: the persisted request was already consumed before the
teardown, so the reboot boots the application and the user can retry. It is
**not** a recovery on the strap route -- `main()` re-samples PE11 before
`HAL_Init()`, and with the strap still held (holding it is how the user got
there) the unit re-enters `bl_jump_to_dfu()`, hits the same persistent failure
(a dead LSI, or a clock tree that never settles) and resets again: an
**unbounded reset loop** with interrupts off and no logs, ended only by
releasing UP or removing power. The loop is a consequence of the persistent
failure plus the held strap, not of strap entry as such. The
policy is deliberate -- jumping into the ROM with a dead LSI or a half-torn
clock tree is worse than rebooting loudly, and there is no spare
backup-register budget for a persistent-failure attempt counter -- and
checklist item 5 below records how a technician tells this loop from a boot
hang.

The legacy fork did not tear the clock tree down; it is done here because
the issue #41 acceptance criterion requires the application clock tree to be
dropped before the jump and it removes any chance of the ROM inheriting a PLL
it cannot service. What the teardown normalises is what the ST ROM's own clock
init depends on (SYSCLK on HSI, prescalers /1, HSE/CSI/HSI48 and PLL1-3 off,
PLL config registers at reset values, RCC interrupts and flags cleared,
`SystemCoreClock = HSI_VALUE`), and the ROM brings up the HSI48 it needs for USB
itself.

It is **not** "exactly the state a hardware reset leaves behind". What is
deliberately left un-normalised:

- `FLASH->ACR` keeps the application's 3 wait states, where `HAL_RCC_DeInit()`
  would have restored `FLASH_LATENCY_DEFAULT` at both ends -- over-wait-stated
  for a 64 MHz HSI SYSCLK, so the safe direction;
- the `RCC_CCIPR*` kernel-clock muxes keep the application's selection. That is
  **not** a deviation from the HAL: `HAL_RCC_DeInit()` does not reset them
  either (it never writes a `CCIPR` register), so both hand the ROM the
  application's mux selection and the ROM re-initialises what it uses;
- the MPU stays enabled with region 0 read-only over `0x08FFF800-0x08FFF80C`
  and `PRIVDEFENA` set, and the ICACHE control register keeps the application's
  configuration even though caching is switched off at step 8. Benign for a
  privileged ROM, but not a reset-equivalent machine state.

So the two entry routes reach the ROM in **different** machine states: the
strap route runs before `HAL_Init()`, `MPU_Config()` and `MX_ICACHE_Init()`
(MPU off, ICACHE never enabled, no HAL tick), while the software-request and
recovery-offer routes come from the fully initialised application (MPU on,
ICACHE enabled and then disabled at step 8). Checklist item 6 requires both
routes to be exercised separately.

No step programs option bytes: the legacy boot-time IWDG
option-byte rewrite (`HAL_FLASHEx_OBProgram` + `HAL_FLASH_OB_Launch`) was a
verified defect and is deliberately **not** ported -- every entry path above is
read-only with respect to option bytes.

### Hardware qualification checklist (issue #41 DFU gate)

None of this is executable in CI. It is tracked as the issue #41 hardware gate
(milestone "Post-merge stabilization"):

1. **Strap entry**: hold UP at power-on -> speaker tick (~23 ms), PD12/PD13
   (external connector pins 6-7) driven high, the ROM DFU enumerates
   (`dfu-util -l` / STM32CubeProgrammer).
2. **Strap release**: without the strap the unit boots the application.
3. **Software request**: `dfu` -> device resets -> ROM DFU enumerates.
4. **Single-shot**: power-cycle during/after the ROM session -> the unit boots
   the application, not back into DFU.
5. **Watchdog net (it bounds the whole session, not just an abandoned one)**:
   the net is armed immediately before the jump and nothing re-feeds it
   afterwards -- not the teardown, not the ROM -- so the unit's **entire** ROM
   residency (connect, enumerate, download, verify) must complete inside
   32.768 s (LSI/256, reload 4095, LSI-dependent). A successful transfer is
   capped exactly like an abandoned session: an operator who holds UP and only
   then opens the host tool, or a slow download, sees the unit reboot
   mid-transfer and reads it as "DFU is broken". The legacy fork ran the same
   net at `PR` 7 (65.536 s), so this build halves the window -- measure it
   rather than assuming the margin. Steps: (a) **time a full-image DFU
   end-to-end** and record it against 32.768 s; (b) with no USB host attached,
   confirm the ROM session is not cut off before ~30 s and that it recovers on
   its own afterwards (the abandoned-session case); (c) confirm the net is armed
   on every route (the update wait tests the four update flags, so `0xAAAA` is
   reached); if it were not,
   the teardown's failure exit (`bl_dfu_teardown_failed()` ->
   `NVIC_SystemReset()`) would fire. That exit firing *persistently* while UP is
   still held is the only reset-loop case: the unit reset-loops back into DFU
   with no net and no logs -- confirm the loop stops as soon as the strap is
   released (or power is removed), and that the software-request route performs
   a single reset into the application rather than looping. A successful
   teardown of any route resets nothing.
6. **Teardown**: after the jump no application interrupt fires; SysTick/VTOR
   point into system memory; ICACHE shows disabled (`CR.EN == 0`). `DCACHE1->CR`
   also reads 0, but trivially: its clock gate is never enabled in this build,
   so record that read as "data cache was never on", **not** as evidence that a
   data-cache teardown ran. Exercise the **strap route and the
   software-request route separately** -- they reach the ROM in different
   machine states (strap: MPU off, ICACHE never enabled; request: MPU enabled
   with region 0 read-only + `PRIVDEFENA`, ICACHE enabled then disabled). Record
   `MPU->CTRL`, the `FLASH->ACR` latency (still 3 wait states) and the
   `RCC_CCIPR*` muxes as intentionally **not** normalised, so neither route is
   read as covering the other.
7. **Corrupt bank**: flash an image with one flipped byte -> `FW CORRUPT!` +
   offer; OK within 5 s enters DFU; no key leaves the banner and the UI is
   reachable (no hang).
8. **No option-byte churn**: `FLASH->OPTSR_CUR` is unchanged across repeated
   strap/request entries.
9. **Reflash and rollback demonstration** (deferred acceptance criterion): DFU
   a known-good image, boot it, and exercise the update + rollback path.

## Recovery and rollback status

The source contains a manual alternate-bank rollback path, but it is not part
of the supported FW 0.8.0.5 release claim and has not been hardware-qualified for
this release. Automatic failed-boot rollback is not implemented; boot-time
integrity detection and boot/swap accounting are (see "Boot failsafe" above),
and a failed boot offers bounded DFU entry (see "DFU entry" above). Supported
recovery for this release is the SD-card update path, the DFU strap/request,
the recovery-screen DFU offer, and the interactive rollback offer.

## Safety requirements

- Keep external power or adequate battery charge throughout programming.
- Never alter option bytes outside the update/rollback implementation.
- Do not ship an image whose metadata and CRC were produced from different
  binaries.
- Qualify update, interrupted update, CRC failure, invalid metadata, full/low
  battery, and the supported recovery paths on every supported hardware revision.

## Security limitations

Repository evidence shows integrity checking through CRC. CRC is not a digital
signature and does not establish publisher authenticity.

**Needs validation:** secure boot, authenticated update, anti-rollback policy,
key provisioning, and production debug-lock status are not established by the
current documentation.

## Required release evidence

Every released firmware image should have a manifest containing source commit,
version, hardware targets, region, image length, CRC value, SHA-256, toolchain,
build preset, signing status, and qualification result.
