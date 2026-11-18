/* See COPYING.txt for license details. */
/*
*
* ir_remotes.c
*
* Infrared remotes
*
* M1 Project
*
*/

/*
* ===========================================================================
* PROTOCOL TABLE AUDIT (issue #43 part 1, PR-4)
* ===========================================================================
* ir_protocols_mapping_table[] is the ONE protocol map in this firmware
* ("single map" rule): the .ir "protocol:" vocabulary is resolved through it
* by ir_protocol_map_lookup() and the golden-decode host test. The legacy
* fork alias table (ir_proto_map in m1_ir_universal.c, which aliased
* NEC/NECext->NEC, RC5X->RC5 and SIRC15/20->SIRC, transmitting wrong codes)
* is intentionally NOT ported.
*
* Audited against the official Flipper .ir protocol vocabulary required by
* issue #43 -- all present, none added (audit result: complete):
*   NEC      -> IRMP_NEC8_PROTOCOL  8-bit addr + 8-bit cmd, each followed by
*                                  its inverted byte (the vendored add-on
*                                  encoder emits exactly the Flipper "NEC"
*                                  8/8-with-inversion frame)
*   NECext   -> IRMP_NEC_PROTOCOL   16-bit address passed through verbatim;
*                                  16-bit command with the low byte emitted
*                                  as the inverse of the high byte (the only
*                                  extended-NEC encoder in the vendored IRMP;
*                                  the frame still validates as NEC family)
*   RC5X     -> IRMP_RC5X_PROTOCOL  RC5 extended frame (S/start bit cleared)
*   SIRC15   -> IRMP_SIRCS15_PROTOCOL 15-bit Sony frame (cmd7 + addr8)
*   SIRC20   -> IRMP_SIRCS20_PROTOCOL 20-bit Sony frame (cmd7 + addr13)
*   RCA      -> IRMP_RCA_PROTOCOL   24-bit frame AAAA CCCC CCCC ~CMD ~ADDR
* Plus further official names already covered: SIRC, RC5, RC6, NEC42,
* Samsung32, Kaseikyo. Plus upstream extras passing through: Pioneer
* (NEC-shaped add-on encoder), raw (timing stream; must stay the last entry)
* and the NEC8 encoder class behind the "NEC" name. "UNKNOWN" must stay the
* first entry.
*
* Intentional omissions (strict-skip keeps them safe: unknown names skip the
* block, increment the skipped counter and raise a visible notice -- never a
* garbage transmit): NEC42ext, Samsung48/Samsung64, Cassette and any other
* Flipper name with no encoder in the vendored IRMP build.
*
* PR-6 (issue #43 part 3): the session entry is exposed as
* infrared_universal_all_remotes(remote_type, cycle_all). The four
* category entries run it with cycle_all == 0 -- ONE frame per OK press,
* the default per-press behavior everywhere in this firmware -- while
* the explicit "Cycle all ..." submenu actions run it with cycle_all == 1,
* preserving the upstream cycle-through-every-device session with its
* pause/resume and progress UI.
* ===========================================================================
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "stm32h5xx_hal.h"
#include "m1_ir_signals.h"
#include "m1_log_debug.h"
#include "m1_infrared.h"
#include "irmp.h"
#include "irsnd.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"IR_REMOTE"

#define IR_REMOTE_TVS_BUTTON_ROWS			3
#define IR_REMOTE_TVS_BUTTON_COLS			2

#define IR_REMOTE_AUDIOS_BUTTON_ROWS		4
#define IR_REMOTE_AUDIOS_BUTTON_COLS		2

#define IR_REMOTE_PROJECTORS_BUTTON_ROWS	2
#define IR_REMOTE_PROJECTORS_BUTTON_COLS	2

#define IR_REMOTE_ACS_BUTTON_ROWS			3
#define IR_REMOTE_ACS_BUTTON_COLS			2

#define IR_BUTTON_FRAME_WIDTH				21
#define IR_BUTTON_FRAME_HEIGHT				21

#define IR_REMOTE_TV_BUTTON_WIDTH			18
#define IR_REMOTE_TV_BUTTON_HEIGHT			18

#define IR_REMOTE_AUDIO_BUTTON_WIDTH		18
#define IR_REMOTE_AUDIO_BUTTON_HEIGHT		18

#define IR_REMOTE_PROJECTOR_BUTTON_WIDTH	18
#define IR_REMOTE_PROJECTOR_BUTTON_HEIGHT	18

#define IR_REMOTE_AC_BUTTON_WIDTH			18
#define IR_REMOTE_AC_BUTTON_HEIGHT			18
#define IR_REMOTE_AC_BUTTON_FRAME_WIDTH		21
#define IR_REMOTE_AC_BUTTON_FRAME_HEIGHT	21

#define IR_REMOTE_TVS_BUTTON_ROW1_Y			15
#define IR_REMOTE_TVS_BUTTON_ROW1_X1		7
#define IR_REMOTE_TVS_BUTTON_ROW1_X2		39
#define IR_REMOTE_TVS_BUTTON_ROW2_Y			61
#define IR_REMOTE_TVS_BUTTON_ROW2_X1		7
#define IR_REMOTE_TVS_BUTTON_ROW2_X2		39
#define IR_REMOTE_TVS_BUTTON_ROW3_Y			99
#define IR_REMOTE_TVS_BUTTON_ROW3_X1		7
#define IR_REMOTE_TVS_BUTTON_ROW3_X2		39

#define IR_REMOTE_AUDIOS_BUTTON_ROW1_Y		12
#define IR_REMOTE_AUDIOS_BUTTON_ROW1_X1		7
#define IR_REMOTE_AUDIOS_BUTTON_ROW1_X2		39
#define IR_REMOTE_AUDIOS_BUTTON_ROW2_Y		41
#define IR_REMOTE_AUDIOS_BUTTON_ROW2_X1		7
#define IR_REMOTE_AUDIOS_BUTTON_ROW2_X2		39
#define IR_REMOTE_AUDIOS_BUTTON_ROW3_Y		70
#define IR_REMOTE_AUDIOS_BUTTON_ROW3_X1		7
#define IR_REMOTE_AUDIOS_BUTTON_ROW3_X2		39
#define IR_REMOTE_AUDIOS_BUTTON_ROW4_Y		99
#define IR_REMOTE_AUDIOS_BUTTON_ROW4_X1		7
#define IR_REMOTE_AUDIOS_BUTTON_ROW4_X2		39

#define IR_REMOTE_PROJECTORS_BUTTON_ROW1_Y	15
#define IR_REMOTE_PROJECTORS_BUTTON_ROW1_X1	7
#define IR_REMOTE_PROJECTORS_BUTTON_ROW1_X2	39
#define IR_REMOTE_PROJECTORS_BUTTON_ROW2_Y	61
#define IR_REMOTE_PROJECTORS_BUTTON_ROW2_X1	7
#define IR_REMOTE_PROJECTORS_BUTTON_ROW2_X2	39

#define IR_REMOTE_ACS_BUTTON_ROW1_Y			15
#define IR_REMOTE_ACS_BUTTON_ROW1_X1		7
#define IR_REMOTE_ACS_BUTTON_ROW1_X2		39
#define IR_REMOTE_ACS_BUTTON_ROW2_Y			50
#define IR_REMOTE_ACS_BUTTON_ROW2_X1		7
#define IR_REMOTE_ACS_BUTTON_ROW2_X2		39
#define IR_REMOTE_ACS_BUTTON_ROW3_Y			73
#define IR_REMOTE_ACS_BUTTON_ROW3_X1		7
#define IR_REMOTE_ACS_BUTTON_ROW3_X2		39
#define IR_REMOTE_ACS_BUTTON_ROW4_Y			105
#define IR_REMOTE_ACS_BUTTON_ROW4_X1		7
#define IR_REMOTE_ACS_BUTTON_ROW4_X2		39

#define IR_TRANSMIT_SLIDER_WIDTH				5 // pixel
#define IR_TRANSMIT_SLIDER_OVERLAP				4 // pixel, each slider will be overlapped by 4 pixels when it's drawn on screen
#define IR_TRANSMIT_FULL_PROGRESS_COUNT			54
#define IR_TRANSMIT_FULL_PROGRESS_FACTOR		(float)100/IR_TRANSMIT_FULL_PROGRESS_COUNT
#define IR_TRANSMIT_FULL_PROGRESS_WIDTH			58 // pixel
#define IR_TRANSMIT_FULL_PROGRESS_HEIGHT		8 // pixel
#define IR_TRANSMIT_PROGRESS_SLIDE_STRIP_Y0	48
#define IR_TRANSMIT_PROGRESS_SLIDE_STRIP_X0	2

//************************** S T R U C T U R E S *******************************

typedef struct
{
	uint8_t button_x;
	uint8_t button_y;
	uint8_t button_width;
	uint8_t button_height;
	uint8_t button_type;
	const uint8_t *button_image;
	const uint8_t *button_image_inverted;
} S_M1_IR_Remote_Buttons_t;

typedef struct
{
	const char *protocol_name;
	uint8_t protocol_number;
} S_M1_IR_Protocol_Mapping_t;

typedef struct
{
	uint8_t button_row_n;
	uint8_t button_col_n;
	const char *db_filename;
	S_M1_IR_Remote_Buttons_t *button_post;
} S_M1_IR_Button_Info_t;

typedef enum
{
	IR_REMOTES_STATE_STANDBY = 0,
	IR_REMOTES_STATE_ACTIVE, // Playing
	IR_REMOTES_STATE_PAUSE, // Pause request granted
	IR_REMOTES_STATE_PAUSE_REQ, // Pause request
	IR_REMOTES_STATE_COMPLETE,
	IR_REMOTES_STATE_STOP_REQ, // Stop request
	IR_REMOTES_STATE_ERROR,
	IR_REMOTES_STATE_EOL
} S_M1_IR_Remotes_GUI_Mode_t;

//************************** C O N S T A N T **********************************/

/*
* The order of the buttons must match that of ir_remote_tv_functions[]
*/
const S_M1_IR_Remote_Buttons_t ir_remote_tvs_buttons[IR_REMOTE_TVS_BUTTON_ROWS*IR_REMOTE_TVS_BUTTON_COLS] = {
		{	IR_REMOTE_TVS_BUTTON_ROW1_X1,			// Power button
			IR_REMOTE_TVS_BUTTON_ROW1_Y,
			IR_REMOTE_TV_BUTTON_WIDTH,
			IR_REMOTE_TV_BUTTON_HEIGHT,
			IR_REMOTETYPE_TV_POWER,
			button_power_18x18,
			NULL
		},
		{	IR_REMOTE_TVS_BUTTON_ROW1_X2,			// Mute button
			IR_REMOTE_TVS_BUTTON_ROW1_Y,
			IR_REMOTE_TV_BUTTON_WIDTH,
			IR_REMOTE_TV_BUTTON_HEIGHT,
			IR_REMOTETYPE_TV_MUTE,
			button_mute_18x18,
			NULL
		},
		{	IR_REMOTE_TVS_BUTTON_ROW2_X1,			// Volume-up button
			IR_REMOTE_TVS_BUTTON_ROW2_Y,
			IR_REMOTE_TV_BUTTON_WIDTH,
			IR_REMOTE_TV_BUTTON_HEIGHT,
			IR_REMOTETYPE_TV_CH_NEXT,
			button_plus_18x18,
			NULL
		},
		{	IR_REMOTE_TVS_BUTTON_ROW2_X2,			// Channel-up button
			IR_REMOTE_TVS_BUTTON_ROW2_Y,
			IR_REMOTE_TV_BUTTON_WIDTH,
			IR_REMOTE_TV_BUTTON_HEIGHT,
			IR_REMOTETYPE_TV_VOL_UP,
			button_up_18x18,
			NULL
		},
		{	IR_REMOTE_TVS_BUTTON_ROW3_X1,			// Volume-down button
			IR_REMOTE_TVS_BUTTON_ROW3_Y,
			IR_REMOTE_TV_BUTTON_WIDTH,
			IR_REMOTE_TV_BUTTON_HEIGHT,
			IR_REMOTETYPE_TV_CH_PREV,
			button_minus_18x18,
			NULL
		},
		{	IR_REMOTE_TVS_BUTTON_ROW3_X2,			// Channel-down button
			IR_REMOTE_TVS_BUTTON_ROW3_Y,
			IR_REMOTE_TV_BUTTON_WIDTH,
			IR_REMOTE_TV_BUTTON_HEIGHT,
			IR_REMOTETYPE_TV_VOL_DN,
			button_down_18x18,
			NULL
		}
};

/*
* The order of the buttons must match that of ir_remote_audio_functions[]
*/
const S_M1_IR_Remote_Buttons_t ir_remote_audios_buttons[IR_REMOTE_AUDIOS_BUTTON_ROWS*IR_REMOTE_AUDIOS_BUTTON_COLS] = {
		{	IR_REMOTE_AUDIOS_BUTTON_ROW1_X1,			// Power button
			IR_REMOTE_AUDIOS_BUTTON_ROW1_Y,
			IR_REMOTE_AUDIO_BUTTON_WIDTH,
			IR_REMOTE_AUDIO_BUTTON_HEIGHT,
			IR_REMOTETYPE_AUDIO_POWER,
			button_power_18x18,
			NULL
		},
		{	IR_REMOTE_AUDIOS_BUTTON_ROW1_X2,			// Mute button
			IR_REMOTE_AUDIOS_BUTTON_ROW1_Y,
			IR_REMOTE_AUDIO_BUTTON_WIDTH,
			IR_REMOTE_AUDIO_BUTTON_HEIGHT,
			IR_REMOTETYPE_AUDIO_MUTE,
			button_mute_18x18,
			NULL
		},
		{	IR_REMOTE_AUDIOS_BUTTON_ROW2_X1,			// Play button
			IR_REMOTE_AUDIOS_BUTTON_ROW2_Y,
			IR_REMOTE_AUDIO_BUTTON_WIDTH,
			IR_REMOTE_AUDIO_BUTTON_HEIGHT,
			IR_REMOTETYPE_AUDIO_PLAY,
			button_play_18x18,
			NULL
		},
		{	IR_REMOTE_AUDIOS_BUTTON_ROW2_X2,			//  Volume-up button
			IR_REMOTE_AUDIOS_BUTTON_ROW2_Y,
			IR_REMOTE_AUDIO_BUTTON_WIDTH,
			IR_REMOTE_AUDIO_BUTTON_HEIGHT,
			IR_REMOTETYPE_AUDIO_VOL_UP,
			button_plus_18x18,
			NULL
		},
		{	IR_REMOTE_AUDIOS_BUTTON_ROW3_X1,			// Pause button
			IR_REMOTE_AUDIOS_BUTTON_ROW3_Y,
			IR_REMOTE_AUDIO_BUTTON_WIDTH,
			IR_REMOTE_AUDIO_BUTTON_HEIGHT,
			IR_REMOTETYPE_AUDIO_PAUSE,
			button_pause_18x18,
			NULL
		},
		{	IR_REMOTE_AUDIOS_BUTTON_ROW3_X2,			// Volume-down button
			IR_REMOTE_AUDIOS_BUTTON_ROW3_Y,
			IR_REMOTE_AUDIO_BUTTON_WIDTH,
			IR_REMOTE_AUDIO_BUTTON_HEIGHT,
			IR_REMOTETYPE_AUDIO_VOL_DN,
			button_minus_18x18,
			NULL
		},
		{	IR_REMOTE_AUDIOS_BUTTON_ROW4_X1,			//  Previous button
			IR_REMOTE_AUDIOS_BUTTON_ROW4_Y,
			IR_REMOTE_AUDIO_BUTTON_WIDTH,
			IR_REMOTE_AUDIO_BUTTON_HEIGHT,
			IR_REMOTETYPE_AUDIO_PREV,
			button_previous_18x18,
			NULL
		},
		{	IR_REMOTE_AUDIOS_BUTTON_ROW4_X2,			// Next button
			IR_REMOTE_AUDIOS_BUTTON_ROW4_Y,
			IR_REMOTE_AUDIO_BUTTON_WIDTH,
			IR_REMOTE_AUDIO_BUTTON_HEIGHT,
			IR_REMOTETYPE_AUDIO_NEXT,
			button_next_18x18,
			NULL
		}
};

/*
* The order of the buttons must match that of ir_remote_projector_functions[]
*/
const S_M1_IR_Remote_Buttons_t ir_remote_projectors_buttons[IR_REMOTE_PROJECTORS_BUTTON_ROWS*IR_REMOTE_PROJECTORS_BUTTON_COLS] = {
		{	IR_REMOTE_PROJECTORS_BUTTON_ROW1_X1,			// Power button
			IR_REMOTE_PROJECTORS_BUTTON_ROW1_Y,
			IR_REMOTE_PROJECTOR_BUTTON_WIDTH,
			IR_REMOTE_PROJECTOR_BUTTON_HEIGHT,
			IR_REMOTETYPE_PROJECTOR_POWER,
			button_power_18x18,
			NULL
		},
		{	IR_REMOTE_PROJECTORS_BUTTON_ROW1_X2,			// Mute button
			IR_REMOTE_PROJECTORS_BUTTON_ROW1_Y,
			IR_REMOTE_PROJECTOR_BUTTON_WIDTH,
			IR_REMOTE_PROJECTOR_BUTTON_HEIGHT,
			IR_REMOTETYPE_PROJECTOR_MUTE,
			button_mute_18x18,
			NULL
		},
		{	IR_REMOTE_PROJECTORS_BUTTON_ROW2_X1,			// Volume-up button
			IR_REMOTE_PROJECTORS_BUTTON_ROW2_Y,
			IR_REMOTE_PROJECTOR_BUTTON_WIDTH,
			IR_REMOTE_PROJECTOR_BUTTON_HEIGHT,
			IR_REMOTETYPE_PROJECTOR_VOL_UP,
			button_plus_18x18,
			NULL
		},
		{	IR_REMOTE_PROJECTORS_BUTTON_ROW2_X2,			// Volume-down button
			IR_REMOTE_PROJECTORS_BUTTON_ROW2_Y,
			IR_REMOTE_PROJECTOR_BUTTON_WIDTH,
			IR_REMOTE_PROJECTOR_BUTTON_HEIGHT,
			IR_REMOTETYPE_PROJECTOR_VOL_DN,
			button_minus_18x18,
			NULL
		}
};

/*
* The order of the buttons must match that of ir_remote_ac_functions[]
*/
const S_M1_IR_Remote_Buttons_t ir_remote_acs_buttons[IR_REMOTE_ACS_BUTTON_ROWS*IR_REMOTE_ACS_BUTTON_COLS] = {
		{	IR_REMOTE_ACS_BUTTON_ROW1_X1,			// Power off button
			IR_REMOTE_ACS_BUTTON_ROW1_Y,
			IR_REMOTE_AC_BUTTON_WIDTH,
			IR_REMOTE_AC_BUTTON_HEIGHT,
			IR_REMOTETYPE_AC_OFF,
			button_power_off_18x18,
			NULL
		},
		{	IR_REMOTE_ACS_BUTTON_ROW1_X2,			// Dry button
			IR_REMOTE_ACS_BUTTON_ROW1_Y,
			IR_REMOTE_AC_BUTTON_WIDTH,
			IR_REMOTE_AC_BUTTON_HEIGHT,
			IR_REMOTETYPE_AC_DH,
			weather_dry_18x18,
			NULL
		},
		{	IR_REMOTE_ACS_BUTTON_ROW2_X1,			// Cool-high button
			IR_REMOTE_ACS_BUTTON_ROW2_Y,
			IR_REMOTE_AC_BUTTON_FRAME_WIDTH,
			IR_REMOTE_AC_BUTTON_FRAME_HEIGHT,
			IR_REMOTETYPE_AC_COOL_HI,
			ir_button_frame_up_21x21,
			ir_button_frame_up_i_21x21
		},
		{	IR_REMOTE_ACS_BUTTON_ROW2_X2,			// Heat-high button
			IR_REMOTE_ACS_BUTTON_ROW2_Y,
			IR_REMOTE_AC_BUTTON_FRAME_WIDTH,
			IR_REMOTE_AC_BUTTON_FRAME_HEIGHT,
			IR_REMOTETYPE_AC_HEAT_HI,
			ir_button_frame_up_21x21,
			ir_button_frame_up_i_21x21
		},
		{	IR_REMOTE_ACS_BUTTON_ROW4_X1,			// Cool-low button
			IR_REMOTE_ACS_BUTTON_ROW4_Y,
			IR_REMOTE_AC_BUTTON_FRAME_WIDTH,
			IR_REMOTE_AC_BUTTON_FRAME_HEIGHT,
			IR_REMOTETYPE_AC_COOL_LO,
			ir_button_frame_down_21x21,
			ir_button_frame_down_i_21x21
		},
		{	IR_REMOTE_ACS_BUTTON_ROW4_X2,			// Heat-low button
			IR_REMOTE_ACS_BUTTON_ROW4_Y,
			IR_REMOTE_AC_BUTTON_FRAME_WIDTH,
			IR_REMOTE_AC_BUTTON_FRAME_HEIGHT,
			IR_REMOTETYPE_AC_HEAT_HI,
			ir_button_frame_down_21x21,
			ir_button_frame_down_i_21x21
		}
};

const S_M1_IR_Button_Info_t ir_buttons_info[] =
{
	{
		IR_REMOTE_TVS_BUTTON_ROWS,
		IR_REMOTE_TVS_BUTTON_COLS,
		IR_REMOTE_TYPE_TVS_FILENAME,
		ir_remote_tvs_buttons
	},
	{
		IR_REMOTE_AUDIOS_BUTTON_ROWS,
		IR_REMOTE_AUDIOS_BUTTON_COLS,
		IR_REMOTE_TYPE_AUDIOS_FILENAME,
		ir_remote_audios_buttons
	},
	{
		IR_REMOTE_PROJECTORS_BUTTON_ROWS,
		IR_REMOTE_PROJECTORS_BUTTON_COLS,
		IR_REMOTE_TYPE_PROJECTORS_FILENAME,
		ir_remote_projectors_buttons
	},
	{
		IR_REMOTE_ACS_BUTTON_ROWS,
		IR_REMOTE_ACS_BUTTON_COLS,
		IR_REMOTE_TYPE_ACS_FILENAME,
		ir_remote_acs_buttons
	}
};

/* ===================== IR_PROTOCOL_MAP_PURE_BEGIN ===================== */
#define IR_PROTOCOLS_MAPPING_MAX			15 /* keep in sync with the table below */
const S_M1_IR_Protocol_Mapping_t ir_protocols_mapping_table[IR_PROTOCOLS_MAPPING_MAX] =
{
	{
		"UNKNOWN",
		IRMP_UNKNOWN_PROTOCOL // Should be the first element of this array!
	},
	{
		"SIRC",
		IRMP_SIRCS_PROTOCOL
	},
	{
		"SIRC15",
		IRMP_SIRCS15_PROTOCOL
	},
	{
		"SIRC20",
		IRMP_SIRCS20_PROTOCOL
	},
	{	"NEC",
		IRMP_NEC8_PROTOCOL
	},
	{	"NECext",
		IRMP_NEC_PROTOCOL
	},
	{
		"RC5",
		IRMP_RC5_PROTOCOL
	},
	{
		"RC5X",
		IRMP_RC5X_PROTOCOL
	},
	{
		"RC6",
		IRMP_RC6_PROTOCOL
	},
	{
		"NEC42",
		IRMP_NEC42_PROTOCOL
	},
	{	"Samsung32",
		IRMP_SAMSUNG32_PROTOCOL
	},
	{
		"Kaseikyo",
		IRMP_KASEIKYO_PROTOCOL
	},
	{
		"RCA",
		IRMP_RCA_PROTOCOL
	},
	{
		"Pioneer",
		IRMP_PIONEER_PROTOCOL
	},
	{
		"raw",
		IRMP_RAW_PROTOCOL // Should be the last element of this array!
	}
};

/*============================================================================*/
/**
  * @brief  Resolve a .ir "protocol:" name to its IRMP protocol number through
  * 		the single upstream mapping table (issue #43 protocol audit).
  * @param  protocol_name: NUL-terminated name as found in the .ir file
  * @param  pprotocol_number: receives the IRMP protocol constant on success
  * @retval 0 if the name is in the table, 1 otherwise (strict-skip caller)
  */
/*============================================================================*/
uint8_t ir_protocol_map_lookup(const char *protocol_name, uint8_t *pprotocol_number)
{
	uint8_t i;

	if ( (protocol_name==NULL) || (pprotocol_number==NULL) )
		return 1;

	for (i=0; i<IR_PROTOCOLS_MAPPING_MAX; i++)
	{
		if ( !strcmp(protocol_name, ir_protocols_mapping_table[i].protocol_name) )
		{
			*pprotocol_number = ir_protocols_mapping_table[i].protocol_number;
			return 0;
		} // if ( !strcmp(protocol_name, ir_protocols_mapping_table[i].protocol_name) )
	} // for (i=0; i<IR_PROTOCOLS_MAPPING_MAX; i++)

	return 1;
} // uint8_t ir_protocol_map_lookup(const char *protocol_name, uint8_t *pprotocol_number)
/* ====================== IR_PROTOCOL_MAP_PURE_END ====================== */
/***************************** V A R I A B L E S ******************************/

S_M1_IR_Payload_t ir_universal_payload;
static uint16_t ir_samples_counter = 0;
static uint8_t ir_remote_state;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

uint8_t ir_remote_file_load(void);
uint8_t ir_protocol_map_lookup(const char *protocol_name, uint8_t *pprotocol_number);
void infrared_universal_tv_remotes(void);
void infrared_universal_audio_remotes(void);
void infrared_universal_projector_remotes(void);
void infrared_universal_ac_remotes(void);
void infrared_universal_cycle_all_tv(void);
void infrared_universal_cycle_all_audio(void);
void infrared_universal_cycle_all_projector(void);
void infrared_universal_cycle_all_ac(void);
void infrared_universal_all_remotes(uint8_t remote_type, uint8_t cycle_all);
static void ir_button_icons_update(uint8_t remote_type);
static void ir_ac_button_text_update(uint8_t pos_x, uint8_t pos_y);
static void ir_button_icon_redraw(S_M1_IR_Remote_Buttons_t *pbutton, uint8_t inverted);
static uint8_t ir_remote_function_play(uint8_t ir_remote_type, uint8_t function_type);
static void ir_play_status_update(uint8_t init, uint16_t round_n, uint8_t refresh);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief  Load infrared remote file from SD card
  * @param
  * @retval 0 if success
  */
/*============================================================================*/
uint8_t ir_remote_file_load(void)
{
	uint8_t sys_error;

	do
	{
		sys_error = ir_remote_file_header_check(IR_REMOTE_TYPE_TVS_FILENAME, IR_REMOTETYPE_TV);
		if ( sys_error )
			break;
		sys_error = ir_remote_file_data_check(IR_REMOTETYPE_TV);
		if ( sys_error )
			break;
	} while (0);

	ir_remote_file_deinit();

    M1_LOG_I(M1_LOGDB_TAG, "Pass %d\r\n", sys_error);

	return sys_error;
} // uint8_t ir_remote_file_load(void)



/*============================================================================*/
/**
  * @brief  One universal-remote session over a firmware-owned category
  * 		database (0:/infrared/db). Exposed for the explicit "cycle all
  * 		in category" submenu actions (issue #43: upstream behavior
  * 		preserved, never the default per-press behavior).
  * @param  remote_type: category (IR_REMOTETYPE_*)
  * @param  cycle_all: 0 = the default remote UI: every OK press plays
  * 		exactly ONE frame of the next device's code (press again to
  * 		advance, END_OF_FILE wraps back to the first device);
  * 		1 = the upstream cycle: after the first OK the session plays
  * 		through every device automatically with pause/resume (OK) and
  * 		stop (BACK), progress shown as device_count/total.
  * @retval None (screen session; takes the single-owner IRMP guard).
  */
/*============================================================================*/
void infrared_universal_all_remotes(uint8_t remote_type, uint8_t cycle_all)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t sys_error, header_checked;
	uint8_t next_transmission_req;
	uint8_t this_button_row, this_button_col, this_remote_col_n;
	uint8_t function_type;
	uint16_t total_devices, device_count;

	if ( remote_type >= IR_REMOTETYPE_UNKNOWN )
		return;

	if ( !m1_ir_subsys_acquire() ) /* single-owner IRMP rule */
	{
		m1_u8g2_firstpage();
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
		u8g2_DrawXBMP(&m1_u8g2, 2, 2, 48, 25, remote_48x25);
		u8g2_DrawStr(&m1_u8g2, 60, 20, "IR busy");
		m1_u8g2_nextpage(); // Update display RAM
		vTaskDelay(pdMS_TO_TICKS(1500));
		return;
	} // if ( !m1_ir_subsys_acquire() )

	header_checked = 0;
	next_transmission_req = 0;
	this_button_row = 0;
	this_button_col = 0;
	this_remote_col_n = 0;
	total_devices = 0;
	device_count = 0;
	ir_remote_state = IR_REMOTES_STATE_STANDBY;

	/* Graphic work starts here */
	m1_u8g2_firstpage();
	u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 32/2, 18, 32, hourglass_18x32);
	m1_u8g2_nextpage();	 // Update display RAM

	do
	{
		sys_error = ir_remote_file_header_check(ir_buttons_info[remote_type].db_filename, remote_type);
		if ( sys_error )
			break;
		sys_error = ir_remote_file_data_check(remote_type);
		if ( sys_error )
			break;
	} while (0);

	if ( sys_error )
	{
		m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_db_error_message);
		ir_remote_state = IR_REMOTES_STATE_ERROR;
	} // if ( sys_error )
	else
	{
		this_remote_col_n = ir_buttons_info[remote_type].button_col_n;
		u8g2_SetDisplayRotation(&m1_u8g2, U8G2_R1); // 90 degree clockwise rotation
		ir_button_icons_update(remote_type);
		ir_button_icon_redraw(&ir_buttons_info[remote_type].button_post[this_button_row*this_remote_col_n + this_button_col], true); // Highlight the button
	} // else
	ir_remote_file_deinit();

	while (1) // Main loop of this task
	{
		;
		; // Do other parts of this task here
		;

		// Wait for the notification from button_event_handler_task to subfunc_handler_task.
		// This task is the sub-task of subfunc_handler_task.
		// The notification is given in the form of an item in the main queue.
		// So let read the main queue.
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE)
		{
			if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			{
				// Notification is only sent to this task when there's any button activity,
				// so it doesn't need to wait when reading the event from the queue
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
				{
					if ( ir_remote_state==IR_REMOTES_STATE_ACTIVE )
					{
						ir_remote_state = IR_REMOTES_STATE_STOP_REQ; // Stop request
					} // if ( ir_remote_state==IR_REMOTES_STATE_ACTIVE )
					else if ( (ir_remote_state==IR_REMOTES_STATE_STANDBY) ||  (ir_remote_state==IR_REMOTES_STATE_PAUSE) ||
																						ir_remote_state==IR_REMOTES_STATE_ERROR )
					{
						ir_remote_file_deinit();
						header_checked = 0;
						m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF); // Turn off

						u8g2_SetDisplayRotation(&m1_u8g2, U8G2_R2); // 180 degree clockwise rotation
						; // Do extra tasks here if needed
						if ( ir_ota_data_tx_active ) // Tx not completed?
						{
							m1_ir_ota_frame_repeat_handler(IRMP_UNKNOWN_PROTOCOL); // Reset
						} // if ( !ir_ota_data_tx_active )
						infrared_encode_sys_deinit();

						xQueueReset(main_q_hdl); // Reset main q before return
						break; // Exit and return to the calling task (subfunc_handler_task)
					} // else if ( (ir_remote_state==IR_REMOTES_STATE_STANDBY) ||  (ir_remote_state==IR_REMOTES_STATE_PAUSE) ||

				} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				else
				{
					if ( sys_error )
						continue;
				} // else

				if ( this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK )
				{
					; // Do other things for this task, if needed
					if ( ir_remote_state==IR_REMOTES_STATE_STANDBY )
					{
						if ( this_button_row )
						{
							ir_button_icon_redraw(&ir_buttons_info[remote_type].button_post[this_button_row*this_remote_col_n + this_button_col], false); // Restore old image
							this_button_row--;
							ir_button_icon_redraw(&ir_buttons_info[remote_type].button_post[this_button_row*this_remote_col_n + this_button_col], true); // Draw new image
						}
					} // if ( ir_remote_state==IR_REMOTES_STATE_STANDBY )
				} // else if ( this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )
				{
					; // Do other things for this task, if needed
					if ( ir_remote_state==IR_REMOTES_STATE_STANDBY )
					{
						if ( this_button_row < (ir_buttons_info[remote_type].button_row_n-1) )
						{
							ir_button_icon_redraw(&ir_buttons_info[remote_type].button_post[this_button_row*this_remote_col_n + this_button_col], false); // Restore old image
							this_button_row++;
							ir_button_icon_redraw(&ir_buttons_info[remote_type].button_post[this_button_row*this_remote_col_n + this_button_col], true); // Draw new image
						}
					} // if ( ir_remote_state==IR_REMOTES_STATE_STANDBY )
				} // else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK )
				{
					; // Do other things for this task, if needed
					if ( ir_remote_state==IR_REMOTES_STATE_STANDBY )
					{
						if ( this_button_col < (ir_buttons_info[remote_type].button_col_n-1) )
						{
							ir_button_icon_redraw(&ir_buttons_info[remote_type].button_post[this_button_row*this_remote_col_n + this_button_col], false); // Restore old image
							this_button_col++;
							ir_button_icon_redraw(&ir_buttons_info[remote_type].button_post[this_button_row*this_remote_col_n + this_button_col], true); // Draw new image
						}
					} // if ( ir_remote_state==IR_REMOTES_STATE_STANDBY )
				} // else if ( this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK )
				{
					; // Do other things for this task, if needed
					if ( ir_remote_state==IR_REMOTES_STATE_STANDBY )
					{
						if ( this_button_col )
						{
							ir_button_icon_redraw(&ir_buttons_info[remote_type].button_post[this_button_row*this_remote_col_n + this_button_col], false); // Restore old image
							this_button_col--;
							ir_button_icon_redraw(&ir_buttons_info[remote_type].button_post[this_button_row*this_remote_col_n + this_button_col], true); // Draw new image
						}
					} // if ( ir_remote_state==IR_REMOTES_STATE_STANDBY )
				} // else if ( this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
				{
					; // Do other things for this task, if needed
					if ( ir_remote_state==IR_REMOTES_STATE_STANDBY )
					{
						if ( !header_checked )
						{
							sys_error = ir_remote_file_header_check(ir_buttons_info[remote_type].db_filename, remote_type); // Validate again before playing
							if ( sys_error )
							{
								ir_play_status_update(0xFF, 0, 0);
								ir_remote_state = IR_REMOTES_STATE_ERROR;
								continue;
							}
							header_checked = 1;
							ir_samples_counter = 0;
						} // if ( !header_checked )
						// Let play
						function_type = this_button_row*ir_buttons_info[remote_type].button_col_n + this_button_col;
					    infrared_encode_sys_init();
						sys_error = ir_remote_function_play(remote_type, function_type);
						if ( sys_error )
						{
							if ( sys_error==IR_ERROR_CODE_END_OF_FILE )
							{
								ir_play_status_update(1, 1, 0); // Init
								ir_play_status_update(0, 1, 0); // Make 100% progress
								sys_error = 0; // Reset
								/* cycle mode ends in COMPLETE (upstream); the default
								* single-frame mode returns to STANDBY so the next OK
								* restarts from the first device without leaving the
								* screen */
								ir_remote_state = ( cycle_all ) ? IR_REMOTES_STATE_COMPLETE : IR_REMOTES_STATE_STANDBY;
								m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF); // Turn off
							} // if ( sys_error==IR_ERROR_CODE_END_OF_FILE )
							else
							{
								ir_play_status_update(0xFF, 0, 0);
								ir_remote_state = IR_REMOTES_STATE_ERROR;
								m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF); // Turn off
								M1_LOG_E(M1_LOGDB_TAG, "Error occurred L1. Total devices: %d, device count: %d\r\n", total_devices, device_count);
							}
						} // if ( sys_error )
						else
						{
							total_devices = ir_remote_dev_func_counter_get(function_type);
							ir_play_status_update(1, total_devices, 0); // Init
							device_count = 1;
							//ir_play_status_update(0, device_count, 0); // Update
							infrared_transmit(0, 0);
							m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M);
							ir_remote_state = IR_REMOTES_STATE_ACTIVE;
						} // else
					} // if ( ir_remote_state==IR_REMOTES_STATE_STANDBY )
					else if ( ir_remote_state==IR_REMOTES_STATE_ACTIVE )
					{
						ir_remote_state = IR_REMOTES_STATE_PAUSE_REQ; // Pause request
					} // else if ( ir_remote_state==IR_REMOTES_STATE_ACTIVE )
					else if ( ir_remote_state==IR_REMOTES_STATE_PAUSE )
					{
						// Resume from pause.
						m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M);
						ir_remote_state = IR_REMOTES_STATE_ACTIVE;
						ir_play_status_update(0, device_count, 1);
						next_transmission_req = 1; // Request
						// Let pause
					} // else if ( ir_remote_state==IR_REMOTES_STATE_PAUSE )
				} // else if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			else if ( q_item.q_evt_type==Q_EVENT_IRRED_TX ) // Transmit completed?
			{
				ret = infrared_transmit(0, 0);
				if ( ret != IR_TX_COMPLETED ) // Repeated packet?
					continue;
				switch ( ir_remote_state )
				{
					case IR_REMOTES_STATE_PAUSE_REQ: // Pause request?
						ir_remote_state = IR_REMOTES_STATE_PAUSE; // Granted
						m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF); // Turn off
						ir_play_status_update(0, device_count, 1); // Pause
						continue;
						break;

					case IR_REMOTES_STATE_STOP_REQ: // Stop request?
						ir_remote_state = IR_REMOTES_STATE_STANDBY;
						ir_remote_file_deinit();
						header_checked = 0; // Reset
						sys_error = 0;
						ir_button_icons_update(remote_type);
						ir_button_icon_redraw(&ir_buttons_info[remote_type].button_post[this_button_row*this_remote_col_n + this_button_col], true); // Highlight the button
						m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF); // Turn off
						continue;
						break;

					default:
						break;
				} // switch ( ir_remote_state )

				if ( cycle_all )
				{
					next_transmission_req = 1; // Request
				}
				else
				{
					/* Default per-press behavior (issue #43): the pressed
					 * function's ONE frame is done -- back to STANDBY with
					 * the grid redrawn. The database file stays open, so
					 * the next OK plays the next device's code; END_OF_FILE
					 * (below) wraps it back to the first device. */
					ir_remote_state = IR_REMOTES_STATE_STANDBY;
					m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF); // Turn off
					ir_play_status_update(0, device_count, 0);
					ir_button_icons_update(remote_type);
					ir_button_icon_redraw(&ir_buttons_info[remote_type].button_post[this_button_row*this_remote_col_n + this_button_col], true); // Highlight the button
				} // if ( cycle_all )
			} // else if ( q_item.q_evt_type==Q_EVENT_IRRED_TX )
			else
			{
				; // Do other things for this task
			}

			if ( next_transmission_req ) // Next transmission request?
			{
				next_transmission_req = 0; // Reset
				irsnd_init(&timerhdl_ir_carrier, IR_ENCODE_TIMER_TX_CHANNEL); // Reinitialize the timer before new transmission
				sys_error = ir_remote_function_play(remote_type, function_type);
				if ( sys_error )
				{
					if ( sys_error==IR_ERROR_CODE_END_OF_FILE )
					{
						ir_play_status_update(0, total_devices, 0); // Update
						header_checked = 0; // Reset
						sys_error = 0;
						//ir_remote_state = IR_REMOTES_STATE_COMPLETE;
						ir_remote_state = IR_REMOTES_STATE_STANDBY;
						ir_remote_file_deinit();
						m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF); // Turn off
						ir_button_icons_update(remote_type);
						ir_button_icon_redraw(&ir_buttons_info[remote_type].button_post[this_button_row*this_remote_col_n + this_button_col], true); // Highlight the button
					} // if ( sys_error==IR_ERROR_CODE_END_OF_FILE )
					else
					{
						ir_play_status_update(0xFF, 0, 0);
						ir_remote_state = IR_REMOTES_STATE_ERROR;
						m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF); // Turn off
						M1_LOG_E(M1_LOGDB_TAG, "Error occurred L2. Total devices: %d, device count: %d\r\n", total_devices, device_count);
					} // else
				} // if ( sys_error )
				else
				{
					// Update transmit percentage
					ir_play_status_update(0, device_count, 0);
					device_count++;
					vTaskDelay(IR_REMOTE_PAUSE_TIME_BETWEEN_FRAME); // A delay here is necessary for the function to work properly!
					infrared_transmit(0, 0);
				} // else
			} // if ( next_transmission_req )

		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task

	m1_ir_subsys_release(); // single-owner IRMP rule
} // void infrared_universal_all_remotes(uint8_t remote_type, uint8_t cycle_all)



/*============================================================================*/
/**
  * @brief  Default TV remote session: ONE frame per OK press.
  * @param
  * @retval
  */
/*============================================================================*/
void infrared_universal_tv_remotes(void)
{
	infrared_universal_all_remotes(IR_REMOTETYPE_TV, 0); // default: single frame per press
} // void infrared_universal_tv_remotes(void)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void infrared_universal_audio_remotes(void)
{
	infrared_universal_all_remotes(IR_REMOTETYPE_AUDIO, 0); // default: single frame per press
} // void infrared_universal_audio_remotes(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void infrared_universal_projector_remotes(void)
{
	infrared_universal_all_remotes(IR_REMOTETYPE_PROJECTOR, 0); // default: single frame per press
} // void infrared_universal_projector_remotes(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void infrared_universal_ac_remotes(void)
{
	infrared_universal_all_remotes(IR_REMOTETYPE_AC, 0); // default: single frame per press
} // void infrared_universal_ac_remotes(void)


/*============================================================================*/
/**
  * @brief  Explicit "cycle all in category" submenu actions (issue #43
  * 		part 3 / PR-6): the upstream cycle-through-every-device session
  * 		with pause/resume and progress UI -- never the default
  * 		per-press behavior (that is the single-frame remote above and
  * 		the SD-card browser's one-frame-per-OK).
  * @param
  * @retval
  */
/*============================================================================*/
void infrared_universal_cycle_all_tv(void)
{
	infrared_universal_all_remotes(IR_REMOTETYPE_TV, 1); // explicit cycle-all action
} // void infrared_universal_cycle_all_tv(void)


/*============================================================================*/
/**
  * @brief  Cycle-all session for the audio category (see
  * 		infrared_universal_cycle_all_tv).
  * @param
  * @retval
  */
/*============================================================================*/
void infrared_universal_cycle_all_audio(void)
{
	infrared_universal_all_remotes(IR_REMOTETYPE_AUDIO, 1); // explicit cycle-all action
} // void infrared_universal_cycle_all_audio(void)


/*============================================================================*/
/**
  * @brief  Cycle-all session for the projector category (see
  * 		infrared_universal_cycle_all_tv).
  * @param
  * @retval
  */
/*============================================================================*/
void infrared_universal_cycle_all_projector(void)
{
	infrared_universal_all_remotes(IR_REMOTETYPE_PROJECTOR, 1); // explicit cycle-all action
} // void infrared_universal_cycle_all_projector(void)


/*============================================================================*/
/**
  * @brief  Cycle-all session for the AC category (see
  * 		infrared_universal_cycle_all_tv).
  * @param
  * @retval
  */
/*============================================================================*/
void infrared_universal_cycle_all_ac(void)
{
	infrared_universal_all_remotes(IR_REMOTETYPE_AC, 1); // explicit cycle-all action
} // void infrared_universal_cycle_all_ac(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static uint8_t ir_remote_function_play(uint8_t ir_remote_type, uint8_t function_type)
{
	uint8_t sys_error, i;
	IRMP_DATA ir_remote_data;

	do
	{
		sys_error = ir_remote_function_data_read(ir_remote_type, function_type);
		if ( sys_error )
			break;
		sys_error = ir_payload_info_get(&ir_universal_payload);
		if ( sys_error )
			break;
	} while (0);

	while ( !sys_error )
	{
		if ( ir_universal_payload.ir_data_type==IR_DATATYPE_PARSED )
		{
			for (i=0; i<IR_PROTOCOLS_MAPPING_MAX; i++)
			{
				if ( !strcmp(ir_universal_payload.pprotocol, ir_protocols_mapping_table[i].protocol_name) )
					break;
			}
			if ( i >= IR_PROTOCOLS_MAPPING_MAX )
			{
			    M1_LOG_E(M1_LOGDB_TAG, "%s not supported!\r\n", ir_universal_payload.pprotocol);
			    ir_remote_data.protocol = IRMP_UNKNOWN_PROTOCOL;
			    sys_error = 1;
			} // if ( i >= IR_PROTOCOLS_MAPPING_MAX )
			else
			{
				ir_remote_data.protocol = ir_protocols_mapping_table[i].protocol_number;
				ir_remote_data.address  = ir_universal_payload.address;
				ir_remote_data.command  = ir_universal_payload.command;
				ir_remote_data.duty_cycle = ir_universal_payload.duty_cycle;
				ir_remote_data.frequency = ir_universal_payload.frequency;
				ir_remote_data.flags    = 1; // don't repeat frame
			} // else
		} // if ( ir_universal_payload.ir_data_type==IR_DATATYPE_PARSED )
		else if ( ir_universal_payload.ir_data_type==IR_DATATYPE_RAW )
		{
			ir_remote_data.protocol = IRMP_RAW_PROTOCOL;
			ir_remote_data.duty_cycle = ir_universal_payload.duty_cycle;
			ir_remote_data.frequency = ir_universal_payload.frequency;
			ir_universal_payload.pprotocol = ir_protocols_mapping_table[IR_PROTOCOLS_MAPPING_MAX-1].protocol_name;
			ir_remote_data.flags    = 1; // don't repeat frame
		} // else if ( ir_universal_payload.ir_data_type==IR_DATATYPE_RAW )
		else
		{
			break;
		} // else
		ir_samples_counter++;
		M1_LOG_I(M1_LOGDB_TAG, "%s -> %d\r\n", ir_universal_payload.pprotocol, ir_samples_counter);
		if ( !sys_error )
		{
			irsnd_generate_tx_data(ir_remote_data); // make ota data
			infrared_transmit(1, ir_remote_data.protocol); // initialize the tx
		} // if ( !sys_error )
		break;
	} // while ( !sys_error )

	return sys_error;

} // static uint8_t ir_remote_function_play(uint8_t ir_remote_type, uint8_t function_type)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void ir_button_icons_update(uint8_t remote_type)
{
	m1_u8g2_firstpage();
	u8g2_SetFont(&m1_u8g2, M1_DISP_IR_REMOTES_FONT_B1);
	switch ( remote_type )
	{
		case IR_REMOTETYPE_TV:
			u8g2_DrawStr(&m1_u8g2, 28, 8, "TV");

			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_TVS_BUTTON_ROW1_X1 - 2, IR_REMOTE_TVS_BUTTON_ROW1_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_21x21);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_TVS_BUTTON_ROW1_X2 - 2, IR_REMOTE_TVS_BUTTON_ROW1_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_21x21);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_TVS_BUTTON_ROW2_X1 - 2, IR_REMOTE_TVS_BUTTON_ROW2_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_21x21);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_TVS_BUTTON_ROW2_X2 - 2, IR_REMOTE_TVS_BUTTON_ROW2_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_21x21);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_TVS_BUTTON_ROW3_X1 - 2, IR_REMOTE_TVS_BUTTON_ROW3_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_21x21);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_TVS_BUTTON_ROW3_X2 - 2, IR_REMOTE_TVS_BUTTON_ROW3_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_21x21);

			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_TVS_BUTTON_ROW1_X1, IR_REMOTE_TVS_BUTTON_ROW1_Y, IR_REMOTE_TV_BUTTON_WIDTH, IR_REMOTE_TV_BUTTON_HEIGHT, button_power_18x18);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_TVS_BUTTON_ROW1_X2, IR_REMOTE_TVS_BUTTON_ROW1_Y, IR_REMOTE_TV_BUTTON_WIDTH, IR_REMOTE_TV_BUTTON_HEIGHT, button_mute_18x18);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_TVS_BUTTON_ROW2_X1, IR_REMOTE_TVS_BUTTON_ROW2_Y, IR_REMOTE_TV_BUTTON_WIDTH, IR_REMOTE_TV_BUTTON_HEIGHT, button_plus_18x18);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_TVS_BUTTON_ROW2_X2, IR_REMOTE_TVS_BUTTON_ROW2_Y, IR_REMOTE_TV_BUTTON_WIDTH, IR_REMOTE_TV_BUTTON_HEIGHT, button_up_18x18);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_TVS_BUTTON_ROW3_X1, IR_REMOTE_TVS_BUTTON_ROW3_Y, IR_REMOTE_TV_BUTTON_WIDTH, IR_REMOTE_TV_BUTTON_HEIGHT, button_minus_18x18);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_TVS_BUTTON_ROW3_X2, IR_REMOTE_TVS_BUTTON_ROW3_Y, IR_REMOTE_TV_BUTTON_WIDTH, IR_REMOTE_TV_BUTTON_HEIGHT, button_down_18x18);

			u8g2_SetFont(&m1_u8g2, M1_DISP_IR_REMOTES_FONT_1);
			u8g2_DrawStr(&m1_u8g2, 3, 46, "POWER");
			u8g2_DrawStr(&m1_u8g2, 38, 46, "MUTE");
			u8g2_DrawStr(&m1_u8g2, 1, 92, "VOLUME");
			u8g2_DrawStr(&m1_u8g2, 31, 92, "CHANNEL");
			break;

		case IR_REMOTETYPE_AUDIO:
			u8g2_DrawStr(&m1_u8g2, 2, 8, "Audio Player");

			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_AUDIOS_BUTTON_ROW1_X1 - 2, IR_REMOTE_AUDIOS_BUTTON_ROW1_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_21x21);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_AUDIOS_BUTTON_ROW1_X2 - 2, IR_REMOTE_AUDIOS_BUTTON_ROW1_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_21x21);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_AUDIOS_BUTTON_ROW2_X1 - 2, IR_REMOTE_AUDIOS_BUTTON_ROW2_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_21x21);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_AUDIOS_BUTTON_ROW2_X2 - 2, IR_REMOTE_AUDIOS_BUTTON_ROW2_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_21x21);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_AUDIOS_BUTTON_ROW3_X1 - 2, IR_REMOTE_AUDIOS_BUTTON_ROW3_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_21x21);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_AUDIOS_BUTTON_ROW3_X2 - 2, IR_REMOTE_AUDIOS_BUTTON_ROW3_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_21x21);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_AUDIOS_BUTTON_ROW4_X1 - 2, IR_REMOTE_AUDIOS_BUTTON_ROW4_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_21x21);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_AUDIOS_BUTTON_ROW4_X2 - 2, IR_REMOTE_AUDIOS_BUTTON_ROW4_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_21x21);

			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_AUDIOS_BUTTON_ROW1_X1, IR_REMOTE_AUDIOS_BUTTON_ROW1_Y, IR_REMOTE_AUDIO_BUTTON_WIDTH, IR_REMOTE_AUDIO_BUTTON_HEIGHT, button_power_18x18);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_AUDIOS_BUTTON_ROW1_X2, IR_REMOTE_AUDIOS_BUTTON_ROW1_Y, IR_REMOTE_AUDIO_BUTTON_WIDTH, IR_REMOTE_AUDIO_BUTTON_HEIGHT, button_mute_18x18);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_AUDIOS_BUTTON_ROW2_X1, IR_REMOTE_AUDIOS_BUTTON_ROW2_Y, IR_REMOTE_AUDIO_BUTTON_WIDTH, IR_REMOTE_AUDIO_BUTTON_HEIGHT, button_play_18x18);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_AUDIOS_BUTTON_ROW2_X2, IR_REMOTE_AUDIOS_BUTTON_ROW2_Y, IR_REMOTE_AUDIO_BUTTON_WIDTH, IR_REMOTE_AUDIO_BUTTON_HEIGHT, button_plus_18x18);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_AUDIOS_BUTTON_ROW3_X1, IR_REMOTE_AUDIOS_BUTTON_ROW3_Y, IR_REMOTE_AUDIO_BUTTON_WIDTH, IR_REMOTE_AUDIO_BUTTON_HEIGHT, button_pause_18x18);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_AUDIOS_BUTTON_ROW3_X2, IR_REMOTE_AUDIOS_BUTTON_ROW3_Y, IR_REMOTE_AUDIO_BUTTON_WIDTH, IR_REMOTE_AUDIO_BUTTON_HEIGHT, button_minus_18x18);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_AUDIOS_BUTTON_ROW4_X1, IR_REMOTE_AUDIOS_BUTTON_ROW4_Y, IR_REMOTE_AUDIO_BUTTON_WIDTH, IR_REMOTE_AUDIO_BUTTON_HEIGHT, button_previous_18x18);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_AUDIOS_BUTTON_ROW4_X2, IR_REMOTE_AUDIOS_BUTTON_ROW4_Y, IR_REMOTE_AUDIO_BUTTON_WIDTH, IR_REMOTE_AUDIO_BUTTON_HEIGHT, button_next_18x18);

			u8g2_SetFont(&m1_u8g2, M1_DISP_IR_REMOTES_FONT_1);
			u8g2_DrawStr(&m1_u8g2, 3, 38, "POWER");
			u8g2_DrawStr(&m1_u8g2, 38, 38, "MUTE");
			u8g2_DrawStr(&m1_u8g2, 7, 67, "PLAY");
			u8g2_DrawStr(&m1_u8g2, 34, 67, "VOLUME");
			u8g2_DrawStr(&m1_u8g2, 4, 97, "PAUSE");
			u8g2_DrawStr(&m1_u8g2, 7, 125, "PREV");
			u8g2_DrawStr(&m1_u8g2, 39, 125, "NEXT");
			break;

		case IR_REMOTETYPE_PROJECTOR:
			u8g2_DrawStr(&m1_u8g2, 10, 8, "Projector");

			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_PROJECTORS_BUTTON_ROW1_X1 - 2, IR_REMOTE_PROJECTORS_BUTTON_ROW1_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_21x21);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_PROJECTORS_BUTTON_ROW1_X2 - 2, IR_REMOTE_PROJECTORS_BUTTON_ROW1_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_21x21);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_PROJECTORS_BUTTON_ROW2_X1 - 2, IR_REMOTE_PROJECTORS_BUTTON_ROW2_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_21x21);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_PROJECTORS_BUTTON_ROW2_X2 - 2, IR_REMOTE_PROJECTORS_BUTTON_ROW2_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_21x21);

			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_PROJECTORS_BUTTON_ROW1_X1, IR_REMOTE_PROJECTORS_BUTTON_ROW1_Y, IR_REMOTE_PROJECTOR_BUTTON_WIDTH, IR_REMOTE_PROJECTOR_BUTTON_HEIGHT, button_power_18x18);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_PROJECTORS_BUTTON_ROW1_X2, IR_REMOTE_PROJECTORS_BUTTON_ROW1_Y, IR_REMOTE_PROJECTOR_BUTTON_WIDTH, IR_REMOTE_PROJECTOR_BUTTON_HEIGHT, button_mute_18x18);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_PROJECTORS_BUTTON_ROW2_X1, IR_REMOTE_PROJECTORS_BUTTON_ROW2_Y, IR_REMOTE_PROJECTOR_BUTTON_WIDTH, IR_REMOTE_PROJECTOR_BUTTON_HEIGHT, button_plus_18x18);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_PROJECTORS_BUTTON_ROW2_X2, IR_REMOTE_PROJECTORS_BUTTON_ROW2_Y, IR_REMOTE_PROJECTOR_BUTTON_WIDTH, IR_REMOTE_PROJECTOR_BUTTON_HEIGHT, button_minus_18x18);

			u8g2_SetFont(&m1_u8g2, M1_DISP_IR_REMOTES_FONT_1);
			u8g2_DrawStr(&m1_u8g2, 3, 46, "POWER");
			u8g2_DrawStr(&m1_u8g2, 38, 46, "MUTE");
			u8g2_DrawStr(&m1_u8g2, 18, 92, "VOLUME");
			break;

		case IR_REMOTETYPE_AC:
			u8g2_DrawStr(&m1_u8g2, 28, 8, "AC");

			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_ACS_BUTTON_ROW1_X1 - 2, IR_REMOTE_ACS_BUTTON_ROW1_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_21x21);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_ACS_BUTTON_ROW1_X2 - 2, IR_REMOTE_ACS_BUTTON_ROW1_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_21x21);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_ACS_BUTTON_ROW2_X1 - 2, IR_REMOTE_ACS_BUTTON_ROW2_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_up_21x21);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_ACS_BUTTON_ROW2_X2 - 2, IR_REMOTE_ACS_BUTTON_ROW2_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_up_21x21);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_ACS_BUTTON_ROW4_X1 - 2, IR_REMOTE_ACS_BUTTON_ROW4_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_down_21x21);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_ACS_BUTTON_ROW4_X2 - 2, IR_REMOTE_ACS_BUTTON_ROW4_Y - 1, IR_BUTTON_FRAME_WIDTH, IR_BUTTON_FRAME_HEIGHT, ir_button_frame_down_21x21);

			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_ACS_BUTTON_ROW3_X1 - 2, IR_REMOTE_ACS_BUTTON_ROW3_Y - 1, 20, 20, weather_cold_20x20);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_ACS_BUTTON_ROW3_X2 - 2, IR_REMOTE_ACS_BUTTON_ROW3_Y - 1, 20, 20, weather_hot_20x20);

			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_ACS_BUTTON_ROW1_X1, IR_REMOTE_ACS_BUTTON_ROW1_Y, IR_REMOTE_AC_BUTTON_WIDTH, IR_REMOTE_AC_BUTTON_HEIGHT, button_power_off_18x18);
			u8g2_DrawXBMP(&m1_u8g2, IR_REMOTE_ACS_BUTTON_ROW1_X2, IR_REMOTE_ACS_BUTTON_ROW1_Y, IR_REMOTE_AC_BUTTON_WIDTH, IR_REMOTE_AC_BUTTON_HEIGHT, weather_dry_18x18);

			u8g2_SetFont(&m1_u8g2, M1_DISP_IR_REMOTES_FONT_1);
			u8g2_DrawStr(&m1_u8g2, 9, 44, "OFF");
			u8g2_DrawStr(&m1_u8g2, 40, 44, "DRY");
			u8g2_DrawStr(&m1_u8g2, IR_REMOTE_ACS_BUTTON_ROW3_X1 - 1, IR_REMOTE_ACS_BUTTON_ROW3_Y + 27, "COOL");
			u8g2_DrawStr(&m1_u8g2, IR_REMOTE_ACS_BUTTON_ROW3_X2 - 1, IR_REMOTE_ACS_BUTTON_ROW3_Y + 27, "HEAT");
			ir_ac_button_text_update(0, 0);
			break;

		default:
			break;
	} // switch ( remote_type )
	m1_u8g2_nextpage();	 // Update display RAM
} // static void ir_button_icons_update(uint8_t remote_type)




/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void ir_ac_button_text_update(uint8_t pos_x, uint8_t pos_y)
{
	uint8_t pos_sum, text_mask = 0;

	pos_sum = pos_x + pos_y;

	text_mask = 0x0F; // 00001111
	if ( pos_sum >= (IR_REMOTE_ACS_BUTTON_ROW4_X2 + IR_REMOTE_ACS_BUTTON_ROW4_Y) )
	{
		text_mask &= 0x08; // 00001000
	}
	else if ( pos_sum >= (IR_REMOTE_ACS_BUTTON_ROW4_X1 + IR_REMOTE_ACS_BUTTON_ROW4_Y) )
	{
		text_mask &= 0x04; // 00000100
	}
	else if ( pos_sum >= (IR_REMOTE_ACS_BUTTON_ROW2_X2 + IR_REMOTE_ACS_BUTTON_ROW2_Y) )
	{
		text_mask &= 0x02; // 00000010
	}
	else if ( pos_sum >= (IR_REMOTE_ACS_BUTTON_ROW2_X1 + IR_REMOTE_ACS_BUTTON_ROW2_Y) )
	{
		text_mask &= 0x01; // 00000001
	}

	u8g2_SetFont(&m1_u8g2, M1_DISP_IR_REMOTES_FONT_1);

	if ( text_mask & 0x01 )
		u8g2_DrawStr(&m1_u8g2, IR_REMOTE_ACS_BUTTON_ROW2_X1 + 2, IR_REMOTE_ACS_BUTTON_ROW2_Y + 14, "MAX");
	if ( text_mask & 0x02 )
		u8g2_DrawStr(&m1_u8g2, IR_REMOTE_ACS_BUTTON_ROW2_X2 + 2, IR_REMOTE_ACS_BUTTON_ROW2_Y + 14, "MAX");
	if ( text_mask & 0x04 )
	{
		u8g2_DrawStr(&m1_u8g2, IR_REMOTE_ACS_BUTTON_ROW4_X1 + 1, IR_REMOTE_ACS_BUTTON_ROW4_Y + 9, "23  C");
		u8g2_SetFont(&m1_u8g2, M1_DISP_IR_REMOTES_FONT_S2);
		u8g2_DrawStr(&m1_u8g2, IR_REMOTE_ACS_BUTTON_ROW4_X1 + 1 + 8, IR_REMOTE_ACS_BUTTON_ROW4_Y + 7, "o");
	}
	if ( text_mask & 0x08 )
	{
		u8g2_DrawStr(&m1_u8g2, IR_REMOTE_ACS_BUTTON_ROW4_X2 + 1, IR_REMOTE_ACS_BUTTON_ROW4_Y + 9, "23  C");
		u8g2_SetFont(&m1_u8g2, M1_DISP_IR_REMOTES_FONT_S2);
		u8g2_DrawStr(&m1_u8g2, IR_REMOTE_ACS_BUTTON_ROW4_X2 + 1 + 8, IR_REMOTE_ACS_BUTTON_ROW4_Y + 7, "o");
	}
} // static void ir_ac_button_text_update(uint8_t pos_x, uint8_t pos_y)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void ir_button_icon_redraw(S_M1_IR_Remote_Buttons_t *pbutton, uint8_t inverted)
{
	uint8_t text_color, background_color;
	const uint8_t *pimage;

	if ( inverted ) // Draw color is the background color?
	{
		text_color = M1_DISP_DRAW_COLOR_BG;
		background_color = M1_DISP_DRAW_COLOR_TXT;
	}
	else
	{
		text_color = M1_DISP_DRAW_COLOR_TXT;
		background_color = M1_DISP_DRAW_COLOR_BG;
	}
	if ( !pbutton->button_image_inverted )
	{
		u8g2_SetDrawColor(&m1_u8g2, background_color);
		u8g2_DrawBox(&m1_u8g2, pbutton->button_x, pbutton->button_y, pbutton->button_width, pbutton->button_height); // Clear old content
		u8g2_SetDrawColor(&m1_u8g2, text_color);
		u8g2_DrawXBMP(&m1_u8g2, pbutton->button_x, pbutton->button_y, pbutton->button_width, pbutton->button_height, pbutton->button_image); // Draw new content
	} // if ( !pbutton->button_image_inverted )
	else
	{
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
		u8g2_DrawBox(&m1_u8g2, pbutton->button_x - 2, pbutton->button_y - 1, pbutton->button_width, pbutton->button_height); // Clear old content
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		if ( !inverted )
			pimage = pbutton->button_image;
		else
			pimage = pbutton->button_image_inverted;
		u8g2_DrawXBMP(&m1_u8g2, pbutton->button_x - 2, pbutton->button_y - 1, pbutton->button_width, pbutton->button_height, pimage); // Draw new content
		u8g2_SetDrawColor(&m1_u8g2, text_color);
		ir_ac_button_text_update(pbutton->button_x, pbutton->button_y);
	} // else
	m1_u8g2_nextpage();	 // Update display RAM
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // Restore default system color
} // static void ir_button_icon_redraw(S_M1_IR_Remote_Buttons_t *pbutton, uint8_t inverted)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
#define SDCARD_INFO_BOX_FRAME_WIDTH		64
#define SDCARD_INFO_BOX_FRAME_HEIGHT	76
static void ir_play_status_update(uint8_t init, uint16_t round_n, uint8_t refresh)
{
	uint8_t percent_txt[30];
	uint8_t percent, txt_width, txt_pos;
	uint8_t msg_len, msg_x0, msg_y0, sdcard_x0, sdcard_y0;
	uint16_t percent_cnt;
	static uint16_t total_rounds = 0;
	static uint8_t progress_percent_count = 0;

	if ( init )
	{
		total_rounds = round_n;
		round_n = 0; // Reset after init
		progress_percent_count = 0;
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // set to background color
		// Draw solid box to clear existing content
		u8g2_DrawBox(&m1_u8g2, 0, 32, SDCARD_INFO_BOX_FRAME_WIDTH, SDCARD_INFO_BOX_FRAME_HEIGHT);
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // return to text color
		u8g2_DrawXBMP(&m1_u8g2, 0, 32, SDCARD_INFO_BOX_FRAME_WIDTH, SDCARD_INFO_BOX_FRAME_HEIGHT, m1_frame_64x76); // Info frame

		if ( init > 1 ) // Error message
		{
			msg_len = strlen(sdcard_db_error_message);
			txt_width = msg_len*4; // Font width = 4 (4x5)
			sdcard_x0 = (M1_LCD_DISPLAY_HEIGHT - SDCARD_ERROR_IMAGE_WIDTH)/2 - 1;
			sdcard_y0 = (M1_LCD_DISPLAY_WIDTH - SDCARD_ERROR_IMAGE_HEIGHT)/2 - 1;
			msg_x0 = (M1_LCD_DISPLAY_HEIGHT - txt_width)/2 + 1;
			msg_y0 = M1_LCD_DISPLAY_WIDTH - (M1_LCD_DISPLAY_WIDTH - SDCARD_ERROR_IMAGE_HEIGHT)/2 + 7 + 3; // 7 = font height, 3 = space from sdcard_image bottom to the top of the message
			// Draw sdcard error image
			u8g2_DrawXBMP(&m1_u8g2, sdcard_x0, sdcard_y0, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sd_card_error_46x36);
			u8g2_SetFont(&m1_u8g2, M1_DISP_IR_REMOTES_FONT_1);
			// Draw error warning message
			u8g2_DrawStr(&m1_u8g2, msg_x0, msg_y0, sdcard_db_error_message);
			m1_u8g2_nextpage(); // Update display RAM
			return;
		} // if ( init > 1 )

		u8g2_DrawXBMP(&m1_u8g2, IR_TRANSMIT_PROGRESS_SLIDE_STRIP_X0, IR_TRANSMIT_PROGRESS_SLIDE_STRIP_Y0,
					60, 10, m1_progress_60x10); // Progress slide strip
		u8g2_DrawXBMP(&m1_u8g2, 4, 72, 10, 10, target_i_10x10); // Play/Pause
		u8g2_DrawXBMP(&m1_u8g2, 4, 88, 10, 10, return_10x10); // Return
		u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 26, 68, "00%");
		u8g2_DrawStr(&m1_u8g2, 18, 96, "to stop");
	} // if ( init )

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // set to background color
	// Draw solid box to clear existing content
	u8g2_DrawBox(&m1_u8g2, 6, 37, 54, 9);
	u8g2_DrawBox(&m1_u8g2, 18, 73, 42, 8);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // return to text color
	if ( ir_remote_state!=IR_REMOTES_STATE_PAUSE ) // Play?
	{
		u8g2_DrawStr(&m1_u8g2, 6, 44, "Transmitting");
		u8g2_DrawStr(&m1_u8g2, 18, 80, "to pause");
	} // if ( ir_remote_state!=IR_REMOTES_STATE_PAUSE )
	else // Pause
	{
		u8g2_DrawStr(&m1_u8g2, 18, 44, "Paused");
		u8g2_DrawStr(&m1_u8g2, 18, 80, "to play");
	} // else

	percent_cnt = ((uint32_t)IR_TRANSMIT_FULL_PROGRESS_WIDTH*round_n)/total_rounds;
	if ( (percent_cnt > progress_percent_count) || (refresh) ) // Is there progress or refresh request?
	{
		progress_percent_count = percent_cnt; // Update new progress
		percent = ((uint32_t)100*round_n)/total_rounds;
		if ( ir_remote_state!=IR_REMOTES_STATE_PAUSE ) // Play?
			sprintf(percent_txt, "%02d%%", percent);
		else
			sprintf(percent_txt, "%d/%d", round_n, total_rounds);
		txt_width = strlen(percent_txt)*4; // 4 = font width
		txt_pos = (M1_LCD_DISPLAY_HEIGHT - txt_width)/2 + 1;
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // set to background color
		// Draw solid box to clear existing content
		u8g2_DrawBox(&m1_u8g2, 4, 68 - M1_SUB_MENU_FONT_HEIGHT + 1, 58, M1_SUB_MENU_FONT_HEIGHT);
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // return to text color
		u8g2_DrawStr(&m1_u8g2, txt_pos, 68, percent_txt); // Write new content
		// Draw progess bar (box)
		u8g2_DrawBox(&m1_u8g2, IR_TRANSMIT_PROGRESS_SLIDE_STRIP_X0 + 1, IR_TRANSMIT_PROGRESS_SLIDE_STRIP_Y0 + 1,
				progress_percent_count, M1_SUB_MENU_FONT_HEIGHT);
		if ( progress_percent_count==IR_TRANSMIT_FULL_PROGRESS_WIDTH ) // complete 100%?
			total_rounds = 0; // Reset
 	} // if ( (percent_cnt > progress_percent_count) || (refresh) )

	m1_u8g2_nextpage(); // Update display RAM
} // static void ir_play_status_update(uint8_t init)
