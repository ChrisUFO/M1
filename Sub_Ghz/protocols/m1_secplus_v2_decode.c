/* See COPYING.txt for license details. */

/*
*
*  m1_secplus_v2_decode.c
*
*  M1 sub-ghz Security plus V2 decoding
*
* M1 Project
*
* Reference: https://github.com/argilo/secplus
*/

/*************************** I N C L U D E S **********************************/
#include <string.h>
#include <stdlib.h>
#include "stm32h5xx_hal.h"
#include "bit_util.h"
#include "m1_sub_ghz_decenc.h"
#include "m1_log_debug.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"SUBGHZ_SEC+V2"

/**
      * Packet:  | 001111 xx | xx xxxx xx|xx dddddd | dddddddd | dddddddd | dddddddd |
      * 				 Type  ID  ORD  INV  Data								   LSB
      * Type: payload type (00 or 01)
      * ID: Frame ID (always 00)
      * ORD: Order indicator
      * INV: Inversion indicator
*/
#define SEC_PLUS_V2_HEADER_MASK 	0x03FFC0000000
#define SEC_PLUS_V2_PACKET_X_MASK  	0x030000000000
#define SEC_PLUS_V2_ID_MASK  		0x00C000000000

//************************** C O N S T A N T **********************************/



//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/


/********************* F U N C T I O N   P R O T O T Y P E S ******************/

uint8_t subghz_decode_security_plus_20(uint16_t p, uint16_t pulsecount);
uint8_t m1_secplus_v2_decode(uint32_t fixed[], uint8_t half_codes[][10], uint32_t *rolling_code, uint64_t *out_bits);
uint8_t m1_secplus_v2_decode_half(uint64_t in_bits, uint8_t *half_code, uint32_t *out_bits);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
uint8_t subghz_decode_security_plus_20(uint16_t p, uint16_t pulsecount)
{
    uint64_t code;
    static uint64_t prev_code;
    uint16_t te_long, te_short, i;
    uint16_t tolerance_long, tolerance_short;
    uint8_t odd_bit, bit_pulses, prev_bit_high;
    uint8_t ret, state, preamble_count, data_count;
    uint8_t half_codes[2][10];
    static uint8_t rx_packets = 0;
    uint32_t fixed[2], rolling_code;
    const SubGHz_protocol_t *plist;

    plist = &subghz_protocols_list[p];

    te_short = plist->te_short;
    te_long = plist->te_long;
    tolerance_short = (te_short*plist->te_tolerance)/100;
    tolerance_long = (te_long*plist->te_tolerance)/100;

    state = 0;
    preamble_count = 0;
    data_count = 0;
    odd_bit = 0;
    ret = 0;

    for (i = 0; i<pulsecount; i++)
    {
    	if ( state==0 ) // Preamble bits?
    	{
            if ( get_diff(subghz_decenc_ctl.pulse_times[i], te_short) < tolerance_short )
            {
            	if ( odd_bit )
            		preamble_count++;
            } // if ( get_diff(subghz_decenc_ctl.pulse_times[i], te_short) < tolerance_short )
            else if ( get_diff(subghz_decenc_ctl.pulse_times[i], te_long) < tolerance_long )
            {
            	// Bits 0 and half of bit 1 received
            	if ( !odd_bit )
            		break;
            	preamble_count++;
            	if ( preamble_count < (plist->preamble_bits/2) ) // Not enough preamble bits received?
            		break;
            	state = 1;
            	prev_bit_high = 0;
            	bit_pulses = 1;
            	code = 0;
            } // else if ( get_diff(subghz_decenc_ctl.pulse_times[i], te_long) < tolerance_long )
            else // Error occurred
            {
            	if ( preamble_count ) // If no bit has been decoded, attempt to decode the remaining bits
            		break;
            } // else
            odd_bit ^= 1;
    	} // if ( state==0 )

    	else // Data bits
    	{
    		if ( !prev_bit_high )
    		{
                if ( (get_diff(subghz_decenc_ctl.pulse_times[i], te_short) < tolerance_short) ||
                		(get_diff(subghz_decenc_ctl.pulse_times[i], te_long) < tolerance_long) )
                {
                	// Bit 1 received, or bit 1 and half of bit 0 received
                	bit_pulses++;
                	if ( bit_pulses==2 )
                	{
                		data_count++;
                		code <<= 1;
                		code |= 0x01;
                		bit_pulses = 0;
                		if ( get_diff(subghz_decenc_ctl.pulse_times[i], te_long) < tolerance_long )
                			bit_pulses = 1;
                	} // if ( bit_pulses==2 )
                } // if ( get_diff(subghz_decenc_ctl.pulse_times[i], te_short) < tolerance_short )
                else // Error occurred
                {
                	break;
                } // else
    		} // if ( !prev_bit_high )
    		else // Previous bit is high
    		{
                if ( (get_diff(subghz_decenc_ctl.pulse_times[i], te_short) < tolerance_short) ||
                	(get_diff(subghz_decenc_ctl.pulse_times[i], te_long) < tolerance_long) )
                {
                	// Bit 0 received, or bit 0 and half of bit 1 received
                	bit_pulses++;
                	if ( bit_pulses==2 )
                	{
                    	data_count++;
                    	code <<= 1;
                    	bit_pulses = 0;
                		if ( get_diff(subghz_decenc_ctl.pulse_times[i], te_long) < tolerance_long )
                			bit_pulses = 1;
                	} // if ( bit_pulses==2 )
                } // if ( get_diff(subghz_decenc_ctl.pulse_times[i], te_short) < tolerance_short )
                else if ( data_count >= (plist->data_bits-1) ) // Last bit 0?
                {
                	if ( bit_pulses ) // High pulse of the last bit 0 received?
                	{
                		data_count++;
                		code <<= 1;
                	}
                	ret = 1;
    			    break;
                } // else if ( data_count >= (plist->data_bits-1) )
                else // Error occurred
                {
                	break;
                } // else
    		} // else
    		prev_bit_high ^= 1;
    	} // else
    } // for (i = 0; i<pulsecount; i++)

    M1_LOG_D(M1_LOGDB_TAG, "Security+: E:%d pulses:%d, preamble:%d, data: %d\r\n", !ret, pulsecount, preamble_count, data_count);
/*
    M1_LOG_D(M1_LOGDB_TAG, "te[0,1,2,3]:%d %d %d %d\r\n", subghz_decenc_ctl.pulse_times[0], subghz_decenc_ctl.pulse_times[1],
    														subghz_decenc_ctl.pulse_times[2], subghz_decenc_ctl.pulse_times[3]);
*/
/**
      * Packet:  | 001111 xx | xx xxxx xx|xx dddddd | dddddddd | dddddddd | dddddddd |
      * 				 Type  ID  ORD  INV  Data								   LSB
      * Type: payload type (00 or 01)
      * ID: Frame ID (always 00)
      * ORD: Order indicator
      * INV: Inversion indicator
*/
    while (ret)
    {
    	ret = 0;
        if ( code & SEC_PLUS_V2_ID_MASK ) // ID
        {
        	ret = 1;
        	break;
        }

        if ( code & SEC_PLUS_V2_PACKET_X_MASK ) // Type
        	i = 0x02;
        else
        	i = 0x01;
    	if ( !(rx_packets & i) ) // This packet is not decoded yet?
    	{
    		rx_packets |= i;
    		if ( i==0x01 )
    			prev_code = code;
    	} // if ( !(rx_packets & i) )
    	else
    	{
    		ret = 1; // reset
    	}
    	if ( ret )
    		break;

    	if ( rx_packets==0x03 ) // Received enough 2 packets?
    	{
    		if ( i==0x01 ) // Wrong order? The last decoded packet should be 0x02
    		{
    			rx_packets = 0x01; // Restart
    			M1_LOG_E(M1_LOGDB_TAG, "Wrong packet order\r\n");
    			return 1;
    		} // if ( i==0x01 )
    		else
    		{
    			do
    			{
    				memset(&half_codes[0], 0, 10);
    				memset(&half_codes[1], 0, 10);
    				M1_LOG_D(M1_LOGDB_TAG, "Packet 1 0x%lX%lX\r\n", (uint32_t)(prev_code>>32), (uint32_t)prev_code);
    				M1_LOG_D(M1_LOGDB_TAG, "Packet 2 0x%lX%lX\r\n", (uint32_t)(code>>32), (uint32_t)code);
    				ret = m1_secplus_v2_decode_half(prev_code, (uint8_t *)&half_codes[0], &fixed[0]);
    				if ( ret )
    					break;
    				ret = m1_secplus_v2_decode_half(code, (uint8_t *)&half_codes[1], &fixed[1]);
    				if ( ret )
    					break;
    				ret = m1_secplus_v2_decode(fixed, half_codes, &rolling_code, &code);
    			} while (false);
    		} // else
        	if ( ret )
        		break;

        	rx_packets = 0; // reset
            // button-id = out_bits >> 32;
            // remote-id = out_bits & 0xffffffff;
            // rolling_code is a 28 bit unsigned number
            // fixed is 40 bit in a uint64_t
            subghz_decenc_ctl.n64_decodedvalue = code;
            subghz_decenc_ctl.n32_serialnumber = (fixed[0] << 20) | fixed[1];
            subghz_decenc_ctl.n32_rollingcode = rolling_code;
            subghz_decenc_ctl.n8_buttonid = (uint8_t)(fixed[0] >> 12);
            subghz_decenc_ctl.ndecodedbitlength = data_count;
            subghz_decenc_ctl.ndecodeddelay = 0; //delay;
            subghz_decenc_ctl.ndecodedprotocol = p;
            // Packet 1 = prev_code
            // Packet 2 = code
    	    M1_LOG_I(M1_LOGDB_TAG, "Decoded 0x%lX%lX\r\n", (uint32_t)(code>>32), (uint32_t)code);
    		M1_LOG_D(M1_LOGDB_TAG, "Button 0x%X\r\n", (uint8_t)(fixed[0] >> 12));
    		M1_LOG_D(M1_LOGDB_TAG, "Serial 0x%lX\r\n", (fixed[0] << 20) | fixed[1]);
    	} // if ( rx_packets==0x03 )
    } // while (ret)

    if ( ret )
    {
    	rx_packets = 0; // reset
    }

    return ret;
} // uint8_t subghz_decode_security_plus_20(uint16_t p, uint16_t pulsecount)



/*============================================================================*/
/**
  * @brief
  * in_bits: | 001111 xx | xx xxxx xx|xx dddddd | dddddddd | dddddddd | dddddddd |
  * 				 Type  ID  ORD  INV  Data								   LSB
  * Type: payload type (00 or 01)
  * ID: Frame ID (always 00)
  * ORD: Order indicator
  * INV: Inversion indicator
  * Data: 30 bits p0p1p2... p0p1p2
  * @param  None
  * @retval None
  */
/*============================================================================*/
uint8_t m1_secplus_v2_decode(uint32_t fixed[], uint8_t half_codes[][10], uint32_t *rolling_code, uint64_t *out_bits)
{
    uint32_t rolling_temp;
    uint8_t rolling_digits[24] = {0};
    uint8_t i, *r;

    // Assemble half_codes[0][] and half_codes[1][] into rolling_digits[]
    r    = rolling_digits;
    *r++ = half_codes[1][8];
    *r++ = half_codes[0][8];
    for (i = 4; i < 8; i++)
    {
        *r++ = half_codes[1][i];
    }
    for (i = 4; i < 8; i++)
    {
        *r++ = half_codes[0][i];
    }

    for (i = 0; i < 4; i++)
    {
        *r++ = half_codes[1][i];
    }
    for (i = 0; i < 4; i++)
    {
        *r++ = half_codes[0][i];
    }

    // compute rolling_code from rolling_digits[]
    *rolling_code = 0;
    rolling_temp  = 0;

    for (i = 0; i < 18; i++)
    {
        rolling_temp = (rolling_temp * 3) + rolling_digits[i];
    }

    // Max value = 2^28 (268435456)
    if (rolling_temp >= 0x10000000)
    {
        return 1;
    }

    // value is 28 bits thus need to shift over 4 bit
    *rolling_code = reverse32(rolling_temp);
    *rolling_code >>= 4;

    // Assemble "fixed" data part
    *out_bits = 0;
    *out_bits ^= ((uint64_t)((fixed[0] >> 0) & 0xFF)) << 32;
    *out_bits ^= ((uint64_t)((fixed[0] >> 8) & 0xFF)) << 24;
    *out_bits ^= ((uint64_t)((fixed[0] >> 16) & 0xFF)) << 16;

    *out_bits ^= ((uint64_t)((fixed[1] >> 0) & 0xFF)) << 12;
    *out_bits ^= ((uint64_t)((fixed[1] >> 8) & 0xFF)) << 4;
    *out_bits ^= (((fixed[1] >> 16) & 0xFF) >> 4) & 0x0f;

    return 0;
} // uint8_t m1_secplus_v2_decode(uint32_t fixed[], uint8_t half_codes[][10], uint32_t *rolling_code, uint64_t *out_bits)



/*============================================================================*/
/**
  * @brief
  * in_bits: | 001111 xx | xx xxxx xx|xx dddddd | dddddddd | dddddddd | dddddddd |
  * 				 Type  ID  ORD  INV  Data								   LSB
  * Type: payload type (00 or 01)
  * ID: Frame ID (always 00)
  * ORD: Order indicator
  * INV: Inversion indicator
  * Data: 30 bits p0p1p2... p0p1p2
  * @param  None
  * @retval None
  */
/*============================================================================*/
uint8_t m1_secplus_v2_decode_half(uint64_t in_bits, uint8_t *half_code, uint32_t *out_bits)
{
    uint8_t k, invert, order;
    uint32_t w32, data_bits;
    uint16_t p0, p1, p2, a, b, c;
    int i;

    data_bits = in_bits; // Get the Data part
    data_bits &= 0x3FFFFFFF; // Clear 2 MSB bits
    w32 = in_bits >> 30; // Get these parts: Type  ID  ORD INV

    order = (w32 >> 4) & 0x0F;
    invert = w32 & 0x0F;

    p0 = p1 = p2 = 0;
    // sort 30 in_bits of interleaved data into three 10 bit buffers
    for (i = 0; i < 10; i++)
    {
        p2 |= ((data_bits & 0x01) << i);
        data_bits >>= 1;
        p1 |= ((data_bits & 0x01) << i);
        data_bits >>= 1;
        p0 |= ((data_bits & 0x01) << i);
        data_bits >>= 1;
    }

    // selectively invert buffers
    switch (invert)
    {
    	case 0x00: // 0b0000 (True, True, False),
        	p0 = ~p0 & 0x03FF;
        	p1 = ~p1 & 0x03FF;
        	break;

    	case 0x01: // 0b0001 (False, True, False),
    		p1 = ~p1 & 0x03FF;
    		break;

    	case 0x02: // 0b0010 (False, False, True),
    		p2 = ~p2 & 0x03FF;
    		break;

    	case 0x04: // 0b0100 (True, True, True),
    		p0 = ~p0 & 0x03FF;
    		p1 = ~p1 & 0x03FF;
    		p2 = ~p2 & 0x03FF;
    		break;

    	case 0x05: // 0b0101 (True, False, True),
    	case 0x0A: // 0b1010 (True, False, True),
    		p0 = ~p0 & 0x03FF;
    		p2 = ~p2 & 0x03FF;
    		break;

    	case 0x06: // 0b0110 (False, True, True),
    		p1 = ~p1 & 0x03FF;
    		p2 = ~p2 & 0x03FF;
    		break;

    	case 0x08: // 0b1000 (True, False, False),
    		p0 = ~p0 & 0x03FF;
    		break;

    	case 0x09: // 0b1001 (False, False, False),
    		break;

    	default:
    		return 1;
    } // switch (invert)

    a = p0;
    b = p1;
    c = p2;

    // selectively reorder buffers
    switch (order)
    {
    	case 0x06: // 0b0110  2, 1, 0],
    	case 0x09: // 0b1001  2, 1, 0],
    		p2 = a;
    		p1 = b;
    		p0 = c;
    		break;

    	case 0x08: // 0b1000  1, 2, 0],
    	case 0x04: // 0b0100  1, 2, 0],
    		p1 = a;
    		p2 = b;
    		p0 = c;
    		break;

    	case 0x01: // 0b0001 2, 0, 1],
    		p2 = a;
    		p0 = b;
    		p1 = c;
    		break;

    	case 0x00: // 0b0000  0, 2, 1],
    		p0 = a;
    		p2 = b;
    		p1 = c;
    		break;

    	case 0x05: // 0b0101 1, 0, 2],
    		p1 = a;
    		p0 = b;
    		p2 = c;
    		break;

    	case 0x02: // 0b0010 0, 1, 2],
    	case 0x0A: // 0b1010 0, 1, 2],
    		p0 = a;
    		p1 = b;
    		p2 = c;
    		break;

    	default:
    		return 2;
    } // switch (order)

    data_bits = w32 & 0xFF;
    k = 0;
    for (i = 6; i >= 0; i -= 2)
    {
        half_code[k++] = (data_bits >> i) & 0x03;
    }

    // assemble binary in_bits into trinary
    data_bits = p2;
    for (i = 8; i >= 0; i -= 2)
    {
        half_code[k++] = (data_bits >> i) & 0x03;
    }

    for (i = 0; i < 9; i++)
    {
        if (half_code[i]==3)
        {
            M1_LOG_E(M1_LOGDB_TAG, "half_code failed!\r\n");
            return 1;
        }
    }

    *out_bits = (p0 << 10) | p1;

    return 0;
} // uint8_t m1_secplus_v2_decode_half(uint64_t in_bits, uint8_t *half_code, uint32_t *out_bits)
