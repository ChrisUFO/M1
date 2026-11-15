/* See COPYING.txt for license details. */

/*
*
*  si4463_revc2_patch.h
*
*  M1 sub-ghz radio configuration patch
*
* M1 Project
*
*/

#ifndef M1_SUB_GHZ_PATCH_CONF_H_
#define M1_SUB_GHZ_PATCH_CONF_H_

#ifndef RADIO_PATCH_CONFIG_H_
#define RADIO_PATCH_CONFIG_H_

#include "m1_sub_ghz_si4463_patch.h"

// CONFIGURATION COMMANDS

/*
// Command:                  RF_PATCH_POWER_UP
// Description:              Command to power-up the device and select the operational mode and functionality.
*/
#define RF_PATCH_POWER_UP 0x02, 0x81, 0x00, 0x01, 0xE8, 0x48, 0x00

// AUTOMATICALLY GENERATED CODE! 
// DO NOT EDIT/MODIFY BELOW THIS LINE!
// --------------------------------------------

#ifndef FIRMWARE_LOAD_COMPILE
#define RADIO_PATCH_CONFIGURATION_DATA_ARRAY { \
        SI446X_PATCH_CMDS, \
        0x07, RF_PATCH_POWER_UP, \
        0x00 \
 }
#else
#define RADIO_PATCH_CONFIGURATION_DATA_ARRAY { 0 }
#endif

#define RADIO_PATCH_CONFIGURATION_DATA { \
                            Radio_Patch_Configuration_Data_Array,                            \
                            }

#endif /* RADIO_PATCH_CONFIG_H_ */

#endif // #ifndef M1_SUB_GHZ_PATCH_CONF_H_
