
/**
  ******************************************************************************
  * @file    rfal_crc.h
  * @author  MMY Application Team
  * @brief   CRC calculation module
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

#ifndef RFAL_CRC_H_
#define RFAL_CRC_H_

/*
******************************************************************************
* INCLUDES
******************************************************************************
*/
#include "rfal_platform.h"

/*
******************************************************************************
* GLOBAL FUNCTION PROTOTYPES
******************************************************************************
*/
/*! 
 *****************************************************************************
 *  \brief  Calculate CRC according to CCITT standard.
 *
 *  This function takes \a length bytes from \a buf and calculates the CRC
 *  for this data. The result is returned.
 *  \note This implementation calculates the CRC with LSB first, i.e. all
 *  bytes are "read" from right to left.
 *
 *  \param[in] preloadValue : Initial value of CRC calculation.
 *  \param[in] buf : buffer to calculate the CRC for.
 *  \param[in] length : size of the buffer.
 *
 *  \return 16 bit long crc value.
 *
 *****************************************************************************
 */
extern uint16_t rfalCrcCalculateCcitt(uint16_t preloadValue, const uint8_t* buf, uint16_t length);

#endif /* RFAL_CRC_H_ */