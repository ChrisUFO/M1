
/**
  ******************************************************************************
  * @file    rfal_t1t.h
  * @author  MMY Application Team
  * @brief   Provides NFC-A T1T convenience methods and definitions
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

#ifndef RFAL_T1T_H
#define RFAL_T1T_H

/*
 ******************************************************************************
 * INCLUDES
 ******************************************************************************
 */
#include "rfal_platform.h"
#include "rfal_utils.h"
#include "rfal_rf.h"

/*
 ******************************************************************************
 * GLOBAL DEFINES
 ******************************************************************************
 */
#define RFAL_T1T_UID_LEN               4   /*!< T1T UID length of cascade level 1 only tag  */
#define RFAL_T1T_HR_LENGTH             2   /*!< T1T HR(Header ROM) length                   */

#define RFAL_T1T_HR0_NDEF_MASK      0xF0   /*!< T1T HR0 NDEF capability mask  T1T 1.2 2.2.2 */
#define RFAL_T1T_HR0_NDEF_SUPPORT   0x10   /*!< T1T HR0 NDEF capable value    T1T 1.2 2.2.2 */


/*! NFC-A T1T (Topaz) command set */
typedef enum
{
    RFAL_T1T_CMD_RID      = 0x78,          /*!< T1T Read UID                                */
    RFAL_T1T_CMD_RALL     = 0x00,          /*!< T1T Read All                                */
    RFAL_T1T_CMD_READ     = 0x01,          /*!< T1T Read                                    */
    RFAL_T1T_CMD_WRITE_E  = 0x53,          /*!< T1T Write with erase (single byte)          */
    RFAL_T1T_CMD_WRITE_NE = 0x1A           /*!< T1T Write with no erase (single byte)       */
} rfalT1Tcmds;


/*
******************************************************************************
* GLOBAL TYPES
******************************************************************************
*/


/*! NFC-A T1T (Topaz) RID_RES  Digital 1.1  10.6.2 & Table 50 */
typedef struct
{
    uint8_t hr0;                           /*!< T1T Header ROM: HR0                         */
    uint8_t hr1;                           /*!< T1T Header ROM: HR1                         */
    uint8_t uid[RFAL_T1T_UID_LEN];         /*!< T1T UID                                     */
} rfalT1TRidRes;

/*
******************************************************************************
* GLOBAL FUNCTION PROTOTYPES
******************************************************************************
*/


/*! 
 *****************************************************************************
 * \brief  Initialize NFC-A T1T Poller mode
 *  
 * This methods configures RFAL RF layer to perform as a 
 * NFC-A T1T Poller/RW (Topaz) including all default timings 
 *
 * \return RFAL_ERR_WRONG_STATE  : RFAL not initialized or mode not set
 * \return RFAL_ERR_NONE         : No error
 *****************************************************************************
 */
ReturnCode rfalT1TPollerInitialize( void );


/*! 
 *****************************************************************************
 * \brief  NFC-A T1T Poller RID
 *  
 * This method reads the UID of a NFC-A T1T Listener device  
 *
 *
 * \param[out]  ridRes           : pointer to place the RID_RES
 * 
 * \return RFAL_ERR_WRONG_STATE  : RFAL not initialized or mode not set
 * \return RFAL_ERR_PARAM        : Invalid parameter
 * \return RFAL_ERR_NONE         : No error
 *****************************************************************************
 */
ReturnCode rfalT1TPollerRid( rfalT1TRidRes *ridRes );


/*! 
 *****************************************************************************
 * \brief  NFC-A T1T Poller RALL
 *  
 * This method send a Read All command to a NFC-A T1T Listener device  
 *
 *
 * \param[in]   uid              : the UID of the device to read data
 * \param[out]  rxBuf            : pointer to place the read data
 * \param[in]   rxBufLen         : size of rxBuf
 * \param[out]  rxRcvdLen        : actual received data
 * 
 * \return RFAL_ERR_WRONG_STATE  : RFAL not initialized or mode not set
 * \return RFAL_ERR_PARAM        : Invalid parameter
 * \return RFAL_ERR_NONE         : No error
 *****************************************************************************
 */
ReturnCode rfalT1TPollerRall( const uint8_t* uid, uint8_t* rxBuf, uint16_t rxBufLen, uint16_t *rxRcvdLen );


/*! 
 *****************************************************************************
 * \brief  NFC-A T1T Poller Write
 *  
 * This method writes the given data on the address of a NFC-A T1T Listener device  
 *
 *
 * \param[in]   uid              : the UID of the device to read data
 * \param[in]   address          : address to write the data
 * \param[in]   data             : the data to be written
 * 
 * \return RFAL_ERR_WRONG_STATE  : RFAL not initialized or mode not set
 * \return RFAL_ERR_PARAM        : Invalid parameter
 * \return RFAL_ERR_NONE         : No error
 *****************************************************************************
 */
ReturnCode rfalT1TPollerWrite( const uint8_t* uid, uint8_t address, uint8_t data );

#endif /* RFAL_T1T_H */

/**
  * @}
  *
  * @}
  *
  * @}
  */
