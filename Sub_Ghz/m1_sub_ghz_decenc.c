/* See COPYING.txt for license details. */

/*
*
*  m1_sub_ghz_decenc.c
*
*  M1 sub-ghz decoding encoding
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "stm32h5xx_hal.h"
#include <m1_sub_ghz_decenc.h>
#include "m1_sub_ghz.h"
#include "m1_sub_ghz_api.h"
#include "si446x_cmd.h"
#include "m1_io_defs.h" // Test only
#include "m1_log_debug.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"SUBGHZ_DECENC"

//************************** C O N S T A N T **********************************/

const SubGHz_protocol_t subghz_protocols_list[] =
{
	/*{160, 470, PACKET_PULSE_TIME_TOLERANCE20, 0, 24}, // Princeton: bit 0 |^|___, bit 1 |^^^|_*/
	{370, 1140, PACKET_PULSE_TIME_TOLERANCE20, 0, 24}, // Princeton: bit 0 |^|___, bit 1 |^^^|_
	{250, 500, PACKET_PULSE_TIME_TOLERANCE20, 16, 46} // Security+ 2.0: bit 0 |^|___, bit 1 |^|_
};

const char *protocol_text[] =
{
	"Princeton",
	"Security+ 2.0"
};


enum {
   n_protocol = sizeof(subghz_protocols_list) / sizeof(subghz_protocols_list[0])
};


//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

SubGHz_DecEnc_t subghz_decenc_ctl;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

inline uint16_t get_diff(uint16_t n_a, uint16_t n_b);
uint8_t subghz_pulse_handler(uint16_t duration);
static bool subghz_decode_protocol(uint16_t p, uint16_t pulsecount);
bool subghz_decenc_read(SubGHz_Dec_Info_t *received, bool raw);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
inline uint16_t get_diff(uint16_t n_a, uint16_t n_b)
{
	return abs(n_a - n_b);
}



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
bool subghz_data_ready()
{
  return (subghz_decenc_ctl.n64_decodedvalue != 0);
}


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
bool subghz_raw_data_ready()
{
  return (subghz_decenc_ctl.pulse_times[0] != 0);
}


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
void subghz_reset_data()
{
	subghz_decenc_ctl.n64_decodedvalue = 0;
	subghz_decenc_ctl.ndecodedbitlength = 0;
	memset(subghz_decenc_ctl.pulse_times, 0, sizeof(subghz_decenc_ctl.pulse_times));
}


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
uint64_t subghz_get_decoded_value()
{
	return subghz_decenc_ctl.n64_decodedvalue;
}


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
uint16_t subghz_get_decoded_bitlength()
{
	return subghz_decenc_ctl.ndecodedbitlength;
}


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
uint16_t subghz_get_decoded_delay()
{
	return subghz_decenc_ctl.ndecodeddelay;
}


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
uint16_t subghz_get_decoded_protocol()
{
	return subghz_decenc_ctl.ndecodedprotocol;
}


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
int16_t subghz_get_decoded_rssi()
{
	return subghz_decenc_ctl.ndecodedrssi;
}


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
uint16_t *subghz_get_rawdata()
{
	return subghz_decenc_ctl.pulse_times;
}



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
bool subghz_decode_protocol(uint16_t p, uint16_t pulsecount)
{
    uint8_t ret = false;

    switch ( p )
    {
    	case PRINCETON:
    		ret = subghz_decode_princeton(p, pulsecount);
    		break;

    	case SECURITY_PLUS_20:
    		ret = subghz_decode_security_plus_20(p, pulsecount);
    		break;

    	default:
    		break;
    } // switch ( p )

    return ret;
} // bool subghz_decode_protocol(uint16_t p, uint16_t subghz_decenc_ctl.npulsecount)


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
uint8_t subghz_pulse_handler(uint16_t duration)
{
	  static uint32_t interpacket_gap = 0;
	  uint8_t i;
	  int16_t rssi;
	  struct si446x_reply_GET_MODEM_STATUS_map *pmodemstat;

	  if (duration >= PACKET_PULSE_TIME_MIN)
	  {
		  if (duration >= INTERPACKET_GAP_MIN) // Possible gap between packets?
		  {
			  subghz_decenc_ctl.pulse_times[subghz_decenc_ctl.npulsecount++] = duration; // End bit

			  M1_LOG_D(M1_LOGDB_TAG, "Valid gap: %d, pulses:%d\r\n", duration, subghz_decenc_ctl.npulsecount);
			  if ( subghz_decenc_ctl.npulsecount >= PACKET_PULSE_COUNT_MIN ) // Potential packet received?
			  {
				  for(i = 0; i < n_protocol; i++)
				  {
					  if ( !subghz_decode_protocol(i, subghz_decenc_ctl.npulsecount) )
					  {
						  // receive successfully for protocol i
						  break;
					  }
				  } // for(i = 0; i < n_protocol; i++)
			  } // if ( subghz_decenc_ctl.npulsecount >= PACKET_PULSE_COUNT_MIN )
			  interpacket_gap = duration; // update
			  subghz_decenc_ctl.npulsecount = 0;
			  // A potential interpacket gap has been detected, so it's not required to check for this condition for the next packet, if any.
			  return PULSE_DET_EOP; // error or end of packet has been met
		  } // if (duration >= INTERPACKET_GAP_MIN)
	  } // if (duration >= PACKET_PULSE_TIME_MIN)
	  else
	  {
		  subghz_decenc_ctl.npulsecount = 0; // reset
		  interpacket_gap += duration;
		  // Interpacket gap has been timeout for a potential packet
		  if ( interpacket_gap > INTERPACKET_GAP_MAX )
		  {
			  interpacket_gap = 0; // reset
			  return PULSE_DET_IDLE; // error
		  }
		  else
		  {
			  return PULSE_DET_NORMAL;
		  }
	  } // else
	  // detect overflow
	  if (subghz_decenc_ctl.npulsecount >= PACKET_PULSE_COUNT_MAX)
	  {
		  subghz_decenc_ctl.npulsecount = 0; // Reset rx buffer
		  return PULSE_DET_IDLE; // error
	  }
	  subghz_decenc_ctl.pulse_times[subghz_decenc_ctl.npulsecount++] = duration;
	  // Read RSSI when half of this potential packet has been received
	  if ( subghz_decenc_ctl.npulsecount==PACKET_PULSE_COUNT_MIN/2 )
	  {
		  // Read INTs, clear pending ones
		  SI446x_Get_IntStatus(0, 0, 0);
		  pmodemstat = SI446x_Get_ModemStatus(0x00);
		  // RF_Input_Level_dBm = (RSSI_value / 2) – MODEM_RSSI_COMP – 70
		  rssi = pmodemstat->CURR_RSSI/2 - MODEM_RSSI_COMP - 70;
		  subghz_decenc_ctl.ndecodedrssi = rssi;
	  } // if ( subghz_decenc_ctl.npulsecount==PACKET_PULSE_COUNT_MIN/2 )

	  return PULSE_DET_NORMAL;
} // uint8_t subghz_pulse_handler(uint16_t duration)



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
bool subghz_decenc_read(SubGHz_Dec_Info_t *received, bool raw)
{
    bool ret = false;

	if ( subghz_decenc_ctl.subghz_data_ready() )
    {
        uint64_t value = subghz_decenc_ctl.subghz_get_decoded_value();
        if (value)
        {
            received->frequency = 0;
            received->key = subghz_decenc_ctl.subghz_get_decoded_value();
            received->protocol = subghz_decenc_ctl.subghz_get_decoded_protocol();
            received->rssi = subghz_decenc_ctl.subghz_get_decoded_rssi();
            received->te = subghz_decenc_ctl.subghz_get_decoded_delay();
            received->bit_len = subghz_decenc_ctl.subghz_get_decoded_bitlength();
        } // if (value)
        subghz_decenc_ctl.subghz_reset_data();
        ret = true;
    } // if ( subghz_decenc_ctl.subghz_data_ready() )

    if (raw && subghz_decenc_ctl.subghz_raw_data_ready())
    {
    	received->raw = true;
    	received->raw_data = subghz_decenc_ctl.subghz_get_rawdata();
        subghz_decenc_ctl.subghz_reset_data();
        ret = true;
    } // if (raw && subghz_decenc_ctl.subghz_raw_data_ready())

    return ret;
} // bool subghz_decenc_read(bool raw)


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
void subghz_decenc_init(void)
{
    subghz_decenc_ctl.subghz_data_ready = subghz_data_ready;
    subghz_decenc_ctl.subghz_raw_data_ready = subghz_raw_data_ready;
    subghz_decenc_ctl.subghz_reset_data = subghz_reset_data;

    subghz_decenc_ctl.subghz_get_decoded_value = subghz_get_decoded_value;
    subghz_decenc_ctl.subghz_get_decoded_bitlength = subghz_get_decoded_bitlength;
    subghz_decenc_ctl.subghz_get_decoded_delay = subghz_get_decoded_delay;
    subghz_decenc_ctl.subghz_get_decoded_protocol = subghz_get_decoded_protocol;
    subghz_decenc_ctl.subghz_get_decoded_rssi = subghz_get_decoded_rssi;
    subghz_decenc_ctl.subghz_get_rawdata = subghz_get_rawdata;
    subghz_decenc_ctl.subghz_pulse_handler = subghz_pulse_handler;

	subghz_decenc_ctl.n64_decodedvalue = 0;
	subghz_decenc_ctl.ndecodedbitlength = 0;
	subghz_decenc_ctl.ndecodedrssi = 0;
	subghz_decenc_ctl.ndecodeddelay = 0;
	subghz_decenc_ctl.ndecodedprotocol = 0;
	subghz_decenc_ctl.npulsecount = 0;
	subghz_decenc_ctl.pulse_det_stat = PULSE_DET_IDLE;
	memset(subghz_decenc_ctl.pulse_times, 0, sizeof(subghz_decenc_ctl.pulse_times));
	subghz_decenc_ctl.n64_decodedvalue = 0;
} // void subghz_decenc_init(void)
