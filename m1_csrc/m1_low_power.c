/* See COPYING.txt for license details. */

/*
*
* m1_low_power.c
*
* Library for M1 in low power mode
*
* M1 Project
*
*/
/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include "stm32h5xx_hal.h"
#include "FreeRTOSConfig.h"
#include "FreeRTOS.h"
#include "main.h"
#include "cmsis_os.h"
#include "m1_low_power.h"

/*************************** D E F I N E S ************************************/

//#define SYSTICK_CURRENT_VALUE_REG		( * ( ( volatile uint32_t * ) 0xe000e018 ) )


//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

//static uint32_t tick_1, tick_2;
static TickType_t systick_count_1, systick_count_2;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

/*
 * Setup the timer to generate the tick interrupts.  The implementation in this
 * file is weak to allow application writers to change the timer used to
 * generate the tick interrupt.
 */
void vPortSetupTimerInterrupt( void );

/*
 * Exception handlers.
 */

/*-----------------------------------------------------------*/


/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/*============================================================================*/
/*
 * This function is called before the device is placed in sleep mode
 *
 */
/*============================================================================*/
void PreSleepProcessing(uint32_t ulExpectedIdleTime)
{
/* place for user code */
	HAL_SuspendTick();
	//tick_1 = SYSTICK_CURRENT_VALUE_REG;
	systick_count_1 = xTaskGetTickCount();
	//ulExpectedIdleTime = prvGetExpectedIdleTime();
//	HAL_LPTIM_TimeOut_Start_IT(&hlptim1, 0xFFFF, ulExpectedIdleTime);
	// Put the microcontroller into low-power mode
	//HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
	//or
	//HAL_PWREx_EnterSTOP1Mode(PWR_STOPENTRY_WFI);
	//or
	//HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);
	//__WFI();  // Wait for interrupt
} // void PreSleepProcessing(uint32_t ulExpectedIdleTime)



/*============================================================================*/
/*
 * This function is called after the device goes out of sleep mode
 *
 */
/*============================================================================*/
void PostSleepProcessing(uint32_t ulExpectedIdleTime)
{
/* place for user code */
//	HAL_LPTIM_TimeOut_Stop_IT(&hlptim1);
	//tick_2 = SYSTICK_CURRENT_VALUE_REG;
	systick_count_2 = xTaskGetTickCount();
	HAL_ResumeTick();
} // void PostSleepProcessing(uint32_t ulExpectedIdleTime)



#ifndef M1_MYTICKLESS_USE_RTC

/*============================================================================*/
/*
 *  Generated when configUSE_TICKLESS_IDLE == 2.
 * 	Function called in tasks.c (in portTASK_FUNCTION).
 * 	TO BE COMPLETED or TO BE REPLACED by a user one, overriding that weak one.
 */
/*============================================================================*/
void vPortSuppressTicksAndSleep( TickType_t xExpectedIdleTime )
{

}

#else


/*============================================================================*/
/*
 *  Generated when configUSE_TICKLESS_IDLE == 2.
 * 	Function called in tasks.c (in portTASK_FUNCTION).
 * 	TO BE COMPLETED or TO BE REPLACED by a user one, overriding that weak one.
 */
/*============================================================================*/
/*
void vPortSuppressTicksAndSleep( TickType_t xExpectedIdleTime )
{

} // void vPortSuppressTicksAndSleep( TickType_t xExpectedIdleTime )
*/
#endif // #ifdef M1_MYTICKLESS_USE_RTC



/*============================================================================*/
/*
 * Setup the systick timer to generate the tick interrupts at the required
 * frequency.
 */
/*============================================================================*/
/*
void vPortSetupTimerInterrupt( void )
{

} // void vPortSetupTimerInterrupt( void )
*/


#if (USE_CUSTOM_SYSTICK_HANDLER_IMPLEMENTATION == 1)
void SysTick_Handler (void)
{

}
#endif // #if (USE_CUSTOM_SYSTICK_HANDLER_IMPLEMENTATION == 1)

