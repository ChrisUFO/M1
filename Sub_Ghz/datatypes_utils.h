/* See COPYING.txt for license details. */

#ifndef __TYPE_CONVERTION_H__
#define __TYPE_CONVERTION_H__

#include <stdint.h>

char *hexStrToBinStr(char *hexStr);
uint8_t hexCharToDecimal(char c);
uint32_t hexStringToDecimal(const char *hexString);
char *dec2binWzerofill(uint64_t Dec, unsigned int bitLength);

#endif // #ifndef __TYPE_CONVERTION_H__
