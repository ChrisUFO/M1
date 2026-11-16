/* See COPYING.txt for license details. */

/*
*
* m1_fw_update.h
*
* Firmware update functionality
*
* M1 Project
*
*/

#ifndef M1_FW_UPDATE_H_
#define M1_FW_UPDATE_H_

#include "m1_display.h"

void firmware_update_init(void);
void firmware_update_exit(void);
void firmware_update_get_image_file(void);
void firmware_update_start(void);
void firmware_update_gui_update(const S_M1_Menu_t *phmenu, uint8_t sel_item);

/* Software USB DFU entry (issue #41-B): persist DEV_OP_STATUS_USB_DFU_REQUEST
 * through startup_config_write() and reset. The next boot's
 * startup_config_handler() honours it as the first status branch (clearing it
 * before bl_jump_to_dfu() jumps to the ROM bootloader), so there is exactly one
 * DFU entry path for the CLI (`dfu`, unit PR-CLI) and the recovery-screen
 * offer. NON-RETURNING. */
void firmware_update_enter_usb_dfu(void) __attribute__((noreturn));

#endif /* M1_FW_UPDATE_H_ */
