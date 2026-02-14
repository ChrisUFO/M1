/* See COPYING.txt for license details. */

/*
*
* m1_fusb302.h
*
* Driver for FUSB302
*
* M1 Project
*
* Reference:
*/
#ifndef M1_FUSB302_H_
#define M1_FUSB302_H_

#include <stdint.h>
#include "stm32h5xx_hal.h"
#include <stdbool.h>

#define FUSB302_I2C_ADDR	0x44	//

#define REG01_ADDR 			0x01	// Device ID,	R,	0x1000_00XX
#define REG01_REV			0xF0
#define REG01_VER			0x0F

#define REG02_ADDR 			0x02	// Switches0,	RW,	0x0000_0011
#define REG03_ADDR 			0x03	// Switches1,	RW,	0x0010_0000

#define REG04_ADDR 			0x04	// Measure,		RW,	0x0011_0001
#define REG04_MEAS_BIAS 	0x40	// Maesure BIAS
#define REG04_MDAC		 	0x3F	// Maesure Block DAC data input

#define REG05_ADDR 			0x05	// Slice,		RW,	0x0110_0000
#define REG06_ADDR 			0x06	// Control0,	V,	0x0010_0100
#define REG07_ADDR 			0x07	// Control1,	V,	0x0000_0000
#define REG08_ADDR 			0x08	// Control2,	V,	0x0000_0010
#define REG09_ADDR 			0x09	// Control3,	V,	0x0000_0110
#define REG0A_ADDR 			0x0A	// Mask,		RW,	0x0000_0000
#define REG0B_ADDR 			0x0B	// Power,		RW,	0x0000_0001

#define REG0C_ADDR 			0x0C	// Reset,		WC,	0x0000_0000
#define REG0C_RST_PD		0x02	// Reset PD Logic
#define REG0C_RST_I2C		0x02	// Reset PD Logic


#define REG0D_ADDR 			0x0D	// OCPreg,		RW,	0x0000_1111
#define REG0E_ADDR 			0x0E	// Maska,		RW,	0x0000_0000
#define REG0F_ADDR 			0x0F	// Maskb,		RW,	0x0000_0000

#define REG10_ADDR 			0x10	// Control4,	RW,	0x0000_0000

#define REG3C_ADDR 			0x3C	// Status0a,	R,	0x0000_0000
#define REG3D_ADDR 			0x3D	// Status1a,	R,	0x0000_0000
#define REG3E_ADDR 			0x3E	// Interrupta,	R,	0x0000_0000
#define REG3F_ADDR 			0x3F	// Interruptb,	R,	0x0000_0000
#define REG40_ADDR 			0x40	// Status0,		R,	0x0000_0000
#define REG41_ADDR 			0x41	// Status1,		R,	0x0010_1000
#define REG42_ADDR 			0x42	// Interrupt,	R,	0x0000_0000
#define REG43_ADDR 			0x43	// FIFOs,		RW,	0x0000_0000


uint8_t pd_getID(void);
uint8_t pd_measure(int vbus);

int pd_disable(void);
int pd_enable(void);
int pd_reset(void);
int pd_setRegister(uint8_t reg, uint8_t value);
int pd_readRegister(uint8_t reg);
void pd_setBit(uint8_t *reg, uint8_t mask);
void pd_unsetBit(uint8_t *reg, uint8_t mask);
void pd_fusb302_init(void);

void pd_cli_status(void);

#endif /* M1_FUSB302_H_ */
