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

//https://github.com/FreeRTOS/FreeRTOS/tree/main/FreeRTOS-Plus/Source/FreeRTOS-Plus-CLI

#ifndef CLI_COMMANDS_H
#define CLI_COMMANDS_H

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"
#include "stdbool.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "m1_log_debug.h"
#include "m1_log_capture.h"        /* M1_LOG_CAPTURE_SIZE: the `log` window */
#include "m1_sdcard_capacity.h"    /* m1_sdcard_used_percent(): 64-bit capacity math */
#include "m1_cli.h"
#include "m1_system.h"             /* m1_device_stat (status/version) */
#include "m1_fw_update_bl.h"       /* BANK1_ACTIVE, FW_VERSION_FORK_TAG */
#include "m1_fw_update.h"          /* firmware_update_enter_usb_dfu() */
#include "m1_sdcard.h"             /* m1_sdcard_get_info()/status (FatFs-backed) */
#include "m1_esp32_hal.h"          /* m1_esp32_get_init_status() */
#include "m1_bq27421.h"            /* battery gauge accessors */
#include "usbd_cdc_if.h"           /* CDC_Transmit_FS for the M1CP send adapter */
#include "m1_manager_protocol.h"   /* M1CP init/process host */
#include "usbd_cdc_if.h"

#define MAX_INPUT_LENGTH 		64
#define USING_VS_CODE_TERMINAL 	0
#define USING_OTHER_TERMINAL 	1 // e.g. Putty, TerraTerm

char cOutputBuffer[configCOMMAND_INT_MAX_OUTPUT_SIZE], pcInputString[MAX_INPUT_LENGTH];
extern const CLI_Command_Definition_t xCommandList[];
int8_t cRxedChar;
const char * cli_prompt = "\r\ncli> ";
/* CLI escape sequences*/
uint8_t backspace[] = "\b \b";
uint8_t backspace_tt[] = " \b";

volatile uint32_t dbg_cli_task_wake = 0;

/* M1CP transmit adapter: send a protocol frame over CDC. Bounded, non-blocking
 * retry while the CDC IN endpoint is busy (task context only). */
static int m1cp_cdc_send(const uint8_t *data, uint16_t len)
{
    uint8_t r;
    uint8_t tries;
    for (tries = 0U; tries < 8U; tries++)
    {
        r = CDC_Transmit_FS((uint8_t *)data, len);
        if (r != (uint8_t)USBD_BUSY)
        {
            return (r == (uint8_t)USBD_OK) ? (int)len : -1;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return -1;
}

volatile uint32_t dbg_cli_fifo_bytes = 0;
volatile uint32_t dbg_cli_fallback_chars = 0;
volatile uint32_t dbg_cli_newline = 0;

BaseType_t cmd_clearScreen(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_clearScreen_help(void);
BaseType_t cmd_log(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_log_help(void);
BaseType_t cmd_status(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_status_help(void);
BaseType_t cmd_version(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_version_help(void);
BaseType_t cmd_sdcard(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_sdcard_help(void);
BaseType_t cmd_wifi(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_wifi_help(void);
BaseType_t cmd_battery(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_battery_help(void);
BaseType_t cmd_reboot(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_reboot_help(void);
BaseType_t cmd_memory(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_memory_help(void);
BaseType_t cmd_dfu(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_dfu_help(void);
BaseType_t cmd_cdcstats(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_cdcstats_help(void);
BaseType_t cmd_cdcreset(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_cdcreset_help(void);
void vRegisterCLICommands(void);
void cliWrite(const char *str);
void handleNewline(const char *const pcInputString, char *cOutputBuffer, uint8_t *cInputIndex);
void handleBackspace(uint8_t *cInputIndex, char *pcInputString);
void handleCharacterInput(uint8_t *cInputIndex, char *pcInputString);

const CLI_Command_Definition_t xCommandList[] = {
    {
        .pcCommand = "cls", /* The command string to type. */
        .pcHelpString = "cls:\r\n Clears screen\r\n\r\n",
        .pxCommandInterpreter = cmd_clearScreen, /* The function to run. */
		.pxCommandHelper = cmd_clearScreen_help, /* Help for the function */
        .cExpectedNumberOfParameters = 0 /* No parameters are expected. */
    },
    {
        .pcCommand = "mtest", /* The command string to type. */
        .pcHelpString = "mtest:\r\nThis is the multi-purpose test command\r\n\r\n",
        .pxCommandInterpreter = cmd_m1_mtest, /* The function to run. */
		.pxCommandHelper = cmd_m1_mtest_help, /* Help for the function. */
        .cExpectedNumberOfParameters = -1 /* variable parameters are expected. */
    },
    {
        .pcCommand = "log", /* The command string to type. */
        .pcHelpString = "log:\r\n Shows the last 2 KB of log output at command time. Best effort: output from before the log transport was ready is missing.\r\n\r\n",
        .pxCommandInterpreter = cmd_log, /* The function to run. */
        .pxCommandHelper = cmd_log_help, /* Help for the function. */
        .cExpectedNumberOfParameters = 0 /* No parameters are expected. */
    },
    {
        .pcCommand = "status", /* The command string to type. */
        .pcHelpString = "status:\r\n Shows firmware version, active bank and USB CDC RX state (refused-at-arrival, lost, buffered-now)\r\n\r\n",
        .pxCommandInterpreter = cmd_status, /* The function to run. */
        .pxCommandHelper = cmd_status_help, /* Help for the function. */
        .cExpectedNumberOfParameters = 0 /* No parameters are expected. */
    },
    {
        .pcCommand = "version", /* The command string to type. */
        .pcHelpString = "version:\r\n Shows detailed version info (numeric version plus fork tag) and the active bank\r\n\r\n",
        .pxCommandInterpreter = cmd_version, /* The function to run. */
        .pxCommandHelper = cmd_version_help, /* Help for the function. */
        .cExpectedNumberOfParameters = 0 /* No parameters are expected. */
    },
    {
        .pcCommand = "sdcard", /* The command string to type. */
        .pcHelpString = "sdcard:\r\n Shows SD card mount status, total/free/used capacity\r\n\r\n",
        .pxCommandInterpreter = cmd_sdcard, /* The function to run. */
        .pxCommandHelper = cmd_sdcard_help, /* Help for the function. */
        .cExpectedNumberOfParameters = 0 /* No parameters are expected. */
    },
    {
        .pcCommand = "wifi", /* The command string to type. */
        .pcHelpString = "wifi:\r\n Shows ESP32 (WiFi/BLE co-processor) init status\r\n\r\n",
        .pxCommandInterpreter = cmd_wifi, /* The function to run. */
        .pxCommandHelper = cmd_wifi_help, /* Help for the function. */
        .cExpectedNumberOfParameters = 0 /* No parameters are expected. */
    },
    {
        .pcCommand = "battery", /* The command string to type. */
        .pcHelpString = "battery:\r\n Shows battery level, voltage and remaining/full capacity from the BQ27421 gauge\r\n\r\n",
        .pxCommandInterpreter = cmd_battery, /* The function to run. */
        .pxCommandHelper = cmd_battery_help, /* Help for the function. */
        .cExpectedNumberOfParameters = 0 /* No parameters are expected. */
    },
    {
        .pcCommand = "reboot", /* The command string to type. */
        .pcHelpString = "reboot:\r\n Reboots the device (acknowledges first, then resets)\r\n\r\n",
        .pxCommandInterpreter = cmd_reboot, /* The function to run. */
        .pxCommandHelper = cmd_reboot_help, /* Help for the function. */
        .cExpectedNumberOfParameters = 0 /* No parameters are expected. */
    },
    {
        .pcCommand = "memory", /* The command string to type. */
        .pcHelpString = "memory:\r\n Shows FreeRTOS heap usage (free, lowest ever, total, used), task count and the USB CDC RX high watermark\r\n\r\n",
        .pxCommandInterpreter = cmd_memory, /* The function to run. */
        .pxCommandHelper = cmd_memory_help, /* Help for the function. */
        .cExpectedNumberOfParameters = 0 /* No parameters are expected. */
    },
    {
        .pcCommand = "cdcstats", /* The command string to type. */
        .pcHelpString = "cdcstats:\r\n Shows USB CDC bridge RX diagnostics (VCP/ESP32 bridge path): bytes refused at arrival (retryable back-pressure), bytes lost unrecoverably, the high watermark (sticky until cdcreset) and the sampled buffered-now level. The 32-bit counters can wrap after long sustained load.\r\n\r\n",
        .pxCommandInterpreter = cmd_cdcstats, /* The function to run. */
        .pxCommandHelper = cmd_cdcstats_help, /* Help for the function. */
        .cExpectedNumberOfParameters = 0 /* No parameters are expected. */
    },
    {
        .pcCommand = "cdcreset", /* The command string to type. */
        .pcHelpString = "cdcreset:\r\n Resets the USB CDC bridge RX diagnostic counters and high watermark\r\n\r\n",
        .pxCommandInterpreter = cmd_cdcreset, /* The function to run. */
        .pxCommandHelper = cmd_cdcreset_help, /* Help for the function. */
        .cExpectedNumberOfParameters = 0 /* No parameters are expected. */
    },
    {
        .pcCommand = "dfu", /* The command string to type. */
        .pcHelpString = "dfu:\r\n Reboots into the ROM USB DFU bootloader (does not return until the reset)\r\n\r\n",
        .pxCommandInterpreter = cmd_dfu, /* The function to run. */
        .pxCommandHelper = cmd_dfu_help, /* Help for the function. */
        .cExpectedNumberOfParameters = 0 /* No parameters are expected. */
    },
    {
        .pcCommand = NULL /* simply used as delimeter for end of array*/
    }
};


/*============================================================================*/
/*
 * Command Line Interface handler task
 *
 */
/*============================================================================*/
void vCommandConsoleTask(void *pvParameters)
{
    uint8_t cInputIndex = 0; // simply used to keep track of the index of the input string
    uint32_t receivedValue; // used to store the received value from the notification
    uint8_t fifoChar;
    uint8_t handled = 0;
    char echoBuf[2];

    UNUSED(pvParameters);
    vRegisterCLICommands();

    /* Initialize the M1 Manager protocol (M1CP). Creates its static RX stream
     * buffer and resets session/parser state. Legacy console remains the default
     * until a validated HELLO handshake arrives. */
    m1cp_init(m1cp_cdc_send, CDC_LogCli_PushBytes);

    for (;;)
    {
    (void)xTaskNotifyWait(pdFALSE,          // Don't clear bits on entry
                      0,                    // Clear all bits on exit
                      &receivedValue,       // Receives the notification value
                      pdMS_TO_TICKS(20));   // Also poll FIFO periodically
        dbg_cli_task_wake++;

        /* M1CP: drain the manager RX stream buffer, parse frames, dispatch and
         * respond in this task context (never in the USB callback). Also enforces
         * the session and partial-frame timeouts. */
        m1cp_process();

        handled = 0;
        while (CDC_LogCli_GetChar(&fifoChar) != 0)
        {
            handled = 1;
            dbg_cli_fifo_bytes++;
            cRxedChar = (int8_t)fifoChar;
            echoBuf[0] = (char)cRxedChar;
            echoBuf[1] = '\0';
            cliWrite(echoBuf);

        if (cRxedChar == '\r' || cRxedChar == '\n')
        {
            // user pressed enter, process the command
            handleNewline(pcInputString, cOutputBuffer, &cInputIndex);
        }
        else
        {
            // user pressed a character add it to the input string
            handleCharacterInput(&cInputIndex, pcInputString);
        }
    }

        // Fallback path for legacy UART/notify-value based sources.
        if ((handled == 0) && (receivedValue != 0U))
        {
            dbg_cli_fallback_chars++;
            cRxedChar = receivedValue & 0xFF;
            echoBuf[0] = (char)cRxedChar;
            echoBuf[1] = '\0';
            cliWrite(echoBuf);

            if (cRxedChar == '\r' || cRxedChar == '\n')
            {
                handleNewline(pcInputString, cOutputBuffer, &cInputIndex);
            }
            else
            {
                handleCharacterInput(&cInputIndex, pcInputString);
            }
        }
    }
} // void vCommandConsoleTask(void *pvParameters)



/*============================================================================*/
/*
 * CLI command: Clear Screen
 *
 */
/*============================================================================*/
BaseType_t cmd_clearScreen(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)
{
    /* Remove compile time warnings about unused parameters, and check the
	write buffer is not NULL.  NOTE - for simplicity, this example assumes the
	write buffer length is adequate, so does not check for buffer overflows. */
    (void)pcCommandString;
    (void)xWriteBufferLen;
    (void)num_of_params;   /* drive-by: silences a pre-existing -Wunused-parameter */
    memset(pcWriteBuffer, 0x00, xWriteBufferLen);
    printf("\033[2J\033[1;1H");
    return pdFALSE;
} // BaseType_t cmd_clearScreen(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)



/*============================================================================*/
/*
 * Hlep for the CLI command: Clear Screen
 *
 */
/*============================================================================*/
BaseType_t cmd_clearScreen_help(void)
{
    return pdFALSE;
} // BaseType_t cmd_clearScreen_help(void)



/*============================================================================*/
/*
 * CLI command: USB CDC bridge RX diagnostics (issue #44, part A)
 *
 * Reports the counters maintained on upstream's CDC stream-buffer RX path
 * (VCP / ESP32 bridge modes; the console path is not observed). "Refused at
 * arrival" is retryable back-pressure, "Lost" is unrecoverable loss. Read-only:
 * `cdcreset` clears them.
 */
/*============================================================================*/
BaseType_t cmd_cdcstats(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)
{
    (void)pcCommandString;
    (void)num_of_params;

    (void)snprintf(pcWriteBuffer, xWriteBufferLen,
                   "USB CDC bridge RX diagnostics:\r\n"
                   "  Refused at arrival: %lu (retryable)\r\n"
                   "  Lost (unrecoverable): %lu\r\n"
                   "  High watermark: %lu bytes (sticky)\r\n"
                   "  Buffered now: %lu (sampled)\r\n",
                   (unsigned long)CDC_CLI_RxDroppedBytes(),
                   (unsigned long)CDC_CLI_RxLostBytes(),
                   (unsigned long)CDC_CLI_RxHighWatermark(),
                   (unsigned long)CDC_CLI_RxBufferedBytes());
    return pdFALSE;
} // BaseType_t cmd_cdcstats(...)



/*============================================================================*/
/*
 * Help for the CLI command: USB CDC RX diagnostics
 *
 */
/*============================================================================*/
BaseType_t cmd_cdcstats_help(void)
{
    return pdFALSE;
} // BaseType_t cmd_cdcstats_help(void)



/*============================================================================*/
/*
 * CLI command: reset the USB CDC RX diagnostic counters
 *
 */
/*============================================================================*/
BaseType_t cmd_cdcreset(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)
{
    (void)pcCommandString;
    (void)num_of_params;

    CDC_CLI_RxResetStats();
    (void)snprintf(pcWriteBuffer, xWriteBufferLen,
                   "USB CDC bridge RX diagnostics reset.\r\n");
    return pdFALSE;
} // BaseType_t cmd_cdcreset(...)



/*============================================================================*/
/*
 * Help for the CLI command: reset the USB CDC RX diagnostic counters
 *
 */
/*============================================================================*/
BaseType_t cmd_cdcreset_help(void)
{
    return pdFALSE;
} // BaseType_t cmd_cdcreset_help(void)



/*============================================================================*/
/*
 * CLI command: show a snapshot of the recent log output (issue #44, part B)
 *
 * Adaptation of legacy/v0.8.11:Core/Src/cli_app.c cmd_log() (~:346): the fork
 * captured into a 2047-byte ring that froze once full, and truncated its capture
 * length to the TX-ring return value, so this does NOT reuse that code. The
 * snapshot now comes from m1_logdb_get_recent() (linear window,
 * m1_log_capture.h) and is emitted through FreeRTOS+CLI's multi-call protocol:
 * the handler returns pdTRUE while more output follows and pdFALSE when done.
 * Cost: one 2 KB static snapshot, taken once per invocation.
 */
/*============================================================================*/
static char log_snapshot[M1_LOG_CAPTURE_SIZE + 1U];
static size_t log_snapshot_len;
static size_t log_snapshot_pos;
static uint32_t log_snapshot_total;
static uint8_t log_snapshot_active;

BaseType_t cmd_log(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)
{
    size_t room;
    size_t chunk;

    (void)pcCommandString;
    (void)num_of_params;

    if ((pcWriteBuffer == NULL) || (xWriteBufferLen < 2U))
    {
        log_snapshot_active = 0U;
        return pdFALSE;
    }
    room = xWriteBufferLen - 1U; // keep space for the terminator

    if (log_snapshot_active == 0U)
    {
        size_t len = m1_logdb_get_recent(log_snapshot, sizeof(log_snapshot),
                                         &log_snapshot_total);

        if (len == 0U)
        {
            /* Same one-line answer for both honest cases: nothing captured yet,
             * or the capture was busy for the whole 20 ms window. */
            (void)snprintf(pcWriteBuffer, xWriteBufferLen,
                           "Recent log messages: none captured yet "
                           "(best effort: empty or busy)\r\n");
            return pdFALSE;
        }

        log_snapshot_len = len;
        log_snapshot_pos = 0U;
        log_snapshot_active = 1U;

        if (log_snapshot_total > (uint32_t)len)
        {
            (void)snprintf(pcWriteBuffer, xWriteBufferLen,
                           "Recent log messages (%lu bytes, window %u B):\r\n",
                           (unsigned long)log_snapshot_total,
                           (unsigned)M1_LOG_CAPTURE_SIZE);
        }
        else
        {
            (void)snprintf(pcWriteBuffer, xWriteBufferLen,
                           "Recent log messages (%u bytes):\r\n",
                           (unsigned)len);
        }
        return pdTRUE; // the snapshot body follows
    }

    if (log_snapshot_pos >= log_snapshot_len)
    {
        /* Terminator call: keeps the trailing CRLF out of the chunk arithmetic. */
        (void)snprintf(pcWriteBuffer, xWriteBufferLen, "\r\n");
        log_snapshot_active = 0U;
        log_snapshot_len = 0U;
        log_snapshot_pos = 0U;
        log_snapshot_total = 0U;
        return pdFALSE;
    }

    chunk = log_snapshot_len - log_snapshot_pos;
    if (chunk > room)
    {
        chunk = room;
    }
    memcpy(pcWriteBuffer, &log_snapshot[log_snapshot_pos], chunk);
    pcWriteBuffer[chunk] = '\0';
    log_snapshot_pos += chunk;

    return pdTRUE; // either more body, or the terminator call, follows
} // BaseType_t cmd_log(...)



/*============================================================================*/
/*
 * Help for the CLI command: recent log snapshot
 *
 */
/*============================================================================*/
BaseType_t cmd_log_help(void)
{
    return pdFALSE;
} // BaseType_t cmd_log_help(void)



/*============================================================================*/
/*
 * CLI command: system status (issue #44, part B; fork cmd_status ~:393)
 *
 * Adapted: the fork reported a single "USB CLI dropped" number. This branch
 * split that counter (issue #44, part A), so the retryable refusal and the
 * unrecoverable loss are reported separately and the sampled buffered-now level
 * keeps its own label. Everything fits the 200-byte CLI buffer.
 */
/*============================================================================*/
BaseType_t cmd_status(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)
{
    (void)pcCommandString;
    (void)num_of_params;

    (void)snprintf(pcWriteBuffer, xWriteBufferLen,
                   "System status:\r\n"
                   "  FW: %u.%u.%u.%u-%s\r\n"
                   "  Bank: %u\r\n"
                   "  RX refused: %lu B (retryable)\r\n"
                   "  RX lost: %lu B, buffered: %lu B\r\n",
                   (unsigned)m1_device_stat.config.fw_version_major,
                   (unsigned)m1_device_stat.config.fw_version_minor,
                   (unsigned)m1_device_stat.config.fw_version_build,
                   (unsigned)m1_device_stat.config.fw_version_rc,
                   FW_VERSION_FORK_TAG,
                   (unsigned)((m1_device_stat.active_bank == BANK1_ACTIVE) ? 1U : 2U),
                   (unsigned long)CDC_CLI_RxDroppedBytes(),
                   (unsigned long)CDC_CLI_RxLostBytes(),
                   (unsigned long)CDC_CLI_RxBufferedBytes());
    return pdFALSE;
} // BaseType_t cmd_status(...)



/*============================================================================*/
/*
 * Help for the CLI command: system status
 *
 */
/*============================================================================*/
BaseType_t cmd_status_help(void)
{
    return pdFALSE;
} // BaseType_t cmd_status_help(void)



/*============================================================================*/
/*
 * CLI command: detailed version (issue #44, part B; fork cmd_version ~:443)
 *
 * Adapted: the fork line is kept verbatim in shape (numeric quad + fork tag);
 * the active bank is appended because `status` and `version` are the two places
 * a support report looks for "which bank am I running".
 */
/*============================================================================*/
BaseType_t cmd_version(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)
{
    (void)pcCommandString;
    (void)num_of_params;

    (void)snprintf(pcWriteBuffer, xWriteBufferLen,
                   "M1 firmware version:\r\n"
                   "  %u.%u.%u.%u-%s\r\n"
                   "  Bank: %u\r\n",
                   (unsigned)m1_device_stat.config.fw_version_major,
                   (unsigned)m1_device_stat.config.fw_version_minor,
                   (unsigned)m1_device_stat.config.fw_version_build,
                   (unsigned)m1_device_stat.config.fw_version_rc,
                   FW_VERSION_FORK_TAG,
                   (unsigned)((m1_device_stat.active_bank == BANK1_ACTIVE) ? 1U : 2U));
    return pdFALSE;
} // BaseType_t cmd_version(...)



/*============================================================================*/
/*
 * Help for the CLI command: detailed version
 *
 */
/*============================================================================*/
BaseType_t cmd_version_help(void)
{
    return pdFALSE;
} // BaseType_t cmd_version_help(void)



/*============================================================================*/
/*
 * CLI command: SD card info (issue #44, part B; fork cmd_sdcard ~:475)
 *
 * Adapted: the fork called FatFs f_getfree("0:") directly and reached into
 * FATFS internals (n_fatent/csize). The drive letter and the FATFS object live
 * in m1_sdcard.c statics, so this uses the public m1_sdcard_get_info() accessor
 * instead (it performs the same f_getfree() and pre-divides to KB), and only
 * asks it when the cached access status says the card is mounted -- otherwise
 * the probe just logs an error. MB are derived from the KB figures; the used
 * percentage comes from m1_sdcard_used_percent() (m1_sdcard_capacity.h) because
 * `used_kb * 100` overflows 32 bits on any card more than ~41 GiB full.
 */
/*============================================================================*/
BaseType_t cmd_sdcard(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)
{
    S_M1_SDCard_Access_Status status = m1_sdcard_get_status();
    S_M1_SDCard_Info *info = NULL;
    uint32_t total_kb = 0U;
    uint32_t free_kb = 0U;

    (void)pcCommandString;
    (void)num_of_params;

    if (status == SD_access_OK)
    {
        info = m1_sdcard_get_info();
    }

    if (status != SD_access_OK)
    {
        /* Cached status only: the card is absent, not mounted or holds no
         * filesystem, so no probe was attempted. This is the text
         * m1_sd_error_msg() has always given. */
        (void)snprintf(pcWriteBuffer, xWriteBufferLen,
                       "SD card:\r\n  Status: %s\r\n",
                       m1_sd_error_msg(status));
        return pdFALSE;
    }

    if (info == NULL)
    {
        /* The cached status said mounted, but the probe's f_getfree()/
         * f_getlabel() failed (card pulled mid-command, I/O error) and
         * m1_sdcard_get_info() returned NULL without touching the cached
         * status. Printing that cached status here would say "Status: OK"
         * exactly when the probe failed, so report only what is known: the
         * probe failed, and the FatFs result that made it fail. */
        (void)snprintf(pcWriteBuffer, xWriteBufferLen,
                       "SD card:\r\n"
                       "  Status: capacity probe failed (FatFs error %d)\r\n",
                       (int)m1_sdcard_get_error_code());
        return pdFALSE;
    }

    total_kb = info->total_cap_kb;
    free_kb = info->free_cap_kb;

    (void)snprintf(pcWriteBuffer, xWriteBufferLen,
                   "SD card:\r\n"
                   "  Status: Mounted\r\n"
                   "  Total: %lu MB\r\n"
                   "  Free: %lu MB\r\n"
                   "  Used: %lu MB (%lu%%)\r\n",
                   (unsigned long)(total_kb / 1024U),
                   (unsigned long)(free_kb / 1024U),
                   (unsigned long)((total_kb - free_kb) / 1024U),
                   (unsigned long)m1_sdcard_used_percent(total_kb, free_kb));
    return pdFALSE;
} // BaseType_t cmd_sdcard(...)



/*============================================================================*/
/*
 * Help for the CLI command: SD card info
 *
 */
/*============================================================================*/
BaseType_t cmd_sdcard_help(void)
{
    return pdFALSE;
} // BaseType_t cmd_sdcard_help(void)



/*============================================================================*/
/*
 * CLI command: ESP32 / WiFi co-processor status (fork cmd_wifi ~:537)
 *
 */
/*============================================================================*/
BaseType_t cmd_wifi(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)
{
    (void)pcCommandString;
    (void)num_of_params;

    (void)snprintf(pcWriteBuffer, xWriteBufferLen,
                   "WiFi status:\r\n"
                   "  ESP32: %s\r\n",
                   m1_esp32_get_init_status() ? "Ready" : "Not ready");
    return pdFALSE;
} // BaseType_t cmd_wifi(...)



/*============================================================================*/
/*
 * Help for the CLI command: ESP32 / WiFi co-processor status
 *
 */
/*============================================================================*/
BaseType_t cmd_wifi_help(void)
{
    return pdFALSE;
} // BaseType_t cmd_wifi_help(void)



/*============================================================================*/
/*
 * CLI command: battery status (fork cmd_battery ~:566)
 *
 * Adapted: the fork ignored every gauge read result, so a failed I2C read
 * looked like a flat battery at 0 mV. A failed read is now reported as such --
 * including the state-of-charge read, which goes through the checked
 * bq27421_readStateofCharge_percent() rather than bq27421_soc(): the latter
 * discards the I2C result and returns 0, so a NAK on the SoC register alone
 * would print "Level: 0 %" beside a valid voltage and capacity.
 */
/*============================================================================*/
BaseType_t cmd_battery(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)
{
    uint16_t voltage_mV = 0U;
    uint16_t remaining_mAh = 0U;
    uint16_t full_mAh = 0U;
    uint16_t soc_percent = 0U;
    bool read_ok;

    (void)pcCommandString;
    (void)num_of_params;

    read_ok = bq27421_readVoltage_mV(&voltage_mV);
    read_ok = bq27421_readRemainingCapacity_mAh(&remaining_mAh) && read_ok;
    read_ok = bq27421_readFullChargeCapacity_mAh(&full_mAh) && read_ok;
    read_ok = bq27421_readStateofCharge_percent(&soc_percent) && read_ok;

    if (!read_ok)
    {
        (void)snprintf(pcWriteBuffer, xWriteBufferLen,
                       "Battery: gauge read failed\r\n");
        return pdFALSE;
    }

    (void)snprintf(pcWriteBuffer, xWriteBufferLen,
                   "Battery status:\r\n"
                   "  Level: %u%%\r\n"
                   "  Voltage: %u mV\r\n"
                   "  Capacity: %u/%u mAh\r\n",
                   (unsigned)soc_percent,
                   (unsigned)voltage_mV,
                   (unsigned)remaining_mAh,
                   (unsigned)full_mAh);
    return pdFALSE;
} // BaseType_t cmd_battery(...)



/*============================================================================*/
/*
 * Help for the CLI command: battery status
 *
 */
/*============================================================================*/
BaseType_t cmd_battery_help(void)
{
    return pdFALSE;
} // BaseType_t cmd_battery_help(void)



/*============================================================================*/
/*
 * CLI command: reboot (issue #44, part B; fork cmd_reboot ~:621)
 *
 * Adapted: the fork filled the CLI output buffer with "Rebooting..." and reset
 * immediately. The buffer is only written to the host AFTER the handler returns
 * (handleNewline() -> cliWrite()), so that message raced the reset and was lost.
 * This handler writes the acknowledgement itself, waits a bounded moment so the
 * USB transfer / log TX can complete, and only then resets.
 */
/*============================================================================*/
BaseType_t cmd_reboot(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)
{
    (void)pcCommandString;
    (void)num_of_params;

    (void)snprintf(pcWriteBuffer, xWriteBufferLen, "Rebooting...\r\n");

    /* cliWrite() sends immediately in CDC log/CLI mode and queues onto the log
     * TX path otherwise; either way the bytes leave the host-facing queue a few
     * milliseconds later, so give them a bounded head start before the reset
     * discards everything. Nothing is echoed twice: the buffer is cleared here
     * and this handler never returns to handleNewline()'s writer anyway. */
    cliWrite(pcWriteBuffer);
    pcWriteBuffer[0] = '\0';
    vTaskDelay(pdMS_TO_TICKS(50));

    NVIC_SystemReset(); // noreturn: nothing after this may run
} // BaseType_t cmd_reboot(...)



/*============================================================================*/
/*
 * Help for the CLI command: reboot
 *
 */
/*============================================================================*/
BaseType_t cmd_reboot_help(void)
{
    return pdFALSE;
} // BaseType_t cmd_reboot_help(void)



/*============================================================================*/
/*
 * CLI command: heap and task telemetry (fork cmd_memory ~:640)
 *
 * Adapted: the fork's last line used the CDC high-watermark accessor with the
 * "USB CLI max buf" label; it now says what it measures (RX high watermark of
 * the bridge path, sticky since `cdcreset`), and "Used" is clamped so a heap
 * that grew past configTOTAL_HEAP_SIZE (impossible today) can never underflow.
 */
/*============================================================================*/
BaseType_t cmd_memory(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)
{
    size_t free_heap = (size_t)xPortGetFreeHeapSize();
    size_t min_ever_free = (size_t)xPortGetMinimumEverFreeHeapSize();
    size_t total_heap = (size_t)configTOTAL_HEAP_SIZE;
    size_t used_heap = (total_heap > free_heap) ? (total_heap - free_heap) : 0U;
    UBaseType_t num_tasks = uxTaskGetNumberOfTasks();

    (void)pcCommandString;
    (void)num_of_params;

    (void)snprintf(pcWriteBuffer, xWriteBufferLen,
                   "Heap:\r\n"
                   "  Free: %lu B\r\n"
                   "  Min ever free: %lu B\r\n"
                   "  Total: %lu B\r\n"
                   "  Used: %lu B (%lu%%)\r\n"
                   "  Tasks: %lu\r\n"
                   "  CDC RX high watermark: %lu B\r\n",
                   (unsigned long)free_heap,
                   (unsigned long)min_ever_free,
                   (unsigned long)total_heap,
                   (unsigned long)used_heap,
                   (unsigned long)((total_heap != 0U) ? ((used_heap * 100U) / total_heap) : 0U),
                   (unsigned long)num_tasks,
                   (unsigned long)CDC_CLI_RxHighWatermark());
    return pdFALSE;
} // BaseType_t cmd_memory(...)



/*============================================================================*/
/*
 * Help for the CLI command: heap and task telemetry
 *
 */
/*============================================================================*/
BaseType_t cmd_memory_help(void)
{
    return pdFALSE;
} // BaseType_t cmd_memory_help(void)



/*============================================================================*/
/*
 * CLI command: enter the ROM USB DFU bootloader (fork cmd_dfu ~:700)
 *
 * firmware_update_enter_usb_dfu() is the one DFU entry path on this branch (the
 * boot recovery offer in m1_system.c calls the same function): it persists
 * DEV_OP_STATUS_USB_DFU_REQUEST, waits for the log to drain and resets, and it is
 * declared noreturn. So the acknowledgement is written here first -- exactly
 * like cmd_reboot(), because the CLI buffer would otherwise never reach the host
 * -- and no code follows the call.
 */
/*============================================================================*/
BaseType_t cmd_dfu(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)
{
    (void)pcCommandString;
    (void)num_of_params;

    (void)snprintf(pcWriteBuffer, xWriteBufferLen,
                   "Entering USB DFU mode...\r\n");
    cliWrite(pcWriteBuffer);
    pcWriteBuffer[0] = '\0';
    vTaskDelay(pdMS_TO_TICKS(50));

    firmware_update_enter_usb_dfu(); // persists the request, then resets (noreturn)
} // BaseType_t cmd_dfu(...)



/*============================================================================*/
/*
 * Help for the CLI command: enter the ROM USB DFU bootloader
 *
 */
/*============================================================================*/
BaseType_t cmd_dfu_help(void)
{
    return pdFALSE;
} // BaseType_t cmd_dfu_help(void)



/*============================================================================*/
/*
 * Register the list of commands
 *
 */
/*============================================================================*/
void vRegisterCLICommands(void)
{
    //itterate through the list of commands and register them
    for (int i = 0; xCommandList[i].pcCommand != NULL; i++)
    {
        FreeRTOS_CLIRegisterCommand(&xCommandList[i]);
    }
} // void vRegisterCLICommands(void)




/*============================================================================*/
/*
 * Write to CLI UART
 *
 */
/*============================================================================*/
void cliWrite(const char *str)
{
   size_t remaining;
   const uint8_t *chunk_ptr;
    static uint8_t direct_cli_tx_buf[64];

   if (str == NULL)
   {
       return;
   }

   if (m1_usbcdc_mode == CDC_MODE_LOG_CLI)
   {
       remaining = strlen(str);
       chunk_ptr = (const uint8_t *)str;

       while (remaining > 0U)
       {
           uint16_t chunk_len = (remaining > 64U) ? 64U : (uint16_t)remaining;
           uint8_t retry = 0U;

           memcpy(direct_cli_tx_buf, chunk_ptr, chunk_len);

           while (CDC_Transmit_FS(direct_cli_tx_buf, chunk_len) != USBD_OK)
           {
               if (++retry >= 20U)
               {
                   return;
               }
               vTaskDelay(pdMS_TO_TICKS(2));
           }

           cdc_tx_owner_mode = CDC_MODE_LOG_CLI;
           chunk_ptr += chunk_len;
           remaining -= chunk_len;
       }
       return;
   }

   printf("%s", str);
   fflush(stdout);
} // void cliWrite(const char *str)




/*============================================================================*/
/*
 * Handle the CR + LF from the input CLI command
 *
 */
/*============================================================================*/
void handleNewline(const char *const pcInputString, char *cOutputBuffer, uint8_t *cInputIndex)
{
    dbg_cli_newline++;
    cliWrite("\r\n");

    BaseType_t xMoreDataToFollow;
    do
    {
        xMoreDataToFollow = FreeRTOS_CLIProcessCommand(pcInputString, cOutputBuffer, configCOMMAND_INT_MAX_OUTPUT_SIZE);
        cliWrite(cOutputBuffer);
        *cOutputBuffer = 0x00; // Clear string after use
    } while (xMoreDataToFollow != pdFALSE);

    cliWrite(cli_prompt);
    *cInputIndex = 0;
    memset((void*)pcInputString, 0x00, MAX_INPUT_LENGTH);
} // void handleNewline(const char *const pcInputString, char *cOutputBuffer, uint8_t *cInputIndex)




/*============================================================================*/
/*
 * Handle the backspace from the input CLI command
 *
 */
/*============================================================================*/
void handleBackspace(uint8_t *cInputIndex, char *pcInputString)
{
    if (*cInputIndex > 0)
    {
        (*cInputIndex)--;
        pcInputString[*cInputIndex] = '\0';

#if USING_VS_CODE_TERMINAL
        cliWrite((char *)backspace);
#elif USING_OTHER_TERMINAL
        cliWrite((char *)backspace_tt);
#endif
    }
    else
    {
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
void handleCharacterInput(uint8_t *cInputIndex, char *pcInputString)
{
    if (cRxedChar == '\r')
    {
        return;
    }
    else if (cRxedChar == (uint8_t)0x08 || cRxedChar == (uint8_t)0x7F)
    {
        handleBackspace(cInputIndex, pcInputString);
    }
    else
    {
        if (*cInputIndex < MAX_INPUT_LENGTH)
        {
            pcInputString[*cInputIndex] = cRxedChar;
            (*cInputIndex)++;
        }
    }
} // void handleCharacterInput(uint8_t *cInputIndex, char *pcInputString)

#endif /* CLI_COMMANDS_H */
