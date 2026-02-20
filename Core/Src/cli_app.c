/*
FreeRTOS+CLI is released under the following MIT license.

Copyright (C) 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

// https://github.com/FreeRTOS/FreeRTOS/tree/main/FreeRTOS-Plus/Source/FreeRTOS-Plus-CLI

#ifndef CLI_COMMANDS_H
#define CLI_COMMANDS_H

#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "FreeRTOS_CLI.h"
#include "ff.h"
#include "m1_bq27421.h"
#include "m1_cli.h"
#include "m1_esp32_hal.h"
#include "m1_fw_update_bl.h"
#include "m1_log_debug.h"
#include "m1_system.h"
#include "main.h"
#include "stdbool.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "task.h"


#define USING_VS_CODE_TERMINAL 0
#define USING_OTHER_TERMINAL 1 // e.g. Putty, TerraTerm

char cOutputBuffer[configCOMMAND_INT_MAX_OUTPUT_SIZE],
    pcInputString[MAX_INPUT_LENGTH];
extern const CLI_Command_Definition_t xCommandList[];
int8_t cRxedChar;
const char *cli_prompt = "\r\ncli> ";
/* CLI escape sequences*/
uint8_t backspace[] = "\b \b";
uint8_t backspace_tt[] = " \b";

BaseType_t cmd_clearScreen(char *pcWriteBuffer, size_t xWriteBufferLen,
                           const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_clearScreen_help(void);
void vRegisterCLICommands(void);
void cliWrite(const char *str);
void handleNewline(const char *const pcInputString, char *cOutputBuffer,
                   uint8_t *cInputIndex);
void handleBackspace(uint8_t *cInputIndex, char *pcInputString);
void handleCharacterInput(uint8_t *cInputIndex, char *pcInputString);

BaseType_t cmd_log(char *pcWriteBuffer, size_t xWriteBufferLen,
                   const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_log_help(void);
BaseType_t cmd_status(char *pcWriteBuffer, size_t xWriteBufferLen,
                      const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_status_help(void);
BaseType_t cmd_version(char *pcWriteBuffer, size_t xWriteBufferLen,
                       const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_version_help(void);
BaseType_t cmd_sdcard(char *pcWriteBuffer, size_t xWriteBufferLen,
                      const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_sdcard_help(void);
BaseType_t cmd_wifi(char *pcWriteBuffer, size_t xWriteBufferLen,
                    const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_wifi_help(void);
BaseType_t cmd_battery(char *pcWriteBuffer, size_t xWriteBufferLen,
                       const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_battery_help(void);
BaseType_t cmd_reboot(char *pcWriteBuffer, size_t xWriteBufferLen,
                      const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_reboot_help(void);
BaseType_t cmd_memory(char *pcWriteBuffer, size_t xWriteBufferLen,
                      const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_memory_help(void);

const CLI_Command_Definition_t xCommandList[] = {
    {.pcCommand = "cls",
     .pcHelpString = "cls:\r\n Clears screen\r\n\r\n",
     .pxCommandInterpreter = cmd_clearScreen,
     .pxCommandHelper = cmd_clearScreen_help,
     .cExpectedNumberOfParameters = 0},
    {.pcCommand = "mtest",
     .pcHelpString = "mtest:\r\nThis is the multi-purpose test command\r\n\r\n",
     .pxCommandInterpreter = cmd_m1_mtest,
     .pxCommandHelper = cmd_m1_mtest_help,
     .cExpectedNumberOfParameters = -1},
    {.pcCommand = "log",
     .pcHelpString = "log:\r\n Shows recent log messages\r\n\r\n",
     .pxCommandInterpreter = cmd_log,
     .pxCommandHelper = cmd_log_help,
     .cExpectedNumberOfParameters = 0},
    {.pcCommand = "status",
     .pcHelpString = "status:\r\n Shows system status\r\n\r\n",
     .pxCommandInterpreter = cmd_status,
     .pxCommandHelper = cmd_status_help,
     .cExpectedNumberOfParameters = 0},
    {.pcCommand = "version",
     .pcHelpString = "version:\r\n Shows detailed version info\r\n\r\n",
     .pxCommandInterpreter = cmd_version,
     .pxCommandHelper = cmd_version_help,
     .cExpectedNumberOfParameters = 0},
    {.pcCommand = "sdcard",
     .pcHelpString = "sdcard:\r\n Shows SD card info\r\n\r\n",
     .pxCommandInterpreter = cmd_sdcard,
     .pxCommandHelper = cmd_sdcard_help,
     .cExpectedNumberOfParameters = 0},
    {.pcCommand = "wifi",
     .pcHelpString = "wifi:\r\n Shows WiFi status\r\n\r\n",
     .pxCommandInterpreter = cmd_wifi,
     .pxCommandHelper = cmd_wifi_help,
     .cExpectedNumberOfParameters = 0},
    {.pcCommand = "battery",
     .pcHelpString = "battery:\r\n Shows battery status\r\n\r\n",
     .pxCommandInterpreter = cmd_battery,
     .pxCommandHelper = cmd_battery_help,
     .cExpectedNumberOfParameters = 0},
    {.pcCommand = "reboot",
     .pcHelpString = "reboot:\r\n Reboots the device\r\n\r\n",
     .pxCommandInterpreter = cmd_reboot,
     .pxCommandHelper = cmd_reboot_help,
     .cExpectedNumberOfParameters = 0},
    {.pcCommand = "memory",
     .pcHelpString = "memory:\r\n Shows memory usage\r\n\r\n",
     .pxCommandInterpreter = cmd_memory,
     .pxCommandHelper = cmd_memory_help,
     .cExpectedNumberOfParameters = 0},
    {
        .pcCommand = "dfu", /* The command string to type. */
        .pcHelpString = "dfu:\r\n Reboot to USB DFU mode\r\n\r\n",
        .pxCommandInterpreter = cmd_dfu, /* The function to run. */
        .pxCommandHelper = cmd_dfu_help, /* Help for the function. */
        .cExpectedNumberOfParameters = 0 /* No parameters are expected. */
    },
    {.pcCommand = NULL}};

/*============================================================================*/
/*
 * Command Line Interface handler task
 *
 */
/*============================================================================*/
void vCommandConsoleTask(void *pvParameters) {
  uint8_t cInputIndex =
      0; // simply used to keep track of the index of the input string
  uint32_t
      receivedValue; // used to store the received value from the notification

  UNUSED(pvParameters);
  vRegisterCLICommands();

  for (;;) {
    xTaskNotifyWait(pdFALSE,        // Don't clear bits on entry
                    0,              // Clear all bits on exit
                    &receivedValue, // Receives the notification value
                    portMAX_DELAY); // Wait indefinitely
    // echo recevied char
    cRxedChar = receivedValue & 0xFF;
    cliWrite((char *)&cRxedChar);
    if (cRxedChar == '\r' || cRxedChar == '\n') {
      // user pressed enter, process the command
      handleNewline(pcInputString, cOutputBuffer, &cInputIndex);
    } else {
      // user pressed a character add it to the input string
      handleCharacterInput(&cInputIndex, pcInputString);
    }
  }
} // void vCommandConsoleTask(void *pvParameters)

/*============================================================================*/
/*
 * CLI command: Clear Screen
 *
 */
/*============================================================================*/
BaseType_t cmd_clearScreen(char *pcWriteBuffer, size_t xWriteBufferLen,
                           const char *pcCommandString, uint8_t num_of_params) {
  (void)
      num_of_params; /* Unused: stub for future work. May need removal later. */
  /* Remove compile time warnings about unused parameters, and check the
      write buffer is not NULL.  NOTE - for simplicity, this example assumes the
      write buffer length is adequate, so does not check for buffer overflows.
   */
  (void)pcCommandString;
  (void)xWriteBufferLen;
  memset(pcWriteBuffer, 0x00, xWriteBufferLen);
  printf("\033[2J\033[1;1H");
  return pdFALSE;
} // BaseType_t cmd_clearScreen(char *pcWriteBuffer, size_t xWriteBufferLen,
  // const char *pcCommandString, uint8_t num_of_params)

/*============================================================================*/
/*
 * Hlep for the CLI command: Clear Screen
 *
 */
/*============================================================================*/
BaseType_t cmd_clearScreen_help(void) {
  return pdFALSE;
} // BaseType_t cmd_clearScreen_help(void)

/*============================================================================*/
/*
 * Register the list of commands
 *
 */
/*============================================================================*/
void vRegisterCLICommands(void) {
  // itterate through the list of commands and register them
  for (int i = 0; xCommandList[i].pcCommand != NULL; i++) {
    FreeRTOS_CLIRegisterCommand(&xCommandList[i]);
  }
} // void vRegisterCLICommands(void)

/*============================================================================*/
/*
 * Write to CLI UART
 *
 */
/*============================================================================*/
void cliWrite(const char *str) {
  printf("%s", str);
  fflush(stdout);
} // void cliWrite(const char *str)

/*============================================================================*/
/*
 * Handle the CR + LF from the input CLI command
 *
 */
/*============================================================================*/
void handleNewline(const char *const pcInputString, char *cOutputBuffer,
                   uint8_t *cInputIndex) {
  cliWrite("\r\n");

  BaseType_t xMoreDataToFollow;
  do {
    xMoreDataToFollow = FreeRTOS_CLIProcessCommand(
        pcInputString, cOutputBuffer, configCOMMAND_INT_MAX_OUTPUT_SIZE);
    cliWrite(cOutputBuffer);
    *cOutputBuffer = 0x00; // Clear string after use
  } while (xMoreDataToFollow != pdFALSE);

  cliWrite(cli_prompt);
  *cInputIndex = 0;
  memset((void *)pcInputString, 0x00, MAX_INPUT_LENGTH);
} // void handleNewline(const char *const pcInputString, char *cOutputBuffer,
  // uint8_t *cInputIndex)

/*============================================================================*/
/*
 * Handle the backspace from the input CLI command
 *
 */
/*============================================================================*/
void handleBackspace(uint8_t *cInputIndex, char *pcInputString) {
  if (*cInputIndex > 0) {
    (*cInputIndex)--;
    pcInputString[*cInputIndex] = '\0';

#if USING_VS_CODE_TERMINAL
    cliWrite((char *)backspace);
#elif USING_OTHER_TERMINAL
    cliWrite((char *)backspace_tt);
#endif
  } else {
#if USING_OTHER_TERMINAL
    uint8_t right[] = "\x1b\x5b\x43";
    cliWrite((char *)right);
#endif
  }
} // void handleBackspace(uint8_t *cInputIndex, char *pcInputString)

/*============================================================================*/
/*
 * Handle a character from the input CLI command
 *
 */
/*============================================================================*/
void handleCharacterInput(uint8_t *cInputIndex, char *pcInputString) {
  if (cRxedChar == '\r') {
    return;
  } else if (cRxedChar == (uint8_t)0x08 || cRxedChar == (uint8_t)0x7F) {
    handleBackspace(cInputIndex, pcInputString);
  } else {
    if (*cInputIndex < MAX_INPUT_LENGTH) {
      pcInputString[*cInputIndex] = cRxedChar;
      (*cInputIndex)++;
    }
  }
} // void handleCharacterInput(uint8_t *cInputIndex, char *pcInputString)

/*============================================================================*/
/*
 * CLI command: Show recent log messages from capture buffer
 */
/*============================================================================*/
static char log_output_buf[2048];
static size_t log_output_len = 0;
static size_t log_output_pos = 0;

BaseType_t cmd_log(char *pcWriteBuffer, size_t xWriteBufferLen,
                   const char *pcCommandString, uint8_t num_of_params) {
  (void)pcCommandString;
  (void)num_of_params;

  /* On first call, capture the log snapshot */
  if (log_output_len == 0) {
    log_output_len =
        m1_logdb_get_recent(log_output_buf, sizeof(log_output_buf));
    log_output_pos = 0;
    if (log_output_len == 0) {
      (void)snprintf(pcWriteBuffer, xWriteBufferLen,
                     "Recent log messages:\r\n"
                     "(No log messages captured)\r\n");
      return pdFALSE;
    }
    /* Print header */
    (void)snprintf(pcWriteBuffer, xWriteBufferLen,
                   "Recent log messages (%u bytes):\r\n",
                   (unsigned)log_output_len);
    return pdTRUE; /* More data to follow */
  }

  /* Subsequent calls: output in chunks */
  size_t remaining = log_output_len - log_output_pos;
  size_t chunk =
      (remaining < (xWriteBufferLen - 1)) ? remaining : (xWriteBufferLen - 1);
  memcpy(pcWriteBuffer, log_output_buf + log_output_pos, chunk);
  pcWriteBuffer[chunk] = '\0';
  log_output_pos += chunk;

  if (log_output_pos >= log_output_len) {
    /* Done — reset for next invocation */
    log_output_len = 0;
    log_output_pos = 0;
    return pdFALSE;
  }
  return pdTRUE; /* More data to follow */
}

BaseType_t cmd_log_help(void) { return pdFALSE; }

/*============================================================================*/
/*
 * CLI command: Show system status
 */
/*============================================================================*/
BaseType_t cmd_status(char *pcWriteBuffer, size_t xWriteBufferLen,
                      const char *pcCommandString, uint8_t num_of_params) {
  (void)pcCommandString;
  (void)num_of_params;

  size_t offset = 0;
  int written;

  written = snprintf(pcWriteBuffer + offset, xWriteBufferLen - offset,
                     "System Status:\r\n");
  if (written > 0 && (size_t)written < xWriteBufferLen - offset)
    offset += written;

  written = snprintf(pcWriteBuffer + offset, xWriteBufferLen - offset,
                     "  Firmware: v%d.%d.%d.%d\r\n",
                     m1_device_stat.config.fw_version_major,
                     m1_device_stat.config.fw_version_minor,
                     m1_device_stat.config.fw_version_build,
                     m1_device_stat.config.fw_version_rc);
  if (written > 0 && (size_t)written < xWriteBufferLen - offset)
    offset += written;

  written = snprintf(pcWriteBuffer + offset, xWriteBufferLen - offset,
                     "  Active Bank: %d\r\n",
                     (m1_device_stat.active_bank == BANK1_ACTIVE) ? 1 : 2);
  if (written > 0 && (size_t)written < xWriteBufferLen - offset)
    offset += written;

  return pdFALSE;
}

BaseType_t cmd_status_help(void) { return pdFALSE; }

/*============================================================================*/
/*
 * CLI command: Show version info
 */
/*============================================================================*/
BaseType_t cmd_version(char *pcWriteBuffer, size_t xWriteBufferLen,
                       const char *pcCommandString, uint8_t num_of_params) {
  (void)pcCommandString;
  (void)num_of_params;

  size_t offset = 0;
  int written;

  written = snprintf(pcWriteBuffer + offset, xWriteBufferLen - offset,
                     "M1 Firmware Version:\r\n");
  if (written > 0 && (size_t)written < xWriteBufferLen - offset)
    offset += written;

  written =
      snprintf(pcWriteBuffer + offset, xWriteBufferLen - offset,
               "  %d.%d.%d.%d-%s\r\n", m1_device_stat.config.fw_version_major,
               m1_device_stat.config.fw_version_minor,
               m1_device_stat.config.fw_version_build,
               m1_device_stat.config.fw_version_rc, FW_VERSION_FORK_TAG);
  if (written > 0 && (size_t)written < xWriteBufferLen - offset)
    offset += written;

  return pdFALSE;
}

BaseType_t cmd_version_help(void) { return pdFALSE; }

/*============================================================================*/
/*
 * CLI command: Show SD card info via FatFS
 */
/*============================================================================*/
BaseType_t cmd_sdcard(char *pcWriteBuffer, size_t xWriteBufferLen,
                      const char *pcCommandString, uint8_t num_of_params) {
  (void)pcCommandString;
  (void)num_of_params;

  size_t offset = 0;
  int written;
  FATFS *fs;
  DWORD fre_clust, fre_sect, tot_sect;
  FRESULT fres;

  written = snprintf(pcWriteBuffer + offset, xWriteBufferLen - offset,
                     "SD Card Status:\r\n");
  if (written > 0 && (size_t)written < xWriteBufferLen - offset)
    offset += (size_t)written;

  fres = f_getfree("0:", &fre_clust, &fs);
  if (fres != FR_OK) {
    written = snprintf(pcWriteBuffer + offset, xWriteBufferLen - offset,
                       "  Status: Not available (err %d)\r\n", (int)fres);
    if (written > 0 && (size_t)written < xWriteBufferLen - offset)
      offset += (size_t)written;
    return pdFALSE;
  }

  /* Calculate sector counts */
  tot_sect = (fs->n_fatent - 2) * fs->csize;
  fre_sect = fre_clust * fs->csize;

  written = snprintf(pcWriteBuffer + offset, xWriteBufferLen - offset,
                     "  Status: Mounted\r\n");
  if (written > 0 && (size_t)written < xWriteBufferLen - offset)
    offset += (size_t)written;

  written = snprintf(pcWriteBuffer + offset, xWriteBufferLen - offset,
                     "  Total:  %lu MB\r\n", (unsigned long)(tot_sect / 2048));
  if (written > 0 && (size_t)written < xWriteBufferLen - offset)
    offset += (size_t)written;

  written = snprintf(pcWriteBuffer + offset, xWriteBufferLen - offset,
                     "  Free:   %lu MB\r\n", (unsigned long)(fre_sect / 2048));
  if (written > 0 && (size_t)written < xWriteBufferLen - offset)
    offset += (size_t)written;

  written = snprintf(
      pcWriteBuffer + offset, xWriteBufferLen - offset,
      "  Used:   %lu MB (%lu%%)\r\n",
      (unsigned long)((tot_sect - fre_sect) / 2048),
      (unsigned long)(tot_sect ? ((tot_sect - fre_sect) * 100 / tot_sect) : 0));
  if (written > 0 && (size_t)written < xWriteBufferLen - offset)
    offset += (size_t)written;

  return pdFALSE;
}

BaseType_t cmd_sdcard_help(void) { return pdFALSE; }

/*============================================================================*/
/*
 * CLI command: Show WiFi status
 */
/*============================================================================*/
BaseType_t cmd_wifi(char *pcWriteBuffer, size_t xWriteBufferLen,
                    const char *pcCommandString, uint8_t num_of_params) {
  (void)pcCommandString;
  (void)num_of_params;

  size_t offset = 0;
  int written;

  written = snprintf(pcWriteBuffer + offset, xWriteBufferLen - offset,
                     "WiFi Status:\r\n");
  if (written > 0 && (size_t)written < xWriteBufferLen - offset)
    offset += (size_t)written;

  written = snprintf(pcWriteBuffer + offset, xWriteBufferLen - offset,
                     "  ESP32: %s\r\n",
                     m1_esp32_get_init_status() ? "Ready" : "Not Ready");
  if (written > 0 && (size_t)written < xWriteBufferLen - offset)
    offset += (size_t)written;

  return pdFALSE;
}

BaseType_t cmd_wifi_help(void) { return pdFALSE; }

/*============================================================================*/
/*
 * CLI command: Show battery status
 */
/*============================================================================*/
BaseType_t cmd_battery(char *pcWriteBuffer, size_t xWriteBufferLen,
                       const char *pcCommandString, uint8_t num_of_params) {
  (void)pcCommandString;
  (void)num_of_params;

  uint16_t voltage = 0;
  uint16_t capacity = 0;
  uint16_t full_capacity = 0;
  uint8_t soc = 0;

  // Read battery voltage
  bq27421_readVoltage_mV(&voltage);

  // Read State of Charge (percentage)
  soc = (uint8_t)bq27421_soc(FILTERED);

  // Read remaining capacity
  bq27421_readRemainingCapacity_mAh(&capacity);

  // Read full charge capacity
  bq27421_readFullChargeCapacity_mAh(&full_capacity);

  size_t offset = 0;
  int written;

  written = snprintf(pcWriteBuffer + offset, xWriteBufferLen - offset,
                     "Battery Status:\r\n");
  if (written > 0 && (size_t)written < xWriteBufferLen - offset)
    offset += (size_t)written;

  written = snprintf(pcWriteBuffer + offset, xWriteBufferLen - offset,
                     "  Level: %d%%\r\n", soc);
  if (written > 0 && (size_t)written < xWriteBufferLen - offset)
    offset += (size_t)written;

  written = snprintf(pcWriteBuffer + offset, xWriteBufferLen - offset,
                     "  Voltage: %d mV\r\n", voltage);
  if (written > 0 && (size_t)written < xWriteBufferLen - offset)
    offset += (size_t)written;

  written = snprintf(pcWriteBuffer + offset, xWriteBufferLen - offset,
                     "  Capacity: %d/%d mAh\r\n", capacity, full_capacity);
  if (written > 0 && (size_t)written < xWriteBufferLen - offset)
    offset += (size_t)written;

  return pdFALSE;
}

BaseType_t cmd_battery_help(void) { return pdFALSE; }

/*============================================================================*/
/*
 * CLI command: Reboot device
 */
/*============================================================================*/
BaseType_t cmd_reboot(char *pcWriteBuffer, size_t xWriteBufferLen,
                      const char *pcCommandString, uint8_t num_of_params) {
  (void)pcCommandString;
  (void)num_of_params;

  (void)snprintf(pcWriteBuffer, xWriteBufferLen, "Rebooting...\r\n");
  // Trigger system reset
  NVIC_SystemReset();

  return pdFALSE;
}

BaseType_t cmd_reboot_help(void) { return pdFALSE; }

/*============================================================================*/
/*
 * CLI command: Show memory usage from FreeRTOS heap
 */
/*============================================================================*/
BaseType_t cmd_memory(char *pcWriteBuffer, size_t xWriteBufferLen,
                      const char *pcCommandString, uint8_t num_of_params) {
  (void)pcCommandString;
  (void)num_of_params;

  size_t free_heap = xPortGetFreeHeapSize();
  size_t min_ever_free = xPortGetMinimumEverFreeHeapSize();
  size_t total_heap = (size_t)configTOTAL_HEAP_SIZE;
  size_t used_heap = total_heap - free_heap;
  UBaseType_t num_tasks = uxTaskGetNumberOfTasks();

  size_t offset = 0;
  int written;

  written = snprintf(pcWriteBuffer + offset, xWriteBufferLen - offset,
                     "Memory Status:\r\n");
  if (written > 0 && (size_t)written < xWriteBufferLen - offset)
    offset += (size_t)written;

  written =
      snprintf(pcWriteBuffer + offset, xWriteBufferLen - offset,
               "  Free heap:     %lu bytes\r\n", (unsigned long)free_heap);
  if (written > 0 && (size_t)written < xWriteBufferLen - offset)
    offset += (size_t)written;

  written =
      snprintf(pcWriteBuffer + offset, xWriteBufferLen - offset,
               "  Min ever free: %lu bytes\r\n", (unsigned long)min_ever_free);
  if (written > 0 && (size_t)written < xWriteBufferLen - offset)
    offset += (size_t)written;

  written =
      snprintf(pcWriteBuffer + offset, xWriteBufferLen - offset,
               "  Total heap:    %lu bytes\r\n", (unsigned long)total_heap);
  if (written > 0 && (size_t)written < xWriteBufferLen - offset)
    offset += (size_t)written;

  written = snprintf(
      pcWriteBuffer + offset, xWriteBufferLen - offset,
      "  Used:          %lu bytes (%lu%%)\r\n", (unsigned long)used_heap,
      (unsigned long)(total_heap ? (used_heap * 100 / total_heap) : 0));
  if (written > 0 && (size_t)written < xWriteBufferLen - offset)
    offset += (size_t)written;

  written = snprintf(pcWriteBuffer + offset, xWriteBufferLen - offset,
                     "  Tasks:         %lu\r\n", (unsigned long)num_tasks);
  if (written > 0 && (size_t)written < xWriteBufferLen - offset)
    offset += (size_t)written;

  return pdFALSE;
}

BaseType_t cmd_memory_help(void) { return pdFALSE; }

#endif /* CLI_COMMANDS_H */
