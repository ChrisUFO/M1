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
automatic; the *recovery action* is not. On a failure the boot stops at
`FW CORRUPT!` and the fault is recorded, but:

- the unvalidated image is **not** prevented from running — execution continues
  into the application exactly as on a healthy boot,
- `FW CORRUPT!` is **not** a lock-out screen: the normal UI is one key press
  away. The message leaves the display mode in `M1_OPERATION_MODE_DISPLAY_ON`,
  and the first OK press takes `m1_menu.c` into `menu_main_init()`, which
  redraws the main menu over the banner,
- no automatic recovery action runs until the DFU unit of issue #41 (#41-B)
  fills that seam with `bl_jump_to_dfu()`; it currently only logs.

Treat `FW CORRUPT!` as a diagnostic banner rather than containment. What it
does give you is durable evidence: `boot_fault_signature` and `boot_attempts`
survive a power cycle in DR1, so a technician can distinguish an erased metadata
block (magic/header fault) from a genuinely corrupted image (CRC fault). The
supported ways out of a failed bank for this release are service DFU and the
SD-card update path (plus the interactive rollback offer below). Automatic DFU
entry lands with #41-B, and on-device qualification of the whole corrupt-bank
path is the issue #41 hardware gate (milestone "Post-merge stabilization").

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
the recovery screen and the eventual DFU entry -- is tracked as the issue #41
hardware gate (milestone "Post-merge stabilization"); the host suite covers the
span arithmetic, the CRC decision, every fault class, the counters and the swap
guard, and signs/injects a synthetic `0xFFC14`-byte bank with the build's own CRC
algorithm (valid, flipped byte, zeroed magic, relocated CRC word).

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

## Recovery and rollback status

The source contains a manual alternate-bank rollback path, but it is not part
of the supported FW 0.8.0.5 release claim and has not been hardware-qualified for
this release. Automatic failed-boot rollback is not implemented; boot-time
integrity detection and boot/swap accounting are (see "Boot failsafe" above).
Supported recovery for this release is the SD-card update path, the interactive
rollback offer, and service DFU.

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
