/* See COPYING.txt for license details. */

/*
*
*  m1_sub_ghz_api.c
*
*  M1 sub-ghz API
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_compile_cfg.h"
#include "m1_sub_ghz_api.h"
#include "m1_rf_spi.h"

#include "radio_config/si4463_revc2_patch.h"
#include "radio_config/si4463_revc2_300_rxtx_spacing250_bw200_baud10_error1_10_OOK.h"
#include "radio_config/si4463_revc2_310_rxtx_spacing250_bw100_baud10_error1_10_OOK.h"
#include "radio_config/si4463_revc2_315_rxtx_spacing250_bw100_baud10_error1_10_OOK.h"
#include "radio_config/si4463_revc2_345_rxtx_spacing250_bw100_baud10_error1_10_OOK.h"
#include "radio_config/si4463_revc2_372_rxtx_spacing250_bw200_baud10_error1_10_OOK.h"
#include "radio_config/si4463_revc2_390_rxtx_spacing250_bw100_baud10_error1_10_OOK.h"
#include "radio_config/si4463_revc2_433_rxtx_spacing250_bw200_baud10_error1_10_OOK.h"
#include "radio_config/si4463_revc2_433_92_rxtx_spacing250_bw100_baud10_error1_10_OOK.h"
#include "radio_config/m1_sub_ghz_915_rxtx_spc25_bwauto_baud106_dev200_e1_10_fsk.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"SUB_GHZ"

#define SI4463_CTS_TIMEOUT    		12000	// Number of SPI retries to read the CTS ready status
#define SPI_DUMMY_CMD_DATA			0xFF
#define SI4463_CTS_READY        	0xFF

#define SI446X_GROUP_MODEM                          0x20
#define SI446X_GROUP_MODEM_PROPERTY_MODEM_MOD_TYPE  0x00
#define SI446X_GROUP_MODEM_PROPERTY_MODEM_OOK_PDTC	0x40

/* Property offsets used by the runtime modem config (AN633 / WDS emits):
 * MODEM group 0x20: MOD_TYPE 0x00, FREQ_DEV 0x0A:0x0C, DECIMATION_CFG1/0/2
 * 0x1E:0x20, AFC_LIMITER 0x30:0x31, SPIKE_DET 0x54, DSA_QUAL 0x5D;
 * CHFLT group 0x21: RX1/RX2_CHFLT_COE13..0 + COEM3..0 = 0x00:0x23. */
#define SI446X_MODEM_PROP_FREQ_DEV                  0x0A
#define SI446X_MODEM_PROP_DECIMATION_CFG1           0x1E
#define SI446X_MODEM_PROP_AFC_LIMITER_1             0x30
#define SI446X_MODEM_PROP_SPIKE_DET                 0x54
#define SI446X_MODEM_PROP_DSA_QUAL                  0x5D
#define SI446X_GROUP_CHFLT                          0x21
#define SI446X_MODEM_CHFLT_COE_N                    36 /* 0x2100..0x2123 */


#define RADIO_COMM_BUFFER_MAX          30


//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

uint8_t radio_comm_rx_buffer[RADIO_COMM_BUFFER_MAX];
uint8_t radio_comm_tx_buffer[RADIO_COMM_BUFFER_MAX];
uint8_t si446x_cmd_buffer[RADIO_COMM_BUFFER_MAX];

static uint8_t si446x_CTS_ready = FALSE;
static uint8_t si446x_got_reset = FALSE;
static union si446x_cmd_reply_union si446x_cmd;

/*! Si446x configuration array */
const uint8_t Radio_300_Configuration_Data_Array[] = RADIO_300_CONFIGURATION_DATA_ARRAY;
const tRadioConfiguration RadioConfiguration_300 = RADIO_300_CONFIGURATION_DATA;

const uint8_t Radio_310_Configuration_Data_Array[] = RADIO_310_CONFIGURATION_DATA_ARRAY;
const tRadioConfiguration RadioConfiguration_310 = RADIO_310_CONFIGURATION_DATA;

const uint8_t Radio_315_Configuration_Data_Array[] = RADIO_315_CONFIGURATION_DATA_ARRAY;
const tRadioConfiguration RadioConfiguration_315 = RADIO_315_CONFIGURATION_DATA;

const uint8_t Radio_345_Configuration_Data_Array[] = RADIO_345_CONFIGURATION_DATA_ARRAY;
const tRadioConfiguration RadioConfiguration_345 = RADIO_345_CONFIGURATION_DATA;

const uint8_t Radio_372_Configuration_Data_Array[] = RADIO_372_CONFIGURATION_DATA_ARRAY;
const tRadioConfiguration RadioConfiguration_372 = RADIO_372_CONFIGURATION_DATA;

const uint8_t Radio_390_Configuration_Data_Array[] = RADIO_390_CONFIGURATION_DATA_ARRAY;
const tRadioConfiguration RadioConfiguration_390 = RADIO_390_CONFIGURATION_DATA;

const uint8_t Radio_433_Configuration_Data_Array[] = RADIO_433_CONFIGURATION_DATA_ARRAY;
const tRadioConfiguration RadioConfiguration_433 = RADIO_433_CONFIGURATION_DATA;

const uint8_t Radio_433_92_Configuration_Data_Array[] = RADIO_433_92_CONFIGURATION_DATA_ARRAY;
const tRadioConfiguration RadioConfiguration_433_92 = RADIO_433_92_CONFIGURATION_DATA;

const uint8_t Radio_915_Configuration_Data_Array[] = RADIO_915_CONFIGURATION_DATA_ARRAY;
const tRadioConfiguration RadioConfiguration_915 = RADIO_915_CONFIGURATION_DATA;


const uint8_t Radio_Patch_Configuration_Data_Array[] = RADIO_PATCH_CONFIGURATION_DATA_ARRAY;
const tRadioConfiguration RadioConfiguration_Patch = RADIO_PATCH_CONFIGURATION_DATA;

const tRadioConfiguration *RadioConfigList[SUB_GHZ_BAND_EOL] = {
																		&RadioConfiguration_300,
																		&RadioConfiguration_310,
																		&RadioConfiguration_315,
																		&RadioConfiguration_345,
																		&RadioConfiguration_372,
																		&RadioConfiguration_390,
																		&RadioConfiguration_433_92, /* index 6 -> 433.920 (reordered) */
																		&RadioConfiguration_433,    /* index 7 -> 434.059 (reordered) */
																		&RadioConfiguration_915,
																	};

const tRadioConfiguration *pradioconfig = NULL;

volatile uint8_t si446x_nIRQ_active = FALSE; // Updated by interrupt
volatile uint8_t radio_state_flag = RADIO_STATE_IDLE; // Updated by interrupt
static uint8_t radio_init_done = FALSE;
static uint8_t radio_mod_type = MODEM_MOD_TYPE_OOK;

/* outdiv of the most recently loaded configuration (see
 * radio_get_last_outdiv()). Set by radio_init_rx_tx() from the WDS base
 * headers' own MODEM_CLKGEN_BAND values and by radio_tune_exact_hz()
 * (whose range->outdiv selection is the verified authority). The
 * 0x08/0x0A/0x0B encodings (outdiv 4/8/12) are cross-verified by the FSK
 * deviation words in the repo's WDS FSK headers and radio_tune_exact_hz();
 * the 0x09 encoding used by the 345/372/390 MHz headers is mapped to
 * outdiv 6 per the consecutive BAND enum {4,6,8,12,16} -> {0x08..0x0C}
 * (inferred - flagged for hardware acceptance in the deviation path). */
static uint32_t radio_last_outdiv = 0;
static const uint8_t radio_band_outdiv[SUB_GHZ_BAND_EOL] =
{
	/*300*/12, /*310*/12, /*315*/12, /*345*/6, /*372*/6, /*390*/6,
	/*433*/8,  /*433_92*/8, /*915*/4,
};


static S_M1_SPI_Trans_Inf radio_spi_trans_inf = {
													.dev_id = SPI_DEVICE_SUBGHZ,
													.timeout = SPI_WRITE_TIMEOUT,
												};

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void SI446x_Wait_CTS(void);
void SI446x_PowerUp(void);
void SI446x_Reset(void);
uint8_t SI446x_Send_Cmd(uint8_t byteCount, uint8_t *pData);
uint8_t SI446x_Get_Resp(uint8_t byteCount, uint8_t *pData);
uint8_t SI446x_Poll_CTS(void);
uint8_t SI446x_Get_Reset_Stat(void);
void SI446x_Get_IntStatus(uint8_t PH_CLR_PEND, uint8_t MODEM_CLR_PEND, uint8_t CHIP_CLR_PEND);
struct si446x_reply_GET_MODEM_STATUS_map *SI446x_Get_ModemStatus( uint8_t MODEM_CLR_PEND);
void SI446x_Get_ChipStatus( uint8_t CHIP_CLR_PEND);
struct si446x_reply_PART_INFO_map *SI446x_PartInfo(void);
struct si446x_reply_REQUEST_DEVICE_STATE_map *SI446x_Request_DeviceState(void);
uint8_t SI446x_ConfigInit(const uint8_t* pSetPropCmd);
void Radio_Start_Rx(uint8_t CHANNEL, uint8_t CONDITION, uint16_t RX_LEN, uint8_t NEXT_STATE1, uint8_t NEXT_STATE2, uint8_t NEXT_STATE3);
void SI446x_Start_Rx(uint8_t channel);
void Radio_Start_Tx(uint8_t CHANNEL, uint8_t CONDITION, uint16_t TX_LEN);
void SI446x_Start_Tx(uint8_t channel, uint8_t *pradio_tx_buffer, uint8_t length);
void SI446x_Calibrate_IR(void);
void SI446x_GPIO_Config(uint8_t GPIO0, uint8_t GPIO1, uint8_t GPIO2, uint8_t GPIO3, uint8_t NIRQ, uint8_t SDO, uint8_t GEN_CONFIG);
void SI446x_GPIO_ConfigFast(void);
void SI446x_Change_State(uint8_t NEXT_STATE1);
void SI446x_FiFoInfo(uint8_t FIFO);
void SI446x_Write_TxFiFo(uint8_t numBytes, uint8_t *pTxData);
void SI446x_Change_ModType(uint8_t NEW_MOD_TYPE);
void SI446x_Change_Modem_OOK_PDTC(uint8_t NEW_PDTC);
void SI446x_Change_Radio_Setting(uint8_t mode, uint8_t pa_power);
void SI446x_Set_Tx_Power(uint8_t power);
void SI446x_Select_Frontend(S_M1_SubGHz_Band network);
void SI446x_Start_Tx_CW(uint16_t channel, uint8_t radio_mod_type);
void radio_init_rx_tx(S_M1_SubGHz_Band freq, uint8_t mod_type, bool do_reset);
void radio_patch_init(void);
uint8_t radio_get_init_state(void);
void radio_set_antenna_mode(tRadioAntennaMode mode);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/******************************************************************************/
/*
 * This function checks the CTS status and waits until the CTS gets ready
 *
*/
/******************************************************************************/
#ifdef M1_APP_RADIO_POLL_CTS_ON_GPIO
void SI446x_Wait_CTS(void)
{
	// Check CTS pin
	// GPIO1 is CTS by default after a POR
	for ( uint32_t timeout = 0xFFFF; (timeout > 0) && (HAL_GPIO_ReadPin(SI4463_GPIO1_GPIO_Port, SI4463_GPIO1_Pin)==GPIO_PIN_RESET); timeout-- )
		; // Let wait here until CTS getting ready (bounded to avoid a hang on a stuck CTS pin)
} // void SI446x_Wait_CTS(void)
#endif // #ifdef M1_APP_RADIO_POLL_CTS_ON_GPIO



/******************************************************************************/
/*
 * Waits for CTS to be high
 * return CTS value
 *
*/
/******************************************************************************/
uint8_t SI446x_Poll_CTS(void)
{
#ifdef M1_APP_RADIO_POLL_CTS_ON_GPIO
    SI446x_Wait_CTS(); // wait here
    si446x_CTS_ready = TRUE;
    return SI4463_CTS_READY;
#else
    return SI446x_Get_Resp(0, NULL);
#endif // #ifdef M1_APP_RADIO_POLL_CTS_ON_GPIO
} // uint8_t SI446x_Poll_CTS(void)



/******************************************************************************/
/*
 * Check the SI4463 if it just got reset
 * return:
*/
/******************************************************************************/
uint8_t SI446x_Get_Reset_Stat(void)
{
	return si446x_got_reset;
} // uint8_t SI446x_Get_Reset_Stat(void)



/******************************************************************************/
/*
 * This function resets the radio after power up and waits for the CTS to get ready
 *
*/
/******************************************************************************/
void SI446x_PowerUp(void)
{
    /* Hardware reset the chip */
    SI446x_Reset();
    radio_patch_init();
#ifdef M1_APP_RADIO_POLL_CTS_ON_GPIO
    SI446x_Wait_CTS();
#else
    HAL_Delay(15); // Delay 15ms for the CTS to get ready
#endif // #ifdef M1_APP_RADIO_POLL_CTS_ON_GPIO
} // void SI446x_PowerUp(void)



/******************************************************************************/
/*
 *  This functions is used to reset the si446x radio by applying shutdown and
 * releasing it.
 *
 */
/******************************************************************************/
void SI446x_Reset(void)
{
	//The SDN pin needs to be held high for at least 10us before driving low again
	// so that internal capacitors can discharge
    /* Put radio in shutdown, wait then release */
    HAL_GPIO_WritePin(SI4463_ENA_GPIO_Port, SI4463_ENA_Pin, GPIO_PIN_SET);
    HAL_Delay(1); // 15
    HAL_GPIO_WritePin(SI4463_ENA_GPIO_Port, SI4463_ENA_Pin, GPIO_PIN_RESET);
    HAL_Delay(1); // 15
    si446x_CTS_ready = FALSE;
	M1_LOG_I(M1_LOGDB_TAG, "SI446x_Reset!\r\n");
} // void SI446x_Reset(void)



/******************************************************************************/
/*
 * Sends a command to the radio chip
 * byteCount:     Number of bytes in the command to send to the radio device
 * pData:         Pointer to the command to send.
 */
/******************************************************************************/
uint8_t SI446x_Send_Cmd(uint8_t byteCount, uint8_t *pData)
{
	uint8_t reset_cnt = 0;

    while ( !si446x_CTS_ready )
    {
        //SI446x_Poll_CTS();
        if (SI446x_Get_Resp(0, NULL) != SI4463_CTS_READY )
        {
        	HAL_Delay(50); // 15
        	reset_cnt++;
        }
        if (reset_cnt > 6) // 20
        {
        	SI446x_Reset();
        	si446x_got_reset = TRUE;
        	return 1;
        }
    } // while ( !si446x_CTS_ready )

    radio_spi_trans_inf.pdata_tx = pData;
    radio_spi_trans_inf.data_len = byteCount;
    radio_spi_trans_inf.trans_type = SPI_TRANS_WRITE_DATA;
    m1_spi_hal_trans_req(&radio_spi_trans_inf);
    si446x_CTS_ready = FALSE;

    return 0;
} // uint8_t SI446x_Send_Cmd(uint8_t byteCount, uint8_t *pData)



/******************************************************************************/
/*
 * Gets a command response from the radio chip
 * byteCount     Number of bytes to get from the radio chip
 * pData         Pointer to where to put the data
 * Return CTS value
 */
/******************************************************************************/
uint8_t SI446x_Get_Resp(uint8_t byteCount, uint8_t *pData)
{
    uint16_t errCnt;
    uint8_t cmd_buffer, rsp_buffer;

    cmd_buffer = SI446X_CMD_ID_READ_CMD_BUFF;
    radio_spi_trans_inf.pdata_tx = &cmd_buffer;
    radio_spi_trans_inf.pdata_rx = &rsp_buffer;
    radio_spi_trans_inf.data_len = 1;
    errCnt = SI4463_CTS_TIMEOUT;
    si446x_CTS_ready = FALSE;
    while ( errCnt != 0 )      //wait until radio IC is ready with the data
    {
    	rsp_buffer = 0x00;
        radio_spi_trans_inf.trans_type = SPI_TRANS_ASSERT_NSS;
        m1_spi_hal_trans_req(&radio_spi_trans_inf); // Assert NSS manually
        radio_spi_trans_inf.trans_type = SPI_TRANS_WRITE_DATA_NO_NSS;
        m1_spi_hal_trans_req(&radio_spi_trans_inf);
        // "The typical time for a valid FFh CTS reading is 20 μs"
        radio_spi_trans_inf.trans_type = SPI_TRANS_READ_DATA_NO_NSS;
        m1_spi_hal_trans_req(&radio_spi_trans_inf);
        if ( rsp_buffer==SI4463_CTS_READY )
        {
            if (byteCount)
            {
            	radio_spi_trans_inf.pdata_rx = pData;
            	radio_spi_trans_inf.data_len = byteCount;
            	radio_spi_trans_inf.trans_type = SPI_TRANS_READ_DATA_NO_NSS;
            	m1_spi_hal_trans_req(&radio_spi_trans_inf);
            } // if (byteCount)
            si446x_CTS_ready = TRUE;
        } // if ( rsp_buffer == SI4463_CTS_READY )
        radio_spi_trans_inf.trans_type = SPI_TRANS_DEASSERT_NSS;
        m1_spi_hal_trans_req(&radio_spi_trans_inf); // Deassert NSS manually
        if ( si446x_CTS_ready )
        {
        	break;
        } // if ( si446x_CTS_ready )
        errCnt--;
    } // while (errCnt != 0)

    if (errCnt==0)
    {
    	M1_LOG_E(M1_LOGDB_TAG, "Error with CTS!\r\n");
    	//Error_Handler();
    } // if (errCnt == 0)

    return rsp_buffer;
} // uint8_t SI446x_Get_Resp(uint8_t byteCount, uint8_t *pData)



/******************************************************************************/
/*
 * Get the Interrupt status/pending flags form the radio and clear flags if requested.
 * PH_CLR_PEND     Packet Handler pending flags clear.
 * MODEM_CLR_PEND  Modem Status pending flags clear.
 * CHIP_CLR_PEND   Chip State pending flags clear.
 */
/******************************************************************************/
void SI446x_Get_IntStatus(uint8_t PH_CLR_PEND, uint8_t MODEM_CLR_PEND, uint8_t CHIP_CLR_PEND)
{
    si446x_cmd_buffer[0] = SI446X_CMD_ID_GET_INT_STATUS;
    si446x_cmd_buffer[1] = PH_CLR_PEND;
    si446x_cmd_buffer[2] = MODEM_CLR_PEND;
    si446x_cmd_buffer[3] = CHIP_CLR_PEND;

    SI446x_Send_Cmd(SI446X_CMD_ARG_COUNT_GET_INT_STATUS, si446x_cmd_buffer);
    SI446x_Get_Resp(SI446X_CMD_REPLY_COUNT_GET_INT_STATUS, si446x_cmd_buffer);

    si446x_cmd.GET_INT_STATUS.INT_PEND       = si446x_cmd_buffer[0];
    si446x_cmd.GET_INT_STATUS.INT_STATUS     = si446x_cmd_buffer[1];
    si446x_cmd.GET_INT_STATUS.PH_PEND        = si446x_cmd_buffer[2];
    si446x_cmd.GET_INT_STATUS.PH_STATUS      = si446x_cmd_buffer[3];
    si446x_cmd.GET_INT_STATUS.MODEM_PEND     = si446x_cmd_buffer[4];
    si446x_cmd.GET_INT_STATUS.MODEM_STATUS   = si446x_cmd_buffer[5];
    si446x_cmd.GET_INT_STATUS.CHIP_PEND      = si446x_cmd_buffer[6];
    si446x_cmd.GET_INT_STATUS.CHIP_STATUS    = si446x_cmd_buffer[7];
} // void SI446x_Get_IntStatus(uint8_t PH_CLR_PEND, uint8_t MODEM_CLR_PEND, uint8_t CHIP_CLR_PEND)



/******************************************************************************/
/*
 * Gets the Modem status flags. Optionally clears them.
 * MODEM_CLR_PEND: Flags to clear.
 */
/******************************************************************************/
struct si446x_reply_GET_MODEM_STATUS_map *SI446x_Get_ModemStatus( uint8_t MODEM_CLR_PEND )
{
    si446x_cmd_buffer[0] = SI446X_CMD_ID_GET_MODEM_STATUS;
    si446x_cmd_buffer[1] = MODEM_CLR_PEND;

    SI446x_Send_Cmd(SI446X_CMD_ARG_COUNT_GET_MODEM_STATUS, si446x_cmd_buffer);
    SI446x_Get_Resp(SI446X_CMD_REPLY_COUNT_GET_MODEM_STATUS, si446x_cmd_buffer);

    si446x_cmd.GET_MODEM_STATUS.MODEM_PEND   = si446x_cmd_buffer[0];
    si446x_cmd.GET_MODEM_STATUS.MODEM_STATUS = si446x_cmd_buffer[1];
    si446x_cmd.GET_MODEM_STATUS.CURR_RSSI    = si446x_cmd_buffer[2];
    si446x_cmd.GET_MODEM_STATUS.LATCH_RSSI   = si446x_cmd_buffer[3];
    si446x_cmd.GET_MODEM_STATUS.ANT1_RSSI    = si446x_cmd_buffer[4];
    si446x_cmd.GET_MODEM_STATUS.ANT2_RSSI    = si446x_cmd_buffer[5];
    si446x_cmd.GET_MODEM_STATUS.AFC_FREQ_OFFSET =  ((uint16_t)si446x_cmd_buffer[6] << 8) & 0xFF00;
    si446x_cmd.GET_MODEM_STATUS.AFC_FREQ_OFFSET |= (uint16_t)si446x_cmd_buffer[7] & 0x00FF;

    return &si446x_cmd.GET_MODEM_STATUS;
} // struct si446x_reply_GET_MODEM_STATUS_map *SI446x_Get_ModemStatus( uint8_t MODEM_CLR_PEND )



/******************************************************************************/
/*
 * Gets the Chip status flags. Optionally clears them.
 * CHIP_CLR_PEND: Flags to clear.
 */
/******************************************************************************/
void SI446x_Get_ChipStatus( uint8_t CHIP_CLR_PEND )
{
    si446x_cmd_buffer[0] = SI446X_CMD_ID_GET_CHIP_STATUS;
    si446x_cmd_buffer[1] = CHIP_CLR_PEND;

    SI446x_Send_Cmd(SI446X_CMD_ARG_COUNT_GET_CHIP_STATUS, si446x_cmd_buffer);
    SI446x_Get_Resp(SI446X_CMD_REPLY_COUNT_GET_CHIP_STATUS, si446x_cmd_buffer);

    si446x_cmd.GET_CHIP_STATUS.CHIP_PEND         = si446x_cmd_buffer[0];
    si446x_cmd.GET_CHIP_STATUS.CHIP_STATUS       = si446x_cmd_buffer[1];
    si446x_cmd.GET_CHIP_STATUS.CMD_ERR_STATUS    = si446x_cmd_buffer[2];
} // void SI446x_Get_ChipStatus( uint8_t CHIP_CLR_PEND )



/******************************************************************************/
/*
 * This function sends the PART_INFO command to the radio and receives the answer
 */
/******************************************************************************/
struct si446x_reply_PART_INFO_map *SI446x_PartInfo(void)
{
    si446x_cmd_buffer[0] = SI446X_CMD_ID_PART_INFO;
    //si446x_cmd_buffer[0] = 0x00;

    SI446x_Send_Cmd(SI446X_CMD_ARG_COUNT_PART_INFO, si446x_cmd_buffer);
    SI446x_Get_Resp(SI446X_CMD_REPLY_COUNT_PART_INFO, si446x_cmd_buffer);

    si446x_cmd.PART_INFO.CHIPREV         = si446x_cmd_buffer[0];
    si446x_cmd.PART_INFO.PART            = ((uint16_t)si446x_cmd_buffer[1] << 8) & 0xFF00;
    si446x_cmd.PART_INFO.PART           |= (uint16_t)si446x_cmd_buffer[2] & 0x00FF;
    si446x_cmd.PART_INFO.PBUILD          = si446x_cmd_buffer[3];
    si446x_cmd.PART_INFO.ID              = ((uint16_t)si446x_cmd_buffer[4] << 8) & 0xFF00;
    si446x_cmd.PART_INFO.ID             |= (uint16_t)si446x_cmd_buffer[5] & 0x00FF;
    si446x_cmd.PART_INFO.CUSTOMER        = si446x_cmd_buffer[6];
    si446x_cmd.PART_INFO.ROMID           = si446x_cmd_buffer[7];

    return &si446x_cmd.PART_INFO;
} // struct si446x_reply_PART_INFO_map *(void)



/******************************************************************************/
/*
 * Requests the current state of the device and lists pending TX and RX requests
 */
/******************************************************************************/
struct si446x_reply_REQUEST_DEVICE_STATE_map *SI446x_Request_DeviceState(void)
{
    si446x_cmd_buffer[0] = SI446X_CMD_ID_REQUEST_DEVICE_STATE;

    SI446x_Send_Cmd(SI446X_CMD_ARG_COUNT_REQUEST_DEVICE_STATE, si446x_cmd_buffer);
    SI446x_Get_Resp(SI446X_CMD_REPLY_COUNT_REQUEST_DEVICE_STATE, si446x_cmd_buffer);

    si446x_cmd.REQUEST_DEVICE_STATE.CURR_STATE       = si446x_cmd_buffer[0];
    si446x_cmd.REQUEST_DEVICE_STATE.CURRENT_CHANNEL  = si446x_cmd_buffer[1];

    return &si446x_cmd.REQUEST_DEVICE_STATE;
} // struct si446x_reply_REQUEST_DEVICE_STATE_map *SI446x_Request_DeviceState(void)



/******************************************************************************/
/*
 * This function is used to load all properties and commands with a list of NULL terminated commands.
 * Commands structure in the array:
 * --------------------------------
 * LEN | <LEN length of data>
 */
/******************************************************************************/
uint8_t SI446x_ConfigInit(const uint8_t* pSetPropCmd)
{
    uint8_t col;
    uint8_t numOfBytes;

    /* While cycle as far as the pointer points to a command */
    while (*pSetPropCmd != 0x00)
    {
        numOfBytes = *pSetPropCmd++;

        if (numOfBytes > RADIO_COMM_BUFFER_MAX)
        {
            /* Number of command bytes exceeds maximal allowable length */
            return SI446X_COMMAND_ERROR;
        }

        for (col = 0; col < numOfBytes; col++)
        {
            si446x_cmd_buffer[col] = *pSetPropCmd;
            pSetPropCmd++;
        }

        SI446x_Send_Cmd(numOfBytes, si446x_cmd_buffer);
        if (SI446x_Get_Resp(0, NULL) != SI4463_CTS_READY )
        {
            /* Timeout occurred */
            return SI446X_CTS_TIMEOUT;
        }

        if ( HAL_GPIO_ReadPin(SI4463_nINT_GPIO_Port, SI4463_nINT_Pin)==GPIO_PIN_RESET )
        {
            /* Get and clear all interrupts.  An error has occured... */
            SI446x_Get_IntStatus(0, 0, 0);
/*
            if (si446x_cmd.GET_INT_STATUS.CHIP_PEND & SI446X_CMD_GET_CHIP_STATUS_REP_CHIP_PEND_CMD_ERROR_PEND_MASK)
            {
                return SI446X_COMMAND_ERROR;
            }
*/
        }
  } // while (*pSetPropCmd != 0x00)

  return SI446X_SUCCESS;
} // uint8_t SI446x_ConfigInit(const uint8_t* pSetPropCmd)



/******************************************************************************/
/*
 * Sends START_RX command to the radio.
 * CHANNEL:     Channel number.
 * CONDITION:   Start RX condition.
 * RX_LEN:      Payload length (exclude the PH generated CRC).
 * NEXT_STATE1: Next state when Preamble Timeout occurs.
 * NEXT_STATE2: Next state when a valid packet received.
 * NEXT_STATE3: Next state when invalid packet received (e.g. CRC error).
 */
/******************************************************************************/
void Radio_Start_Rx(uint8_t CHANNEL, uint8_t CONDITION, uint16_t RX_LEN, uint8_t NEXT_STATE1, uint8_t NEXT_STATE2, uint8_t NEXT_STATE3)
{
    si446x_cmd_buffer[0] = SI446X_CMD_ID_START_RX;
    si446x_cmd_buffer[1] = CHANNEL;
    si446x_cmd_buffer[2] = CONDITION;
    si446x_cmd_buffer[3] = (uint8_t)(RX_LEN >> 8);
    si446x_cmd_buffer[4] = (uint8_t)(RX_LEN);
    si446x_cmd_buffer[5] = NEXT_STATE1;
    si446x_cmd_buffer[6] = NEXT_STATE2;
    si446x_cmd_buffer[7] = NEXT_STATE3;

    SI446x_Send_Cmd(SI446X_CMD_ARG_COUNT_START_RX, si446x_cmd_buffer);
} // void Radio_Start_Rx(uint8_t CHANNEL, uint8_t CONDITION, uint16_t RX_LEN, uint8_t NEXT_STATE1, uint8_t NEXT_STATE2, uint8_t NEXT_STATE3)




/******************************************************************************/
/*
 *  Set Radio to RX mode, fixed packet length.
 * channel: Freq. Channel
 */
/******************************************************************************/
void SI446x_Start_Rx(uint8_t channel)
{
    // Read INTs, clear pending ones
    SI446x_Get_IntStatus(0, 0, 0);
/*
    // Start Receiving packet, channel 0, START immediately, Packet n bytes long
    Radio_Start_Rx(channel, 0, RadioConfiguration.Radio_PacketLength,
                  SI446X_CMD_START_RX_ARG_NEXT_STATE1_RXTIMEOUT_STATE_ENUM_RX,
                  SI446X_CMD_START_RX_ARG_NEXT_STATE2_RXVALID_STATE_ENUM_RX,
                  SI446X_CMD_START_RX_ARG_NEXT_STATE3_RXINVALID_STATE_ENUM_RX);
*/
#ifndef DEBUG_SI4463_SPISTATE
    // NOCHANGE	0	Remain in RX state if RXTIMEOUT occurs
    // REMAIN	0	Remain in RX state (but do not re-arm to acquire another packet).
    // RX	8	RX state (briefly exit and re-enter RX state to re-arm for acquisition of another packet).
    // RXTIMEOUT_STATE - RXVALID_STATE - RXINVALID_STATE
    //Radio_Start_Rx(channel, 0, RadioConfiguration.Radio_PacketLength, 0, 8, 8);
    Radio_Start_Rx(channel, 0, RADIO_XX_CONFIGURATION_DATA_RADIO_PACKET_LENGTH,
    		SI446X_CMD_CHANGE_STATE_ARG_NEXT_STATE1_NEW_STATE_ENUM_NOCHANGE,
				SI446X_CMD_CHANGE_STATE_ARG_NEXT_STATE1_NEW_STATE_ENUM_RX,
				SI446X_CMD_CHANGE_STATE_ARG_NEXT_STATE1_NEW_STATE_ENUM_RX);
#else
    SI446x_Change_State(SI446X_CMD_CHANGE_STATE_ARG_NEXT_STATE1_NEW_STATE_ENUM_SPI_ACTIVE);
#endif // #ifndef DEBUG_SI4463_SPISTATE
} // void SI446x_Start_Rx(uint8_t channel)



/******************************************************************************/
/*
 * Sends START_TX command to the radio.
 * CHANNEL:   Channel number.
 * CONDITION: Start TX condition.
 * TX_LEN:    Payload length (exclude the PH generated CRC).
 */
/******************************************************************************/
void Radio_Start_Tx(uint8_t CHANNEL, uint8_t CONDITION, uint16_t TX_LEN)
{
    si446x_cmd_buffer[0] = SI446X_CMD_ID_START_TX;
    si446x_cmd_buffer[1] = CHANNEL;
    si446x_cmd_buffer[2] = CONDITION;
    si446x_cmd_buffer[3] = (uint8_t)(TX_LEN >> 8);
    si446x_cmd_buffer[4] = (uint8_t)(TX_LEN);
    si446x_cmd_buffer[5] = 0x00;

    // Don't repeat the packet,
    // ie. transmit the packet only once
    si446x_cmd_buffer[6] = 0x00;

    SI446x_Send_Cmd(SI446X_CMD_ARG_COUNT_START_TX, si446x_cmd_buffer);
} // void Radio_Start_Tx(uint8_t CHANNEL, uint8_t CONDITION, uint16_t TX_LEN)



/******************************************************************************/
/*
 *  Set Radio to TX mode, fixed packet length.
 *  channel: Freq. Channel
 *  pradio_tx_buffer: Packet to be sent
 */
/******************************************************************************/
void SI446x_Start_Tx(uint8_t channel, uint8_t *pradio_tx_buffer, uint8_t length)
{
    uint8_t tx_channel;

    tx_channel = channel;
    if ( tx_channel==0x00 )
        tx_channel = pradioconfig->Radio_ChannelNumber;

    /* Leave RX state */
    SI446x_Change_State(SI446X_CMD_CHANGE_STATE_ARG_NEXT_STATE1_NEW_STATE_ENUM_READY);

    // Read INTs, clear pending ones
    SI446x_Get_IntStatus(0, 0, 0);

    /* Reset the Tx Fifo */
    SI446x_FiFoInfo(SI446X_CMD_FIFO_INFO_ARG_FIFO_TX_BIT);

    /* Fill the Tx FiFo with data */
    SI446x_Write_TxFiFo(length, pradio_tx_buffer);

    /* Start sending packet, channel 0, START immediately */
    Radio_Start_Tx(tx_channel, START_TX_COMPLETE_STATE_READY, length); // Enter the Ready state after completion of the packet transmission
} // void SI446x_Start_Tx(uint8_t channel, uint8_t *pradio_tx_buffer, uint8_t length)



/******************************************************************************/
/*
 * invoke radio image rejection self-calibration
 *
 */
/******************************************************************************/
void SI446x_Calibrate_IR(void)
{
    // send calibration sequence as per datasheet
    // note: looking at si446x_ircal.c in provided examples, CMD_IRCAL has 3 undocumented return values CAL_STATE, RSSI, DIR_CH
#ifdef SI446X_RADIO_IRCAL
    SI446x_Send_Cmd(SI446X_CMD_ARG_COUNT_IRCAL, (uint8_t *)Radio_IRcal_Sequence_Coarse);
    SI446x_Send_Cmd(SI446X_CMD_ARG_COUNT_IRCAL, (uint8_t *)Radio_IRcal_Sequence_Fine);
#endif // #ifdef SI446X_RADIO_IRCAL
} // SI446x_Calibrate_IR



/******************************************************************************/
/*
 *	Configure the GPIO0-GPIO3
 */
/******************************************************************************/
void SI446x_GPIO_Config(uint8_t GPIO0, uint8_t GPIO1, uint8_t GPIO2, uint8_t GPIO3, uint8_t NIRQ, uint8_t SDO, uint8_t GEN_CONFIG)
{
    si446x_cmd_buffer[0] = SI446X_CMD_ID_GPIO_PIN_CFG;
    si446x_cmd_buffer[1] = GPIO0;
    si446x_cmd_buffer[2] = GPIO1;
    si446x_cmd_buffer[3] = GPIO2;
    si446x_cmd_buffer[4] = GPIO3;
    si446x_cmd_buffer[5] = NIRQ;
    si446x_cmd_buffer[6] = SDO;
    si446x_cmd_buffer[7] = GEN_CONFIG;

    SI446x_Send_Cmd(SI446X_CMD_ARG_COUNT_GPIO_PIN_CFG, si446x_cmd_buffer);
    SI446x_Get_Resp(SI446X_CMD_REPLY_COUNT_GPIO_PIN_CFG, si446x_cmd_buffer);

    si446x_cmd.GPIO_PIN_CFG.GPIO[0]        = si446x_cmd_buffer[0];
    si446x_cmd.GPIO_PIN_CFG.GPIO[1]        = si446x_cmd_buffer[1];
    si446x_cmd.GPIO_PIN_CFG.GPIO[2]        = si446x_cmd_buffer[2];
    si446x_cmd.GPIO_PIN_CFG.GPIO[3]        = si446x_cmd_buffer[3];
    si446x_cmd.GPIO_PIN_CFG.NIRQ         = si446x_cmd_buffer[4];
    si446x_cmd.GPIO_PIN_CFG.SDO          = si446x_cmd_buffer[5];
    si446x_cmd.GPIO_PIN_CFG.GEN_CONFIG   = si446x_cmd_buffer[6];
} // void SI446x_GPIO_Config(uint8_t GPIO0, uint8_t GPIO1, uint8_t GPIO2, uint8_t GPIO3, uint8_t NIRQ, uint8_t SDO, uint8_t GEN_CONFIG)



/******************************************************************************/
/*
 * Reads back current GPIO pin configuration. Does NOT configure GPIO pins
 */
/******************************************************************************/
void SI446x_GPIO_ConfigFast(void)
{
    si446x_cmd_buffer[0] = SI446X_CMD_ID_GPIO_PIN_CFG;

    SI446x_Send_Cmd(1, si446x_cmd_buffer);
    SI446x_Get_Resp(SI446X_CMD_REPLY_COUNT_GPIO_PIN_CFG, si446x_cmd_buffer);

    si446x_cmd.GPIO_PIN_CFG.GPIO[0]        = si446x_cmd_buffer[0];
    si446x_cmd.GPIO_PIN_CFG.GPIO[1]        = si446x_cmd_buffer[1];
    si446x_cmd.GPIO_PIN_CFG.GPIO[2]        = si446x_cmd_buffer[2];
    si446x_cmd.GPIO_PIN_CFG.GPIO[3]        = si446x_cmd_buffer[3];
    si446x_cmd.GPIO_PIN_CFG.NIRQ         = si446x_cmd_buffer[4];
    si446x_cmd.GPIO_PIN_CFG.SDO          = si446x_cmd_buffer[5];
    si446x_cmd.GPIO_PIN_CFG.GEN_CONFIG   = si446x_cmd_buffer[6];
} // void SI446x_GPIO_Config(uint8_t GPIO0, uint8_t GPIO1, uint8_t GPIO2, uint8_t GPIO3, uint8_t NIRQ, uint8_t SDO, uint8_t GEN_CONFIG)



/******************************************************************************/
/*
 * Issue a change state command to the radio.
 * NEXT_STATE1 Next state.
 */
/******************************************************************************/
void SI446x_Change_State(uint8_t NEXT_STATE1)
{
    si446x_cmd_buffer[0] = SI446X_CMD_ID_CHANGE_STATE;
    si446x_cmd_buffer[1] = NEXT_STATE1;

    SI446x_Send_Cmd(SI446X_CMD_ARG_COUNT_CHANGE_STATE, si446x_cmd_buffer);
} // void SI446x_Change_State(uint8_t NEXT_STATE1)



/******************************************************************************/
/*
 * Send the FIFO_INFO command to the radio. Optionally resets the TX/RX FIFO. Reads the radio response back
 * FIFO  RX/TX FIFO reset flags.
 */
/******************************************************************************/
void SI446x_FiFoInfo(uint8_t FIFO)
{
    si446x_cmd_buffer[0] = SI446X_CMD_ID_FIFO_INFO;
    si446x_cmd_buffer[1] = FIFO;

    SI446x_Send_Cmd(SI446X_CMD_ARG_COUNT_FIFO_INFO, si446x_cmd_buffer);
    SI446x_Get_Resp(SI446X_CMD_REPLY_COUNT_FIFO_INFO, si446x_cmd_buffer);

    si446x_cmd.FIFO_INFO.RX_FIFO_COUNT   = si446x_cmd_buffer[0];
    si446x_cmd.FIFO_INFO.TX_FIFO_SPACE   = si446x_cmd_buffer[1];
} // void SI446x_FiFoInfo(uint8_t FIFO)



/******************************************************************************/
/*
 * Send Tx data to the Tx FiFo
 * FIFO  RX/TX FIFO reset flags.
 */
/******************************************************************************/
void SI446x_Write_TxFiFo(uint8_t numBytes, uint8_t *pTxData)
{
    uint8_t i, fifo_buffer[RF_PACKET_LEN_MAX + 1];

    if ( numBytes==0 )
    	return;
    if ( numBytes > RF_PACKET_LEN_MAX)
        numBytes = RF_PACKET_LEN_MAX;
    fifo_buffer[0] = SI446X_CMD_ID_WRITE_TX_FIFO;
    for (i=0; i<numBytes; i++)
    	fifo_buffer[i+1] = pTxData[i]; // move to tx buffer

    SI446x_Send_Cmd(numBytes + 1, fifo_buffer);
} // void SI446x_Write_TxFiFo(uint8_t numBytes, uint8_t *pTxData)



/******************************************************************************/
/*
 * Selects the type of modulation. In TX mode, additionally selects the source of the modulation.
 * NEW_MOD_TYPE:  TX_DIRECT_MODE_TYPE	TX_DIRECT_MODE_GPIO	MOD_SOURCE	MOD_TYPE
 */
/******************************************************************************/
void SI446x_Change_ModType(uint8_t NEW_MOD_TYPE)
{
    si446x_cmd_buffer[0] = SI446X_CMD_ID_SET_PROPERTY;
    si446x_cmd_buffer[1] = SI446X_GROUP_MODEM; // group ID
    si446x_cmd_buffer[2] = 0x01; // Number of properties
    si446x_cmd_buffer[3] = SI446X_GROUP_MODEM_PROPERTY_MODEM_MOD_TYPE; // start ID
    si446x_cmd_buffer[4] = NEW_MOD_TYPE; // Values
    SI446x_Send_Cmd(5, si446x_cmd_buffer);
} // void SI446x_Change_ModType(uint8_t NEW_MOD_TYPE)



/******************************************************************************/
/*
 * Reconfigure the attack and decay times of the OOK Peak Detector
 * NEW_PDTC:  default: 0x28
 */
void SI446x_Change_Modem_OOK_PDTC(uint8_t NEW_PDTC)
{
    si446x_cmd_buffer[0] = SI446X_CMD_ID_SET_PROPERTY;
    si446x_cmd_buffer[1] = SI446X_GROUP_MODEM; // group ID
    si446x_cmd_buffer[2] = 0x01; // Number of properties
    si446x_cmd_buffer[3] = SI446X_GROUP_MODEM_PROPERTY_MODEM_OOK_PDTC; // start ID
    si446x_cmd_buffer[4] = NEW_PDTC; // Values
    SI446x_Send_Cmd(5, si446x_cmd_buffer);
} // void SI446x_Change_Modem_OOK_PDTC(uint8_t NEW_PDTC)




/******************************************************************************/
/*
 * Change radio setting based on given mode
 *
 */
/******************************************************************************/
void SI446x_Change_Radio_Setting(uint8_t mode, uint8_t pa_power)
{
    switch (mode)
    {
        case RADIO_SETTING_MODE_PAPOWER:
            si446x_cmd_buffer[0] = SI446X_CMD_ID_SET_PROPERTY; // 0x11
            si446x_cmd_buffer[1] = SI446X_GRP_ID_0x22;
            si446x_cmd_buffer[2] = 2; // number of properties
            si446x_cmd_buffer[3] = SI446X_GRP_ID_0x22_PA_PWR_LVL; // first property index in the group
            si446x_cmd_buffer[4] = pa_power; // for PA_PWR_LVL, 7 bits
            si446x_cmd_buffer[5] = 0;
            SI446x_Send_Cmd(6, si446x_cmd_buffer);
            break;

        case RADIO_SETTING_MODE_CLEAR_INT:
            si446x_cmd_buffer[0] = SI446X_CMD_ID_GET_INT_STATUS; // 0x20
            si446x_cmd_buffer[1] = 0;
            si446x_cmd_buffer[2] = 0;
            si446x_cmd_buffer[3] = 0;
            SI446x_Send_Cmd(4, si446x_cmd_buffer);

        case RADIO_SETTING_MODE_TX:
            si446x_cmd_buffer[0] = SI446X_CMD_ID_START_TX;
            si446x_cmd_buffer[1] = 0; // CHANNEL;
            si446x_cmd_buffer[2] = 0; // CONDITION;
            si446x_cmd_buffer[3] = 0; // (uint8_t)(TX_LEN >> 8);
            si446x_cmd_buffer[4] = 0; //(uint8_t)(TX_LEN);
            si446x_cmd_buffer[5] = 0x00;
            si446x_cmd_buffer[6] = 0x00;
            SI446x_Send_Cmd(7, si446x_cmd_buffer);
            break;

        case RADIO_SETTING_MODE_IDLE:
            si446x_cmd_buffer[0] = SI446X_CMD_ID_CHANGE_STATE;
            si446x_cmd_buffer[1] = SI446X_STATE_SLEEP;
            SI446x_Send_Cmd(2, si446x_cmd_buffer);
            break;

        default: // unknown mode
            assert(0); // Should not reach here
            break;
    } // switch (mode)
} // void SI446x_Change_Radio_Setting(uint8_t mode, uint8_t pa_power)



/******************************************************************************/
/*
 * Start or stop the radio in CW mode
 *
 */
/******************************************************************************/
void SI446x_Start_Tx_CW(uint16_t channel, uint8_t radio_mod_type)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	if ( channel <= 255 ) // Turn on?
	{
		if  ( radio_state_flag & RADIO_STATE_TX_CW ) // Already in CW mode?
			return;
		radio_state_flag |= RADIO_STATE_TX_CW;
		; // Switch to antenna output for Tx
	    // Read INTs, clear pending ones
	    SI446x_Get_IntStatus(0, 0, 0);
	    SI446x_Change_State(SI446X_CMD_CHANGE_STATE_ARG_NEXT_STATE1_NEW_STATE_ENUM_READY);
		// Direct mode asynchronous mode, TX direct mode on GPIO2,  modulation is sourced in real-time, OOK
		// Mode: TX_DIRECT_MODE_TYPE[7]	TX_DIRECT_MODE_GPIO[6:5]	MOD_SOURCE[4:3]	MOD_TYPE[2:0]
		//					1					10						01				000
	    //																				CW		0
	    //																				OOK		1
	    //																				2FSK	2
	    //																				2GFSK	3
	    //																				4FSK	4
	    //																				4GFSK	5
		SI446x_Change_ModType(0xC8 | radio_mod_type);
		// Set GPIO2 as Input to receive direct Tx data from host, others unchanged
		SI446x_GPIO_Config(0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00);
		// Set this pin to output
		GPIO_InitStruct.Pin = SI4463_GPIO2_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(SI4463_GPIO2_GPIO_Port, &GPIO_InitStruct);

        // Raise Tx data to high level
        HAL_GPIO_WritePin(SI4463_GPIO2_GPIO_Port, SI4463_GPIO2_Pin, GPIO_PIN_SET);

        // Read INTs, clear pending ones
        SI446x_Get_IntStatus(0, 0, 0);

        /* Start sending packet, channel 0, START immediately */
    	Radio_Start_Tx(channel, START_TX_COMPLETE_STATE_NOCHANGE, 0); // Do not change state after completion of the packet transmission
		; // Start timer to automatically stop the CW mode after some time!
	} // if ( channel <= 255 )
	else
	{
		if  ( !(radio_state_flag & RADIO_STATE_TX_CW) ) // Not in CW mode?
			return;

        // Restore GPIO2 as Tx Data output from FIFO, others unchanged
        SI446x_GPIO_Config(0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x00);

		// Set this pin to input
		GPIO_InitStruct.Pin = SI4463_GPIO2_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(SI4463_GPIO2_GPIO_Port, &GPIO_InitStruct);

        // Direct mode asynchronous mode, TX direct mode on GPIO2,  modulation is sourced from the internal pseudo-random generator, OOK
		// Mode: TX_DIRECT_MODE_TYPE[7]	TX_DIRECT_MODE_GPIO[6:5]	MOD_SOURCE[4:3]	MOD_TYPE[2:0]
		//					1					10						00				001
        SI446x_Change_ModType(0xC0 | radio_mod_type);
        // Put the radio in sleep mode
        SI446x_Change_State(SI446X_CMD_CHANGE_STATE_ARG_NEXT_STATE1_NEW_STATE_ENUM_SLEEP);
        radio_state_flag = RADIO_STATE_IDLE; // reset status
	} // else
} // void SI446x_Start_Tx_CW(uint16_t channel, uint8_t radio_mod_type)



/******************************************************************************/
/*
 * Set new Tx power
 *
*/
/******************************************************************************/
void SI446x_Set_Tx_Power(uint8_t power)
{
    // Read INTs, clear pending ones
    SI446x_Get_IntStatus(0, 0, 0);
    SI446x_Change_Radio_Setting(RADIO_SETTING_MODE_PAPOWER, power);
} // void SI446x_Set_Tx_Power(uint8_t power)



/******************************************************************************/
/*
 * Select radio frontend network
 *
*/
/******************************************************************************/
void SI446x_Select_Frontend(S_M1_SubGHz_Band network)
{
	switch (network)
	{
		case SUB_GHZ_BAND_300:
		case SUB_GHZ_BAND_310:
		case SUB_GHZ_BAND_315:
		case SUB_GHZ_BAND_345:
		case SUB_GHZ_BAND_372:
		//case SUB_GHZ_BAND_390:
			HAL_GPIO_WritePin(SI4463_SW_V1_GPIO_Port, SI4463_SW_V1_Pin, GPIO_PIN_SET);
			HAL_GPIO_WritePin(SI4463_SW_V2_GPIO_Port, SI4463_SW_V2_Pin, GPIO_PIN_RESET);
			break;

		case SUB_GHZ_BAND_390:
		case SUB_GHZ_BAND_433:
		case SUB_GHZ_BAND_433_92:
	        HAL_GPIO_WritePin(SI4463_SW_V1_GPIO_Port, SI4463_SW_V1_Pin, GPIO_PIN_RESET);
	        HAL_GPIO_WritePin(SI4463_SW_V2_GPIO_Port, SI4463_SW_V2_Pin, GPIO_PIN_SET);
			break;

		case SUB_GHZ_BAND_915:
			HAL_GPIO_WritePin(SI4463_SW_V1_GPIO_Port, SI4463_SW_V1_Pin, GPIO_PIN_SET);
			HAL_GPIO_WritePin(SI4463_SW_V2_GPIO_Port, SI4463_SW_V2_Pin, GPIO_PIN_SET);
			break;

		default: // SUB_GHZ_BAND_EOL
			HAL_GPIO_WritePin(SI4463_SW_V1_GPIO_Port, SI4463_SW_V1_Pin, GPIO_PIN_RESET);
			HAL_GPIO_WritePin(SI4463_SW_V2_GPIO_Port, SI4463_SW_V2_Pin, GPIO_PIN_RESET);
			break;
	} // switch (network)
} // void SI446x_Select_Frontend(S_M1_SubGHz_Band network)




/******************************************************************************/
/*
 * This function initializes the radio with the predefined configuration
 * @Param: freq frequency range 315, 433, 915, etc.
 *
*/
/******************************************************************************/
void radio_init_rx_tx(S_M1_SubGHz_Band freq, uint8_t mod_type, bool do_reset)
{

	if ( freq >= SUB_GHZ_BAND_EOL )
		return;

	if ( do_reset )
	{
		SI446x_PowerUp();
	} // if ( do_reset )
	pradioconfig = RadioConfigList[freq];
	assert(pradioconfig!=NULL);
	radio_mod_type = mod_type & MODEM_MOD_TYPE_MASK;
	radio_last_outdiv = radio_band_outdiv[freq];

    /* Load radio configuration */
    while (SI446X_SUCCESS != SI446x_ConfigInit(pradioconfig->Radio_ConfigurationArray) )
    {
        HAL_Delay(1); // 10
        /* Power Up the radio chip */
        SI446x_PowerUp();
    } // while (SI446X_SUCCESS != SI446x_ConfigInit(pradioconfig->Radio_ConfigurationArray) )

    // Read INTs, clear pending ones
    SI446x_Get_IntStatus(0, 0, 0);

    radio_init_done = TRUE;
    radio_state_flag = RADIO_STATE_IDLE;
} // void radio_init_rx_tx(S_M1_SubGHz_Band freq, uint8_t mod_type, bool do_reset)



/*
 * SI4463 synthesizer coverage test used by the Record RAW extended frequency
 * list. Returns true if 'hz' falls in a band this driver can program with a
 * known-good base configuration (outdiv). This is a RECEIVE/tuning-capability
 * test only; it says nothing about TX power, RF matching, or regional policy.
 */
bool radio_freq_in_range(uint32_t hz)
{
    if ( hz >= 705000000UL && hz <= 1050000000UL ) return true;  /* outdiv 4  */
    if ( hz >= 353000000UL && hz <= 525000000UL  ) return true;  /* outdiv 8  */
    if ( hz >= 235000000UL && hz <= 350000000UL  ) return true;  /* outdiv 12 */
    return false;
} // bool radio_freq_in_range(uint32_t hz)



/*
 * radio_tune_exact_hz()
 *
 * Program the SI4463 to an EXACT frequency (integer Hz). A known-good base
 * configuration is loaded first (this sets MODEM_CLKGEN_BAND / outdiv and the
 * modem parameters for the target band); FC_INTE and FC_FRAC are then overridden
 * to place the PLL on the exact requested frequency.
 *
 * Synthesis (verified against the factory 300 / 434.059 / 915 configs):
 *     f_rf = (FC_INTE + FC_FRAC / 2^19) * (2 * XO / outdiv),   XO = 32 MHz
 *   =>  N       = f_rf * outdiv / (2 * XO)
 *       FC_INTE = floor(N) - 1
 *       FC_FRAC = round( (N - FC_INTE) * 2^19 )     (always in [2^19, 2^20))
 *
 * No silent fallback: an out-of-range / unrepresentable / un-applied tune is
 * reported via a RADIO_TUNE_ERR_* code. RX sensitivity, TX output power / RF
 * matching and regional TX enablement are NOT validated here (factory-pending).
 *
 * Returns RADIO_TUNE_OK or a RADIO_TUNE_ERR_* code.
 */
uint8_t radio_tune_exact_hz(uint32_t hz, uint8_t mod_type, bool do_reset)
{
    S_M1_SubGHz_Band base_band;
    uint32_t outdiv;
    double   n;
    uint8_t  fc_inte;
    uint32_t fc_frac;

    /* 1) Range -> outdiv + base config (SI4463 synthesizer bands). */
    if ( hz >= 705000000UL && hz <= 1050000000UL )
    {
        outdiv = 4;  base_band = SUB_GHZ_BAND_915;  /* only 900 MHz config   */
    }
    else if ( hz >= 353000000UL && hz <= 525000000UL )
    {
        outdiv = 8;  base_band = SUB_GHZ_BAND_433;  /* tested 434 OOK config */
    }
    else if ( hz >= 235000000UL && hz <= 350000000UL )
    {
        outdiv = 12; base_band = SUB_GHZ_BAND_300;
    }
    else
    {
        return RADIO_TUNE_ERR_RANGE;                /* outside coverage      */
    }

    /* 2) Compute FC_INTE / FC_FRAC for the exact frequency. */
    n = (double)hz * (double)outdiv / 64000000.0;   /* 2 * XO = 64 MHz       */
    if ( n < 2.0 || n >= 128.0 )
        return RADIO_TUNE_ERR_CALC;                 /* FC_INTE would not fit */
    fc_inte = (uint8_t)((uint32_t)n - 1U);
    fc_frac = (uint32_t)(((n - (double)fc_inte) * 524288.0) + 0.5); /* * 2^19 */
    if ( fc_frac < 0x080000UL || fc_frac > 0x0FFFFFUL )
        return RADIO_TUNE_ERR_CALC;                 /* FC_FRAC out of field  */

    /* 3) Load the base config (outdiv + modem), then override FREQ_CONTROL. */
    radio_init_rx_tx(base_band, mod_type, do_reset);
    SI446x_Select_Frontend(base_band);
    radio_last_outdiv = outdiv; /* exact-tune's range->outdiv is authoritative */

    si446x_cmd_buffer[0] = SI446X_CMD_ID_SET_PROPERTY;         /* 0x11             */
    si446x_cmd_buffer[1] = 0x40;                               /* FREQ_CONTROL grp */
    si446x_cmd_buffer[2] = 0x04;                               /* FC_INTE + 3 FRAC */
    si446x_cmd_buffer[3] = 0x00;                               /* start = FC_INTE  */
    si446x_cmd_buffer[4] = fc_inte;
    si446x_cmd_buffer[5] = (uint8_t)((fc_frac >> 16) & 0xFF);  /* FC_FRAC2         */
    si446x_cmd_buffer[6] = (uint8_t)((fc_frac >>  8) & 0xFF);  /* FC_FRAC1         */
    si446x_cmd_buffer[7] = (uint8_t)( fc_frac        & 0xFF);  /* FC_FRAC0         */
    if ( SI446x_Send_Cmd(8, si446x_cmd_buffer) != 0 )
        return RADIO_TUNE_ERR_APPLY;                /* radio CTS / apply fault */

    M1_LOG_D(M1_LOGDB_TAG, "TUNE hz=%lu outdiv=%lu INTE=0x%02X FRAC=0x%06lX\r\n",
             (unsigned long)hz, (unsigned long)outdiv, fc_inte, (unsigned long)fc_frac);
    return RADIO_TUNE_OK;
} // uint8_t radio_tune_exact_hz(uint32_t hz, uint8_t mod_type, bool do_reset)


/* ================= M1_SUBGHZ_MODEM_PURE_CORE BEGIN ================= */
/* Runtime modem-config decision math (Radio Settings, issue #3). Plain
 * C99 over stdint only -- no HAL, no FreeRTOS, no logging -- so the host
 * unit test (m1_csrc/test/m1_subghz_radio_settings_test.py) extracts
 * exactly this region and compiles it standalone. Encodings, formulas and
 * the provenance of every constant are documented here and asserted by
 * that test. */

/* Modulation preset -> raw MODEM_MOD_TYPE (property 0x2000 MOD_TYPE[2:0],
 * AN633): 1=OOK, 2=2-FSK, 3=2-GFSK, 4=4-FSK, 5=4-GFSK. Returns 0 for the
 * BAND_DEFAULT sentinel (keep the loaded preset's modulation) and for
 * unknown ids (refuse rather than guess). */
int8_t subghx_modem_mod_raw_code(uint8_t mod_preset)
{
	switch ( mod_preset )
	{
		case M1_SUBGHZ_MODP_OOK:   return 0x01;
		case M1_SUBGHZ_MODP_2FSK:  return 0x02;
		case M1_SUBGHZ_MODP_2GFSK: return 0x03;
		case M1_SUBGHZ_MODP_4FSK:  return 0x04;
		case M1_SUBGHZ_MODP_4GFSK: return 0x05;
		default:                   return 0;
	}
} // int8_t subghx_modem_mod_raw_code(uint8_t mod_preset)

/* FSK-family presets carry a frequency deviation; OOK/BAND_DEFAULT do not. */
uint8_t subghx_modem_mod_is_fsk(uint8_t mod_preset)
{
	return ( mod_preset>=M1_SUBGHZ_MODP_2FSK && mod_preset<=M1_SUBGHZ_MODP_4GFSK ) ? 1u : 0u;
} // uint8_t subghx_modem_mod_is_fsk(uint8_t mod_preset)

/* MODEM_FREQ_DEV (0x200A:200C, 17-bit) encoding, verified against the
 * repo's own WDS-generated FSK headers:
 *   Fdev = FreqDev * (Npresc * fxo) / (2^19 * outdiv), Npresc=2, fxo=32 MHz
 *     => FreqDev = round( Fdev * 2^19 * outdiv / (Npresc * fxo) )
 * Verified points: 2000 Hz @ outdiv 8 -> 131 (m1_sub_ghz_433_rxtx_...
 * dev2_e1_10_fsk.h emits 0x000083) and 200000 Hz @ outdiv 4 -> 6554
 * (m1_sub_ghz_915_..._dev200_e1_10_fsk.h emits 0x00199A). Returns 1 and
 * the word when representable, 0 otherwise (never clamps a deviation:
 * an unrepresentable request is refused). */
uint8_t subghx_modem_dev_word(uint32_t dev_hz, uint32_t outdiv, uint32_t *p_word)
{
	uint64_t word;

	if ( (p_word==NULL) || (dev_hz==0u) )
		return 0;
	switch ( outdiv )
	{
		case 4: case 6: case 8: case 12: case 16:
			break;
		default:
			return 0; /* unknown loaded divider: refuse, never guess */
	}
	word = ( (uint64_t)dev_hz * 524288uLL * (uint64_t)outdiv + 32000000uLL ) / 64000000uLL;
	if ( (word==0uLL) || (word>0x1FFFFuLL) )
		return 0; /* 17-bit field */
	*p_word = (uint32_t)word;
	return 1;
} // uint8_t subghx_modem_dev_word(uint32_t dev_hz, uint32_t outdiv, uint32_t *p_word)

/* "Auto" deviation for an FSK modulation on top of a non-FSK base preset
 * (the OOK base presets carry FREQ_DEV = 0, which would make a 2-FSK
 * override transmit with zero deviation): fall back to the deviation the
 * repo's own WDS FSK headers generated for that band family - 200 kHz on
 * the 900 MHz config (m1_sub_ghz_915_..._dev200_e1_10_fsk.h), 2 kHz below
 * (m1_sub_ghz_433_..._dev2_e1_10_fsk.h). Values are WDS-generated facts,
 * not computed here. */
uint32_t subghx_modem_dev_auto_hz(uint8_t base_band_900mhz)
{
	return base_band_900mhz ? 200000UL : 2000UL;
} // uint32_t subghx_modem_dev_auto_hz(uint8_t base_band_900mhz)

/* Nominal dBm -> PA_PWR_LVL (property 0x2201, DDAC[6:0] 0..127) map.
 * There is no public dBm formula for the Si4463 PA (AN648 publishes
 * measured curves only), so this is a documented NOMINAL approximation:
 * linear in code between the two anchors
 *   PA_PWR_LVL 0x7F (127) <-> +20 dBm  -- every WDS +20 dBm config in
 *                                         radio_config/ writes 0x7F, and
 *                                         the stock TX path's 0xFF hits
 *                                         the same 7-bit DDAC
 *   PA_PWR_LVL 0x00       <-> -10 dBm  -- nominal floor of the range
 * so lvl = round((dbm + 10) * 127 / 30). Intermediate points are labelled
 * approximate (measured output departs from linear-in-code); absolute
 * accuracy is a hardware-acceptance item (follow-up issue). Returns -1
 * outside M1_SUBGHZ_POWER_DBM_MIN..MAX. */
int16_t subghx_power_dbm_to_pa_lvl(int8_t power_dbm)
{
	if ( (power_dbm < M1_SUBGHZ_POWER_DBM_MIN) || (power_dbm > M1_SUBGHZ_POWER_DBM_MAX) )
		return -1;
	return (int16_t)( ( (int32_t)(power_dbm - M1_SUBGHZ_POWER_DBM_MIN) * 127 + 15 ) / 30 );
} // int16_t subghx_power_dbm_to_pa_lvl(int8_t power_dbm)

/* RX-bandwidth preset table -- every constant below is copied VERBATIM
 * from the repo's WDS-generated headers in Sub_Ghz/radio_config/ (WDS GUI
 * 3.2.11.0, Si4463 Rev C2); none is computed or invented here. The three
 * selectable bandwidths are the three distinct filter+decimation sets the
 * repo actually has. WDS varies, with RXBW: the CIC decimation trio
 * (MODEM_DECIMATION_CFG1/0/2), the AFC limiter, the (G)FSK spike-detector
 * threshold, the DSA eye-opening qualifier, and the 36 channel-filter
 * coefficients -- proven by cross-band diffs of otherwise-identical
 * headers (e.g. the bw100 vs bw200 433/300/915 OOK configs differ in
 * exactly these properties). The 100 kHz and 200 kHz sets share the same
 * FIR coefficients and differ in decimation, exactly as WDS emitted them.
 *
 * WDS-nominal achieved bandwidths (each source header's own comment):
 *   10 kHz request  -> "WB filter 7 (BW = 10.75 kHz)"
 * 100 kHz request  -> "WB filter 2 (BW = 109.93 kHz)"
 * 200 kHz request  -> "WB filter 2 (BW = 219.87 kHz)"
 *
 * Source headers:
 *  10 kHz : si4463_revc2_433_075_rxtx_spacing25_bw10_baud4_8_error1_10_OOK.h
 *           (identical BW fields in m1_sub_ghz_304_rxtx_spc25_bw10_..._ook.h
 *           and si4463_revc2_915_00_rxtx_spacing25_bw10_..._OOK.h)
 * 100 kHz : si4463_revc2_433_92_rxtx_spacing250_bw100_baud10_error1_10_OOK.h
 *           (identical in the 310/315/390 bw100 headers)
 * 200 kHz : si4463_revc2_433_rxtx_spacing250_bw200_baud10_error1_10_OOK.h
 *           (identical in the 300/372/915 bw200 headers)
 * Known quirks, documented not hidden: the bw10 sets were generated at
 * 4.8 kbaud (their BCR_OSR stays with the loaded preset - baud-coupled,
 * deliberately NOT patched here); the 345 MHz "bw100" header actually
 * carries the 200k-style tuple (WDS generation quirk) - the table keys on
 * the tuple, not the filename; the AFC limiter varies by a few LSB across
 * outdivs at equal BW (0x0709/0x0712 @100k, 0x0DD6/0x0DE7 @200k) and AFC
 * is disabled in every source config, so the majority value is used; the
 * bw10 AFC limiter is the 433.075 value (the 304.5 header emits 0x0175,
 * 3 LSB lower). MODEM_RAW_CONTROL / RAW_EYE differ across bands for
 * non-BW reasons and stay with the loaded preset. 4-FSK/4-GFSK reuse the
 * loaded preset's FSK4 slicer gains/thresholds (hardware acceptance). */
typedef struct
{
	uint16_t bw_khz;                                  /* nominal request        */
	uint8_t  decimation_cfg1, decimation_cfg0, decimation_cfg2; /* 0x201E:20 */
	uint8_t  afc_limiter_1, afc_limiter_0;            /* 0x2030:31              */
	uint8_t  spike_det;                               /* 0x2054                 */
	uint8_t  dsa_qual;                                /* 0x205D                 */
	uint8_t  chflt[SI446X_MODEM_CHFLT_COE_N];         /* 0x2100..0x2123         */
} S_Si446xBwPreset_t;

static const S_Si446xBwPreset_t subghx_bw_presets[] =
{
	{ 10u, 0x72, 0x10, 0x0C, 0x01, 0x78, 0x3B, 0x7F,
		{ 0x39, 0x2B, 0x00, 0xC3, 0x7F, 0x3F, 0x0C, 0xEC, 0xDC, 0xDC, 0xE3, 0xED,
		  0xF6, 0xFD, 0x15, 0xC0, 0xFF, 0x0F,
		  0x39, 0x2B, 0x00, 0xC3, 0x7F, 0x3F, 0x0C, 0xEC, 0xDC, 0xDC, 0xE3, 0xED,
		  0xF6, 0xFD, 0x15, 0xC0, 0xFF, 0x0F } },
	{ 100u, 0x24, 0x20, 0x00, 0x07, 0x09, 0x14, 0x34,
		{ 0xFF, 0xC4, 0x30, 0x7F, 0xF5, 0xB5, 0xB8, 0xDE, 0x05, 0x17, 0x16, 0x0C,
		  0x03, 0x00, 0x15, 0xFF, 0x00, 0x00,
		  0xFF, 0xC4, 0x30, 0x7F, 0xF5, 0xB5, 0xB8, 0xDE, 0x05, 0x17, 0x16, 0x0C,
		  0x03, 0x00, 0x15, 0xFF, 0x00, 0x00 } },
	{ 200u, 0x16, 0x20, 0x00, 0x0D, 0xD6, 0x0A, 0x1E,
		{ 0xFF, 0xC4, 0x30, 0x7F, 0xF5, 0xB5, 0xB8, 0xDE, 0x05, 0x17, 0x16, 0x0C,
		  0x03, 0x00, 0x15, 0xFF, 0x00, 0x00,
		  0xFF, 0xC4, 0x30, 0x7F, 0xF5, 0xB5, 0xB8, 0xDE, 0x05, 0x17, 0x16, 0x0C,
		  0x03, 0x00, 0x15, 0xFF, 0x00, 0x00 } },
};
#define SUBGHX_BW_PRESET_N   (sizeof(subghx_bw_presets)/sizeof(subghx_bw_presets[0]))

/* Selectable-bandwidth lookup: index into subghx_bw_presets[], or -1 when
 * bw_khz is 0 (keep the loaded preset's own filter set) or unknown. */
int16_t subghx_modem_bw_preset_index(uint16_t bw_khz)
{
	uint16_t i;

	if ( bw_khz==0u )
		return -1;
	for ( i=0; i<(uint16_t)SUBGHX_BW_PRESET_N; i++ )
	{
		if ( subghx_bw_presets[i].bw_khz==bw_khz )
			return (int16_t)i;
	}
	return -1;
} // int16_t subghx_modem_bw_preset_index(uint16_t bw_khz)

/* ================= M1_SUBGHZ_MODEM_PURE_CORE END =================== */



/******************************************************************************/
/*
 * Generic SET_PROPERTY (0x11) writer. num_props is capped at 12 exactly as
 * the command specifies (SI446X_CMD_SET_PROPERTY_ARG_NUM_PROPS_MAX) and as
 * WDS chunks its own property writes; the radio comm buffer (30 bytes)
 * bounds a single command to 4 + 12 bytes anyway.
 *
 */
/******************************************************************************/
uint8_t SI446x_Set_Properties(uint8_t group, uint8_t start_prop, uint8_t num_props, const uint8_t *p_values)
{
	uint8_t i;

	if ( (num_props==0u) || (num_props>12u) || (p_values==NULL) )
		return 1;

	si446x_cmd_buffer[0] = SI446X_CMD_ID_SET_PROPERTY;
	si446x_cmd_buffer[1] = group;
	si446x_cmd_buffer[2] = num_props;
	si446x_cmd_buffer[3] = start_prop;
	for ( i=0; i<num_props; i++ )
		si446x_cmd_buffer[4u+i] = p_values[i];

	if ( SI446x_Send_Cmd((uint8_t)(num_props + 4u), si446x_cmd_buffer)!=0 )
		return 1;
	if ( SI446x_Get_Resp(0, NULL)!=SI4463_CTS_READY )
		return 1;
	return 0;
} // uint8_t SI446x_Set_Properties(...)



/******************************************************************************/
/*
 * SI446x_Apply_Modem_Config() -- apply the user's Radio Settings on top of
 * the just-loaded WDS base configuration. See m1_sub_ghz_api.h for the
 * contract and M1_SUBGHZ_MODEM_PURE_CORE above for every encoding.
 * Ordering: modulation first (so a deviation lands under the requested
 * modulation), then deviation (FSK presets only), then the RX-bandwidth
 * preset (decimation trio, AFC limiter, spike/DSA thresholds, 36 CHFLT
 * coefficients in three 12-property writes exactly like WDS). Power is
 * NOT written here: the mapped PA_PWR_LVL byte is returned through
 * *tx_power_out and the TX path writes it right before Radio_Start_Tx,
 * which also keeps CW/direct-mode callers on their own sequencing.
 * Returns the number of refused property writes (0 = fully applied).
 *
 */
/******************************************************************************/
uint8_t SI446x_Apply_Modem_Config(const S_M1_Si446x_ModemCfg_t *cfg, uint8_t *mod_type_out, uint8_t *tx_power_out)
{
	int8_t   raw_mod;
	int16_t  bw_idx;
	int16_t  pa_lvl;
	uint8_t  faults;
	uint8_t  i;
	uint8_t  v[3];
	uint32_t dev_hz;
	uint32_t dev_word;

	if ( cfg==NULL )
		return 1;
	faults = 0;

	/* Modulation: plain MODEM_MOD_TYPE (a TX direct-mode caller re-encodes
	 * 0xC8|raw itself with the returned code). */
	raw_mod = subghx_modem_mod_raw_code(cfg->mod_preset);
	if ( raw_mod > 0 )
	{
		SI446x_Change_ModType((uint8_t)raw_mod);
		if ( mod_type_out!=NULL )
			*mod_type_out = (uint8_t)raw_mod;
	}

	/* Deviation: FSK presets only (OOK ignores MODEM_FREQ_DEV). "Auto"
	 * falls back to the band-family WDS FSK deviation so an FSK override
	 * on an OOK base preset never transmits zero deviation. */
	if ( subghx_modem_mod_is_fsk(cfg->mod_preset)!=0u )
	{
		dev_hz = ( cfg->dev_khz!=0u ) ? (uint32_t)cfg->dev_khz * 1000UL
		                              : subghx_modem_dev_auto_hz(cfg->base_band_900mhz);
		if ( subghx_modem_dev_word(dev_hz, radio_last_outdiv, &dev_word)!=0u )
		{
			v[0] = (uint8_t)((dev_word >> 16) & 0xFFUL);
			v[1] = (uint8_t)((dev_word >>  8) & 0xFFUL);
			v[2] = (uint8_t)( dev_word        & 0xFFUL);
			faults += SI446x_Set_Properties(SI446X_GROUP_MODEM, SI446X_MODEM_PROP_FREQ_DEV, 3, v);
		}
		else
		{
			M1_LOG_E(M1_LOGDB_TAG, "Modem cfg: dev %lu kHz @outdiv %lu refused\r\n",
			         (unsigned long)((cfg->dev_khz!=0u) ? cfg->dev_khz : (dev_hz / 1000UL)),
			         (unsigned long)radio_last_outdiv);
			faults++;
		}
	}

	/* RX bandwidth preset (skipped for the 0 kHz "keep" sentinel). */
	bw_idx = subghx_modem_bw_preset_index(cfg->bw_khz);
	if ( bw_idx >= 0 )
	{
		const S_Si446xBwPreset_t *bp = &subghx_bw_presets[bw_idx];

		v[0] = bp->decimation_cfg1;
		v[1] = bp->decimation_cfg0;
		v[2] = bp->decimation_cfg2;
		faults += SI446x_Set_Properties(SI446X_GROUP_MODEM, SI446X_MODEM_PROP_DECIMATION_CFG1, 3, v);
		v[0] = bp->afc_limiter_1;
		v[1] = bp->afc_limiter_0;
		faults += SI446x_Set_Properties(SI446X_GROUP_MODEM, SI446X_MODEM_PROP_AFC_LIMITER_1, 2, v);
		v[0] = bp->spike_det;
		faults += SI446x_Set_Properties(SI446X_GROUP_MODEM, SI446X_MODEM_PROP_SPIKE_DET, 1, v);
		v[0] = bp->dsa_qual;
		faults += SI446x_Set_Properties(SI446X_GROUP_MODEM, SI446X_MODEM_PROP_DSA_QUAL, 1, v);
		for ( i=0; i<(SI446X_MODEM_CHFLT_COE_N/12u); i++ )
		{
			faults += SI446x_Set_Properties(SI446X_GROUP_CHFLT, (uint8_t)(i*12u), 12, &bp->chflt[i*12u]);
		}
	}

	/* Nominal power: map dBm to the PA DDAC byte for the TX path. */
	pa_lvl = subghx_power_dbm_to_pa_lvl(cfg->power_dbm);
	if ( (pa_lvl >= 0) && (tx_power_out!=NULL) )
	{
		*tx_power_out = (uint8_t)pa_lvl;
	}
	else if ( pa_lvl < 0 )
	{
		M1_LOG_E(M1_LOGDB_TAG, "Modem cfg: power %d dBm out of range, stock PA kept\r\n", (int)cfg->power_dbm);
	}

	if ( faults!=0u )
		M1_LOG_E(M1_LOGDB_TAG, "Modem cfg: %u property write(s) refused\r\n", (unsigned)faults);
	return faults;
} // uint8_t SI446x_Apply_Modem_Config(...)



/******************************************************************************/
/*
 * outdiv of the most recently loaded configuration (0 = unknown); see the
 * radio_last_outdiv declaration above for the provenance of the mapping.
 *
 */
/******************************************************************************/
uint32_t radio_get_last_outdiv(void)
{
	return radio_last_outdiv;
} // uint32_t radio_get_last_outdiv(void)



/******************************************************************************/
/*
 * This function reinitializes the radio with the configuration patch
 * @Param:
 *
*/
/******************************************************************************/
void radio_patch_init(void)
{
	pradioconfig = &RadioConfiguration_Patch;
	assert(pradioconfig!=NULL);

    if ( SI446X_SUCCESS==SI446x_ConfigInit(pradioconfig->Radio_ConfigurationArray) )
    {
    	si446x_got_reset = FALSE; // Reset
    	M1_LOG_I(M1_LOGDB_TAG, "SI446x patch applied!\r\n");
    } // if ( SI446X_SUCCESS==SI446x_ConfigInit(pradioconfig->Radio_ConfigurationArray) )

    // Read INTs, clear pending ones
    SI446x_Get_IntStatus(0, 0, 0);
} // void radio_patch_init(void)



/******************************************************************************/
/*
 * This function returns TRUE if the radio has been initialized.
 * @Param:
 *
*/
/******************************************************************************/
uint8_t radio_get_init_state(void)
{
	return radio_init_done;
} // uint8_t radio_get_init_state(void)



/******************************************************************************/
/*
 * This function changes RF antenna switch mode.
 * @Param:
 *
*/
/******************************************************************************/
void radio_set_antenna_mode(tRadioAntennaMode mode)
{
	switch ( mode )
	{
		case RADIO_ANTENNA_MODE_ISOLATED:
			HAL_GPIO_WritePin(SI4463_RFSW1_GPIO_Port, SI4463_RFSW1_Pin, GPIO_PIN_RESET);
			HAL_GPIO_WritePin(SI4463_RFSW2_GPIO_Port, SI4463_RFSW2_Pin, GPIO_PIN_RESET);
			break;

		case RADIO_ANTENNA_MODE_RX:
			HAL_GPIO_WritePin(SI4463_RFSW1_GPIO_Port, SI4463_RFSW1_Pin, GPIO_PIN_SET);
			HAL_GPIO_WritePin(SI4463_RFSW2_GPIO_Port, SI4463_RFSW2_Pin, GPIO_PIN_RESET);
			break;

		case RADIO_ANTENNA_MODE_TX:
			HAL_GPIO_WritePin(SI4463_RFSW1_GPIO_Port, SI4463_RFSW1_Pin, GPIO_PIN_RESET);
			HAL_GPIO_WritePin(SI4463_RFSW2_GPIO_Port, SI4463_RFSW2_Pin, GPIO_PIN_SET);
			break;

		default:
			break;
	} // switch ( mode )
} // void radio_set_antenna_mode(tRadioAntennaMode mode)
