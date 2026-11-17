/* See COPYING.txt for license details. */

/*
*
*  m1_rtc.h
*
*  M1 RTC calendar access (issue #7 completion)
*
*  The single mediated path to the RTC calendar. Timekeeping design:
*
*  - The battery-backed RTC IS the timebase (backup domain). The clock
*    source is LSI (set by MX_RTC_Init / the CubeMX RCC config): this
*    board's shipped firmware never enables LSE, and switching RTCSEL
*    requires a backup-domain reset that would wipe backup registers
*    DR0/DR1 (boot failsafe state, S_M1_BK_REGS_t) and DR2/DR3 (ESP
*    version cache) -- so the source stays LSI. Consequences, on
*    purpose and documented:
*      * LSI is a VDD-domain RC oscillator: the calendar advances only
*        while the device is powered. With VBAT holding the backup
*        domain the calendar (and DR0-DR3) survive a power cycle, but
*        the unpowered interval is not counted -- the clock resumes
*        where it stopped.
*      * Full backup-domain loss (no/dead VBAT) resets the calendar;
*        m1_rtc_get_datetime() then reports "not set" and the System
*        screen shows "Time not set" until the user sets it.
*      * LSI is a factory-trimmed RC (~32 kHz), not a crystal: expect
*        minutes-per-day drift. Good enough for a settable clock, not
*        for wall-clock accuracy.
*  - No duplication into 0:/System/settings.cfg: the RTC register IS
*    the persisted value. Reset-to-defaults does not touch the clock.
*  - Backup registers DR0-DR3 are owned by S_M1_BK_REGS_t and the ESP
*    version record; this module never touches any TAMP/backup
*    register (scripts/check_no_raw_bkp_writes.sh stays green).
*  - Single writer: the calendar is written only from the UI task
*    (Settings > Date & Time editor through m1_rtc_set_datetime()).
*    The low-power paths own the RTC wakeup timer, not the calendar;
*    neither disturbs the other.
*  - MX_RTC_Init() already ran (and re-runs each boot) before any task
*    exists; HAL_RTC_Init reconfigures prescalers but never clears the
*    calendar, so this module never re-initializes the backup domain.
*
* M1 Project
*
*/

#ifndef M1_RTC_H_
#define M1_RTC_H_

#include <stdint.h>
#include <stdbool.h>
#include "m1_rtc_date.h"

/* Read the current date/time. Returns false when the HAL read fails or
 * the calendar value fails m1_rtc_datetime_valid() (typical after
 * backup-domain loss); *out is only written on success, so callers can
 * keep their seed/previous value. */
bool m1_rtc_get_datetime(S_M1_DateTime_t *out);

/* Validate (m1_rtc_datetime_valid: rejects impossible dates) and write
 * the calendar atomically: HAL_RTC_SetTime/SetDate enter and exit
 * calendar init mode themselves (counters halt for the update, no
 * backup-domain reset), seconds restart at 00 (minute granularity), the
 * true weekday is computed and stored. Returns false when the value is
 * impossible or the HAL refuses; the previous calendar is untouched. */
bool m1_rtc_set_datetime(const S_M1_DateTime_t *dt);

#endif /* M1_RTC_H_ */
