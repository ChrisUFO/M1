/* See COPYING.txt for license details. */
/*
*
* m1_ir_browser.h
*
* SD-card IR database browser (issue #43 part 2 / PR-5)
*
* M1 Project
*
*/

/*
* ===========================================================================
* OWNERSHIP AND CONVENTIONS (binding, extends Infrared/m1_ir_file.h)
* ===========================================================================
* Single-owner IRMP rule: this browser runs entirely in the UI task (the
* subfunc_handler_task path, entered through the Infrared menu) and takes
* the IR subsystem busy guard (m1_ir_subsys_acquire/release) for its whole
* session, so learn, saved-remote replay, the upstream universal remotes
* and the CLI transmit test can never overlap a browser playback.
*
* SD convention (see docs/ir-provisioning.md): user-side IR databases live
* under "0:/IR" in the Flipper IRDB layout. Every device is ONE .ir file;
* the first directory level below 0:/IR is the category, deeper levels
* (typically Brand/Device.ir) are flattened into the category's device
* list by a bounded recursive scan. All signal data access goes through
* the PR-4 streaming loader (Infrared/m1_ir_file.h): an index pass builds
* the button list, m1_ir_file_signal_load() re-parses the selected block
* on demand. There is no whole-file parser here and no per-file command
* cap; the legacy fork's ir_proto_map is not ported (strict-skip through
* the single upstream map applies instead).
*
* Favorites/recent stores (PR-6): two plain text files under 0:/System
* (ir_favorites.txt / ir_recent.txt, one device path per line, LRU caps
* M1_IR_BROWSER_FAVORITES_MAX / M1_IR_BROWSER_RECENT_MAX). They are pure
* filesystem data: loads and rewrites happen only in this UI task between
* transmits -- never inside the transmit path itself, and never while a
* frame is on air (recent is recorded where tx_active clears, after the
* frame completes) -- so they can never overlap IRMP TX/learn (the
* browser holds the session guard while it runs, and no other path
* writes these files). Loads are tolerant: a
* missing, corrupt or over-long line is skipped; an unreadable file
* renders an empty list.
* ===========================================================================
*/

#ifndef M1_IR_BROWSER_H_
#define M1_IR_BROWSER_H_

/* ------------------------------- limits --------------------------------- */

/* Visible list rows on the 128x64 LCD (title bar 10 px + 6 rows x 9 px). */
#define M1_IR_BROWSER_ROWS			6

/* Bounded scan results (documented in docs/ir-provisioning.md). Reaching a
* cap keeps the device list and search usable on huge databases; the lists
* show a truncation marker, nothing is read past the cap. */
#define M1_IR_BROWSER_CATS_MAX		24 /* categories under 0:/IR */
#define M1_IR_BROWSER_DEVICES_MAX	64 /* .ir files per list (category or search) */
#define M1_IR_BROWSER_BUTTONS_MAX	128 /* indexed blocks per device file */

/* Recursive scan depth cap, counted from the scanned root (0:/IR for the
* category list and search, the category dir for a device list). Flipper
* IRDB needs 2 (Brand/Device.ir); extra headroom tolerates deeper trees. */
#define M1_IR_BROWSER_DEPTH_MAX		4

/* Path/name buffer bounds (NUL included). */
#define M1_IR_BROWSER_PATH_LEN_MAX	128
#define M1_IR_BROWSER_NAME_LEN_MAX	32

/* Raw tick buffer for one on-demand signal load (10-us ticks, see
* M1_IR_FILE_RAW_TICK_US). Longer raw signals surface a notice instead of
* overflowing. */
#define M1_IR_BROWSER_RAW_TICKS_MAX	4096

/* Favorites / recent LRU caps (issue #43 part 3 / PR-6, legacy values).
 * Both stores are plain text files under 0:/System -- one full device
 * path per line, newest first -- rewritten whole on every change (a
 * bounded rewrite: never more lines than the cap). See
 * docs/ir-provisioning.md for the format and tolerance rules. */
#define M1_IR_BROWSER_FAVORITES_MAX	20
#define M1_IR_BROWSER_RECENT_MAX		10

/* Menu entry point (Infrared > IR File Browser). */
void m1_ir_browser_run(void);

#endif /* M1_IR_BROWSER_H_ */
