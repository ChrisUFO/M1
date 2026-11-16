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
 * path that can back-pressure and lose host bytes. The console path
 * (CDC_MODE_LOG_CLI) is NOT observed by these counters. Diagnostic only: no
 * data path decision depends on them, and they never block. Task-context reads
 * of the cumulative counters are safe (single aligned 32-bit volatile reads);
 * the counters are updated from the USB interrupt callback.
 *
 * CDC_CLI_RxDroppedBytes(): cumulative host bytes REFUSED AT ARRIVAL because
 *   the stream buffer was full in that callback (partial fit or no room at
 *   all), plus bytes lost when the intermediate usbRxBuffer overflowed under
 *   the same back-pressure. This is a retryable back-pressure metric, not
 *   proven loss: bytes kept queued in usbRxBuffer are delivered on a later
 *   drain, and the ones that overflowed are also counted in
 *   CDC_CLI_RxLostBytes(). Counted once per byte at arrival, so retried
 *   carry-over is never re-counted. The name is legacy (fork API parity); the
 *   legacy fork counter measured true drops on the CDC_MODE_LOG_CLI branch.
 * CDC_CLI_RxLostBytes(): cumulative host bytes that can never be delivered --
 *   bytes that did not fit the intermediate usbRxBuffer, plus bytes still in
 *   the stream buffer when a bridge-mode exit emptied it. This is the loss-only
 *   subset; it stays at 0 under pure back-pressure.
 * CDC_CLI_RxHighWatermark(): maximum bytes observed pending in the stream
 *   buffer since the last reset (monotonic until CDC_CLI_RxResetStats(); usable
 *   capacity is RXSTREAMBUF_USB_SIZE - 1 = 2047 bytes).
 * CDC_CLI_RxBufferedBytes(): bytes pending in the stream buffer now. SAMPLED
 *   gauge, not exact: xStreamBufferBytesAvailable() reads the volatile head and
 *   tail without a critical section, so the value can be off by one burst while
 *   a consumer is draining. Never use it as a synchronisation signal.
 * CDC_CLI_RxResetStats(): zero all counters. Reset is atomic with respect to
 *   the RX interrupt, so no stale value can be resurrected; the reported window
 *   then restarts ("since last reset").
 *
 * Surface them over the CLI with `cdcstats` / `cdcreset`. */
uint32_t CDC_CLI_RxDroppedBytes(void);
uint32_t CDC_CLI_RxLostBytes(void);
uint32_t CDC_CLI_RxHighWatermark(void);
uint32_t CDC_CLI_RxBufferedBytes(void);
void CDC_CLI_RxResetStats(void);

/* Discard accounting for a bridge-mode exit: sample what h_usb_rx_streambuf
 * still holds, add it to the unrecoverable-loss counter, then reset the buffer.
 * Replaces a bare xStreamBufferReset(h_usb_rx_streambuf) so host bytes that are
 * dropped by the mode switch are not invisible. Task context only. */
void CDC_CLI_RxDiscardPendingBytes(void);

#ifdef __cplusplus
}
#endif

#endif /* __USBD_CDC_IF_TEMPLATE_H */

