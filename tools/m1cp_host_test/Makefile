# See COPYING.txt for license details.
#
# Host unit tests for the M1CP firmware protocol + Add Files modules. Compiles
# the REAL m1_csrc sources natively with -DM1CP_HOST_TEST (firmware HAL/FreeRTOS/
# SD/flash deps replaced by the small stubs here, plus a real-file storage
# backend), so the parser, session logic, error model, command builders and the
# file-transfer state machine + FS-safety gate are exercised without hardware.
# Run:  make -C tools/m1cp_host_test test

CC      ?= cc
CFLAGS  ?= -DM1CP_HOST_TEST -Wall -Wextra -std=c11 -O2
SRC_DIR := ../../m1_csrc
INCS    := -I. -I$(SRC_DIR)
CORE    := $(SRC_DIR)/m1_manager_protocol.c $(SRC_DIR)/m1_manager_fs.c $(SRC_DIR)/m1_manager_fw.c $(SRC_DIR)/m1_manager_update_coordinator.c $(SRC_DIR)/m1_manager_fwupdate.c $(SRC_DIR)/m1_manager_espupdate.c $(SRC_DIR)/m1_esp_version.c
DEPS    := m1cp_test_stubs.h FreeRTOS.h $(SRC_DIR)/m1_manager_protocol.h \
           $(SRC_DIR)/m1_manager_fs.h $(SRC_DIR)/m1_manager_fw.h $(SRC_DIR)/m1_manager_update_coordinator.h \
           $(SRC_DIR)/m1_manager_fwupdate.h $(SRC_DIR)/m1_manager_espupdate.h $(SRC_DIR)/m1_esp_version.h

test: test_m1cp test_m1cp_fs test_m1cp_fw test_m1cp_update_coordinator test_m1cp_fwupdate test_m1cp_espver test_m1cp_espupdate m1_manager_espupdate_seam_test m1_manager_fwupdate_seam_test test_m1cp_failsafe test_m1cp_dfu test_update_intake_size test_cdc_rx_stats test_log_capture test_cli_commands
	./test_m1cp
	./test_m1cp_fs
	./test_m1cp_fw
	./test_m1cp_update_coordinator
	./test_m1cp_fwupdate
	./test_m1cp_espver
	./test_m1cp_espupdate
	./m1_manager_espupdate_seam_test
	./m1_manager_fwupdate_seam_test
	./test_m1cp_failsafe
	./test_m1cp_dfu
	./test_update_intake_size
	./test_cdc_rx_stats
	./test_log_capture
	./test_cli_commands
	sh test_no_raw_bkp_writes.sh

test_m1cp: test_m1cp.c $(CORE) $(DEPS)
	$(CC) $(CFLAGS) $(INCS) test_m1cp.c $(CORE) -o $@

test_m1cp_fs: test_m1cp_fs.c $(CORE) $(DEPS)
	$(CC) $(CFLAGS) $(INCS) test_m1cp_fs.c $(CORE) -o $@

test_m1cp_fw: test_m1cp_fw.c $(CORE) $(DEPS)
	$(CC) $(CFLAGS) $(INCS) test_m1cp_fw.c $(CORE) -o $@

# Standalone mock-backend test of the generic coordinator itself (not
# through either target's M1CP dispatch shim, and not linking the rest of
# m1_manager_protocol.c -- the test provides its own minimal stand-ins for
# the 3 status-publish calls the coordinator makes) -- see
# m1_manager_update_coordinator.h for what it's unifying.
test_m1cp_update_coordinator: test_m1cp_update_coordinator.c $(SRC_DIR)/m1_manager_update_coordinator.c $(DEPS)
	$(CC) $(CFLAGS) $(INCS) test_m1cp_update_coordinator.c $(SRC_DIR)/m1_manager_update_coordinator.c -o $@

test_m1cp_fwupdate: test_m1cp_fwupdate.c $(CORE) $(DEPS)
	$(CC) $(CFLAGS) $(INCS) test_m1cp_fwupdate.c $(CORE) -o $@

# Boot-time firmware integrity failsafe (issue #41-A): pure span/decision/
# classification/counter logic from m1_csrc/m1_boot_failsafe.h plus a source
# seam on the device-side running-bank CRC check (no HAL, no flash).
test_m1cp_failsafe: test_m1cp_failsafe.c $(SRC_DIR)/m1_boot_failsafe.h
	$(CC) $(CFLAGS) $(INCS) test_m1cp_failsafe.c -o $@

# USB DFU entry (issue #41-B): pure strap/SP/watchdog/request/recovery policy
# from m1_csrc/m1_dfu_entry.h plus source seams on the device-only teardown
# order, early-boot strap, request branch and recovery offer. No HAL/CMSIS.
test_m1cp_dfu: test_m1cp_dfu.c $(SRC_DIR)/m1_dfu_entry.h
	$(CC) $(CFLAGS) $(INCS) test_m1cp_dfu.c -o $@

# Update intake size gate (PR #61 F1): pure canonical-size decision plus source
# seams on both intakes (SD file path + streamed M1CP path) and the compile-time
# tie to the boot failsafe's signed span. No FatFs/HAL.
test_update_intake_size: test_update_intake_size.c $(SRC_DIR)/m1_boot_failsafe.h
	$(CC) $(CFLAGS) $(INCS) test_update_intake_size.c -o $@

# USB CDC RX diagnostics (issue #44, part A): pure drop/high-watermark
# accounting from m1_cdc_rx_stats.h replayed over a modelled stream buffer,
# plus source seams on the ISR call site and the reset critical section in
# USB/Class/CDC/Src/usbd_cdc_if.c. No HAL/FreeRTOS.
test_cdc_rx_stats: test_cdc_rx_stats.c $(SRC_DIR)/m1_cdc_rx_stats.h
	$(CC) $(CFLAGS) $(INCS) test_cdc_rx_stats.c -o $@

# Log history window (issue #44, part B): the pure, HAL-free capture window
# behind the CLI `log` command (m1_csrc/m1_log_capture.h), exercised natively --
# short content, wrapped content in order, oversized puts, wr wrap-around,
# the no-init empty state, non-destructive peeks and max_len handling.
test_log_capture: test_log_capture.c $(SRC_DIR)/m1_log_capture.h
	$(CC) $(CFLAGS) $(INCS) test_log_capture.c -o $@

# CLI command table + log capture wiring (issue #44, part B): source seams on
# Core/Src/cli_app.c and m1_csrc/m1_log_debug.c (registration, mandatory help
# strings, no orphan handlers, bounded writes, the enqueue funnel using the
# clamped count, reboot/dfu flushing before the reset). Uses seam_util.h, the
# same comment-stripping helpers test_cdc_rx_stats.c uses.
test_cli_commands: test_cli_commands.c seam_util.h
	$(CC) $(CFLAGS) $(INCS) test_cli_commands.c -o $@

test_m1cp_espver: test_m1cp_espver.c $(CORE) $(DEPS)
	$(CC) $(CFLAGS) $(INCS) test_m1cp_espver.c $(CORE) -o $@

test_m1cp_espupdate: test_m1cp_espupdate.c $(CORE) $(DEPS)
	$(CC) $(CFLAGS) $(INCS) test_m1cp_espupdate.c $(CORE) -o $@

# Source-seam tests (bounded string search, no linking) for the device-only
# firmware backends (real HAL/esp_loader/flash deps) -- see each backend
# file's own header comment.
m1_manager_espupdate_seam_test: m1_manager_espupdate_seam_test.c
	$(CC) -std=c11 -Wall -Wextra m1_manager_espupdate_seam_test.c -o $@

m1_manager_fwupdate_seam_test: m1_manager_fwupdate_seam_test.c
	$(CC) -std=c11 -Wall -Wextra m1_manager_fwupdate_seam_test.c -o $@

# Harness that runs a .m1pkg file through the real validator (fixture check).
validate_file: validate_file.c $(CORE) $(DEPS)
	$(CC) $(CFLAGS) $(INCS) validate_file.c $(CORE) -o $@

clean:
	rm -f test_m1cp test_m1cp_fs test_m1cp_fw test_m1cp_update_coordinator test_m1cp_fwupdate test_m1cp_espver test_m1cp_espupdate test_m1cp_failsafe test_m1cp_dfu test_update_intake_size test_cdc_rx_stats test_log_capture test_cli_commands validate_file m1_manager_espupdate_seam_test m1_manager_fwupdate_seam_test
	rm -rf fs_sandbox

.PHONY: test clean
