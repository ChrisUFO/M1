/* See COPYING.txt for license details. */
/*
*
* m1_ir_remotes.h
*
* Header for infrared remotes
*
* M1 Project
*
*/

#ifndef M1_IR_REMOTES_H_
#define M1_IR_REMOTES_H_

uint8_t ir_remote_file_load(void);

/* Universal remote sessions over the firmware-owned 0:/infrared/db
 * databases (issue #43 part 3 / PR-6):
 * - the four per-category entries below are the DEFAULT per-press
 *   behavior: every OK press transmits exactly ONE frame -- the next
 *   device's code for the pressed function each time, never a
 *   category-wide cycle;
 * - infrared_universal_all_remotes(remote_type, cycle_all) is the
 *   exposed session entry: cycle_all != 0 keeps the upstream
 *   cycle-through-every-device behavior with its pause/resume and
 *   progress UI, reachable ONLY through the explicit "Cycle all ..."
 *   submenu actions (infrared_universal_cycle_* below) -- never as a
 *   default per-press behavior. The session acquires the single-owner
 *   IRMP guard itself; callers must not hold or re-acquire it. */
void infrared_universal_all_remotes(uint8_t remote_type, uint8_t cycle_all);
void infrared_universal_tv_remotes(void);
void infrared_universal_audio_remotes(void);
void infrared_universal_projector_remotes(void);
void infrared_universal_ac_remotes(void);
void infrared_universal_cycle_all_tv(void);
void infrared_universal_cycle_all_audio(void);
void infrared_universal_cycle_all_projector(void);
void infrared_universal_cycle_all_ac(void);
uint8_t ir_protocol_map_lookup(const char *protocol_name, uint8_t *pprotocol_number);

#endif /* M1_IR_REMOTES_H_ */
