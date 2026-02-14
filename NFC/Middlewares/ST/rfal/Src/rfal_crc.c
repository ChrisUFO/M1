
/**
  ******************************************************************************
  * @file    rfal_crc.c
  * @author  MMY Application Team
  * @brief   CRC calculation implementation
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/*
******************************************************************************
* INCLUDES
******************************************************************************
*/
#include "rfal_crc.h"

/*
******************************************************************************
* LOCAL FUNCTION PROTOTYPES
******************************************************************************
*/
static uint16_t rfalCrcUpdateCcitt(uint16_t crcSeed, uint8_t dataByte);

/*
******************************************************************************
* GLOBAL FUNCTIONS
******************************************************************************
*/
uint16_t rfalCrcCalculateCcitt(uint16_t preloadValue, const uint8_t* buf, uint16_t length)
{
    uint16_t crc = preloadValue;
    uint16_t index;

    for (index = 0; index < length; index++)
    {
        crc = rfalCrcUpdateCcitt(crc, buf[index]);
    }

    return crc;
}

/*
******************************************************************************
* LOCAL FUNCTIONS
******************************************************************************
*/
static uint16_t rfalCrcUpdateCcitt(uint16_t crcSeed, uint8_t dataByte)
{
    uint16_t crc = crcSeed;
    uint8_t  dat = dataByte;
    
    dat ^= (uint8_t)(crc & 0xFFU);
    dat ^= (dat << 4);

    crc = (crc >> 8)^(((uint16_t) dat) << 8)^(((uint16_t) dat) << 3)^(((uint16_t) dat) >> 4);

    return crc;
}