/**
  ******************************************************************************
  * @file           : nfc_conf.h
  * @brief          : This file contains definitions for the NFC6/8 components bus interfaces
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __NFC0XA1_CONF_H__
#define __NFC0XA1_CONF_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h5xx_hal.h"
#include "stm32h5xx_hal_exti.h"
#include "RTE_Components.h"
#include "st25r3916_irq.h"
#include "rfal_defConfig.h"

#define TEST               0
#define TEST_LINE          EXTI0_IRQn

#define TEST_RFAL_DEFAULT

#define TESTED_VAR_OK_BMP

#define LED_FIELD_Pin 1         /*!< Enable usage of led field pin on the platform      */
#define LED_FIELD_GPIO_Port 1   /*!< Enable usage of led field port on the platform     */
#define USE_LOGGER 1

#define NFC06A1_LED1_PIN_CLK_ENABLE()     __HAL_RCC__CLK_ENABLE()
#define NFC06A1_LED2_PIN_CLK_ENABLE()     __HAL_RCC__CLK_ENABLE()
#define NFC06A1_LED3_PIN_CLK_ENABLE()     __HAL_RCC__CLK_ENABLE()
#define NFC06A1_LED4_PIN_CLK_ENABLE()     __HAL_RCC__CLK_ENABLE()
#define NFC06A1_LED5_PIN_CLK_ENABLE()     __HAL_RCC__CLK_ENABLE()
#define NFC06A1_LED6_PIN_CLK_ENABLE()     __HAL_RCC__CLK_ENABLE()
#define NFC06A1_LED1_PIN
#define NFC06A1_LED1_PIN_PORT
#define NFC06A1_LED2_PIN
#define NFC06A1_LED2_PIN_PORT
#define NFC06A1_LED3_PIN
#define NFC06A1_LED3_PIN_PORT
#define NFC06A1_LED4_PIN
#define NFC06A1_LED4_PIN_PORT
#define NFC06A1_LED5_PIN
#define NFC06A1_LED5_PIN_PORT
#define NFC06A1_LED6_PIN
#define NFC06A1_LED6_PIN_PORT

#define NFC06A1_ALLLED_GPIO_CLK_ENABLE() {NFC06A1_LED1_PIN_CLK_ENABLE();NFC06A1_LED2_PIN_CLK_ENABLE();NFC06A1_LED3_PIN_CLK_ENABLE();NFC06A1_LED4_PIN_CLK_ENABLE();NFC06A1_LED5_PIN_CLK_ENABLE();NFC06A1_LED6_PIN_CLK_ENABLE();}
#define NFC06A1_NFCTAG_INSTANCE         (0)
#define NFC06A1_NFCTAG_GPO_PRIORITY     (0)

#ifdef LED_FIELD_Pin
/* Case of ST25R3916 */
#define PLATFORM_LED_FIELD_PIN                /*!< GPIO pin used as field LED                        */
#endif

#ifdef LED_FIELD_GPIO_Port
/* Case of ST25R3916 */
#define PLATFORM_LED_FIELD_PORT           /*!< GPIO port used as field LED                       */
#endif

/* Case of ST25R3916 */
#define PLATFORM_LED_A_PIN                    /*!< GPIO pin used for LED A    */
#define PLATFORM_LED_A_PORT              /*!< GPIO port used for LED A   */
#define PLATFORM_LED_B_PIN                    /*!< GPIO pin used for LED B    */
#define PLATFORM_LED_B_PORT              /*!< GPIO port used for LED B   */
#define PLATFORM_LED_F_PIN                    /*!< GPIO pin used for LED F    */
#define PLATFORM_LED_F_PORT              /*!< GPIO port used for LED F   */
#define PLATFORM_LED_V_PIN                    /*!< GPIO pin used for LED V    */
#define PLATFORM_LED_V_PORT              /*!< GPIO port used for LED V   */
#define PLATFORM_LED_AP2P_PIN                 /*!< GPIO pin used for LED AP2P */
#define PLATFORM_LED_AP2P_PORT           /*!< GPIO port used for LED AP2P*/

#define BUS_SPI2_NSS_GPIO_PIN           GPIO_PIN_0
#define BUS_SPI2_NSS_GPIO_PORT          GPIOC
#define BUS_SPI2_IRQ_GPIO_PIN           GPIO_PIN_0
#define BUS_SPI2_IRQ_GPIO_PORT          GPIOE

/* Exported constants --------------------------------------------------------*/
/** @defgroup PTD_Platform_Exported_Constants
 *  @{
 */
#define ST25R_SS_PIN             BUS_SPI2_NSS_GPIO_PIN    /*!< GPIO pin used for ST25R SPI SS                */
#define ST25R_SS_PORT            BUS_SPI2_NSS_GPIO_PORT   /*!< GPIO port used for ST25R SPI SS port          */

#define ST25R_INT_PIN            BUS_SPI2_IRQ_GPIO_PIN    /*!< GPIO pin used for ST25R IRQ                   */
#define ST25R_INT_PORT           BUS_SPI2_IRQ_GPIO_PORT   /*!< GPIO port used for ST25R IRQ port             */

#define IRQ_ST25R_EXTI_IRQn      EXTI0_IRQn

//#define PLATFORM_USER_BUTTON_PIN     USER_BUTTON_PIN          /*!< GPIO pin user button       */
//#define PLATFORM_USER_BUTTON_PORT    USER_BUTTON_GPIO_PORT    /*!< GPIO port user button      */

#define USR_INT_LINE             H_EXTI_0
#define USR_INT_LINE_NUM         EXTI_LINE_0
//#define BSP_NFC0XCOMM_Init             BSP_SPI2_Init
#define BSP_NFC0XCOMM_IRQ_Callback     BSP_SPI2_IRQ_Callback

#define BSP_NFC0XCOMM_SequencialSend  BSP_SPI2_SequencialSend
#define BSP_NFC0XCOMM_SequencialRecv  BSP_SPI2_SequencialRecv

void BSP_SPI2_IRQ_Callback(void);
/**
  * @}
  */

/**
  * @}
  */

/*
******************************************************************************
* GLOBAL VARIABLES
******************************************************************************
*/
typedef enum
{
	NFC_TX_A,
	NFC_TX_B,
	NFC_TX_F,
	NFC_TX_V,
} NFCTxType;


extern bool       isNFCDeviceOk;
extern bool       isNFCCardFound;

extern NFCTxType  nfc_tx_type;
extern char       NFC_Family[128];//+251104
extern char       NFC_Type[128];
extern char       NFC_UID[128];
extern uint8_t    NFC_ID[20];
extern uint8_t    NFC_ID_LEN;

extern char    NFC_ATQA[2];
extern char    NFC_SAK;

/* Exported variables --------------------------------------------------------*/
extern SPI_HandleTypeDef hspi2;

extern uint8_t globalCommProtectCnt;
extern  EXTI_HandleTypeDef H_EXTI_0;
/* Exported functions ------------------------------------------------------- */
int32_t BSP_NFC0XCOMM_SendRecv(const uint8_t * const pTxData, uint8_t * const pRxData, uint16_t Length);

int32_t BSP_NFC0XCOMM_Init(void);

#define  COMM_HANDLE                                     hspi2

void _Error_Handler(char * file, int line);

#ifdef __cplusplus
}
#endif

#endif /* __NFC0XA1_CONF_H__*/

