/**
  ******************************************************************************
  * @file    logger.c
  * @brief   Debug log output utility implementation.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

#define BMP_TEST
/* Includes ------------------------------------------------------------------*/
#include "logger.h"
#include "st_errno.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

/** @addtogroup X-CUBE-NFC6_Applications
 *  @{
 */

/** @addtogroup PollingTagDetect
 *  @{
 */

/** @addtogroup PTD_Logger
 * @{
 */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/** @defgroup PTD_Logger_Private_Define
 * @{
 */
#if (USE_LOGGER == LOGGER_ON)

#define MAX_HEX_STR         4
#define MAX_HEX_STR_LENGTH  128
char hexStr[MAX_HEX_STR][MAX_HEX_STR_LENGTH];
uint8_t hexStrIdx = 0;
#endif /* #if USE_LOGGER == LOGGER_ON */

#if (USE_LOGGER == LOGGER_OFF && !defined(HAL_UART_MODULE_ENABLED))
  #define UART_HandleTypeDef void
#endif

/*!< Maximum Timeout values for flags waiting loops. These timeouts are not based
   on accurate values, they just guarantee that the application will not remain
   stuck if the UART communication is corrupted.
   You may modify these timeout values depending on CPU frequency and application
   conditions (interrupts routines ...). */
#define USART_TIMEOUT          1000
/**
  * @}
  */

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/** @defgroup PTD_Logger_Private_Variables
 * @{
 */
// UART_HandleTypeDef *pLogUsart = &hcom_uart[COM1];   /*!< pointer to the logger Handler */
/**
  * @}
  */

/* Private function prototypes -----------------------------------------------*/
uint8_t logUsartTx(uint8_t *data, uint16_t dataLen);

/* Private functions ---------------------------------------------------------*/
/** @defgroup PTD_Logger_Private_Functions
 * @{
 */
/**
  *****************************************************************************
  * @brief  This function initalize the UART handle and UART IP.
  *
  * @param[in,out]	husart : handle to USART HW
  *
  * @return None
  *****************************************************************************
  */
void logUsartInit(UART_HandleTypeDef *husart)
{
  //husart->Instance = USART2;
  //husart->Init.BaudRate = 115200;
  //husart->Init.WordLength = UART_WORDLENGTH_8B;
  //husart->Init.StopBits = UART_STOPBITS_1;
  //husart->Init.Parity = UART_PARITY_NONE;
  //husart->Init.Mode = UART_MODE_TX_RX;
  //husart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
  //husart->Init.OverSampling = UART_OVERSAMPLING_16;
  //husart->Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  //husart->AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  //HAL_UART_Init(husart);
  //
  //pLogUsart = husart;
}

/**
  *****************************************************************************
  * @brief  This function Transmit data via USART
  *
  * @param[in]	data    : data to be transmitted
  * @param[in]	dataLen : length of data to be transmitted
  *
  * @retval ERR_INVALID_HANDLE : in case the UART HW is not initalized yet
  * @retval others             : HAL status
  *****************************************************************************
  */
uint8_t logUsartTx(uint8_t *data, uint16_t dataLen)
{
	return 0;
//  if(pLogUsart == 0)
//    return ERR_INVALID_HANDLE;
// #if (USE_LOGGER == LOGGER_ON)
//  {
//    return HAL_UART_Transmit(pLogUsart, data, dataLen, USART_TIMEOUT);
//    }
//  #else
//  {
//    return 0;
//  }
//  #endif /* #if USE_LOGGER == LOGGER_ON */
}

/**
  *****************************************************************************
  * @brief  This function is used to write a formated string via the UART interface.
  *
  * @param[in]	format : data to be transmitted
  *
  * @return Number of data sent
  *****************************************************************************
  */
int logUsart(const char* format, ...)
{
  #if (USE_LOGGER == LOGGER_ON)
  {
    #define LOG_BUFFER_SIZE 256
    char buf[LOG_BUFFER_SIZE];
    va_list argptr;
    va_start(argptr, format);
    int cnt = vsnprintf(buf, LOG_BUFFER_SIZE, format, argptr);
    va_end(argptr);

    /* */
    logUsartTx((uint8_t*)buf, strlen(buf));
    return cnt;
  }
  #else
  {
    return 0;
  }
  #endif /* #if USE_LOGGER == LOGGER_ON */
}

/**
  *****************************************************************************
  * @brief  helper to convert hex data into formated string
  *
	* @param[in]	data : pointer to buffer to be dumped.
	* @param[in]	dataLen : buffer length
  *
  * @return pointer to converted data
  *****************************************************************************
  */
char* hex2Str(unsigned char * data, size_t dataLen)
{
  #if (USE_LOGGER == LOGGER_ON)
  {
    unsigned char * pin = data;
    const char * hex = "0123456789ABCDEF";
    char * pout = hexStr[hexStrIdx];
    uint8_t i = 0;
    uint8_t idx = hexStrIdx;

    if( dataLen > (MAX_HEX_STR_LENGTH/2) )
    {
      dataLen = (MAX_HEX_STR_LENGTH/2);
    }

    if(dataLen == 0)
    {
      pout[0] = 0;
    }
    else
    {
      for(; i < dataLen - 1; ++i)
      {
          *pout++ = hex[(*pin>>4)&0xF];
          *pout++ = hex[(*pin++)&0xF];
      }
      *pout++ = hex[(*pin>>4)&0xF];
      *pout++ = hex[(*pin)&0xF];
      *pout = 0;
    }

    hexStrIdx++;
    hexStrIdx %= MAX_HEX_STR;

    return hexStr[idx];
  }
  #else
  {
    return NULL;
  }
  #endif /* #if USE_LOGGER == LOGGER_ON */
}

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

