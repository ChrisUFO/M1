/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*******************************************************************************
 * NOTICE
 * The hal is not public api, don't use in application code.
 * See readme.md in hal/include/hal/readme.md
 ******************************************************************************/

// The HAL layer for SPI master (common part)

// SPI HAL usages (without DMA):
// 1. initialize the bus
// 2. setup the clock speed (since this takes long time)
// 3. call setup_device to update parameters for the specific device
// 4. call setup_trans to update parameters for the specific transaction
// 5. prepare data to send into hw registers
// 6. trigger user defined SPI transaction to start
// 7. wait until the user transaction is done
// 8. fetch the received data
// Parameter to be updated only during ``setup_device`` will be highlighted in the
// field comments.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Transaction configuration structure, this should be assigned by driver each time.
 * All these parameters will be updated to the peripheral every transaction.
 */
typedef struct {
    uint16_t cmd;                       ///< Command value to be sent
    int cmd_bits;                       ///< Length (in bits) of the command phase
    int addr_bits;                      ///< Length (in bits) of the address phase
    int dummy_bits;                     ///< Base length (in bits) of the dummy phase. Note when the compensation is enabled, some extra dummy bits may be appended.
    int tx_bitlen;                      ///< TX length, in bits
    int rx_bitlen;                      ///< RX length, in bits
    uint64_t addr;                      ///< Address value to be sent
    uint8_t *send_buffer;               ///< Data to be sent
    uint8_t *rcv_buffer;                ///< Buffer to hold the receive data.
    int cs_keep_active;                 ///< Keep CS active after transaction
} spi_hal_trans_config_t;

