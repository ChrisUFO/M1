## Description

This PR surgically removes FCC test code and placeholders from the M1 firmware project to reduce dead code and improve maintainability. It also addresses build errors and type conflicts that arose during the cleanup, ensuring a stable and warning-free build.

### Changes Made

- **Sub-GHz Module**:
  - Commented out the `SI446x_Start_Tx_CW` function prototype and implementation in `m1_sub_ghz_api.c` and `m1_sub_ghz_api.h`.
  - Removed the unused `sub_ghz_radio_settings` placeholder function in `m1_sub_ghz.c`.
  - Cleaned up unused data structure `RadioConfiguration_Test` in `m1_sub_ghz_api.c`.
  - Re-enabled legitimate ISM band safety checks.

- **CLI & Library Cleanup**:
  - Commented out the `mtest 61` (CW transmit) command in `m1_cli.c`.
  - Removed test-only GPIO functions (`m1_test_gpio`, `m1_test2_gpio`) in `m1_lib.c`.
  - Removed the unused `test_get_available_wifi` stub in `m1_esp_hosted_dummy.c`.
  - Handled the unused-function warnings appropriately.

- **Build Fixes**:
  - Resolved missing include statements (`FreeRTOS.h`, `cmsis_os2.h`, standard libs) in `m1_cli.h` and `m1_fw_update_bl.h`.
  - Fixed an unsupported relocation linking error caused by an undefined reference to `test_get_available_wifi` in `m1_cli.c`.
  - Verified firmwares builds cleanly (`./build`) without any issues.

- **Documentation**:
  - Bumped firmware version to `v0.8.6` in `m1_fw_update_bl.h` (and aligned CHANGELOG.md).

### Testing
- `ninja -C out/build/gcc-14_2_build-release` and `./build` script complete without errors and produce the `.bin`, `.elf`, and `.hex` binaries.
