/* See COPYING.txt for license details. */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "datatypes_utils.h"

char *hexStrToBinStr(char *hexStr)
{
    char *binStr;
    char hexByte[3] = {0};
    uint8_t hexstr_len, value, k;
    uint16_t binstr_cnt;

    if ( !hexStr )
    	return NULL;

    hexstr_len = strlen(hexStr);
    if ( !hexstr_len )
    	return NULL;

    binStr = malloc(hexstr_len*8 + 1); // Extra NULL character at the end of the string
    if ( !binStr )
    	return NULL;

    binstr_cnt = 0;
    hexByte[2] = '\0'; // End of string
    for (int i = 0; i < hexstr_len; i++)
    {
        char c = hexStr[i];
        k = 0;
        // Check if the character is a hexadecimal digit
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))
        {
            hexByte[k++] = c;
            if (k == 2)
            {
                // Convert the hexadecimal pair to a decimal value
                value = strtol(hexByte, NULL, 16);
                // Convert the decimal value to binary and add to the binary string
                for (uint8_t j = 0x80; j; j>>=1)
                {
                	binStr[binstr_cnt++] = (value & j) ? '1' : '0';
                }
            }
        } // if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))
        else // Error in hex data, abort and return
        {
        	free(binStr);
        	binStr = NULL;
        }
    } // for (int i = 0; i < hexstr_len; i++)

    if ( binstr_cnt )
    	binStr[binstr_cnt] = '\0'; // End of string

    return binStr;
} // char *hexStrToBinStr(char *hexStr)




// Converts a Hex char to decimal
uint8_t hexCharToDecimal(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    else if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    else if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    return 0;
} // uint8_t hexCharToDecimal(char c)



// Convert a Hex string to decimal
uint32_t hexStringToDecimal(const char *hexString)
{
    uint32_t decimal = 0;
    int len = strlen(hexString);

    for (int i = 0; i < len; i += 2)
    {
        decimal <<= 8; // Shift left to accommodate next byte

        // Converts two characters hex to a single byte
        uint8_t highNibble = hexCharToDecimal(hexString[i]);
        uint8_t lowNibble = hexCharToDecimal(hexString[i + 1]);
        decimal |= (highNibble << 4) | lowNibble;
    }

    return decimal;
} // uint32_t hexStringToDecimal(const char *hexString)



char *dec2binWzerofill(uint64_t Dec, unsigned int bitLength)
{
    // Allocate memory dynamically for safety
    char *bin = (char *)malloc(bitLength + 1);
    if (!bin)
    	return NULL; // Handle allocation failure

    bin[bitLength] = '\0'; // Null-terminate string

    for (int i = bitLength - 1; i >= 0; i--)
    {
        bin[i] = (Dec & 1) ? '1' : '0';
        Dec >>= 1;
    }

    return bin;
} // char *dec2binWzerofill(uint64_t Dec, unsigned int bitLength)


/*
String hexToStr(uint8_t *data, uint8_t len, char separator)
{
    String str = "";

    for (size_t i = 0; i < len; i++)
    {
        str += separator;
        if (data[i] < 0x10)
        	str += '0';
        str += String(data[i], HEX);
    }

    str.trim();
    str.toUpperCase();
    return str;
}
*/
