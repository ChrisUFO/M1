/* See COPYING.txt for license details. */

/*
*
* m1_md5_hash.c
*
* M1 MD5 hash functions
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_md5_hash.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG				"MD5-HASH"

//************************** C O N S T A N T **********************************/



//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

static struct MD5Context s_md5_context;
static uint32_t s_start_address;
static uint32_t s_image_size;


/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void mh_md5_init(uint32_t address, uint32_t size);
void mh_md5_update(const uint8_t *data, uint32_t size);
void mh_md5_final(uint8_t digets[16]);
void mh_hexify(const uint8_t raw_md5[16], uint8_t hex_md5_out[32]);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
inline void mh_md5_init(uint32_t address, uint32_t size)
{
    s_start_address = address;
    s_image_size = size;
    MD5Init(&s_md5_context);
} // inline void mh_md5_init(uint32_t address, uint32_t size)



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
inline void mh_md5_update(const uint8_t *data, uint32_t size)
{
    MD5Update(&s_md5_context, data, size);
} // inline void mh_md5_update(const uint8_t *data, uint32_t size)



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
inline void mh_md5_final(uint8_t digets[16])
{
    MD5Final(digets, &s_md5_context);
} // inline void mh_md5_final(uint8_t digets[16])



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
void mh_hexify(const uint8_t raw_md5[16], uint8_t hex_md5_out[32])
{
    static const uint8_t dec_to_hex[] = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
    };
    for (int i = 0; i < 16; i++) {
        *hex_md5_out++ = dec_to_hex[raw_md5[i] >> 4];
        *hex_md5_out++ = dec_to_hex[raw_md5[i] & 0xF];
    }
} // void mh_hexify(const uint8_t raw_md5[16], uint8_t hex_md5_out[32])
