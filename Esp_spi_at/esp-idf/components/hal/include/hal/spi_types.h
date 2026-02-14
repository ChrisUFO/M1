/*
 * SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include "esp_bit_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SPI command.
 */
typedef enum {
     /* Slave HD Only */
    SPI_CMD_HD_WRBUF    = BIT(0),
    SPI_CMD_HD_RDBUF    = BIT(1),
    SPI_CMD_HD_WRDMA    = BIT(2),
    SPI_CMD_HD_RDDMA    = BIT(3),
    SPI_CMD_HD_SEG_END  = BIT(4),
    SPI_CMD_HD_EN_QPI   = BIT(5),
    SPI_CMD_HD_WR_END   = BIT(6),
    SPI_CMD_HD_INT0     = BIT(7),
    SPI_CMD_HD_INT1     = BIT(8),
    SPI_CMD_HD_INT2     = BIT(9),
} spi_command_t;
