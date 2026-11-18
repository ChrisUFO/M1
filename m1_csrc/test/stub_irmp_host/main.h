/* See COPYING.txt for license details. */
/*
* Host-test stub: main.h for compiling the REAL vendored IRMP/IRSND
* (Infrared/irmp-irsnd/) on a POSIX host. irmpsystem.h includes "main.h"
* unconditionally; on the firmware it pulls the whole STM32 HAL. Here it
* provides the minimal TIM surface irsnd.c actually touches plus the
* Error_Handler prototype. No behavior is faked beyond returning HAL_OK.
*/
#ifndef STUB_IRMP_HOST_MAIN_H_
#define STUB_IRMP_HOST_MAIN_H_

#include <stdint.h>
#include <stddef.h>

/* ---- minimal TIM model ------------------------------------------------- */

typedef enum { HAL_OK = 0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;

typedef struct
{
	volatile uint32_t CCER;
	volatile uint32_t BDTR;
	volatile uint32_t CR1;
	volatile uint32_t CNT;
} TIM_TypeDef;

typedef enum
{
	TIM_COUNTERMODE_UP = 0,
	TIM_CLOCKDIVISION_DIV1 = 0,
	TIM_AUTORELOAD_PRELOAD_DISABLE = 0,
	TIM_AUTORELOAD_PRELOAD_ENABLE = 1,
	TIM_OCMODE_PWM1 = 0,
	TIM_OCPOLARITY_HIGH = 0,
	TIM_OCFAST_DISABLE = 0,
	TIM_OCNIDLESTATE_RESET = 0,
} TIM_InitMode_t;

typedef struct
{
	uint32_t Prescaler;
	uint32_t CounterMode;
	uint32_t Period;
	uint32_t ClockDivision;
	uint32_t AutoReloadPreload;
} TIM_Base_InitTypeDef;

typedef struct
{
	uint32_t OCMode;
	uint32_t Pulse;
	uint32_t OCPolarity;
	uint32_t OCNPolarity;
	uint32_t OCFastMode;
	uint32_t OCNIdleState;
} TIM_OC_InitTypeDef;

typedef struct
{
	TIM_TypeDef *Instance;
	TIM_Base_InitTypeDef Init;
	uint32_t Channel;
} TIM_HandleTypeDef;

#define TIM_CCxN_ENABLE   0x0004u
#define TIM_CCER_CC1NE    0x0004u

#define __HAL_TIM_MOE_ENABLE(h)    do { (void)(h); } while (0)
#define __HAL_TIM_MOE_DISABLE(h)   do { (void)(h); } while (0)
#define __HAL_TIM_ENABLE(h)        do { (void)(h); } while (0)
#define __HAL_TIM_DISABLE(h)       do { (void)(h); } while (0)

/* The PWM setup only programs the fake timer; the golden test reads the OTA
* mark/space buffer, never these registers. */
static inline HAL_StatusTypeDef stub_hal_pwm_dummy(TIM_HandleTypeDef *h, void *cfg, uint32_t ch)
{
	(void)h; (void)cfg; (void)ch;
	return HAL_OK;
}

#define HAL_TIM_PWM_Init(h)                    stub_hal_pwm_dummy((h), 0, 0)
#define HAL_TIM_PWM_ConfigChannel(h, c, ch)    stub_hal_pwm_dummy((h), (c), (ch))
#define HAL_TIM_PWM_DeInit(h)                  stub_hal_pwm_dummy((h), 0, 0)

static inline uint32_t HAL_RCC_GetPCLK2Freq(void)
{
	return 64000000u; /* any sane clock: the stub never divides it meaningfully */
}

void Error_Handler(void);

#endif /* STUB_IRMP_HOST_MAIN_H_ */
