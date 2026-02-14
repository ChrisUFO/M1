/* See COPYING.txt for license details. */

/*
*
* m1_md5_hash.h
*
* M1 MD5 hash header
*
* M1 Project
*
* Reference: ESP32
*/

#ifndef M1_MD5_HASH_H_
#define M1_MD5_HASH_H_

#include "md5_hash.h"
#include "protocol.h"

void mh_md5_init(uint32_t address, uint32_t size);
void mh_md5_update(const uint8_t *data, uint32_t size);
void mh_md5_final(uint8_t digets[16]);
void mh_hexify(const uint8_t raw_md5[16], uint8_t hex_md5_out[32]);

#endif /* M1_MD5_HASH_H_ */
