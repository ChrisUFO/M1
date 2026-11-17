/* See COPYING.txt for license details. */

/*
*
* m1_cli_help.c
*
* Help for CLI commands
*
* M1 Project
*
*/
/*************************** I N C L U D E S **********************************/

#include "main.h"
#include "m1_cli.h"
#include "FreeRTOS.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"CLI"


//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/


/********************* F U N C T I O N   P R O T O T Y P E S ******************/

BaseType_t cmd_m1_mtest_help(void);
void cmd_m1_mtest_help_basic_system(void);
void cmd_m1_mtest_help_sdcard(void);
void cmd_m1_mtest_help_led(void);
void cmd_m1_mtest_help_lcd(void);
void cmd_m1_mtest_help_infrared(void);
void cmd_m1_mtest_help_power(void);
void cmd_m1_mtest_help_subghz(void);
void cmd_m1_mtest_help_esp32(void);
void cmd_m1_mtest_help_gpio(void);
/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/*
 * This command displays help for the mtest command
 */
/*============================================================================*/
BaseType_t cmd_m1_mtest_help(void)
{
	/* The ID map: every mtest ID this branch actually implements, by decade.
	 * It is the index only -- parameters and per-ID detail stay on the decade
	 * pages ("mtest <decade>" with no parameter prints one). Emitted through
	 * M1_LOG_N because the 200-byte CLI response buffer cannot hold it. IDs
	 * that no handler implements are deliberately absent: the legacy pages
	 * advertised 53/54 (folded into 52) and nothing else is claimed here. */
	M1_LOG_N(M1_LOGDB_TAG, "\r\nCommand syntax: mtest <cmd_type> [param1] [param2]...\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "ID map by decade; \"mtest <decade>\" shows that decade's page.\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n0-9 basic system\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "  0 virtual keyboard (not enabled)   1 buzzer play\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "  2 set font (fixed at build time)  3 write text\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "  4 clear display  5 heap/stack telemetry\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "  6 reset file-browser perf counters\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "  9 firmware CRC corruption test (DEV-ONLY, not in shipped images)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n10-19 SD card\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "  10 create file  11 close file  12 open file  13 write file\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "  14 read file  17 delete file/folder  18 format drive  19 list\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n20-29 LED\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "  20 fast blink RGB  21 RGB off\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "  22/23/24 direct R/G/B (legacy-equivalent)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n30-39 LCD\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "  30 backlight  31 pwr save  32 clear  33 contrast  34 ratio  35 init retry\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n40-49 infrared: 40 transmit\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "50-59 power: 50 off  51 reboot  52 bc|pd [reg] read/status  59 battery info\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n60-69 Sub-GHz\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "  60 init  61 CW TX (DEV-ONLY)  62 TX power\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "  63 RX  64 frontend network  68 RSSI  69 radio state\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n70-79 ESP32: 70 init  71 AP list  72 AT cmd  78 reset  79 deinit\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "80-89 GPIO: 80 ext 3V3 on/off  81 ext 5V on/off\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "90-99 NFC: 90 placeholder  91 mock .m1h to SD (debug)\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "  92 live nonce harvest (debug)\r\n");

	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n");

	return pdFALSE;
} // BaseType_t cmd_m1_mtest_help(void)



/*============================================================================*/
/*
 * This command displays help for basic system test
 */
/*============================================================================*/
void cmd_m1_mtest_help_basic_system(void)
{
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 0: virtual keyboard test\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 0 text_field default_filename\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 1: buzzer - play\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest frequency(Hz) duration(millisecond)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 2: Set font\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 2 font_name\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 3: Write text\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 3 text row_number\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 4: Clear display\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 4 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 5: heap and task stack telemetry\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 5\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n");

} // void cmd_m1_mtest_help_basic_system(void)


/*============================================================================*/
/*
 * This command displays help for sdcard
 */
/*============================================================================*/
void cmd_m1_mtest_help_sdcard(void)
{
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 10: SD card - create a new file\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 10 filename\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 11: SD card - close a file\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 11 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 12: SD card - open a file\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 12 filename\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 13: SD card - write to a file\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 13 write_content write_size\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 14: SD card - read from a file\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 14 read_size\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 17: SD card - delete a file or folder\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 17 file_name\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 18: SD card - Format a drive\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 18 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 19: SD card - list all files and folders\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 19 folder_name\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n");
} // void cmd_m1_mtest_help_sdcard(void)



/*============================================================================*/
/*
 * This command displays help for LED
 */
/*============================================================================*/
void cmd_m1_mtest_help_led(void)
{
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 20: LED - fast blink mode with RGB\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 20 r_g_b(1-2-4 or combined) rgb_pwm(0-255) on_off_time(0-65535)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 21: LED - off mode with RGB\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 21 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 22: LED - direct mode with R\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 22 r_pwm(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 23: LED - direct mode with G\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 23 g_pwm(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 24: LED - direct mode with B\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 24 b_pwm(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n");
} // void cmd_m1_mtest_help_led(void)



/*============================================================================*/
/*
 * This command displays help for LCD
 */
/*============================================================================*/
void cmd_m1_mtest_help_lcd(void)
{
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 30: LCD - set backlight\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 30 bl_pwm(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 31: LCD - set power save\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 31 on(1)_off(0)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 32: LCD - clear display\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 32 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 33: LCD - set contrast\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 33 contrast_val(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 34: LCD - set regulation ratio\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 34 reg_ratio(0-7)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 35: LCD - init retry when display fails\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 35 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n");
} // void cmd_m1_mtest_help_lcd(void)



/*============================================================================*/
/*
 * This command displays help for infrared
 */
/*============================================================================*/
void cmd_m1_mtest_help_infrared(void)
{
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 40: Infrared - transmit\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 40 protocol address command repeat\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n");
} // void cmd_m1_mtest_help_infrared(void)



/*============================================================================*/
/*
 * This command displays help for power
 */
/*============================================================================*/
void cmd_m1_mtest_help_power(void)
{
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 50: power off\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 50 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 51: reboot\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 51 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	/* One ID, three targets: "bc" (battery charger), "pd" (USB-PD), optional
	 * register number; without it the full status is printed. The legacy page
	 * advertised 53 (fuel gauge) and 54 (USB-PD) as separate IDs, but no
	 * handler implements them -- both are this case. */
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 52: read a charger (bc) or USB-PD (pd) register, or print its status\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 52 bc|pd [reg_no]\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 59: get battery information\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 59 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n");
} // void cmd_m1_mtest_help_power(void)



/*============================================================================*/
/*
 * This command displays help for Sub-GHz
 */
/*============================================================================*/
void cmd_m1_mtest_help_subghz(void)
{
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 60: Sub-GHz - init\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 60 frequency(304, 310, 315, 345, 390, 433, 915)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 61: Sub-GHz - transmit in CW mode\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 61 channel(0-255, 256 to turn off) \r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 62: Sub-GHz - set transmit power\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 62 power(0-127) \r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 63: Sub-GHz - receive mode\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 63 channel(0-255) \r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 64: Sub-GHz - select frontend network\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 64 network(0:315, 1:433, 2:915, 3:None)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 68: Sub-GHz - get RSSI\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 68 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 69: Sub-GHz - get device operating state\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 69 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n");
} // void cmd_m1_mtest_help_subghz(void)



/*============================================================================*/
/*
 * This command displays help for ESP32
 */
/*============================================================================*/
void cmd_m1_mtest_help_esp32(void)
{
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 70: ESP32 - init ESP32 module\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 70 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 71: ESP32 - get access point list\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 71 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 72: ESP32 - send AT command\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 72 AT_command\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 78: ESP32 - reset\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 78 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 79: ESP32 - deinit for flash update\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 79 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n");
} // void cmd_m1_mtest_help_esp32(void)



/*============================================================================*/
/*
 * This command displays help for GPIO
 */
/*============================================================================*/
void cmd_m1_mtest_help_gpio(void)
{
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 80: GPIO - turn external 3V3 on/off\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 80 1(on)/0(off)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 81: GPIO - turn external 5V on/off\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 81 1(on)/0(off)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n");
} // void cmd_m1_mtest_help_gpio(void)



/*============================================================================*/
/*
 * This command displays help for NFC
 */
/*============================================================================*/
void cmd_m1_mtest_help_nfc(void)
{
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 90: NFC - NULL\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 90 1(on)/0(off)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job


	M1_LOG_N(M1_LOGDB_TAG, "\r\n");
} // void cmd_m1_mtest_help_nfc(void)
