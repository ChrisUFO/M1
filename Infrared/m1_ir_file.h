/* See COPYING.txt for license details. */
/*
*
* m1_ir_file.h
*
* Streaming .ir file loader (issue #43 part 1 / PR-4)
*
* M1 Project
*
*/

/*
* ===========================================================================
* OWNERSHIP AND CONVENTIONS (binding for the IR subsystem)
* ===========================================================================
* Single-owner IRMP rule: every IRMP/IRSND access (this loader, the transmit
* path below, learn, browser playback and the future cycle-all action) runs
* ONLY in the UI task context (the subfunc_handler_task path), serialized by
* the module-level busy flag of the IR feature. No other task may call into
* IRMP/IRSND or into this module while an IR function is active.
*
* SD convention: user-side IR databases live under "0:/IR/..." (Flipper
* IRDB layout). This module is path-agnostic; the browser (PR-5) owns the
* recursive scan of that directory.
* ===========================================================================
*/

#ifndef M1_IR_FILE_H_
#define M1_IR_FILE_H_

#include <stdint.h>

/* ------------------------------- limits --------------------------------- */

#define M1_IR_FILE_LINE_LEN_MAX		256 /* keyword lines; raw "data:" lines are
										* token-streamed and may be longer */
#define M1_IR_FILE_NAME_LEN_MAX		48
#define M1_IR_FILE_PROTO_LEN_MAX		16
#define M1_IR_FILE_CHUNK_SIZE		512 /* SD read granularity of the parser */

#define M1_IR_FILE_FREQ_MIN_HZ		10000u /* matches IR_ENCODE_CARRIER_FREQ_MIN_KHZ */
#define M1_IR_FILE_FREQ_MAX_HZ		100000u /* matches IR_ENCODE_CARRIER_FREQ_MAX_KHZ */
#define M1_IR_FILE_FREQ_DEFAULT_HZ	38000u
#define M1_IR_FILE_DUTY_DEFAULT_PCT	33u

/* Raw samples are stored in 10us baseband ticks (IR_ENCODE_BASEBAND_PRESCALE_FACTOR),
 * the unit of the OTA transmit buffer. A leading inter-frame pause of 100 ms
 * (10000 ticks) is prepended by the loader, matching the legacy invariant that
 * raw OTA frames start with a pause and carry an odd number of entries. */
#define M1_IR_FILE_RAW_TICK_US		10u
#define M1_IR_FILE_RAW_LEADING_PAUSE_TICKS	10000u

/* ------------------------------- errors --------------------------------- */

typedef enum
{
	M1_IR_FILE_ERR_NONE = 0,
	M1_IR_FILE_ERR_ARG, /* NULL/bad argument */
	M1_IR_FILE_ERR_IO, /* reader failed */
	M1_IR_FILE_ERR_HEADER, /* not a .ir file (Filetype/Version) */
	M1_IR_FILE_ERR_MALFORMED, /* block malformed (or unexpected EOF) */
	M1_IR_FILE_ERR_RAW_OVERFLOW, /* raw block needs a bigger tick buffer */
	M1_IR_FILE_ERR_UNKNOWN_PROTOCOL, /* strict-skip: name not in the map */
	M1_IR_FILE_ERR_EOL
} S_M1_IR_File_Error_t;

/* ------------------------------- model ---------------------------------- */

#define M1_IR_FILE_FLAG_PARSED		0x01u /* type: parsed */
#define M1_IR_FILE_FLAG_RAW			0x02u /* type: raw */
#define M1_IR_FILE_FLAG_HAS_ADDR	0x04u
#define M1_IR_FILE_FLAG_HAS_CMD		0x08u
#define M1_IR_FILE_FLAG_NAME_TRUNC	0x20u /* name was longer than the cap */

/* One device/signal block of a .ir file, as produced by the index pass.
 * Commands are deliberately NOT buffered: the index keeps the address and a
 * command-presence flag only; the full block is re-parsed on demand by
 * m1_ir_file_signal_load(). There is no per-file command cap. */
typedef struct
{
	char name[M1_IR_FILE_NAME_LEN_MAX + 1];
	char protocol[M1_IR_FILE_PROTO_LEN_MAX + 1]; /* "" when raw */
	uint32_t address; /* parsed address value, 0 when raw/absent */
	uint32_t file_offset; /* byte offset of the block's "name:" line */
	uint8_t flags; /* M1_IR_FILE_FLAG_* */
} S_M1_IR_File_Entry_t;

/* One fully parsed block, as produced by the on-demand load. */
typedef struct
{
	char protocol_name[M1_IR_FILE_PROTO_LEN_MAX + 1];
	uint32_t address;
	uint32_t command;
	uint32_t frequency; /* Hz */
	uint8_t duty_cycle; /* percent */
	uint8_t protocol; /* IRMP protocol number, resolved via the one map */
	uint8_t flags; /* M1_IR_FILE_FLAG_* */
	uint16_t raw_count; /* valid tick entries, raw blocks only (0 when parsed) */
	uint16_t *raw_ticks; /* caller-provided buffer, 10us ticks */
} S_M1_IR_File_Signal_t;

typedef struct
{
	uint16_t blocks_seen; /* "name:" blocks found */
	uint16_t blocks_indexed; /* entries stored in the caller array */
	uint16_t skipped_unknown_protocol; /* strict-skip counter */
	uint16_t skipped_malformed;
	uint16_t index_full; /* blocks dropped: caller array exhausted */
} S_M1_IR_File_Stats_t;

/* Visible-notice hook (rendered by the browser in PR-5). Called once per
 * skipped unknown-protocol block while indexing. Never called from ISR. */
typedef void (*S_M1_IR_File_Notice_t)(const char *protocol_name, uint32_t file_offset);

/* Character source of the parser: reads up to len chars into dst, returns
 * the number of chars read (0 = EOF). Lets the host tests feed strings and
 * the firmware feed FatFs through the same pure parsing core. */
typedef int32_t (*S_M1_IR_File_Read_fn)(void *io_ctx, char *dst, int32_t len);

/* Pure parsing core (extracted and host-tested as-is) -------------------- */

/* Index pass over a whole .ir stream. Fills entries[] (up to max_entries)
 * WITHOUT buffering commands; unknown protocols skip the block, increment
 * stats->skipped_unknown_protocol and invoke notice() when non-NULL.
 * Returns the number of entries stored, or a negative S_M1_IR_File_Error_t. */
int32_t m1_ir_file_parse_index(S_M1_IR_File_Read_fn read_fn, void *io_ctx,
		S_M1_IR_File_Entry_t *entries, uint16_t max_entries,
		S_M1_IR_File_Stats_t *stats, S_M1_IR_File_Notice_t notice);

/* On-demand parse of ONE block starting at the reader's current position
 * (the caller seeks to entry->file_offset first). Resolves the protocol name
 * through the single upstream map (ir_protocol_map_lookup); an unknown name
 * returns M1_IR_FILE_ERR_UNKNOWN_PROTOCOL without touching *sig_out.
 * expected_name is the stale-index guard (m1_ir_file_signal_load passes
 * entry->name): when non-NULL, the re-parsed block's name must match it or
 * M1_IR_FILE_ERR_MALFORMED is returned -- SD content changed between index
 * and load can never yield a different-but-valid signal.
 * Raw blocks fill sig_out->raw_ticks[0..raw_count-1] (leading pause included,
 * odd count enforced); a raw block that does not fit the buffer once the
 * leading pause and odd-count pad are accounted for returns
 * M1_IR_FILE_ERR_RAW_OVERFLOW. raw_count/raw_ticks are raw-block fields:
 * a parsed load always publishes raw_count == 0. */
int32_t m1_ir_file_parse_signal(S_M1_IR_File_Read_fn read_fn, void *io_ctx,
		const char *expected_name, S_M1_IR_File_Signal_t *sig_out,
		uint16_t *raw_ticks, uint16_t raw_ticks_cap);

/* Firmware shell (FatFs-backed, pure-core drivers) ------------------------ */

/* Index a .ir file on SD. entries/stats are caller-owned. */
int32_t m1_ir_file_index(const char *path, S_M1_IR_File_Entry_t *entries,
		uint16_t max_entries, S_M1_IR_File_Stats_t *stats, S_M1_IR_File_Notice_t notice);

/* Load one indexed block on demand (opens the file, seeks, parses, closes).
 * The re-parsed block's name is verified against entry->name (stale-index
 * guard): a mismatch returns M1_IR_FILE_ERR_MALFORMED. */
int32_t m1_ir_file_signal_load(const char *path, const S_M1_IR_File_Entry_t *entry,
		S_M1_IR_File_Signal_t *sig_out, uint16_t *raw_ticks, uint16_t raw_ticks_cap);

#endif /* M1_IR_FILE_H_ */
