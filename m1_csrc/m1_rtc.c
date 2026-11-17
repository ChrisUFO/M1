/* See COPYING.txt for license details. */

/*
*
*  m1_rtc.c
*
*  M1 RTC calendar access (issue #7 completion)
*
*  HAL glue over the pure date/time domain in m1_rtc_date.c -- see
*  m1_rtc.h for the timekeeping design (LSI source, VBAT semantics,
*  single writer, backup-register ownership).
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include "stm32h5xx_hal.h"
#include "main.h" // hrtc
#include "m1_rtc_date.h"
#include "m1_rtc.h"

/*************************** D E F I N E S ************************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/*============================================================================*/
/**
  * @brief Read the RTC calendar as a validated S_M1_DateTime_t.
  *
  * Shadow-register order (RM0481): HAL_RTC_GetTime must be followed by
  * HAL_RTC_GetDate -- GetTime latches the shadow registers (and clears
  * RSF), GetDate releases them, so the pair always sees one consistent
  * instant. Reading date first or alone can serve a stale mix. The
  * 2-digit BCD year is mapped onto the fixed century base 2000. An
  * invalid result (HAL failure, or a calendar reset by backup-domain
  * loss) returns false and leaves *out untouched.
  */
/*============================================================================*/
bool m1_rtc_get_datetime(S_M1_DateTime_t *out)
{
	RTC_TimeTypeDef time = {0};
	RTC_DateTypeDef date = {0};
	S_M1_DateTime_t dt;

	if ( out==NULL )
		return false;

	if ( HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN)!=HAL_OK )
		return false;
	if ( HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN)!=HAL_OK )
		return false; // Must run even when unused: releases the shadow registers

	dt.year = (uint16_t)(M1_RTC_YEAR_MIN + date.Year);
	dt.month = date.Month;
	dt.day = date.Date;
	dt.hour = time.Hours;
	dt.minute = time.Minutes;

	if ( !m1_rtc_datetime_valid(&dt) )
		return false; // Not set (backup-domain loss) or outside 2000..2099

	*out = dt;
	return true;
} // bool m1_rtc_get_datetime(S_M1_DateTime_t *out)


/*============================================================================*/
/**
  * @brief Validate and write the RTC calendar.
  *
  * The pure validator runs first: an impossible date (Feb 30, day 31 in
  * a 30-day month, hour 24, ...) is rejected before any register is
  * touched. HAL_RTC_SetTime/HAL_RTC_SetDate enter and exit calendar
  * init mode internally (INITF set/cleared, counters halted for the
  * atomic update) -- this module never manipulates INIT mode or the
  * backup domain itself. Seconds restart at 00 (the editor works at
  * minute granularity, so an applied time starts its minute) and the
  * true weekday is computed by the pure domain, because the hardware
  * derives nothing. Sub-second state follows the seconds reset; the
  * running wakeup timer (RTCCLK/16, low-power owned) is unaffected.
  */
/*============================================================================*/
bool m1_rtc_set_datetime(const S_M1_DateTime_t *dt)
{
	RTC_TimeTypeDef time = {0};
	RTC_DateTypeDef date = {0};

	if ( (dt==NULL) || !m1_rtc_datetime_valid(dt) )
		return false; // Reject impossible dates before touching the RTC

	time.Hours = dt->hour;
	time.Minutes = dt->minute;
	time.Seconds = 0u; // Minute granularity: an applied time starts its minute
	time.TimeFormat = RTC_HOURFORMAT_24; // 24-hour calendar (MX_RTC_Init)
	time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
	time.StoreOperation = RTC_STOREOPERATION_RESET;

	date.WeekDay = m1_rtc_weekday(dt->year, dt->month, dt->day); // True weekday, HAL numbering
	date.Month = dt->month;
	date.Date = dt->day;
	date.Year = (uint8_t)(dt->year - M1_RTC_YEAR_MIN); // 2-digit BCD year

	if ( HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BIN)!=HAL_OK )
		return false;
	if ( HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BIN)!=HAL_OK )
		return false;
	return true;
} // bool m1_rtc_set_datetime(const S_M1_DateTime_t *dt)
