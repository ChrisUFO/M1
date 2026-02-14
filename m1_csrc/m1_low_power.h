/* See COPYING.txt for license details. */

/*
*
* m1_low_power.h
*
* Library for M1 in low power mode
*
* M1 Project
*
*/

#ifndef M1_LOW_POWER_H_
#define M1_LOW_POWER_H_

// Use RTC for applications that need to sleep for hours, instead of a few hundred/thousand milliseconds max using SysTick/LPTIM
#define M1_MYTICKLESS_USE_RTC

#define MYTICKLESS_IDLE_TIME_MAX		(uint32_t) 86400000 // 24hours x 60 minutes x 60 seconds x 1000 milliseconds

#define MYTICKLESS_IDLE_TIME_MIN		(uint32_t) 10000 // 60 seconds x 1000 milliseconds

#define MYTICKLESS_LPTIM_TIME_MAX		(uint32_t) 65535 // LPTIM running with clock LSI=32KHz with prescaler=32, so each tick is 1ms
														// (16-bit counter)max = 65535 x 1ms = 65.535 seconds
#define MYTICKLESS_SYSTICK_TIME_MAX		(uint32_t) 4194 // ***SYSCLK running with clock MSI=4MHz, so each tick is 1/4 uS
														// (24-bit counter)max = 16*2^10*2^10 x 1/4uS = 4.194 seconds
														// ***If SYSCLK=80MHz, so each tick is 1/80 uS
														// (24-bit counter)max = 16*2^10*2^10 x 1/80uS = 209 milliseconds
#define RTC_TIME_REGISTER_SECOND_L_MASK	0x0000000F
#define RTC_TIME_REGISTER_SECOND_H_MASK	0x00000070
#define RTC_TIME_REGISTER_MINUTE_L_MASK	0x00000F00
#define RTC_TIME_REGISTER_MINUTE_H_MASK	0x00007000
#define RTC_TIME_REGISTER_HOUR_L_MASK	0x000F0000
#define RTC_TIME_REGISTER_HOUR_H_MASK	0x00300000

#define RTC_TIME_REGISTER_SECOND_L_POS	0
#define RTC_TIME_REGISTER_SECOND_H_POS	4
#define RTC_TIME_REGISTER_MINUTE_L_POS	8
#define RTC_TIME_REGISTER_MINUTE_H_POS	12
#define RTC_TIME_REGISTER_HOUR_L_POS	16
#define RTC_TIME_REGISTER_HOUR_H_POS	20

extern uint8_t ucRTC_flag_wutf;



#endif /* M1_LOW_POWER_H_ */
