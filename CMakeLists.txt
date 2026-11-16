# See COPYING.txt for license details.

cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

# Set the project name (artifacts inherit it). Extracted from
# m1_csrc/m1_fw_update_bl.h -- the single source of truth for the firmware
# version -- so the binary/hex/bin artifact stems always match the embedded
# version (format: M1_v<major>.<minor>.<build>.<rc>-<fork_tag>).
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/m1_csrc/m1_fw_update_bl.h VERSION_LINE_MAJOR REGEX "^#define FW_VERSION_MAJOR")
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/m1_csrc/m1_fw_update_bl.h VERSION_LINE_MINOR REGEX "^#define FW_VERSION_MINOR")
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/m1_csrc/m1_fw_update_bl.h VERSION_LINE_BUILD REGEX "^#define FW_VERSION_BUILD")
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/m1_csrc/m1_fw_update_bl.h VERSION_LINE_RC REGEX "^#define FW_VERSION_RC")
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/m1_csrc/m1_fw_update_bl.h VERSION_LINE_FORK_TAG REGEX "^#define FW_VERSION_FORK_TAG")

string(REGEX MATCH "[0-9]+" FW_VERSION_MAJOR ${VERSION_LINE_MAJOR})
string(REGEX MATCH "[0-9]+" FW_VERSION_MINOR ${VERSION_LINE_MINOR})
string(REGEX MATCH "[0-9]+" FW_VERSION_BUILD ${VERSION_LINE_BUILD})
string(REGEX MATCH "[0-9]+" FW_VERSION_RC ${VERSION_LINE_RC})
string(REGEX MATCH "\"[^\"]+\"" FW_VERSION_FORK_TAG_QUOTED ${VERSION_LINE_FORK_TAG})
string(REGEX REPLACE "\"" "" FW_VERSION_FORK_TAG ${FW_VERSION_FORK_TAG_QUOTED})

set(CMAKE_PROJECT_NAME M1_v${FW_VERSION_MAJOR}.${FW_VERSION_MINOR}.${FW_VERSION_BUILD}.${FW_VERSION_RC}-${FW_VERSION_FORK_TAG})
message(STATUS "Project name: ${CMAKE_PROJECT_NAME} (version single-sourced from m1_csrc/m1_fw_update_bl.h)")

# Include toolchain file
include("cmake/gcc-arm-none-eabi.cmake")

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Reproducible builds: strip the absolute source-tree prefix from __FILE__ and
# from debug paths, so two clean builds of the same commit made in different
# directories are byte-identical. A few diagnostic strings embed __FILE__
# (the custom assert_param in m1_csrc/m1_compile_cfg.h, and two M1_LOG_I calls
# in m1_csrc/m1_sdcard_man.c); this makes them source-root-relative instead of
# carrying the build machine's absolute path. Operational behavior is
# unchanged -- only the text of those diagnostic paths differs.
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
    "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
)

# Add STM32CubeMX generated sources
add_subdirectory(cmake/m1_01)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    m1_01

    # Add user defined libraries
)

# Specify the output ELF file
set(TARGET_ELF "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.elf")
# Specify the output BIN file
set(TARGET_BIN "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin")
# Command to display memory usage
add_custom_command(
    COMMAND ${CMAKE_COMMAND} -E echo "Memory usage:"    
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_SIZE} ${TARGET_ELF}
    #COMMENT "Memory usage:"
)

# Command to generate .list file
add_custom_command(
    COMMAND ${CMAKE_COMMAND} -E echo "Generating list file ${CMAKE_PROJECT_NAME}.list"
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_OBJDUMP}
-h -S ${TARGET_ELF} > ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.list
    #COMMENT "Generating .list file"
)

# Command to generate .bin file
add_custom_command(
    COMMAND ${CMAKE_COMMAND} -E echo "Generating bin file ${CMAKE_PROJECT_NAME}.bin"
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY}
-O binary ${TARGET_ELF} ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    #COMMENT "Generating .bin file"
)

# Command to generate .hex file
add_custom_command(
    COMMAND ${CMAKE_COMMAND} -E echo "Generating hex file ${CMAKE_PROJECT_NAME}.hex"
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY}
-O ihex ${TARGET_ELF} ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex
    #COMMENT "Generating .hex file"
)

# Command to generate .bin file with CRC -- ONE canonical step, TWO
# interchangeable emitters (issue #40).
#
# The CRC-bearing image is part of the release contract (the on-device update
# path verifies it), so this step must never silently no-op. srecord stays the
# authoritative reference when it is installed; otherwise the repository's
# pure-Python emulator emits the byte-identical image
# (tools/emulate_srec_cat_crc.py, parity-tested by
# scripts/verify_crc_parity.sh against this exact srec_cat invocation). If
# neither emitter exists, configuration fails loudly instead of shipping a
# build directory without ${CMAKE_PROJECT_NAME}_wCRC.bin.
#
# This block must live in this file, not cmake/m1_01/CMakeLists.txt:
# add_custom_command(TARGET ... POST_BUILD) is only valid in the directory
# scope where the target was created by add_executable().
set(M1_WCRC_BIN "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}_wCRC.bin")
find_program(SREC_CAT_EXECUTABLE srec_cat)
if(SREC_CAT_EXECUTABLE)
    set(M1_CRC_EMITTER "srec_cat (${SREC_CAT_EXECUTABLE})")
    set(M1_CRC_EMIT_COMMAND
        "${SREC_CAT_EXECUTABLE}"
        "${TARGET_BIN}" -binary -crop 0x0 0x000FFC14 -STM32_Little_Endian 0x000FFC14
        -o "${M1_WCRC_BIN}" -binary)
else()
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        set(M1_CRC_EMITTER
            "Python emulator (${Python3_EXECUTABLE} tools/emulate_srec_cat_crc.py)")
        set(M1_CRC_EMIT_COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tools/emulate_srec_cat_crc.py"
            "${TARGET_BIN}" "${M1_WCRC_BIN}" --crop-end 0xFFC14)
    else()
        message(FATAL_ERROR
            "No CRC emitter available: neither 'srec_cat' (SRecord) nor a "
            "Python 3 interpreter was found. Install SRecord so srec_cat is on "
            "PATH, or install Python 3 (the repo's tools/emulate_srec_cat_crc.py "
            "emitter produces the byte-identical image). The build deliberately "
            "fails rather than skipping ${CMAKE_PROJECT_NAME}_wCRC.bin.")
    endif()
endif()
message(STATUS "CRC emitter: ${M1_CRC_EMITTER}")
add_custom_command(
    COMMAND ${CMAKE_COMMAND} -E echo "Generating bin file with CRC ${CMAKE_PROJECT_NAME}_wCRC.bin"
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND ${M1_CRC_EMIT_COMMAND}
    VERBATIM
    #COMMENT "Generating .bin file with CRC"
)

# Post-build regression guard: when the production MFC raw-emulation flags
# are on, prove via symbol/map evidence (not log strings) that the canonical
# RAWOWN listener/dispatcher and the DMA {At} transmitter actually made it
# into the link -- fails the build if a future change silently drops or
# renames them. See cmake/m1_01/verify_mfc_raw_hwpass.sh for the exact
# symbol list. Must live in this file (not cmake/m1_01/CMakeLists.txt):
# add_custom_command(TARGET ... POST_BUILD) is only valid in the directory
# scope where the target was created by add_executable()/add_library().
find_program(M1_NM_TOOL NAMES arm-none-eabi-nm)
if(M1_MFC_RAW_EMULATION AND M1_MFC_DMA_AT)
    if(M1_NM_TOOL)
        add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
            COMMAND sh "${CMAKE_SOURCE_DIR}/cmake/m1_01/verify_mfc_raw_hwpass.sh"
                    "${M1_NM_TOOL}" "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
            COMMENT "Verifying production MFC raw-emulation symbols are linked"
            VERBATIM)
        # Single-authoritative-MFC-implementation structural guard: fails the
        # build if any obsolete generic-Type-A route or the deleted duplicate
        # Flow-B crypto backend (symbol, source pattern, or CMake option)
        # reappears, and if any REQUIRED authoritative RAWOWN/Crypto1/DMA symbol
        # is missing. Combines ELF-symbol, source, and configuration evidence
        # (see cmake/m1_01/mfc_authoritative_manifest.txt).
        add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
            COMMAND sh "${CMAKE_SOURCE_DIR}/cmake/m1_01/verify_mfc_arch.sh"
                    "${M1_NM_TOOL}" "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                    "${CMAKE_SOURCE_DIR}"
            COMMENT "Verifying single authoritative MFC implementation (no obsolete/duplicate routes)"
            VERBATIM)
    else()
        message(WARNING "arm-none-eabi-nm not found -- skipping MFC raw-emulation symbol verification")
    endif()
endif()

# Post-build forbidden-symbol guard (self-contained): fail the build if a
# known-bad symbol is linked into the production ELF. It currently guards
# against the Flipper-derived LF-RFID FSK oscillator (lfrfid_fsk_osc) silently
# reappearing in a shipped build. Pure ELF-symbol evidence via nm -- no git
# history, tag, or checkpoint dependency, so it is fully reproducible from a
# standalone public checkout.
if(M1_NM_TOOL)
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND sh "${CMAKE_SOURCE_DIR}/cmake/m1_01/verify_no_forbidden_symbols.sh"
                "${M1_NM_TOOL}" "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
        COMMENT "Verifying no forbidden symbols are linked into the production ELF"
        VERBATIM)
endif()