/**
  ******************************************************************************
  * @file    usbd_cdc_if_template.h
  * @author  MCD Application Team
  * @brief   Header for usbd_cdc_if_template.c file.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2015 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __USBD_CDC_IF_TEMPLATE_H
#define __USBD_CDC_IF_TEMPLATE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "usbd_cdc.h"

/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/

extern USBD_CDC_ItfTypeDef  USBD_CDC_Interface_fops;

/* Exported macro ------------------------------------------------------------*/
/* Exported functions ------------------------------------------------------- */
uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len);
void CDC_Rearm_FS(void);
void CDC_LogCli_ForceRecover_FS(void);
int CDC_LogCli_GetChar(uint8_t *ch);
void CDC_LogCli_ResetFifo(void);
void CDC_LogCli_PushBytes(const uint8_t *data, uint16_t len);

/* USB CDC RX diagnostics (issue #44, part A).
 *
 * These describe the stream-buffer RX path (CDC_MODE_VCP / CDC_MODE_ESP32:
 * USB OUT -> UserRxBufferFS -> usbRxBuffer -> h_usb_rx_streambuf), which is the
 * path that can back-pressure and lose host bytes. Diagnostic only: no data
 * path decision depends on them, and they never block. Task-context reads are
 * safe (single aligned 32-bit volatile reads); the counters are updated from
 * the USB interrupt callback.
 *
 * CDC_CLI_RxDroppedBytes(): cumulative bytes from the host that a callback
 *   could not hand to the stream buffer -- refused because it was full
 *   (partially or wholly), plus bytes lost when the intermediate usbRxBuffer
 *   overflowed under the same backpressure (the only unrecoverable case).
 *   Counted once per byte at arrival; bytes kept queued in usbRxBuffer are
 *   retried and not re-counted, so a counted byte may still be delivered later
 *   after a drain -- the value measures host traffic that met a full stream
 *   buffer, not proven loss.
 * CDC_CLI_RxHighWatermark(): maximum bytes observed pending in the stream
 *   buffer since the last reset (monotonic; useful capacity is
 *   RXSTREAMBUF_USB_SIZE).
 * CDC_CLI_RxBufferedBytes(): bytes pending in the stream buffer now (0 if it
 *   has not been created).
 * CDC_CLI_RxResetStats(): zero both counters. Reset is atomic with respect to
 *   the RX interrupt, so no stale value can be resurrected; the reported window
 *   then restarts ("since last reset").
 *
 * Surface them over the CLI with `cdcstats` / `cdcreset`. */
uint32_t CDC_CLI_RxDroppedBytes(void);
uint32_t CDC_CLI_RxHighWatermark(void);
uint32_t CDC_CLI_RxBufferedBytes(void);
void CDC_CLI_RxResetStats(void);

#ifdef __cplusplus
}
#endif

#endif /* __USBD_CDC_IF_TEMPLATE_H */

