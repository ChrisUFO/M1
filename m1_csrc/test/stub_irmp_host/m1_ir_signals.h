/* See COPYING.txt for license details. */
/*
* Host-test stub: S_M1_IR_Payload_t + the raw-OTA global the REAL vendored
* IRSND references (ir_universal_payload, defined by the test driver).
* Mirrors Infrared/m1_ir_signals.h.
*/
#ifndef STUB_IRMP_HOST_M1_IR_SIGNALS_H_
#define STUB_IRMP_HOST_M1_IR_SIGNALS_H_

#include <stdint.h>

typedef struct
{
	uint32_t address;
	uint32_t command;
	uint32_t frequency;
	uint32_t duty_cycle;
	uint16_t rawdata_counter;
	uint8_t ir_data_type;
	uint8_t *pprotocol;
	uint16_t *pprawdata;
} S_M1_IR_Payload_t;

extern S_M1_IR_Payload_t ir_universal_payload;

#endif /* STUB_IRMP_HOST_M1_IR_SIGNALS_H_ */
