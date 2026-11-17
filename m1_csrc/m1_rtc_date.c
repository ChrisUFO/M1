/* See COPYING.txt for license details. */

/*
*
*  m1_rtc_date.c
*
*  M1 RTC pure date/time domain (issue #7 completion)
*
*  Pure helpers only -- see m1_rtc_date.h for the module contract. This
*  file must stay free of HAL/FreeRTOS/u8g2 includes so the host test
*  compiles it verbatim.
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "m1_rtc_date.h"

/*************************** D E F I N E S ************************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/*============================================================================*/
/**
  * @brief Proleptic Gregorian leap year (pure).
  */
/*============================================================================*/
bool m1_rtc_is_leap_year(uint16_t year)
{
	return (bool)(((year%4u)==0u) && (((year%100u)!=0u) || ((year%400u)==0u)));
} // bool m1_rtc_is_leap_year(uint16_t year)


/*============================================================================*/
/**
  * @brief Days in a month (pure); 0 for an out-of-range month so callers
  *        can never treat a bogus month as having days.
  */
/*============================================================================*/
uint8_t m1_rtc_days_in_month(uint16_t year, uint8_t month)
{
	static const uint8_t days[12] = { 31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u };

	if ( (month<1u) || (month>12u) )
		return 0u;
	if ( (month==2u) && m1_rtc_is_leap_year(year) )
		return 29u;
	return days[month-1u];
} // uint8_t m1_rtc_days_in_month(uint16_t year, uint8_t month)


/*============================================================================*/
/**
  * @brief Full range + calendar validation (pure): year 2000..2099, month
  *        1..12, day 1..days-in-month (leap-aware), hour 0..23, minute 0..59.
  *        The RTC write path and the editor's apply step both gate on this,
  *        so no impossible date can reach the hardware.
  */
/*============================================================================*/
bool m1_rtc_datetime_valid(const S_M1_DateTime_t *dt)
{
	if ( dt==NULL )
		return false;
	if ( (dt->year<M1_RTC_YEAR_MIN) || (dt->year>M1_RTC_YEAR_MAX) )
		return false;
	if ( (dt->month<1u) || (dt->month>12u) )
		return false;
	if ( (dt->day<1u) || (dt->day>m1_rtc_days_in_month(dt->year, dt->month)) )
		return false;
	if ( dt->hour>23u )
		return false;
	return dt->minute<=59u;
} // bool m1_rtc_datetime_valid(const S_M1_DateTime_t *dt)


/*============================================================================*/
/**
  * @brief Weekday for a calendar date (pure), Sakamoto's method. Returns
  *        HAL's numbering (RTC_WEEKDAY_MONDAY=1 .. RTC_WEEKDAY_SUNDAY=7)
  *        because the RTC keeps no derived weekday -- m1_rtc feeds the
  *        true value on write instead of parking a constant lie in the
  *        register. 0 for a month/day outside 1..12 / 1..31.
  */
/*============================================================================*/
uint8_t m1_rtc_weekday(uint16_t year, uint8_t month, uint8_t day)
{
	static const uint8_t t[12] = { 0u, 3u, 2u, 5u, 0u, 3u, 5u, 1u, 4u, 6u, 2u, 4u };
	uint32_t y;

	if ( (month<1u) || (month>12u) || (day<1u) || (day>31u) )
		return 0u; // Callers pass validated dates; keep the guard anyway

	y = year;
	if ( month<3u )
		y--; // Jan/Feb belong to the previous lunar year in Sakamoto's terms
	y += (y/4u) - (y/100u) + (y/400u) + t[month-1u] + (uint32_t)day;
	return (uint8_t)(((y + 6u) % 7u) + 1u); // Sakamoto 0=Sunday -> 1=Monday..7=Sunday
} // uint8_t m1_rtc_weekday(uint16_t year, uint8_t month, uint8_t day)


/*============================================================================*/
/**
  * @brief Step one field by +1/-1 (pure), the editor's value engine.
  *
  * Year clamps at the 2000..2099 window ends (the hardware's own range;
  * wrapping a century base would be surprising); month/day/hour/minute
  * wrap within their domains. After a year or month change the day is
  * clamped to the new days-in-month, so stepping alone can never produce
  * an impossible date (2024-02-29 -> year+1 -> 2025-02-28). dir==0 and
  * unknown fields change nothing.
  */
/*============================================================================*/
void m1_rtc_datetime_step(S_M1_DateTime_t *dt, E_M1_RtcField_t field, int8_t dir)
{
	uint8_t dim;

	if ( (dt==NULL) || (dir==0) )
		return;

	switch (field)
	{
		case M1_RTC_FIELD_YEAR:
			if ( dir>0 )
			{
				if ( dt->year<M1_RTC_YEAR_MAX )
					dt->year++;
			} // if ( dir>0 )
			else
			{
				if ( dt->year>M1_RTC_YEAR_MIN )
					dt->year--;
			} // else
			break;
		case M1_RTC_FIELD_MONTH:
			if ( dir>0 )
				dt->month = (dt->month>=12u) ? 1u : (uint8_t)(dt->month+1u);
			else
				dt->month = (dt->month<=1u) ? 12u : (uint8_t)(dt->month-1u);
			break;
		case M1_RTC_FIELD_DAY:
			dim = m1_rtc_days_in_month(dt->year, dt->month);
			if ( dim==0u )
				return;
			if ( dir>0 )
				dt->day = (dt->day>=dim) ? 1u : (uint8_t)(dt->day+1u);
			else
				dt->day = (dt->day<=1u) ? dim : (uint8_t)(dt->day-1u);
			return; // Day stepping needs no re-clamp
		case M1_RTC_FIELD_HOUR:
			if ( dir>0 )
				dt->hour = (dt->hour>=23u) ? 0u : (uint8_t)(dt->hour+1u);
			else
				dt->hour = (dt->hour==0u) ? 23u : (uint8_t)(dt->hour-1u);
			break;
		case M1_RTC_FIELD_MINUTE:
			if ( dir>0 )
				dt->minute = (dt->minute>=59u) ? 0u : (uint8_t)(dt->minute+1u);
			else
				dt->minute = (dt->minute==0u) ? 59u : (uint8_t)(dt->minute-1u);
			break;
		default:
			return;
	} // switch (field)

	/* A shorter month (or a non-leap February) can strand the day after a
	 * year/month step: clamp so the result always passes validation. */
	if ( (field==M1_RTC_FIELD_YEAR) || (field==M1_RTC_FIELD_MONTH) )
	{
		dim = m1_rtc_days_in_month(dt->year, dt->month);
		if ( (dim!=0u) && (dt->day>dim) )
			dt->day = dim;
	} // if ( YEAR/MONTH step: re-clamp the day )
} // void m1_rtc_datetime_step(S_M1_DateTime_t *dt, E_M1_RtcField_t field, int8_t dir)


/*============================================================================*/
/**
  * @brief Format "YYYY-MM-DD HH:MM" (pure). Zero-padded, so field widths
  *        never jump; truncates safely into a short buffer. Returns buf
  *        ("" when there is no usable buffer), the label-helper
  *        convention of the settings screens.
  */
/*============================================================================*/
const char *m1_rtc_format_datetime(const S_M1_DateTime_t *dt, char *buf, size_t bufsize)
{
	if ( (buf==NULL) || (bufsize==0u) )
		return "";
	if ( dt==NULL )
	{
		buf[0] = '\0';
		return buf;
	} // if ( dt==NULL )
	snprintf(buf, bufsize, "%04u-%02u-%02u %02u:%02u",
		(unsigned)dt->year, (unsigned)dt->month, (unsigned)dt->day,
		(unsigned)dt->hour, (unsigned)dt->minute);
	return buf;
} // const char *m1_rtc_format_datetime(...)
