/* See COPYING.txt for license details. */

/*
*
* m1_led_indicator.h
*
* Library for led indicator
*
* M1 Project
*
*/

#ifndef M1_LED_INDICATOR_H_
#define M1_LED_INDICATOR_H_

typedef enum
{
	LED_INDICATOR_OFF_FN_ID = 0,
	LED_INDICATOR_ON_FN_ID,
	LED_FAST_BLINK_FN_ID,
	LED_FW_UPDATE_ON_FN_ID,
	LED_BATTERY_UNCHARGED_FN_ID,
	LED_BATTERY_CHARGED_ON_FN_ID,
	LED_BATTERY_FULL_ON_FN_ID,
	LED_EOL_FN_ID,
} S_M1_LED_FUNC_ID;

void m1_led_indicator_off(uint8_t *params);
void m1_led_indicator_on(uint8_t *params);
void m1_led_fast_blink_ex(uint8_t *params);
void m1_led_fast_blink(uint8_t r_g_b, uint8_t pwm_rgb, uint8_t on_off_ms);
void m1_led_fw_update_on(uint8_t *params);
void m1_led_fw_update_off(void);
void m1_led_batt_charged_on(uint8_t *params);
void m1_led_batt_full_on(uint8_t *params);
void m1_led_set_blink_timer(uint8_t r_g_b, uint16_t on_off_ms, uint8_t mode);
uint8_t m1_led_get_running_id(void);
void m1_led_insert_function_id(uint8_t func_id, uint8_t *params);
void m1_led_func_recovery(void);

#endif /* M1_LED_INDICATOR_H_ */
