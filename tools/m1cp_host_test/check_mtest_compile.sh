#!/bin/sh
# Syntax-only compile of the dev-guarded `mtest 9` path (issue #44, part B).
#
# No build in this tree defines M1_TEST_CMDS, so the destructive block in
# m1_csrc/m1_cli.c is only parsed when a bench engineer defines it -- a break
# there would otherwise surface on a unit under test instead of in `make test`.
# This compiles that translation unit with the macro defined and discards the
# output (-fsyntax-only): same compiler, same include set and same warning
# flags as the firmware build, no link, no artifact.
#
# Needs the ARM cross compiler the firmware itself is built with
# (arm-none-eabi-gcc on PATH, "arm-none-eabi-" prefix from
# cmake/gcc-arm-none-eabi.cmake). On a host without it the step prints SKIP with
# the reason and exits 0 -- `make test` must stay runnable on a plain host.
#
# The include directories mirror the CMake target. If a header moves to a new
# directory, this step fails with the compiler's own message; add the directory
# here (it is one list, not a second build system).

set -u

CC_ARM="${CC_ARM:-arm-none-eabi-gcc}"

if ! command -v "$CC_ARM" >/dev/null 2>&1; then
    echo "SKIP: $CC_ARM not found on PATH -- cannot compile the M1_TEST_CMDS path."
    echo "      (install the Arm GNU Toolchain used by ./build.sh, or set CC_ARM=<path>)"
    exit 0
fi

# Locate the repository root from this script's own path, so the step works
# whether it is invoked from the root or from tools/m1cp_host_test.
here=$(dirname "$0")
root=$(cd "$here/../.." && pwd) || exit 1
cd "$root" || exit 1

INC="
-I Core/Inc
-I Core/ThreadSafe
-I m1_csrc
-I Drivers/STM32H5xx_HAL_Driver/Inc
-I Drivers/STM32H5xx_HAL_Driver/Inc/Legacy
-I Drivers/CMSIS/Device/ST/STM32H5xx/Include
-I Drivers/CMSIS/Include
-I Drivers/u8g2_csrc
-I Drivers/u8g2_cppsrc
-I Drivers/BSP/Components/ST25R3916
-I Middlewares/Third_Party/FreeRTOS/Source/include
-I Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33_NTZ/non_secure
-I Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2
-I Middlewares/Third_Party/CMSIS/RTOS2/Include
-I FatFs/R015
-I Infrared
-I Infrared/irmp-irsnd
-I Sub_Ghz
-I Sub_Ghz/protocols
-I Sub_Ghz/modules
-I NFC
-I NFC/Middlewares/ST/rfal/Inc
-I NFC/NFC_drv
-I NFC/NFC_drv/common
-I NFC/NFC_drv/legacy
-I Battery
-I lfrfid
-I USB
-I USB/Class/CDC/Inc
-I USB/Class/CDC/Scr
-I USB/Class/CompositeBuilder/Inc
-I USB/Class/CompositeBuilder/Src
-I USB/Class/MSC/Inc
-I USB/Class/MSC/Src
-I USB/Class/HID/Inc
-I USB/Core/Inc
-I USB/Core/Scr
-I Esp32_serial_flasher/include
-I Esp32_serial_flasher/port
-I Esp32_serial_flasher/app/common
-I Esp32_serial_flasher/private_include
-I Esp_spi_at/esp-idf/components/esp_common/include
-I Esp_spi_at/esp-idf/components/esp_driver_spi/include/driver
-I Esp_spi_at/esp-idf/components/hal/include/hal
-I Esp_spi_at/esp-idf/components/log/include/hal
-I Esp_spi_at/esp-idf/components/soc/esp32c6/include/soc
-I Esp_spi_at/esp-idf/components/soc/esp32c6/register/soc
-I Esp_spi_at/examples/at_spi_master/spi/stm32/main
"

echo "guarded path: $CC_ARM -fsyntax-only -DM1_TEST_CMDS m1_csrc/m1_cli.c"

# shellcheck disable=SC2086  # INC is a deliberately word-split flag list
"$CC_ARM" \
    -DM1_TEST_CMDS \
    -DLFRFID_NEXWATCH_ENABLED -DLFRFID_PSK_PASS_ENABLED \
    -DM1_MFC_DMA_AT -DM1_MFC_RAW_EMULATION \
    -DSTM32H573xx -DSTM32_THREAD_SAFE_STRATEGY=4 -DUSE_HAL_DRIVER \
    -DM1_GIT_COMMIT='"local"' -DM1_GIT_DIRTY=1 \
    -std=gnu11 -mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard \
    -Wall -Wextra -Werror \
    $INC \
    -fsyntax-only m1_csrc/m1_cli.c
rc=$?

if [ "$rc" -ne 0 ]; then
    echo "FAIL: the M1_TEST_CMDS path does not compile (rc=$rc)."
    exit "$rc"
fi

# Also prove the guard is what makes it compile-out-able: the same file must
# compile without the macro (that is the shipped configuration).
# shellcheck disable=SC2086
"$CC_ARM" \
    -DLFRFID_NEXWATCH_ENABLED -DLFRFID_PSK_PASS_ENABLED \
    -DM1_MFC_DMA_AT -DM1_MFC_RAW_EMULATION \
    -DSTM32H573xx -DSTM32_THREAD_SAFE_STRATEGY=4 -DUSE_HAL_DRIVER \
    -DM1_GIT_COMMIT='"local"' -DM1_GIT_DIRTY=1 \
    -std=gnu11 -mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard \
    -Wall -Wextra -Werror \
    $INC \
    -fsyntax-only m1_csrc/m1_cli.c
rc=$?

if [ "$rc" -ne 0 ]; then
    echo "FAIL: m1_csrc/m1_cli.c does not compile without M1_TEST_CMDS (rc=$rc)."
    exit "$rc"
fi

echo "ok: M1_TEST_CMDS path and shipped configuration both compile"
exit 0
