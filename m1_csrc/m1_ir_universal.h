/* See COPYING.txt for license details. */

/*
 * m1_ir_universal.h
 *
 * Universal Remote feature for M1 firmware.
 *
 * Parses Flipper Zero-compatible .ir files from the SD card and
 * transmits IR signals using the IRMP/IRSND hardware layer.
 *
 * SD card layout expected:
 *   0:/IR/<Category>/<Brand>/<Device>.ir
 *
 * Example:
 *   0:/IR/TVs/Samsung/Samsung_AA59-00741A.ir
 *
 * Database source: https://github.com/Lucaslhm/Flipper-IRDB (MIT License)
 *
 * M1 Project
 */

#ifndef M1_IR_UNIVERSAL_H_
#define M1_IR_UNIVERSAL_H_

#include <stdint.h>
#include <stdbool.h>
#include "irmp.h"

/* Root directory on SD card where .ir files are stored */
#define IR_UNIVERSAL_SD_ROOT        "0:/IR"

/* Maximum length of a button name (e.g. "Vol_up", "Power") */
#define IR_UNIVERSAL_NAME_LEN_MAX   32

/* Maximum length of a protocol name string (e.g. "Samsung32") */
#define IR_UNIVERSAL_PROTO_LEN_MAX  20

/* Maximum number of buttons/commands in a single .ir file */
#define IR_UNIVERSAL_CMDS_MAX       64

/* Maximum path length for a .ir file on the SD card */
#define IR_UNIVERSAL_PATH_LEN_MAX   128

/*
 * One parsed IR command entry from a .ir file.
 * Corresponds to one "name/type/protocol/address/command" block.
 */
typedef struct
{
    char        name[IR_UNIVERSAL_NAME_LEN_MAX];  /* Button label, e.g. "Power" */
    IRMP_DATA   irmp;                              /* Protocol, address, command ready for IRSND */
    bool        valid;                             /* true if fully parsed */
} S_IR_Cmd_t;

/*
 * Parsed contents of one .ir file (one device remote).
 * Holds up to IR_UNIVERSAL_CMDS_MAX commands.
 */
typedef struct
{
    S_IR_Cmd_t  cmds[IR_UNIVERSAL_CMDS_MAX];
    uint8_t     count;   /* Number of valid commands parsed */
} S_IR_Device_t;

/*
 * ir_universal_run() - Entry point for the Universal Remote menu item.
 *
 * Presents a 3-level SD card browser:
 *   Level 1: Category  (e.g. TVs, Audio, Projectors)
 *   Level 2: Brand     (e.g. Samsung, Sony, LG)
 *   Level 3: Device    (e.g. Samsung_AA59-00741A.ir)
 *
 * After selecting a device, shows a scrollable command list.
 * Pressing OK transmits the selected command via IR.
 * Pressing BACK at any level goes up one level (or exits).
 *
 * This function blocks until the user exits back to the IR menu.
 * It is called from infrared_universal_remotes() in m1_infrared.c.
 */
void ir_universal_run(void);

/*
 * ir_universal_parse_file() - Parse a .ir file from the SD card.
 *
 * @param path     Full FatFs path to the .ir file (e.g. "0:/IR/TVs/Samsung/foo.ir")
 * @param out      Pointer to S_IR_Device_t to populate
 * @return         Number of commands parsed, or 0 on error
 */
uint8_t ir_universal_parse_file(const char *path, S_IR_Device_t *out);

/*
 * ir_universal_proto_to_id() - Map a Flipper protocol name string to an IRMP protocol ID.
 *
 * @param name     Protocol name string (e.g. "Samsung32", "NEC", "RC5")
 * @return         IRMP protocol ID, or IRMP_UNKNOWN_PROTOCOL (0) if not found
 */
uint8_t ir_universal_proto_to_id(const char *name);

#endif /* M1_IR_UNIVERSAL_H_ */
