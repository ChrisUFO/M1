/* See COPYING.txt for license details. */

/*
*
*  m1_settings.h
*
*  M1 Settings functions
*
* M1 Project
*
*/

#ifndef M1_SETTINGS_H_
#define M1_SETTINGS_H_

#include <stddef.h>

void menu_settings_init(void);
void menu_settings_exit(void);

void settings_buzzer(void);
void settings_power(void);
void settings_about(void);
void settings_lcd_notifications(void);
void settings_system(void);
void settings_date_time(void);

/* Bounded copy shared by the modal settings screens (label clipping to
 * what a 128x64 row can render and to the store's field bound). */
void settings_str_clip(char *dst, size_t dst_size, const char *src);

/* Shared exit path of the modal settings screens (see m1_settings.c):
 * persist the editing session with one atomic settings_save() when it
 * changed something (refused saves of real edits are shown, not hidden),
 * then reset the main queue for the return to the menu. Also used by the
 * Sub-GHz Radio Settings screen (m1_sub_ghz.c) -- every modal that edits
 * the store exits through this single save boundary. */
void settings_exit_save(uint8_t session_dirty);

#endif /* M1_SETTINGS_H_ */
