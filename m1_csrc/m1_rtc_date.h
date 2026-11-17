/* See COPYING.txt for license details. */

/*
*
*  m1_rtc_date.h
*
*  M1 RTC pure date/time domain (issue #7 completion)
*
*  Plain C99 over stdint/stdstddef only -- no HAL, no FreeRTOS, no u8g2 --
*  so the host unit test (m1_csrc/test/m1_rtc_test.py) compiles the real
*  m1_rtc_date.c standalone next to a generated harness. Everything that
*  decides "is this date/time possible" or "what does the next step value
*  look like" lives here, never inlined in the HAL or draw/button code.
*
*  Domain: calendar years 2000..2099 (the RTC's BCD year register holds
*  two digits; the fixed century offset 2000 is applied at the m1_rtc
*  boundary), 24-hour time, minute granularity.
*
* M1 Project
*
*/

#ifndef M1_RTC_DATE_H_
#define M1_RTC_DATE_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Calendar window. 2000 is the century base applied to the RTC's 2-digit
 * BCD year; 2099 is the last year it can represent. */
#define M1_RTC_YEAR_MIN					2000u
#define M1_RTC_YEAR_MAX					2099u

/* "YYYY-MM-DD HH:MM" + NUL, the longest string this module formats. */
#define M1_RTC_DATETIME_STR_LEN			17u

typedef struct {
	uint16_t year; // M1_RTC_YEAR_MIN..M1_RTC_YEAR_MAX
	uint8_t month; // 1..12
	uint8_t day; // 1..days in month
	uint8_t hour; // 0..23 (24-hour)
	uint8_t minute; // 0..59
} S_M1_DateTime_t;

/* Editor fields, in on-screen order (date line, then time line). */
typedef enum {
	M1_RTC_FIELD_YEAR = 0,
	M1_RTC_FIELD_MONTH,
	M1_RTC_FIELD_DAY,
	M1_RTC_FIELD_HOUR,
	M1_RTC_FIELD_MINUTE,
	M1_RTC_FIELD_COUNT
} E_M1_RtcField_t;

bool m1_rtc_is_leap_year(uint16_t year);
uint8_t m1_rtc_days_in_month(uint16_t year, uint8_t month); // 0 for month 0/>12
bool m1_rtc_datetime_valid(const S_M1_DateTime_t *dt); // full range + calendar check
uint8_t m1_rtc_weekday(uint16_t year, uint8_t month, uint8_t day); // 1=Mon..7=Sun (HAL numbering), 0 on bad input
void m1_rtc_datetime_step(S_M1_DateTime_t *dt, E_M1_RtcField_t field, int8_t dir); // +/-1, wrapped/clamped, day kept possible
const char *m1_rtc_format_datetime(const S_M1_DateTime_t *dt, char *buf, size_t bufsize); // "YYYY-MM-DD HH:MM"

#endif /* M1_RTC_DATE_H_ */
