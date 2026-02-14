/**
  ******************************************************************************
  * @file    st25r3916_led.h
  * @author  MMY Application Team
  * @brief   ST25R3916 LEDs handling
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

#ifndef ST25R3916_LED_H
#define ST25R3916_LED_H

/*
******************************************************************************
* INCLUDES
******************************************************************************
*/

#include "rfal_platform.h"

/*
******************************************************************************
* GLOBAL DEFINES
******************************************************************************
*/

/*
******************************************************************************
* GLOBAL FUNCTION PROTOTYPES
******************************************************************************
*/


/*! 
 *****************************************************************************
 *  \brief  ST25R3916 LED Initialize
 *
 *  This function initializes the LEDs that represent ST25R3916 activity 
 *
 *****************************************************************************
 */
void st25r3916ledInit( void );

/*! 
 *****************************************************************************
 *  \brief   ST25R3916 LED Event Interrupt
 *
 *  This function should be called upon a ST25R3916 Interrupt, providing 
 *  the interrupt event with ST25R3916 irq flags to update LEDs 
 *
 *  \param[in] irqs: ST25R3916 irqs mask
 *
 *****************************************************************************
 */
void st25r3916ledEvtIrq( uint32_t irqs );

/*! 
 *****************************************************************************
 *  \brief   ST25R3916 LED Event Write Register
 *
 *  This function should be called on a ST25R3916 Write Register operation
 *  providing the event with the register and value to update LEDs 
 *
 *  \param[in] reg: ST25R3916 register to be written
 *  \param[in] val: value to be written on the register
 *
 *****************************************************************************
 */
void st25r3916ledEvtWrReg( uint8_t reg, uint8_t val );

/*! 
 *****************************************************************************
 *  \brief   ST25R3916 LED Event Write Multiple Register
 *
 *  This function should be called upon a ST25R3916 Write Multiple Registers, 
 *  providing the event with the registers and values to update LEDs 
 *
 *  \param[in] reg : ST25R3916 first register written
 *  \param[in] vals: pointer to the values written
 *  \param[in] len : number of registers written
 *
 *****************************************************************************
 */
void st25r3916ledEvtWrMultiReg( uint8_t reg, const uint8_t* vals, uint8_t len );

/*! 
 *****************************************************************************
 *  \brief   ST25R3916 LED Event Direct Command
 *
 *  This function should be called upon a ST25R3916 direct command, providing 
 *  the event with the command executed
 *
 *  \param[in] cmd: ST25R3916 cmd executed
 *
 *****************************************************************************
 */
void st25r3916ledEvtCmd( uint8_t cmd );

#endif /* ST25R3916_LED_H */

/**
  * @}
  *
  * @}
  *
  * @}
  * 
  * @}
  */
