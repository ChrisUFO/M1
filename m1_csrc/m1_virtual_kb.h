/* See COPYING.txt for license details. */

/*
*
* m1_virtual_kb.h
*
* Library for the virtual keyboard
*
* M1 Project
*
*/

#ifndef M1_VIRTUAL_KB_H_
#define M1_VIRTUAL_KB_H_

uint8_t m1_vkb_get_filename(char *description, char *default_name, char *new_name);
uint8_t m1_vkbs_get_data(char *description, char *new_data);
uint8_t m1_vkb_get_password(char *description, char *password_buffer, uint8_t max_len);

#endif /* M1_VIRTUAL_KB_H_ */
