/* See COPYING.txt for license details. */

/*
*
* m1_fusb302.c
*
* Driver for FUSB302
*
* M1 Project
*
* Reference:
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_i2c.h"
#include "m1_power_ctl.h"
#include "m1_bq25896.h"
#include "m1_fusb302.h"

/*************************** D E F I N E S ************************************/

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

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

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/



/*** REG01 ***/


/*============================================================================*/
/*
 * Description: Get Device ID
 *
 */
/*============================================================================*/
uint8_t pd_getID()
{
	uint8_t reg = bq_readRegister(REG01_ADDR);

	return reg;
}


/*** REG02 ***/


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/


/*** REG04 ***/


/*============================================================================*/
/*
 * Description: Measure BIAS
 *
 */
/*============================================================================*/
uint8_t pd_measure(int vbus)
{
	uint8_t reg = pd_readRegister(REG04_ADDR);
	if (vbus == 0)
		pd_unsetBit(&reg, REG04_MEAS_BIAS);
	else
		pd_setBit(&reg, REG04_MEAS_BIAS);
	pd_setRegister(REG04_ADDR, reg);

	reg = pd_readRegister(REG04_ADDR);
	return reg;
} // int bq_reset(void)


/*** REG0C ***/


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
int pd_reset(void)
{
	uint8_t reg = pd_readRegister(REG0C_ADDR);
	pd_setBit(&reg, REG0C_RST_PD);

	return pd_setRegister(REG0C_ADDR, reg);
} // int bq_reset(void)



/*** REG END ***/

int pd_disable(void)
{
	return 0;
}


int pd_enable(void)
{
	return 0;
}



/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
int pd_setRegister(uint8_t reg, uint8_t value)
{
	S_M1_I2C_Trans_Inf i2c_inf;

	i2c_inf.dev_id = I2C_DEVICE_FUSB302;
	i2c_inf.timeout = I2C_WRITE_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_WRITE_REGISTER;
	i2c_inf.reg_address = reg;
	i2c_inf.reg_data = value;
	uint8_t stat = m1_i2c_hal_trans_req(&i2c_inf);

	return (stat==HAL_OK);
} // int pd_setRegister(uint8_t reg, uint8_t value)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
int pd_readRegister(uint8_t reg)
{
	S_M1_I2C_Trans_Inf i2c_inf;

	i2c_inf.dev_id = I2C_DEVICE_FUSB302;
	i2c_inf.timeout = I2C_READ_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_READ_REGISTER;
	i2c_inf.reg_address = reg;
	m1_i2c_hal_trans_req(&i2c_inf);

	return i2c_inf.reg_data;
} // int pd_readRegister(uint8_t reg)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
void pd_setBit(uint8_t *reg, uint8_t mask)
{
	*reg |= mask;
} // void bq_setBit(uint8_t *reg, uint8_t mask)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
void pd_unsetBit(uint8_t *reg, uint8_t mask)
{
	*reg &= ~mask;
} // void bq_unsetBit(uint8_t *reg, uint8_t mask)



/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
void pd_fusb302_init(void)
{
	uint8_t reg = pd_getID();
	reg = pd_measure(0);
	reg = pd_measure(1);
} // void pd_fusb302_init(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
void pd_cli_status(void)
{

}
