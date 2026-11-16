/* See COPYING.txt for license details. */

/*
*
* m1_system.c
*
* System functions for M1
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "stm32h5xx_hal.h"
#include "app_freertos.h"
#include "m1_tasks.h"
#include "m1_power_ctl.h"
#include "m1_fw_update_bl.h"
#include "m1_fw_update.h"		/* firmware_update_enter_usb_dfu() (issue #41-B) */
#include "m1_boot_failsafe.h"
#include "m1_dfu_entry.h"		/* strap/SP/watchdog/request/recovery decisions */
#include "m1_lp5814.h"
#include "m1_bq25896.h"
#include "battery.h"
#include "m1_feedback_manager.h"
#include "m1_feedback_orchestration.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"System"

#define BUTTON_EVENT_ACTIVE		0x01	// A button active event occurs
#define BUTTON_EVENT_RESTORED	0x02	// A button active event restores to idle (released) state

#define SYSTEM_PERIODIC_TASK_DELAY	BUTTON_DEBOUNCE_MS/2 //ms - Period for the task to read buttons' press-release status

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

S_Buttons_Control buttons_ctl[NUM_BUTTONS_MAX];
S_GPIO_IO_t m1_buttons_io[NUM_BUTTONS_MAX] = 	{	{.gpio_port = GPIOC, .gpio_pin = GPIO_PIN_13},
													{.gpio_port = GPIOE, .gpio_pin = GPIO_PIN_11},
													{.gpio_port = GPIOE, .gpio_pin = GPIO_PIN_12},
													{.gpio_port = GPIOE, .gpio_pin = GPIO_PIN_13},
													{.gpio_port = GPIOE, .gpio_pin = GPIO_PIN_14},
													{.gpio_port = GPIOE, .gpio_pin = GPIO_PIN_10}
												};
S_M1_Buttons_Status m1_buttons_status = {	.event= {BUTTON_EVENT_IDLE, BUTTON_EVENT_IDLE, BUTTON_EVENT_IDLE, BUTTON_EVENT_IDLE, BUTTON_EVENT_IDLE, BUTTON_EVENT_IDLE},
											.timestamp = 0x00
										};


S_M1_Device_Status_t 	m1_device_stat = {0};
QueueHandle_t 			button_events_q_hdl = NULL;
TaskHandle_t			system_task_hdl;
TaskHandle_t 			idle_task_hdl;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void system_periodic_task(void *param);
void idle_handler_task(void *param);
static void send_button_evt_to_queue(void);
uint8_t m1_button_event_check(uint8_t button_id, uint8_t event);
void m1_buttons_status_reset(void);
uint32_t TIM_GetCounterCLKValue(uint16_t prescaler);
static void battery_indicator_update(void);
static void lcd_saver_update(void);
/* Post-update "Success!" toast duration before the boot artwork appears. */
#define M1_BOOT_SUCCESS_TOAST_MS   1500U

void startup_config_handler(void);
void power_on_button_check(void);
void HAL_Delay(uint32_t Delay);
static void startup_bu_registers_init(void);
static void startup_boot_failsafe(void);
static void startup_iwdg_option_byte_check(void);
static void startup_dfu_indicate(void);
void startup_info_screen_display(const char *scr_text);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/*============================================================================*/
/**
 * @brief This task handles periodic tasks, e.g. keypad handler
 */
/*============================================================================*/
void system_periodic_task(void *param)
{
	(void)param; /* Unused: stub for future work. May need removal later. */
    uint32_t temp, current_tick;
    uint8_t this_button_level, i;
    uint8_t event_change;

	// Create Queue.
    button_events_q_hdl = xQueueCreate(1, sizeof(S_M1_Buttons_Status));
    assert(button_events_q_hdl!=NULL);

	// After power on, let wait here until user releases the OK button if it's the case!
	while ( true )
	{
		if ( HAL_GPIO_ReadPin(BUTTON_OK_GPIO_Port, BUTTON_OK_Pin)==GPIO_PIN_SET ) // Button released?
			break;
		vTaskDelay(100); // Return some time to the system
        m1_wdt_send_report(M1_REPORT_ID_SYSTEM_PERIODIC_TASK, 100);
	} // while ( true )

    while (TRUE)
    {
        event_change = 0x00;
        current_tick = HAL_GetTick();
        for (i=0; i<NUM_BUTTONS_MAX; i++)
        {
        	this_button_level = HAL_GPIO_ReadPin(m1_buttons_io[i].gpio_port, m1_buttons_io[i].gpio_pin);
        	switch ( buttons_ctl[i].status )
        	{
            	case BUTTON_IS_IDLE: // BUTTON_IS_RELEASED
            		if ( this_button_level==buttons_ctl[i].active_level ) // press attempt
            		{
            			buttons_ctl[i].status = BUTTON_PRESS_ATTEMPT;
            			buttons_ctl[i].click_counter = current_tick; // store current timer tick
            		}
            		if ( buttons_ctl[i].dbc_status==BUTTON_DBC_CLK1 )
            		{
            			temp = current_tick; // get current timer tick
            			temp -= buttons_ctl[i].dbc_counter; // calculate the time past since the first click
            			if ( temp > BUTTON_DBC_MIDDLE ) // timeout for second click, reset
            			{
            				buttons_ctl[i].dbc_status = BUTTON_DBC_IDLE;
            				buttons_ctl[i].event = BUTTON_EVENT_IDLE; // reset if not reset by caller function
            				event_change = BUTTON_EVENT_RESTORED;
            			}
            		} // if ( buttons_ctl[i].dbc_status==BUTTON_DBC_CLK1 )
            		break;

            	case BUTTON_PRESS_ATTEMPT:
            		temp = current_tick; // get current timer tick
            		temp -= buttons_ctl[i].click_counter; // calculate the time past for de-bounce
            		if ( temp >= BUTTON_DEBOUNCE_MS )
            		{
            			if ( this_button_level==buttons_ctl[i].active_level ) // still pressed
            			{
            				buttons_ctl[i].status = BUTTON_IS_PRESSED;
            				buttons_ctl[i].event = BUTTON_EVENT_CLICK;
            				buttons_ctl[i].click_counter = current_tick; // store current timer tick
            				switch ( buttons_ctl[i].dbc_status )
            				{
                            	case BUTTON_DBC_IDLE:
                            		buttons_ctl[i].dbc_counter = current_tick; // store current timer tick
                            		break;

                            	case BUTTON_DBC_CLK1:
                            		temp = current_tick; // get current timer tick
                            		temp -= buttons_ctl[i].dbc_counter; // calculate the duration between two clicks
                            		if ( temp <= BUTTON_DBC_MIDDLE )
                            		{
                            			buttons_ctl[i].dbc_status = BUTTON_DBC_CLK2; // valid double click received
                            			buttons_ctl[i].dbc_counter = current_tick; // store current timer tick
                            		}
                            		else
                            		{
                            			buttons_ctl[i].dbc_status = BUTTON_DBC_IDLE; // invalid double click received, reset
                            		}
                            		break;

                            	case BUTTON_DBC_CLK2:
                            		buttons_ctl[i].dbc_status = BUTTON_DBC_IDLE; // new single click received, reset
                            		break;

                            	default:
                            		break;
            				} // switch ( buttons_ctl[i].dbc_status )
            				event_change = BUTTON_EVENT_ACTIVE;
            			} // if ( this_button_level==buttons_ctl[i].active_level )
            			else // not a valid press (logic 0 read)
            			{
            				buttons_ctl[i].status = BUTTON_IS_IDLE;
            			}
            		} // if ( temp >= BUTTON_DEBOUNCE_MS )
            		break;

            	case BUTTON_IS_PRESSED:
            		if ( this_button_level!=buttons_ctl[i].active_level ) // release attempt
            		{
            			buttons_ctl[i].status = BUTTON_RELEASE_ATTEMPT;
            			buttons_ctl[i].click_counter = current_tick; // store current timer tick
            		}
            		else
            		{
            			temp = current_tick; // get current timer tick
            			temp -= buttons_ctl[i].click_counter;
            			if ( temp >= BUTTON_LONG_PRESS )
            			{
            				buttons_ctl[i].status = BUTTON_IS_LPRESSED;
            				buttons_ctl[i].click_counter = current_tick; // store current timer tick
            				buttons_ctl[i].event = BUTTON_EVENT_LCLICK;
            				event_change = BUTTON_EVENT_ACTIVE;
            			}
            		} // else
            		break;

            	case BUTTON_IS_LPRESSED:
            		if ( this_button_level!=buttons_ctl[i].active_level ) // release attempt
            		{
            			buttons_ctl[i].status = BUTTON_RELEASE_ATTEMPT;
            			buttons_ctl[i].click_counter = current_tick; // store current timer tick
            		}
#ifdef BUTTON_REPEATED_PRESS_ENABLE
            		else
            		{
            			temp = current_tick; // get current timer tick
            			temp -= buttons_ctl[i].click_counter;
            			if ( temp >= BUTTON_REPEATED_PRESS )
            			{
            				buttons_ctl[i].status = BUTTON_IS_LPRESSED;
            				buttons_ctl[i].click_counter = current_tick; // store current timer tick
            				buttons_ctl[i].event = BUTTON_EVENT_CLICK;
            				event_change = BUTTON_EVENT_ACTIVE;
            			} // if ( temp >= BUTTON_REPEATED_PRESS )
            		} // else
#endif // #ifdef BUTTON_REPEATED_PRESS_ENABLE
            		break;

            	case BUTTON_RELEASE_ATTEMPT:
            		temp = current_tick; // get current timer tick
            		temp -= buttons_ctl[i].click_counter; // calculate the time past for debounce
            		if ( temp >= BUTTON_DEBOUNCE_MS )
            		{
            			if ( this_button_level!=buttons_ctl[i].active_level ) // still released
            			{
            				buttons_ctl[i].status = BUTTON_IS_RELEASED;
            				buttons_ctl[i].click_counter = current_tick; // store current timer tick

            				switch ( buttons_ctl[i].dbc_status )
            				{
                            	case BUTTON_DBC_IDLE:
                            		temp = current_tick; // get current timer tick
                            		temp -= buttons_ctl[i].dbc_counter; // calculate the duration of the first click
                            		if ( temp <= BUTTON_DBC_CLICK )
                            		{
                            			buttons_ctl[i].dbc_status = BUTTON_DBC_CLK1;
                            			buttons_ctl[i].dbc_counter = current_tick; // store current timer tick
                            		}
                            		else // Timeout for a potential first click of a double click
                            		{
                            			buttons_ctl[i].event = BUTTON_EVENT_IDLE; // reset if not reset by caller function
                            			event_change = BUTTON_EVENT_RESTORED;
                            		}
                            		break;

                            	case BUTTON_DBC_CLK1: // this case does not exist, do nothing

                            	case BUTTON_DBC_CLK2:
                            		buttons_ctl[i].dbc_status = BUTTON_DBC_IDLE; // double click released, reset
                            		buttons_ctl[i].event = BUTTON_EVENT_IDLE; // reset if not reset by caller function
                            		event_change = BUTTON_EVENT_RESTORED;
                            		break;

                            	default:
                            		break;
            				} // switch ( buttons_ctl[i].dbc_status )
            			} // if ( this_button_level!=buttons_ctl[i].active_level )
            			else // not a valid release (logic 1 read)
            			{
            				if ( buttons_ctl[i].event==BUTTON_EVENT_CLICK )
            					buttons_ctl[i].status = BUTTON_IS_PRESSED;
            				else
            					buttons_ctl[i].status = BUTTON_IS_LPRESSED;
            			}
            		} // if ( temp >= BUTTON_DEBOUNCE_MS )
            		break;

            	default:
            		break;
        	} // switch ( buttons_ctl[i].status )
        } // for (i=0; i<NUM_BUTTONS_MAX; i++)

        if ( event_change )
        {
        	for (i=0; i<NUM_BUTTONS_MAX; i++)
        	{
        		m1_buttons_status.event[i] = buttons_ctl[i].event; // Update latest status
        		buttons_ctl[i].event = BUTTON_EVENT_IDLE; // Reset
        	}
    		m1_device_stat.active_timestamp = current_tick; // Update latest time stamp
        } // if ( event_change )

        if ( m1_device_stat.op_mode != M1_OPERATION_MODE_FIRMWARE_UPDATE )
        {
        	if ( event_change & BUTTON_EVENT_ACTIVE ) // Notify task only when there's an active event
        	{
    			// Update to queue to notify any listener.
    			//xQueueOverwrite(button_events_q_hdl, &m1_buttons_status);
    			xQueueSend(button_events_q_hdl, &m1_buttons_status, 0);
    			//UBaseType_t uxQueueMessagesWaiting( const QueueHandle_t xQueue )
    			// Send notification to button event handling task
    			send_button_evt_to_queue();
        	} // if ( event_change & BUTTON_EVENT_ACTIVE )
        	battery_indicator_update();
        	lcd_saver_update();
        } // if ( m1_device_stat.op_mode != M1_OPERATION_MODE_FIRMWARE_UPDATE )

        vTaskDelay(pdMS_TO_TICKS(SYSTEM_PERIODIC_TASK_DELAY));
        m1_wdt_send_report(M1_REPORT_ID_SYSTEM_PERIODIC_TASK, SYSTEM_PERIODIC_TASK_DELAY);
    } // while (TRUE)

} // void system_periodic_task(void *param)



/*============================================================================*/
/*
 * This function sends notification to an active task for a button event
 */
/*============================================================================*/
static void send_button_evt_to_queue(void)
{
	BaseType_t ret;
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;

	// Check if the queue has any button event
	ret = uxQueueMessagesWaiting(button_events_q_hdl);
	if ( ret ) // Is there a button event?
	{
		switch(m1_device_stat.op_mode)
		{
			case M1_OPERATION_MODE_POWER_UP:
			case M1_OPERATION_MODE_DISPLAY_ON:
			case M1_OPERATION_MODE_MENU_ON:
				//xTaskNotify(menu_main_handler_task_hdl, 0x00, eNoAction);
				//break;
			case M1_OPERATION_MODE_SUB_FUNC_RUNNING:
				// Instead of sending notification to the running task (sub-function),
				// let send a keypad event to the queue that will be read later
				// by the running sub-function
				//xTaskNotify(subfunc_handler_task_hdl, 0x00, eNoAction);
				q_item.q_evt_type = Q_EVENT_KEYPAD;
				q_item.q_data.keypad_evt = 1; // any value,  not used
				xQueueSend(main_q_hdl, &q_item, portMAX_DELAY);
				break;

			default:
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0); // Remove this button event
				break;
		} // switch(m1_device_stat.op_mode)
	} // if ( ret )
} // static void send_button_evt_to_queue(void)



/*============================================================================*/
/*
 * This task handles idle tasks
*/
/*============================================================================*/
void idle_handler_task(void *param)
{
	(void)param; /* Unused: stub for future work. May need removal later. */
	uint32_t task_val;
	while(1)
	{
		xTaskNotifyWait(0, 0, &task_val, portMAX_DELAY);
	} // while(1)
} // void idle_handler_task(void *param)




/*============================================================================*/
/*
 * This function checks if a button is being pressed or not
*/
/*============================================================================*/
uint8_t m1_button_event_check(uint8_t button_id, uint8_t event)
{
	if ( button_id >= NUM_BUTTONS_MAX )
		return FALSE;

	if ( m1_buttons_status.event[button_id]==event )
		return TRUE;

    return FALSE;
} // uint8_t m1_button_event_check(uint8_t button_id, uint8_t event)



/*============================================================================*/
/*
 * This function resets all buttons status to default state
*/
/*============================================================================*/
void m1_buttons_status_reset(void)
{
	uint8_t i;

	for (i=0; i<NUM_BUTTONS_MAX; i++)
		m1_buttons_status.event[i] = BUTTON_EVENT_IDLE;
} // void m1_buttons_status_reset(void)



/*============================================================================*/
/**
  * @brief  Identify TIM clock
  * @param  None
  * @retval Timer clock
  */
/*============================================================================*/
uint32_t TIM_GetCounterCLKValue(uint16_t prescaler)
{
	uint32_t apbprescaler = 0, apbfrequency = 0;

	/* Get the clock prescaler of APB1 */
	apbprescaler = ((RCC->CFGR2 >> 4) & 0x7);
	apbfrequency = HAL_RCC_GetPCLK1Freq();

	/* If APBx clock div >= 4 */
	if (apbprescaler >= 4)
	{
		return ((apbfrequency * 2) / (prescaler + 1));
	}
	else
	{
		return (apbfrequency / (prescaler + 1));
	}
} // uint32_t TIM_GetCounterCLKValue(uint16_t prescaler)



/*============================================================================*/
/**
 * @brief Update LED indicator based on battery charge status
 */
/*============================================================================*/
/* bq_getCHRG_STAT() (Battery/battery.c: battery_status_update() ->
 * bq_getCHRG_STAT(), m1_bq25896.c) is a single, un-filtered I2C register
 * read of the charger IC's own state machine, refreshed every 2s
 * (TASKDELAY_BATTERY_INFO_TIMER). Near a charge-current/voltage boundary
 * (marginal USB input current, battery approaching full) that raw read
 * genuinely bounces between adjacent states from one 2s refresh to the
 * next -- a documented characteristic of this class of charger IC, not a
 * transient fluke. Reacting to every single reading immediately makes that
 * bounce directly visible as the LED flashing/changing colour while
 * connected and charging.
 *
 * PERMANENT FIX, do not revert: require the SAME reading on two
 * consecutive 2s refreshes (~2-4s apart) before actually changing the LED.
 * A single noisy sample can never satisfy that -- it gets superseded by
 * the next sample before the threshold is met -- but a genuine sustained
 * transition (plug in, unplug, reach full) still shows up within a few
 * seconds. This was reverted once before (2026-08-22) as "an unproven
 * experiment made during unrelated RFID investigation work" on the
 * assumption the immediate-reaction version was already hardware-
 * validated flicker-free; that assumption was wrong -- the immediate-
 * reaction version was hardware-confirmed to flash again on 2026-08-25.
 * Do not remove BATTERY_STAT_CONFIRM_SAMPLES or react to new_stat before
 * it is confirmed; that reintroduces the exact flashing this fixes. */
#define BATTERY_STAT_CONFIRM_SAMPLES  2

static void battery_indicator_update(void)
{
	S_M1_Power_Status_t SystemPowerStatus;
	uint8_t new_stat, want;
	int pg;
	static uint8_t pending_stat = 0xFF;
	static uint8_t pending_confirm = 0;
	static uint8_t stat_stable = 0xFF;   /* last DEBOUNCE-confirmed CHRG_STAT */
	static uint8_t full_latched = 0;     /* green latched once "full" seen this power session */
	static uint8_t last_led = 0xFF;      /* last APPLIED charge-LED state (see `want`) */
	static uint16_t batt_info_timer_count = 0;

	batt_info_timer_count += SYSTEM_PERIODIC_TASK_DELAY;
	if ( batt_info_timer_count < TASKDELAY_BATTERY_INFO_TIMER )
		return; // only re-evaluate charge state once per fresh hardware sample

	batt_info_timer_count = 0;
	battery_status_update();
	battery_power_status_get(&SystemPowerStatus);

	if ( SystemPowerStatus.fault != 0 )
		return; // charger fault: leave the LED as-is (same as the prior code, which acted only when fault==0)

	/* Debounce CHRG_STAT: require BATTERY_STAT_CONFIRM_SAMPLES identical
	 * consecutive reads before trusting a new value. A single noisy sample can
	 * never satisfy this -- it is superseded by the next sample before the
	 * threshold is met (PERMANENT FIX, do not revert -- see the block comment
	 * above). */
	new_stat = SystemPowerStatus.stat;
	if ( new_stat != pending_stat )
	{
		pending_stat = new_stat;
		pending_confirm = 1;
	}
	else if ( pending_confirm < BATTERY_STAT_CONFIRM_SAMPLES )
	{
		pending_confirm++;
	}
	if ( pending_confirm >= BATTERY_STAT_CONFIRM_SAMPLES )
		stat_stable = new_stat; // trusted charge state

	/* Solid, no-flashing charge indicator.
	 *
	 * The charger IC's CHRG_STAT genuinely bounces between adjacent states near
	 * a charge boundary (e.g. Fast-Charge <-> Complete while topping off), which
	 * made the LED visibly alternate red<->green / blink while on power. Anchor
	 * the ON/OFF decision on PG_STAT (power-good: a clean, stable "external power
	 * present" signal, NOT the bouncy CHRG_STAT), and LATCH "full/green" for the
	 * duration of a power session so a bounce back to "charging" can never flip
	 * the colour. Result: solid red while charging, solid green once full, and
	 * off only when external power is actually removed -- the LED changes at most
	 * twice per power session (off->red, red->green) and never flashes. The
	 * charge state is still derived from the debounced CHRG_STAT above. */
	pg = bq_getPG_STAT(); // 1 = good external power present
	if ( !pg )
	{
		want = 0;         // not on external power -> off
		full_latched = 0; // reset the latch for the next power session
	}
	else
	{
		if ( stat_stable == 3 ) // charge complete
			full_latched = 1;
		want = full_latched ? 2 : 1; // green once full (latched), else red while charging
	}

	if ( want != last_led ) // apply ONLY on a real change -> no repeated writes, no flicker
	{
		/*
		 * Migrated to the M1-FB-STD-001 v1.1 feedback manager
		 * (documentation/M1_FEEDBACK_RECONCILIATION.md): charging LED
		 * feedback is now a manager-owned FB_OWNER_POWER hold (solid
		 * red/green, nine-state mapping states 7/8) instead of the old
		 * m1_led_indicator "running function id" state machine. The
		 * manager's hold registration is itself idempotent (a repeated
		 * request from the same owner replaces its own hold in place), so
		 * this no longer needs to inspect what LED function was previously
		 * running -- the debounce/latch logic above already guarantees
		 * `want` only changes on a real, confirmed transition.
		 * fb_pwr_charger_attached() also runs the reference section 10.2
		 * cleanup (releases any critical-battery loop/backlight/buzzer
		 * hold) before activating the new charging/charged state, in case
		 * this transition follows a critical-battery episode.
		 */
		switch ( want )
		{
			case 0: // off (power removed)
				fb_pwr_not_charging();
				break;

			case 1: // solid RED -- charging
				fb_pwr_charger_attached(false);
				break;

			case 2: // solid GREEN -- full
				fb_pwr_charger_attached(true);
				break;

			default:
				break;
		}
		last_led = want;
	}

} // static void battery_indicator_update(void)



/*============================================================================*/
/**
 * @brief Update LCD backlight operating status
 */
/*============================================================================*/
static void lcd_saver_update(void)
{
	static uint8_t saver_mode = 0;
	uint32_t delta;

	/*
	 * Migrated to the feedback manager (M1-FB-STD-001 v1.1, section 5.3):
	 * the sleep timer now registers/updates its own lowest-priority
	 * FB_OWNER_SLEEP_TIMER backlight hold instead of calling
	 * lp5814_backlight_on() directly, so any higher-priority backlight
	 * holder (user brightness wake for an alert, critical-battery wake, an
	 * Emulate hold, a settings-brightness preview, ...) transparently takes
	 * over and the sleep timer's own state resumes automatically via the
	 * manager's logical restore once that holder releases. See
	 * documentation/M1_FEEDBACK_RECONCILIATION.md.
	 */
	delta = HAL_GetTick() - m1_device_stat.active_timestamp;
	if ( saver_mode )
	{
		if ( delta < LCD_SAVER_PERIOD ) // Keypad is active?
		{
			fb_sleep_timer_wake();
			saver_mode = 0;
		}
	} // if ( saver_mode )
	else
	{
		if ( delta >= LCD_SAVER_PERIOD ) // Keypad has been inactive?
		{
			fb_sleep_timer_sleep();
			saver_mode = 1;
		}
	} // else

} // static void lcd_saver_update(void)


/*============================================================================*/
/**
 * @brief Wake the LCD backlight now and restart the inactivity timer.
 *
 * Used when a view representing fresh, user-relevant output is shown (e.g. the
 * NFC/RFID successful-read info screen). Mirrors startup_info_screen_display():
 * wake the manager-owned backlight immediately and reseat active_timestamp so
 * the normal LCD_SAVER_PERIOD window starts from this moment. Scanning is not
 * affected and continues to time out normally.
 */
/*============================================================================*/
void m1_lcd_wake_restart_timer(void)
{
	fb_sleep_timer_wake();
	m1_device_stat.active_timestamp = HAL_GetTick();
} // void m1_lcd_wake_restart_timer(void)


/*============================================================================*/
/*
 * This function initializes default values for the system after power on.
 */
/*============================================================================*/
void startup_device_init(void)
{
    uint8_t i, k, stat;
    uint32_t bu_reg_read, *bu_reg_write;

    for (i=0; i<NUM_BUTTONS_MAX; i++)
    {
    	buttons_ctl[i].status = BUTTON_IS_IDLE;
    	buttons_ctl[i].dbc_status = BUTTON_DBC_IDLE;
    	buttons_ctl[i].event = BUTTON_EVENT_IDLE;
    	buttons_ctl[i].active_level = BUTTON_PRESS_STATE;
    } // for (i=0; i<NUM_BUTTONS_MAX; i++)

    m1_device_stat.op_mode = M1_OPERATION_MODE_POWER_UP;
    m1_device_stat.active_timestamp = HAL_GetTick();
    m1_device_stat.sub_func = NULL;

    // Read configuration data from device flash
    memcpy((uint8_t *)&m1_device_stat.config, (const uint8_t *)(uintptr_t)FW_CONFiG_ADDRESS, sizeof(S_M1_FW_CONFIG_t));

	//SBF: System standby flag
	//This bit is set by hardware and cleared only by a POR or by setting the CSSF bit.
	//0: system has not been in Standby mode.
	//1: system has been in Standby mode.
	stat = __HAL_PWR_GET_FLAG(PWR_FLAG_SBF);
	__HAL_PWR_CLEAR_FLAG(PWR_FLAG_SBF); // Clear stand-by flag
	__HAL_PWR_CLEAR_FLAG(PWR_WAKEUP_FLAG4); // Clear Wake-up flag
	if ( !stat ) // Not reset by Stand-by mode, reset by all other reasons
	{
		//HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN4_LOW); // Enable WKUP4 pin, falling edge
		//HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
		//HAL_PWR_EnterSTANDBYMode(); // Enter Standby mode
		;
	} // if ( !stat )

	k = (sizeof(S_M1_BK_REGS_t) + 3)/4;
	bu_reg_read = RTC_BKP_DR0; // Backup address, address offset: 0x100 + 0x04 * x, (x = 0 to 31)
	bu_reg_write = (uint32_t *)&m1_device_stat.bu_regs;
	HAL_PWR_EnableBkUpAccess();
	for (i=0; i<k; i++)
	{
		*bu_reg_write = HAL_RTCEx_BKUPRead(&hrtc, bu_reg_read);
		bu_reg_read++;
		bu_reg_write++;
	} // for (i=0; i<k; i++)
//	HAL_PWR_DisableBkUpAccess();

    // Disable RTC
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
} // void startup_device_init(void)



/*============================================================================*/
/*
 * Boot-time READ-ONLY IWDG option-byte check (issue #41-A)
 *
 * The legacy fork rewrote the IWDG option bytes on EVERY boot
 * (HAL_FLASHEx_OBProgram + HAL_FLASH_OB_Launch, i.e. option-byte programming
 * and its flash-unlock window on every power-up). That rewrite is deliberately
 * NOT ported: option bytes are only written by the update/rollback path
 * (m1_fw_update_bl.c bl_swap_banks) and by the offline power-down path
 * (m1_power_ctl.c m1_pre_power_down). What the recovery story needs is the
 * FACT that the watchdog is software-started by m1_wdt_init() rather than
 * armed by hardware before the application can service it, and observing that
 * is enough.
 *
 * READ-ONLY by contract: no HAL_FLASH_Unlock, no HAL_FLASHEx_OBProgram, no
 * HAL_FLASH_OB_Launch anywhere in this function.
 */
/*============================================================================*/

/* The helper's mask is the OPTSR value mask, which HAL names OB_IWDG_SW.
 * OB_USER_IWDG_SW (0x4) is the USERType selector and bit 2 of OPTSR is
 * BORH_EN: using it here logged brownout-high-enable as the watchdog mode on
 * every boot (audit finding D1). Fail the build if the constants ever diverge. */
_Static_assert(M1_BOOT_OPTSR_IWDG_SW == (uint32_t)OB_IWDG_SW,
               "M1_BOOT_OPTSR_IWDG_SW must equal the HAL's OB_IWDG_SW value mask");

static void startup_iwdg_option_byte_check(void)
{
	FLASH_OBProgramInitTypeDef OBInit = {0};
	bool hw_watchdog;

	HAL_FLASHEx_OBGetConfig(&OBInit);

	hw_watchdog = m1_boot_iwdg_is_hardware(OBInit.USERConfig);
	M1_LOG_I(M1_LOGDB_TAG, "IWDG option byte: %s (%s), read-only.\r\n",
	         hw_watchdog ? "hardware" : "software",
	         hw_watchdog ? "HARDWARE watchdog may reset before init" : "expected");
} // static void startup_iwdg_option_byte_check(void)



/*============================================================================*/
/*
 * Recovery offer screen (issue #41-B): the FW CORRUPT! diagnosis AND the DFU
 * offer are drawn in the SAME u8g2 page. Drawing the offer through
 * startup_info_screen_display() would clear the display buffer
 * (u8g2_FirstPage()) and wipe the banner out for the whole 5 s offer -- exactly
 * while the technician is deciding what to do.
 */
/*============================================================================*/
static void startup_recovery_offer_display(void)
{
	char offer_txt[24];
	uint8_t w;

	/* The visible bound is derived from the policy constant, so it can never
	 * disagree with the decision helper. */
	snprintf(offer_txt, sizeof(offer_txt), "OK=USB DFU %us",
	         (unsigned)(M1_DFU_RECOVERY_OFFER_WINDOW_MS / 1000U));

	u8g2_SetPowerSave(&m1_u8g2, false);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_B);

	w = (uint8_t)u8g2_GetStrWidth(&m1_u8g2, "FW CORRUPT!");
	u8g2_DrawStr(&m1_u8g2, (uint8_t)((M1_LCD_DISPLAY_WIDTH - w) / 2U), 26, "FW CORRUPT!");

	w = (uint8_t)u8g2_GetStrWidth(&m1_u8g2, offer_txt);
	u8g2_DrawStr(&m1_u8g2, (uint8_t)((M1_LCD_DISPLAY_WIDTH - w) / 2U), 46, offer_txt);

	m1_u8g2_nextpage(); // Update display RAM

	fb_sleep_timer_wake(); // keep normal awake brightness manager-owned

	m1_device_stat.op_mode = M1_OPERATION_MODE_DISPLAY_ON;
	m1_device_stat.active_timestamp = HAL_GetTick();
} // static void startup_recovery_offer_display(void)



/*============================================================================*/
/*
 * Recovery seam for a failed boot CRC validation -- DFU offer (issue #41-B)
 *
 * Filled by PR-DFU. On a failed validation the device does NOT silently
 * continue into the normal UI as if healthy: the caller has already drawn the
 * FW CORRUPT! banner and recorded the fault class/attempt in DR1, and this
 * hook offers USB DFU entry on top of that.
 *
 * POLICY -- bounded offer, never an unbounded wait:
 *   - The offer is shown for M1_DFU_RECOVERY_OFFER_WINDOW_MS (5 s) only, with
 *     the FW CORRUPT! diagnosis still on screen. The decision each poll is the
 *     pure m1_dfu_recovery_poll() (m1_dfu_entry.h), which cannot return WAIT
 *     once the window has elapsed.
 *   - Acceptance is EDGE-triggered: the key level is sampled when the offer
 *     opens and a press only counts after a released -> pressed transition, so
 *     a held, shorted or stuck OK key cannot select DFU. A deliberate press
 *     (~100 ms, i.e. two consecutive 50 ms polls, so a single noisy sample
 *     cannot trigger it) calls firmware_update_enter_usb_dfu(): the request is
 *     persisted in DR1 and the device resets, and the request is honoured on
 *     the next boot by startup_config_handler().
 *   - On timeout the offer line is cleared back to the FW CORRUPT! banner and
 *     the pre-existing behaviour resumes (the normal UI is reachable), with the
 *     choice logged. So a flat battery or a stuck key can delay the boot by at
 *     most the window plus one poll interval, never hang it -- and a stuck key
 *     cannot cycle failsafe -> DFU -> failsafe either. The choice itself is
 *     recorded in the log (the DFU choice also persists the op-status request);
 *     DR1 has no free byte left -- magic/op-status/boot_attempts/fault/swap --
 *     so no extra persisted state is invented for it.
 *   - PD12/PD13 are user-owned external connector pins (pin 6/7, see
 *     docs/hardware.md), not DFU LEDs: they are driven only on ENTER, on the
 *     way into the reset, so merely offering or timing out never leaves an
 *     external circuit driven high.
 *   - The five-second window exceeds the application watchdog's ~4 s period,
 *     so the poll loop kicks the IWDG (m1_wdt_kick(), documented for exactly
 *     this kind of long bounded operation) instead of starving it.
 *
 * What this does NOT do: automatic DFU entry without user input, or automatic
 * rollback. Those, and the on-device qualification of the whole corrupt-bank
 * path, are the issue #41 hardware gate (milestone "Post-merge stabilization").
 */
/*============================================================================*/
static void startup_boot_recovery_hook(uint8_t boot_attempts)
{
	uint32_t start_ms, elapsed_ms;
	S_M1_DFU_RECOVERY_POLL_t poll;
	S_M1_DFU_RECOVERY_ACTION_t action;
	bool key_down;

	M1_LOG_I(M1_LOGDB_TAG, "Boot failsafe: press OK within %u ms for USB DFU entry "
	         "(boot attempt %u).\r\n",
	         (unsigned)M1_DFU_RECOVERY_OFFER_WINDOW_MS, (unsigned)boot_attempts);

	/* Banner and offer in ONE page: u8g2_FirstPage() clears the buffer, so the
	 * diagnostic line must be drawn together with the offer or it vanishes. */
	startup_recovery_offer_display();

	/* Seed the edge trigger from the level at offer start: a key already down
	 * (held/shorted) must never count as a press for this offer. */
	key_down = (HAL_GPIO_ReadPin(BUTTON_OK_GPIO_Port, BUTTON_OK_Pin)==GPIO_PIN_RESET);
	poll = m1_dfu_recovery_poll_start(key_down);

	start_ms = HAL_GetTick();
	for (;;)
	{
		/* The offer window is longer than the application watchdog period;
		 * keep the counter fed for its duration only. */
		m1_wdt_kick();

		key_down = (HAL_GPIO_ReadPin(BUTTON_OK_GPIO_Port, BUTTON_OK_Pin)==GPIO_PIN_RESET);
		elapsed_ms = (uint32_t)(HAL_GetTick() - start_ms);
		action = m1_dfu_recovery_poll(&poll, key_down, elapsed_ms);

		if ( action==M1_DFU_RECOVERY_ENTER )
		{
			M1_LOG_I(M1_LOGDB_TAG, "Boot failsafe: DFU entry selected after %u ms; "
			         "recording the request and resetting.\r\n", (unsigned)elapsed_ms);
			/* Indication only on ENTER: the pins are driven immediately before
			 * the reset that tears them down again, so nothing user-facing is
			 * asserted while merely offering DFU (or on fall-through). */
			startup_dfu_indicate();
			firmware_update_enter_usb_dfu(); // persists the request, then resets
		} // if ( action==M1_DFU_RECOVERY_ENTER )

		if ( action==M1_DFU_RECOVERY_FALL_THROUGH )
		{
			break;
		}

		vTaskDelay(pdMS_TO_TICKS(50));
	} // for (;;)

	/* Timeout: clear the offer line back to the PR-FS banner so the visible
	 * state is the diagnostic one, and fall through to the pre-existing
	 * behaviour. */
	startup_info_screen_display("FW CORRUPT!");
	M1_LOG_I(M1_LOGDB_TAG, "Boot failsafe: no DFU request within %u ms; "
	         "continuing with the recovery banner.\r\n", (unsigned)elapsed_ms);
} // static void startup_boot_recovery_hook(uint8_t boot_attempts)



/*============================================================================*/
/*
 * DFU-mode indication: PD12/PD13 are user-owned external connector pins
 * (pins 6-7, docs/hardware.md; also the GPIO menu's "Pin PD12"/"Pin PD13" via
 * m1_ext_gpio[]), NOT dedicated LEDs. They are driven high only at the moment
 * of entering DFU, immediately before the reset that releases them, so an
 * attached external circuit is never left driven by a mere offer or a timeout.
 * The pre-HAL hardware-strap path in main.c drives the same two pins with raw
 * registers (HAL does not exist yet at that point).
 */
/*============================================================================*/
static void startup_dfu_indicate(void)
{
	HAL_GPIO_WritePin(GPIOD, PD12_Pin|PD13_Pin, GPIO_PIN_SET);
} // static void startup_dfu_indicate(void)



/*============================================================================*/
/*
 * Boot-time firmware integrity failsafe (issue #41-A)
 *
 * Runs after the update state machine has been reconciled and before the boot
 * artwork, so a device whose RUNNING bank does not match the CRC the build
 * embedded in it does not silently reach the normal UI:
 *   - the failure is counted in boot_attempts (saturating at 255),
 *   - the fault class is recorded in boot_fault_signature,
 *   - the recovery state is entered (screen message + recovery seam above).
 *
 * A validating boot is the evidence that clears the accounting, including the
 * swap budget -- so swap_attempts means "swaps since the last CRC-validated
 * boot" (m1_boot_failsafe.h) and a device that keeps landing on an unvalidated
 * bank cannot be swapped back and forth indefinitely.
 *
 * Deliberately out of scope here: automatic rollback, bl_jump_to_dfu(), and
 * on-device qualification of the corrupt-bank boot path (the #41 hardware
 * gate, milestone "Post-merge stabilization").
 */
/*============================================================================*/
static void startup_boot_failsafe(void)
{
	S_M1_BOOT_FAULT_t fault;
	uint8_t attempts;
	const char *fault_name;

	fault = bl_crc_validate_active_bank();

	if ( fault==M1_BOOT_FAULT_NONE )
	{
		/* Healthy boot: clear any accounting left by previous failures. */
		if ( startup_config_read(BK_REGS_SELECT_BOOT_ATTEMPTS)!=0 )
		{
			startup_config_write(BK_REGS_SELECT_BOOT_ATTEMPTS, 0);
		}
		if ( startup_config_read(BK_REGS_SELECT_BOOT_FAULT_SIG)!=(uint8_t)M1_BOOT_FAULT_NONE )
		{
			startup_config_write(BK_REGS_SELECT_BOOT_FAULT_SIG, (uint8_t)M1_BOOT_FAULT_NONE);
		}
		if ( startup_config_read(BK_REGS_SELECT_SWAP_ATTEMPTS)!=0 )
		{
			startup_config_write(BK_REGS_SELECT_SWAP_ATTEMPTS, 0);
		}
		return;
	} // if ( fault==M1_BOOT_FAULT_NONE )

	attempts = m1_boot_next_counter(startup_config_read(BK_REGS_SELECT_BOOT_ATTEMPTS));
	startup_config_write(BK_REGS_SELECT_BOOT_ATTEMPTS, attempts);
	startup_config_write(BK_REGS_SELECT_BOOT_FAULT_SIG, (uint8_t)fault);

	switch (fault)
	{
		case M1_BOOT_FAULT_MAGIC_MISMATCH:	fault_name = "metadata magic"; break;
		case M1_BOOT_FAULT_HEADER_MISMATCH:	fault_name = "metadata header"; break;
		case M1_BOOT_FAULT_CRC_MISMATCH:	fault_name = "image CRC"; break;
		default:							fault_name = "unknown"; break;
	} // switch (fault)
	M1_LOG_E(M1_LOGDB_TAG, "Boot CRC validation failed: %s (attempt %u).\r\n",
	         fault_name, (unsigned)attempts);

	startup_info_screen_display("FW CORRUPT!");
	startup_boot_recovery_hook(attempts);
} // static void startup_boot_failsafe(void)



/*============================================================================*/
/*
 * This function checks the start-up status and does tasks accordingly
 */
/*============================================================================*/
void startup_config_handler(void)
{
    uint16_t i, k;
    uint32_t *bu_reg_read, crc32_add;
    uint32_t fw_ver_old, fw_ver_new;
	size_t copy_len;
	S_M1_FW_CONFIG_t old_fw_config = {0};
    BaseType_t ret;
    S_M1_Main_Q_t q_item;
    S_M1_Buttons_Status this_button_status;

	if ( m1_device_stat.bu_regs.magic_number != SYS_CONFIG_MAGIC_NUMBER ) // Not initialized yet?
	{
		startup_bu_registers_init();
	} // if ( m1_device_stat.bu_regs.magic_number != SYS_CONFIG_MAGIC_NUMBER )
	else
	{
		if ( m1_dfu_request_is_pending(m1_device_stat.bu_regs.device_op_status,
		                                (uint8_t)DEV_OP_STATUS_USB_DFU_REQUEST) )
		{
			/* Software DFU request (issue #41-B): honoured FIRST -- before the
			 * update state machine and before the boot failsafe -- so an
			 * explicit request is never reinterpreted as an interrupted
			 * update. The request is CONSUMED before the jump (persisted back
			 * to NO_OP through the same pure transition the host test covers)
			 * so it can never re-fire: an honoured request that reset the
			 * device again would be a DFU boot loop with no way back to the
			 * application, and a power loss during the ROM session must not
			 * keep the unit out of its application. */
			startup_config_write(BK_REGS_SELECT_DEV_OP_STAT,
			                     m1_dfu_request_next(m1_device_stat.bu_regs.device_op_status, false,
			                                         (uint8_t)DEV_OP_STATUS_USB_DFU_REQUEST,
			                                         (uint8_t)DEV_OP_STATUS_NO_OP));
			M1_LOG_I(M1_LOGDB_TAG, "USB DFU request honoured; entering ROM bootloader.\r\n");
			startup_dfu_indicate();
			bl_jump_to_dfu(); // non-returning: teardown + 32 s net + ROM jump
		} // if ( m1_dfu_request_is_pending(...) )
		else if ( m1_device_stat.bu_regs.device_op_status==DEV_OP_STATUS_FW_UPDATE_ACTIVE )
		{
			startup_config_write(BK_REGS_SELECT_DEV_OP_STAT, DEV_OP_STATUS_NO_OP);
			startup_info_screen_display("UPDATE FAILED!");
			M1_LOG_I(M1_LOGDB_TAG, "FW update failed. Device got reset unexpectedly!\r\n");
		} // if ( m1_device_stat.bu_regs.device_op_status==DEV_OP_STATUS_FW_UPDATE_ACTIVE )
		else if ( m1_device_stat.bu_regs.device_op_status==DEV_OP_STATUS_FW_UPDATE_COMPLETE )
		{
			startup_bu_registers_init(); // Reinitialize after update
			startup_config_write(BK_REGS_SELECT_DEV_OP_STAT, DEV_OP_STATUS_NO_OP);
			startup_info_screen_display("Success!");
				vTaskDelay(pdMS_TO_TICKS(M1_BOOT_SUCCESS_TOAST_MS));
				startup_info_screen_display("");   /* -> approved boot artwork */
			M1_LOG_I(M1_LOGDB_TAG, "FW update complete!\r\n");
		} // else if ( m1_device_stat.bu_regs.device_op_status==DEV_OP_STATUS_FW_UPDATE_COMPLETE )
		else if ( m1_device_stat.bu_regs.device_op_status==DEV_OP_STATUS_FW_ROLLBACK_COMPLETE )
		{
			uint8_t swap_used = startup_config_read(BK_REGS_SELECT_SWAP_ATTEMPTS);
			startup_bu_registers_init(); // Reinitialize after rollback
			/* Keep the swap budget across the commit: startup_bu_registers_init()
			 * zeroes the whole struct, but a swap just happened and the bank it
			 * selected has not been validated yet. startup_boot_failsafe()
			 * below clears the budget once a boot validates the image. */
			startup_config_write(BK_REGS_SELECT_SWAP_ATTEMPTS, swap_used);
			startup_config_write(BK_REGS_SELECT_DEV_OP_STAT, DEV_OP_STATUS_NO_OP);
			startup_info_screen_display("ROLLBACK COMPLETED!");
			M1_LOG_I(M1_LOGDB_TAG, "FW rollback complete!\r\n");
		} // else if ( m1_device_stat.bu_regs.device_op_status==DEV_OP_STATUS_FW_ROLLBACK_COMPLETE )
		else if ( m1_device_stat.bu_regs.device_op_status==DEV_OP_STATUS_REBOOT )
		{
			m1_led_fw_update_on(NULL);
			if ( !m1_device_stat.dev_reset_by_wdt ) // Device got reset by normal cause?
			{
				vTaskDelay(POWER_UP_SYS_CONFIG_WAIT_TIME); // Wait for stable key press during power up
				do
				{
					// menu_main_handler_task() is giving some time to this function to read the main_q_hdl during power-up.
					ret = xQueueReceive(main_q_hdl, &q_item, 0);
					if ( ret != pdTRUE )
						break;
					if ( q_item.q_evt_type!=Q_EVENT_KEYPAD )
						break;
					if (button_events_q_hdl == NULL)
						break;
					ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
					if ( ret != pdTRUE )
						break;
					if ( (this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK) &&
											(this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK) ) // Rollback request?
					{
			    		// User requests the update rollback.
			    		M1_LOG_I(M1_LOGDB_TAG, "Update rollback requested!\r\n");
			    		// Display rollback confirm message here
			    		bu_reg_read = (uint32_t *)(uintptr_t)(FW_CONFiG_ADDRESS + M1_FLASH_BANK_SIZE); // Always read data from the other bank (volatile dropped: snapshot read)
			    		k = FW_CONFiG_SIZE/4 - 1; // Convert to 32-bit and exclude the last slot for the add-on CRC32
			    		for (i=0; i<k; i++)
			    		{
			    			if ( *bu_reg_read==FW_CONFIG_MAGIC_NUMBER_2 )
			    				break;
			    			bu_reg_read++;
			    		} // for (i=0; i<k; i++)
			    		if ( i < k )
			    		{
			    			M1_LOG_I(M1_LOGDB_TAG, "Valid firmware found for rollback. Checking CRC...");
			    			bu_reg_read++; // Move to CRC32 location which is right after the Magic Number 2
			    			crc32_add = (uint32_t)bu_reg_read; // Get the CRC address and use it as the size of the firmware resided in this bank
			    			crc32_add -= (FW_START_ADDRESS + M1_FLASH_BANK_SIZE); // Exclude the size of bank 1
			    			crc32_add /= 4; // convert size from byte to word (32-bit)
			    			if ( bl_crc_check(crc32_add)==BL_CODE_OK )
			    			{
			    				M1_LOG_N(M1_LOGDB_TAG, "OK\r\n");
			    				i++; // Move to CRC32 location which is right after the Magic Number 2
					    		copy_len = (size_t)i * 4U;
					    		if (copy_len > sizeof(S_M1_FW_CONFIG_t))
					    		{
					    			copy_len = sizeof(S_M1_FW_CONFIG_t);
					    		}
					    		memcpy((uint8_t *)&old_fw_config, (const uint8_t *)(uintptr_t)(FW_CONFiG_ADDRESS + M1_FLASH_BANK_SIZE), copy_len);
			    				fw_ver_new = *(uint32_t *)&m1_device_stat.config.fw_version_rc;
			    				fw_ver_old = *(uint32_t *)&old_fw_config.fw_version_rc;
			    				if ( fw_ver_old < fw_ver_new ) // Existing FW in bank 2 is older than current FW?
			    				{
			    					/* Loop guard (issue #41-A): at most
			    					 * M1_BOOT_SWAP_ATTEMPTS_MAX swaps between two
			    					 * CRC-validated boots. The counter is written
			    					 * BEFORE the swap because a swap that dies inside
			    					 * HAL_FLASH_OB_Launch() still resets the MCU and
			    					 * must not be forgotten. */
			    					if ( m1_boot_swap_allowed(startup_config_read(BK_REGS_SELECT_SWAP_ATTEMPTS)) )
			    					{
			    						M1_LOG_I(M1_LOGDB_TAG, "Rollback is ready!\r\n");
			    						startup_config_write(BK_REGS_SELECT_SWAP_ATTEMPTS,
			    							m1_boot_next_counter(startup_config_read(BK_REGS_SELECT_SWAP_ATTEMPTS)));
			    						startup_info_screen_display("FW ROLLBACK...");
			    						startup_config_write(BK_REGS_SELECT_DEV_OP_STAT, DEV_OP_STATUS_FW_ROLLBACK_COMPLETE);
			    						vTaskDelay(pdMS_TO_TICKS(2000));
			    						bl_swap_banks();
			    					} // if ( m1_boot_swap_allowed(...) )
			    					else
			    					{
			    						/* No swap happened, so op_status stays
			    						 * DEV_OP_STATUS_REBOOT (the normal
			    						 * post-update state, cleared by the regular
			    						 * power-down path) -- never report a
			    						 * completed rollback. */
			    						startup_info_screen_display("ROLLBACK LIMITED!");
			    						M1_LOG_I(M1_LOGDB_TAG, "Rollback refused: swap budget exhausted (%u of %u), "
			    						         "no CRC-validated boot since the last swap.\r\n",
			    						         (unsigned)startup_config_read(BK_REGS_SELECT_SWAP_ATTEMPTS),
			    						         (unsigned)M1_BOOT_SWAP_ATTEMPTS_MAX);
			    					} // else
			    				} // if ( fw_ver_old < fw_ver_new )
			    				else // Existing FW in bank 2 is newer than current FW. Rollback is not allowed!
			    				{
			    					M1_LOG_I(M1_LOGDB_TAG, "Rollback was already completed!\r\n");
			    				}
			    			} // if ( bl_crc_check(crc32_add)==BL_CODE_OK )
			    			else
			    			{
			    				M1_LOG_N(M1_LOGDB_TAG, "Failed\r\n");
			    			} // else
			    		} // if ( i < k )
			    		else
			    		{
			    			startup_info_screen_display("ROLLBACK FAILED!");
			    			M1_LOG_I(M1_LOGDB_TAG, "No valid firmware found for rollback!\r\n");
			    		} // else
			    	} // if ( (this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK) &&
				} while (0);
			} // if ( !m1_device_stat.dev_reset_by_wdt )
			m1_led_fw_update_off();
		} // else if ( m1_device_stat.bu_regs.device_op_status==DEV_OP_STATUS_REBOOT )
	} // else

	/* Issue #41-A: read-only IWDG review, then the boot integrity failsafe.
	 * Both run AFTER the update state machine above (an interrupted update is
	 * reported by its own branch) and BEFORE any boot artwork, so a corrupt
	 * running bank never reaches the normal UI silently. */
	startup_iwdg_option_byte_check();
	startup_boot_failsafe();

	if ( m1_device_stat.op_mode != M1_OPERATION_MODE_DISPLAY_ON )
	{
		m1_gui_welcome_scr();
	}

	m1_device_stat.active_bank = bl_get_active_bank();

	M1_LOG_I(M1_LOGDB_TAG, "Device firmware version %d.%d.%d.%d %s.\r\n", m1_device_stat.config.fw_version_major,
			m1_device_stat.config.fw_version_minor, m1_device_stat.config.fw_version_build, m1_device_stat.config.fw_version_rc,
			FW_BUILD_LABEL);
} // void startup_config_handler(void)



/*============================================================================*/
/*
 * This function writes one byte field of S_M1_BK_REGS_t to its backup register
 *
 * The struct is 8 bytes (DR0/DR1 only). The selector is an OPAQUE id: the switch
 * below maps it to a named field, and the physical register follows from that
 * field's byte offset inside the struct -- bytes 0-3 live in RTC_BKP_DR0, bytes
 * 4-7 in RTC_BKP_DR1. The selector's numeric value is NOT an offset
 * (BK_REGS_SELECT_DEV_OP_STAT is 1 for source compatibility although its field
 * sits at byte 4), so never reimplement this as `(uint8_t *)&bu_regs + selector`:
 * that writes byte 1 of magic_number in DR0 and forces a magic-mismatch re-init,
 * wiping the update state and the boot accounting. DR2/DR3 hold the ESP32
 * version record and are never touched here (issue #41-A).
 *
 * DR1 carries four independent byte fields, so a single-byte update always
 * rewrites the whole 32-bit word taken from the in-RAM shadow -- writing a
 * hand-computed register address+offset (as the old DEV_OP_STAT-only version
 * did with RTC_BKP_DR0+1) silently corrupts the neighbouring fields once an
 * index >= 4 exists.
 */
/*============================================================================*/
void startup_config_write(uint8_t config_byte, uint8_t config_val)
{
    uint8_t *field;
    uint16_t byte_index, word_index;
    uint32_t word;

	field = NULL;
	switch (config_byte)
	{
		case BK_REGS_SELECT_DEV_OP_STAT:
			field = &m1_device_stat.bu_regs.device_op_status;
			break;

		case BK_REGS_SELECT_BOOT_ATTEMPTS:
			field = &m1_device_stat.bu_regs.boot_attempts;
			break;

		case BK_REGS_SELECT_BOOT_FAULT_SIG:
			field = &m1_device_stat.bu_regs.boot_fault_signature;
			break;

		case BK_REGS_SELECT_SWAP_ATTEMPTS:
			field = &m1_device_stat.bu_regs.swap_attempts;
			break;

		default:
			break;
	} // switch (config_byte)

    if ( field==NULL )
    {
    	return;
    }

    *field = config_val;

    /* Byte offset inside the shadow struct -> backup word index. */
    byte_index = (uint16_t)(field - (uint8_t *)&m1_device_stat.bu_regs);
    word_index = (uint16_t)(byte_index / 4U);
    word = *(uint32_t *)((uint8_t *)&m1_device_stat.bu_regs + (byte_index & ~(uint16_t)3U));

    HAL_PWR_EnableBkUpAccess();
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0 + word_index, word);
//  HAL_PWR_DisableBkUpAccess();
} // void startup_config_write(uint8_t config_byte, uint8_t config_val)



/*============================================================================*/
/*
 * This function reads one byte field of S_M1_BK_REGS_t
 *
 * Reads come from the in-RAM shadow, which startup_device_init() loads from
 * DR0/DR1 in a single two-word pass at power-up. Selector/value semantics are
 * exactly those of startup_config_write(); 0 is returned for an unknown
 * selector (no field is ever meaningfully "unknown" here, and both counters
 * treat 0 as "none").
 */
/*============================================================================*/
uint8_t startup_config_read(uint8_t config_byte)
{
	switch (config_byte)
	{
		case BK_REGS_SELECT_DEV_OP_STAT:
			return m1_device_stat.bu_regs.device_op_status;

		case BK_REGS_SELECT_BOOT_ATTEMPTS:
			return m1_device_stat.bu_regs.boot_attempts;

		case BK_REGS_SELECT_BOOT_FAULT_SIG:
			return m1_device_stat.bu_regs.boot_fault_signature;

		case BK_REGS_SELECT_SWAP_ATTEMPTS:
			return m1_device_stat.bu_regs.swap_attempts;

		default:
			break;
	} // switch (config_byte)

	return 0;
} // uint8_t startup_config_read(uint8_t config_byte)



/*============================================================================*/
/*
 * This function initializes the backup registers during power up
 */
/*============================================================================*/
static void startup_bu_registers_init(void)
{
    uint16_t i, k;
    uint32_t *bu_reg_read, bu_reg_write;

	k = sizeof(S_M1_BK_REGS_t);
	memset((uint8_t *)&m1_device_stat.bu_regs, 0x00, k); // Reset reg data
	m1_device_stat.bu_regs.magic_number = SYS_CONFIG_MAGIC_NUMBER; // Assign magic number
	k = (sizeof(S_M1_BK_REGS_t) + 3)/4; // Convert size in byte to 32-bit
	bu_reg_write = RTC_BKP_DR0; // Backup address, address offset: 0x100 + 0x04 * x, (x = 0 to 31)
	bu_reg_read = (uint32_t *)&m1_device_stat.bu_regs;
	HAL_PWR_EnableBkUpAccess();
	for (i=0; i<k; i++)
	{
		HAL_RTCEx_BKUPWrite(&hrtc, bu_reg_write, *bu_reg_read);
		bu_reg_write++;
		bu_reg_read++;
	} // for (i=0; i<k; i++)
	//HAL_PWR_DisableBkUpAccess();
	M1_LOG_I(M1_LOGDB_TAG, "Backup registers initialized!\r\n");
} // static void startup_bu_registers_init(void)



/*============================================================================*/
/*
 * This function displays the M1 welcome screen
*/
/*============================================================================*/
void startup_info_screen_display(const char *scr_text)
{
	uint8_t len, x0;

	u8g2_SetPowerSave(&m1_u8g2, false);

	/* Normal power-on boot splash: the approved MONSTATEK wordmark artwork,
	 * drawn pixel-for-pixel at (0,0) as a fixed 128x64 bitmap (no old 40x32
	 * logo). The firmware-version line below it is NOT part of that bitmap --
	 * it is live text drawn with u8g2_DrawStr from the real
	 * m1_device_stat.config.fw_version_major/minor fields (same fields/
	 * pattern as settings_about_display_choice(), m1_settings.c), so it
	 * tracks the actual running firmware version rather than being baked
	 * into the artwork. A non-empty scr_text is an update/rollback STATUS
	 * screen, drawn below as its message text ONLY -- the old boot look
	 * appears nowhere. */
	if ((scr_text == NULL) || (scr_text[0] == '\0'))
	{
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_FirstPage(&m1_u8g2);
		u8g2_DrawXBMP(&m1_u8g2, 0, 0, 128, 64, m1_boot_logo_128x64);
		{
			char ver_str[24];
			uint8_t vw;
			snprintf(ver_str, sizeof(ver_str), "FW VERSION %u.%u",
			         (unsigned)m1_device_stat.config.fw_version_major,
			         (unsigned)m1_device_stat.config.fw_version_minor);
			u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
			vw = (uint8_t)u8g2_GetStrWidth(&m1_u8g2, ver_str);
			u8g2_DrawStr(&m1_u8g2, (uint8_t)((M1_LCD_DISPLAY_WIDTH - vw) / 2), 54, ver_str);
		}
		m1_u8g2_nextpage();
		fb_sleep_timer_wake();
		m1_device_stat.op_mode = M1_OPERATION_MODE_DISPLAY_ON;
		m1_device_stat.active_timestamp = HAL_GetTick();
		return;
	}

	/* Status screen (update/rollback): centered message text only -- no logo,
	 * no "MONSTATEK M1", no version. */
	len = strlen(scr_text);
	x0 = (M1_LCD_DISPLAY_WIDTH - len*M1_GUI_FONT_WIDTH)/2;
	if ( x0 >= M1_GUI_FONT_WIDTH )
		x0 -= M1_GUI_FONT_WIDTH;

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, x0, 36, scr_text);   // vertically centered
	m1_u8g2_nextpage(); // Update display RAM

	fb_sleep_timer_wake(); // keep normal awake brightness manager-owned

	m1_device_stat.op_mode = M1_OPERATION_MODE_DISPLAY_ON; // update new state
	m1_device_stat.active_timestamp = HAL_GetTick(); // reset timeout
} // void startup_info_screen_display(const char *scr_text)




/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void power_on_button_check(void)
{
	uint32_t reset_source;
	uint16_t tick_acc;
	int vbus_gd;

	vTaskDelay(50);

	bq_startADC();
	bq_oneShotADC();

	vbus_gd = bq_getVBUS_GD();

	bq_stopADC();

	reset_source = HAL_RCC_GetResetSource();
	reset_source &= (RCC_RESET_FLAG_SW + RCC_RESET_FLAG_IWDG + RCC_RESET_FLAG_WWDG);
	if ( reset_source ) // Not power on by normal cause?
		return;

	if ( vbus_gd==0 ) // No Vbus present?
	{
		if ( HAL_GPIO_ReadPin(BUTTON_OK_GPIO_Port, BUTTON_OK_Pin)==GPIO_PIN_SET )
		{
			m1_power_down();
		}
	}

	// Device turns on by normal cause
	if ( m1_device_stat.bu_regs.device_op_status==DEV_OP_STATUS_NO_OP )
	{
		tick_acc = 0;
		while ( tick_acc < POWER_ON_LONG_PRESS )
		{
			vTaskDelay(100);
			if ( HAL_GPIO_ReadPin(BUTTON_OK_GPIO_Port, BUTTON_OK_Pin)==GPIO_PIN_SET ) // Button released?
				break;
			tick_acc += 100;
			m1_wdt_reset(); // Reset watchdog
		} // while ( tick_acc < POWER_ON_LONG_PRESS )
		if ( tick_acc < POWER_ON_LONG_PRESS ) // Button released too soon?
		{
			if ( vbus_gd==0 ) // No Vbus present?
			{
				m1_power_down(); // Shut down again
			}
		} // if ( tick_acc < POWER_ON_LONG_PRESS )
	} // if ( m1_device_stat.bu_regs.device_op_status==DEV_OP_STATUS_NO_OP )

} // void power_on_button_check(void)




/*============================================================================*/
/*
 * This function does a non-blocking delay. It replaces the default HAL_Delay() function
 */
/*============================================================================*/
void HAL_Delay(uint32_t Delay)
{
	vTaskDelay(Delay);
} // void HAL_Delay(uint32_t Delay)
