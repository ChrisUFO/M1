/* See COPYING.txt for license details. */
/*
*
* m1_ir_file.c
*
* Streaming .ir file loader (issue #43 part 1 / PR-4)
*
* M1 Project
*
* Index pass (names/type/protocol/address/command-presence) plus on-demand
* per-block parse for transmit. The file is consumed through a fixed-size
* chunk reader: no whole-file-in-RAM parsing and no per-file command cap.
* See m1_ir_file.h for the IRMP single-owner rule and the "0:/IR" SD
* convention. The block grammar follows the Flipper .ir format:
*
*   Filetype: IR signals file
*   Version: 1
*   # comment
*   name: Power
*   type: parsed
*   protocol: NECext
*   address: 00 00 00 00
*   command: 00 00 00 00
*   frequency: 38000        (optional, default 38 kHz)
*   duty_cycle: 0.330000    (optional, default 33 %)
*   -- or, for raw signals: type: raw / data: 175 7436 179 ...  (microseconds)
*
* Tolerated everywhere: blank lines, '#' comments, CRLF line endings, fields
* in any order inside a block (one exception: "data:" before "type: raw" --
* the samples arrive before the block type is known, so the line is dropped
* and the block skips as malformed), and arbitrarily long raw "data:" lines
* (they are token-streamed, never materialized). A keyword line longer than
* M1_IR_FILE_LINE_LEN_MAX, a bad value, or a block with missing mandatory
* fields skips that block as malformed (counted, never a crash). An unknown
* protocol name skips its block under the strict-skip policy: counted in
* S_M1_IR_File_Stats_t and reported through the visible-notice hook -- never
* transmitted as garbage.
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_ir_file.h"
#include "m1_file_browser.h"
#include "ff.h"
#include "m1_log_debug.h"

#define M1_LOGDB_TAG	"IR_FILE"

/* ===================== M1_IR_FILE_PURE_CORE_BEGIN ===================== */
/*
* Pure parsing core: no FatFs, no HAL, no dynamic allocation. The host test
* m1_csrc/test/m1_ir_file_test.py extracts this region verbatim and links it
* against the real protocol map extracted from Infrared/m1_ir_remotes.c.
*/

#define M1_IR_FILE_HEADER_FILETYPE	"Filetype" /* read_key splits at ':' */
#define M1_IR_FILE_HEADER_VERSION	"Version"
#define M1_IR_FILE_HEADER_SIG_TEXT	"IR signals file"
#define M1_IR_FILE_HEADER_LIB_TEXT	"IR library file"
#define M1_IR_FILE_VERSION_SUPPORTED	1u

#define M1_IR_FILE_KEY_LEN_MAX		16 /* longer than every key below + NUL */
#define M1_IR_FILE_KEY_NAME			"name"
#define M1_IR_FILE_KEY_TYPE			"type"
#define M1_IR_FILE_KEY_PROTOCOL		"protocol"
#define M1_IR_FILE_KEY_ADDRESS		"address"
#define M1_IR_FILE_KEY_COMMAND		"command"
#define M1_IR_FILE_KEY_FREQUENCY	"frequency"
#define M1_IR_FILE_KEY_DUTY_CYCLE	"duty_cycle"
#define M1_IR_FILE_KEY_DATA			"data"
#define M1_IR_FILE_VAL_PARSED		"parsed"
#define M1_IR_FILE_VAL_RAW			"raw"
#define M1_IR_FILE_PROTO_UNKNOWN_TXT	"UNKNOWN" /* table sentinel, never a file value */
#define M1_IR_FILE_RAW_PAD_USEC		560u /* NEC_PULSE_TIME: legacy trailing pad */

/* The one protocol map lives in Infrared/m1_ir_remotes.c; the pure core only
* sees its lookup. The host test links the real extracted implementation. */
extern uint8_t ir_protocol_map_lookup(const char *protocol_name, uint8_t *pprotocol_number);

/*----------------------------- stream reader ------------------------------*/

typedef struct
{
	S_M1_IR_File_Read_fn read_fn;
	void *io_ctx;
	char chunk[M1_IR_FILE_CHUNK_SIZE];
	int32_t chunk_len; /* valid chars in chunk[] */
	int32_t chunk_pos; /* next unread char in chunk[] */
	uint8_t eof;
	uint32_t consumed; /* chars delivered so far == absolute stream offset */
} S_M1_IR_Reader_t;

/*============================================================================*/
/**
  * @brief  Next char from the chunked reader
  * @retval (int32_t) 0..255 char, -1 on EOF or read error
  */
/*============================================================================*/
static int32_t m1_ir_getc(S_M1_IR_Reader_t *r)
{
	int32_t got;

	if ( r->chunk_pos >= r->chunk_len )
	{
		if ( r->eof )
			return -1;
		got = r->read_fn(r->io_ctx, r->chunk, (int32_t)sizeof(r->chunk));
		if ( got <= 0 )
		{
			r->eof = 1; /* read error and EOF both stop the stream */
			return -1;
		} // if ( got <= 0 )
		r->chunk_len = got;
		r->chunk_pos = 0;
	} // if ( r->chunk_pos >= r->chunk_len )

	r->consumed++;
	return (int32_t)(uint8_t)r->chunk[r->chunk_pos++];
} // static int32_t m1_ir_getc(S_M1_IR_Reader_t *r)

/*============================================================================*/
/**
  * @brief  Read the key token of a line. The whole line is consumed when it
  *         has no ':' (blank/comment) or its token is overlong (unknown key);
  *         otherwise the ':' is consumed and the value part is left.
  * @param  key/keysz: output buffer; has_colon: ':' was consumed after key
  * @retval 1 key read, 0 clean EOF before any char, -1 line skipped as an
  *         unknown/overlong key (value part NOT left in the stream)
  */
/*============================================================================*/
static int32_t m1_ir_read_key(S_M1_IR_Reader_t *r, char *key, int32_t keysz, uint8_t *has_colon)
{
	int32_t c;
	int32_t n = 0;

	*has_colon = 0;
	c = m1_ir_getc(r);
	if ( c < 0 )
		return 0;
	while ( (c >= 0) && (c != '\n') && (c != ':') )
	{
		if ( (c != '\r') && (n < (keysz - 1)) )
			key[n++] = (char)c;
		else if ( (c != '\r') && (c != ' ') && (c != '\t') )
		{
			while ( (c >= 0) && (c != '\n') ) // consume the rest of the line
				c = m1_ir_getc(r);
			return -1;
		} // else if ( (c != '\r') && (c != ' ') && (c != '\t') )
		c = m1_ir_getc(r);
	} // while ( (c >= 0) && (c != '\n') && (c != ':') )
	key[n] = '\0';
	if ( c == ':' )
		*has_colon = 1;
	return 1;
} // static int32_t m1_ir_read_key(S_M1_IR_Reader_t *r, char *key, int32_t keysz, uint8_t *has_colon)

/*============================================================================*/
/**
  * @brief  Read the value part of a keyword line (after ':', up to '\n').
  *         An overlong value is truncated and the rest of the line consumed.
  * @retval 0 ok; 1 ok but the value was overlong; -1 EOF before any char
  */
/*============================================================================*/
static int32_t m1_ir_read_value(S_M1_IR_Reader_t *r, char *val, int32_t max)
{
	int32_t c;
	int32_t n = 0;
	int32_t ret = 0;

	c = m1_ir_getc(r);
	while ( c >= 0 )
	{
		if ( c == '\n' )
			break;
		if ( (c != '\r') && (n < (max - 1)) )
			val[n++] = (char)c;
		else if ( c != '\r' )
			ret = 1; /* overlong: keep consuming to the line end */
		c = m1_ir_getc(r);
	} // while ( c >= 0 )
	val[n] = '\0';
	if ( (c < 0) && (n == 0) && (ret == 0) )
		return -1;
	return ret;
} // static int32_t m1_ir_read_value(S_M1_IR_Reader_t *r, char *val, int32_t max)

/*============================================================================*/
/**
  * @brief  Copy a NUL-terminated value into a bounded destination
  */
/*============================================================================*/
static void m1_ir_copy_trunc(const char *src, char *dst, int32_t cap)
{
	int32_t i = 0;

	while ( (i < (cap - 1)) && (src[i] != '\0') )
	{
		dst[i] = src[i];
		i++;
	} // while ( (i < (cap - 1)) && (src[i] != '\0') )
	dst[i] = '\0';
} // static void m1_ir_copy_trunc(const char *src, char *dst, int32_t cap)

/*============================================================================*/
/**
  * @brief  Trim leading blanks of a value string
  */
/*============================================================================*/
static const char *m1_ir_trim(const char *s)
{
	while ( (*s == ' ') || (*s == '\t') )
		s++;
	return s;
} // static const char *m1_ir_trim(const char *s)

/*============================================================================*/
/**
  * @brief  Hex nibble value, -1 when not a hex digit
  */
/*============================================================================*/
static int32_t m1_ir_hexval(int32_t c)
{
	if ( (c >= '0') && (c <= '9') )
		return c - '0';
	if ( (c >= 'a') && (c <= 'f') )
		return c - 'a' + 10;
	if ( (c >= 'A') && (c <= 'F') )
		return c - 'A' + 10;
	return -1;
} // static int32_t m1_ir_hexval(int32_t c)

/*============================================================================*/
/**
  * @brief  Parse an unsigned decimal number (no range clamp)
  * @retval 0 ok, 1 malformed
  */
/*============================================================================*/
static uint8_t m1_ir_parse_decimal(const char *s, uint32_t *out_val)
{
	uint32_t value = 0;

	s = m1_ir_trim(s);
	if ( !*s )
		return 1;
	while ( *s )
	{
		if ( (*s < '0') || (*s > '9') )
			return 1;
		if ( value > (0xFFFFFFFFu / 10u) )
			return 1;
		value = value * 10u + (uint32_t)(*s - '0');
		s++;
	} // while ( *s )
	*out_val = value;
	return 0;
} // static uint8_t m1_ir_parse_decimal(const char *s, uint32_t *out_val)

/*============================================================================*/
/**
  * @brief  Parse "XX XX XX XX" (4 hex bytes, little-endian) into *out_val
  * @retval 0 ok, 1 malformed
  */
/*============================================================================*/
static uint8_t m1_ir_parse_hex32(const char *s, uint32_t *out_val)
{
	uint32_t value = 0;
	int32_t byte_val, digit;
	uint8_t count = 0;

	s = m1_ir_trim(s);
	while ( *s )
	{
		digit = m1_ir_hexval((uint8_t)*s);
		if ( digit < 0 )
			return 1;
		byte_val = digit;
		s++;
		digit = m1_ir_hexval((uint8_t)*s);
		if ( digit >= 0 )
		{
			byte_val = (byte_val << 4) | digit;
			s++;
		} // if ( digit >= 0 )
		value |= ((uint32_t)(uint8_t)byte_val) << (count * 8); // little endian
		count++;
		if ( count > 4 )
			return 1;
		s = m1_ir_trim(s);
	} // while ( *s )

	if ( count != 4 )
		return 1;
	*out_val = value;
	return 0;
} // static uint8_t m1_ir_parse_hex32(const char *s, uint32_t *out_val)

/*============================================================================*/
/**
  * @brief  Parse a decimal frequency in Hz, validated against the carrier
  *         range the transmitter supports
  * @retval 0 ok, 1 malformed/out-of-range
  */
/*============================================================================*/
static uint8_t m1_ir_parse_frequency(const char *s, uint32_t *out_val)
{
	uint32_t value = 0;

	if ( m1_ir_parse_decimal(s, &value) )
		return 1;
	if ( (value < M1_IR_FILE_FREQ_MIN_HZ) || (value > M1_IR_FILE_FREQ_MAX_HZ) )
		return 1;
	*out_val = value;
	return 0;
} // static uint8_t m1_ir_parse_frequency(const char *s, uint32_t *out_val)

/*============================================================================*/
/**
  * @brief  Parse "duty_cycle: 0.330000" into a percent value (10..100)
  * @retval 0 ok, 1 malformed/out-of-range
  */
/*============================================================================*/
static uint8_t m1_ir_parse_duty(const char *s, uint8_t *out_val)
{
	uint32_t permille = 0;
	uint8_t digits = 0;
	const uint32_t scale[3] = { 100u, 10u, 1u };

	s = m1_ir_trim(s);
	while ( (*s >= '0') && (*s <= '9') && (digits < 2) ) // integer part: 0..99
	{
		permille = permille * 10u + (uint32_t)(*s - '0');
		s++;
		digits++;
	} // while ( (*s >= '0') && (*s <= '9') && (digits < 2) )
	if ( digits == 0 )
		return 1;
	permille *= 1000u;
	if ( *s == '.' )
	{
		s++;
		digits = 0;
		while ( (*s >= '0') && (*s <= '9') )
		{
			if ( digits < 3 )
				permille += (uint32_t)(*s - '0') * scale[digits];
			digits++;
			s++;
		} // while ( (*s >= '0') && (*s <= '9') )
		if ( digits == 0 )
			return 1;
	} // if ( *s == '.' )
	if ( *s )
		return 1; // trailing garbage
	*out_val = (uint8_t)((permille + 5u) / 10u); // rounded percent
	if ( (*out_val < 10u) || (*out_val > 100u) )
		return 1;
	return 0;
} // static uint8_t m1_ir_parse_duty(const char *s, uint8_t *out_val)

/*------------------------------- raw sink ---------------------------------*/

/* Raw samples are delivered one at a time through this sink so "data:" lines
* of any length parse with bounded memory. validate_only (index pass) checks
* the tokens without storing them. */
typedef struct
{
	uint16_t *ticks;
	uint16_t count;
	uint16_t cap;
	uint8_t validate_only;
	uint8_t overflow;
} S_M1_IR_RawSink_t;

/*============================================================================*/
/**
  * @brief  Store one raw sample (microseconds) with the legacy conversion
  *          rules: values below one tick clamp up to one tick, zero fails
  * @retval 0 accepted, 1 invalid value, 2 buffer overflow
  */
/*============================================================================*/
static uint8_t m1_ir_raw_sink_put(S_M1_IR_RawSink_t *sink, uint32_t usec)
{
	if ( (usec == 0u) || (usec > (0xFFFFu * (uint32_t)M1_IR_FILE_RAW_TICK_US)) )
		return 1;
	if ( sink->validate_only )
	{
		sink->count++;
		return 0;
	} // if ( sink->validate_only )
	if ( sink->count >= sink->cap )
	{
		sink->overflow = 1;
		return 2;
	} // if ( sink->count >= sink->cap )
	if ( usec < M1_IR_FILE_RAW_TICK_US )
		usec = M1_IR_FILE_RAW_TICK_US;
	sink->ticks[sink->count] = (uint16_t)(usec / M1_IR_FILE_RAW_TICK_US);
	sink->count++;
	return 0;
} // static uint8_t m1_ir_raw_sink_put(S_M1_IR_RawSink_t *sink, uint32_t usec)

/*============================================================================*/
/**
  * @brief  Token-stream one "data:" line (its ':' already consumed) into the
  *          sink; the line is never materialized, so its length is unbounded.
  *          On error the rest of the line is still consumed.
  * @retval 0 ok, 1 malformed token, 2 sink overflow
  */
/*============================================================================*/
static uint8_t m1_ir_parse_data_line(S_M1_IR_Reader_t *r, S_M1_IR_RawSink_t *sink)
{
	uint32_t number;
	uint8_t ret = 0;
	int32_t c;

	c = m1_ir_getc(r);
	while ( 1 )
	{
		while ( (c == ' ') || (c == '\t') || (c == '\r') )
			c = m1_ir_getc(r);
		if ( (c < 0) || (c == '\n') )
			return ret; // end of the data line
		number = 0;
		while ( (c >= '0') && (c <= '9') )
		{
			number = number * 10u + (uint32_t)(c - '0');
			if ( number > 0xFFFFFu )
				ret = 1; // nonsense duration (over a second)
			c = m1_ir_getc(r);
		} // while ( (c >= '0') && (c <= '9') )
		if ( ret == 0 )
		{
			if ( (c >= 0) && (c != '\n') && (c != ' ') && (c != '\t') && (c != '\r') )
				ret = 1; // non-numeric token
			else
			{
				switch ( m1_ir_raw_sink_put(sink, number) )
				{
					case 0:
						break;
					case 2:
						return 2; // overflow: stop early, line rest consumed below
					default:
						ret = 1;
						break;
				} // switch ( m1_ir_raw_sink_put(sink, number) )
			} // else
		} // if ( ret == 0 )
		if ( ret )
		{
			while ( (c >= 0) && (c != '\n') ) // consume the rest of the line
				c = m1_ir_getc(r);
			return ret;
		} // if ( ret )
	} // while ( 1 )
} // static uint8_t m1_ir_parse_data_line(S_M1_IR_Reader_t *r, S_M1_IR_RawSink_t *sink)

/*------------------------------- block model -------------------------------*/

typedef struct
{
	char name[M1_IR_FILE_NAME_LEN_MAX + 1];
	char protocol[M1_IR_FILE_PROTO_LEN_MAX + 1];
	uint32_t address;
	uint32_t command;
	uint32_t frequency;
	uint8_t duty_cycle;
	uint8_t flags; /* M1_IR_FILE_FLAG_* built while parsing */
	uint8_t have_name;
	uint8_t have_type;
	uint8_t have_protocol;
	uint8_t have_addr;
	uint8_t have_cmd;
	uint8_t have_data;
	uint8_t malformed; /* sticky for this block */
	uint32_t block_offset; /* stream offset of the "name:" line */
} S_M1_IR_Block_t;

/*============================================================================*/
/**
  * @brief  Reset the per-block accumulator
  */
/*============================================================================*/
static void m1_ir_block_reset(S_M1_IR_Block_t *blk, uint32_t offset)
{
	memset(blk, 0, sizeof(*blk));
	blk->block_offset = offset;
	blk->frequency = M1_IR_FILE_FREQ_DEFAULT_HZ;
	blk->duty_cycle = M1_IR_FILE_DUTY_DEFAULT_PCT;
} // static void m1_ir_block_reset(S_M1_IR_Block_t *blk, uint32_t offset)

/*============================================================================*/
/**
  * @brief  Copy the finished block into the next index slot (raw entries
  *          store the literal protocol name "raw")
  */
/*============================================================================*/
static void m1_ir_block_store(const S_M1_IR_Block_t *blk, S_M1_IR_File_Entry_t *entries,
		uint16_t max_entries, S_M1_IR_File_Stats_t *stats)
{
	S_M1_IR_File_Entry_t *e;

	if ( stats->blocks_indexed >= max_entries )
	{
		stats->index_full++;
		return;
	} // if ( stats->blocks_indexed >= max_entries )
	e = &entries[stats->blocks_indexed++];
	m1_ir_copy_trunc(blk->name, e->name, (int32_t)sizeof(e->name));
	if ( blk->flags & M1_IR_FILE_FLAG_RAW )
		m1_ir_copy_trunc(M1_IR_FILE_VAL_RAW, e->protocol, (int32_t)sizeof(e->protocol));
	else
		m1_ir_copy_trunc(blk->protocol, e->protocol, (int32_t)sizeof(e->protocol));
	e->address = (blk->flags & M1_IR_FILE_FLAG_RAW) ? 0u : blk->address;
	e->file_offset = blk->block_offset;
	e->flags = blk->flags;
} // static void m1_ir_block_store(...)

/*============================================================================*/
/**
  * @brief  Index-mode finalization of a finished block: store it, count the
  *          strict-skip (unknown protocol) or the malformed skip
  */
/*============================================================================*/
static void m1_ir_block_finish_index(const S_M1_IR_Block_t *blk, S_M1_IR_File_Entry_t *entries,
		uint16_t max_entries, S_M1_IR_File_Stats_t *stats, S_M1_IR_File_Notice_t notice)
{
	uint8_t protocol_number;

	if ( !blk->have_name || !blk->have_type || blk->malformed )
	{
		stats->skipped_malformed++;
		return;
	} // if ( !blk->have_name || !blk->have_type || blk->malformed )
	if ( blk->flags & M1_IR_FILE_FLAG_PARSED )
	{
		if ( !blk->have_protocol || !blk->have_addr || !blk->have_cmd )
		{
			stats->skipped_malformed++;
			return;
		} // if ( !blk->have_protocol || !blk->have_addr || !blk->have_cmd )
		if ( !strcmp(blk->protocol, M1_IR_FILE_PROTO_UNKNOWN_TXT) ||
				ir_protocol_map_lookup(blk->protocol, &protocol_number) )
		{
			/* strict-skip: never index, never transmit */
			stats->skipped_unknown_protocol++;
			if ( notice )
				notice(blk->protocol, blk->block_offset);
			return;
		} // if ( !strcmp(blk->protocol, ...
	} // if ( blk->flags & M1_IR_FILE_FLAG_PARSED )
	else if ( !blk->have_data )
	{
		stats->skipped_malformed++;
		return;
	} // else if ( !blk->have_data )

	m1_ir_block_store(blk, entries, max_entries, stats);
} // static void m1_ir_block_finish_index(...)

/*============================================================================*/
/**
  * @brief  Load-mode finalization: publish the one requested block.
  *          expected_name (when non-NULL): stale-index guard -- the
  *          re-parsed block must still carry this name or the load fails.
  * @retval 0 ok, negative S_M1_IR_File_Error_t
  */
/*============================================================================*/
static int32_t m1_ir_block_finish_load(const S_M1_IR_Block_t *blk, const char *expected_name,
		S_M1_IR_File_Signal_t *sig_out, S_M1_IR_RawSink_t *sink)
{
	if ( !blk->have_name || !blk->have_type || blk->malformed )
		return -(int32_t)M1_IR_FILE_ERR_MALFORMED;
	if ( expected_name && strcmp(blk->name, expected_name) )
		return -(int32_t)M1_IR_FILE_ERR_MALFORMED; // stale index: another block
	if ( blk->flags & M1_IR_FILE_FLAG_PARSED )
	{
		if ( !blk->have_protocol || !blk->have_addr || !blk->have_cmd )
			return -(int32_t)M1_IR_FILE_ERR_MALFORMED;
		if ( !strcmp(blk->protocol, M1_IR_FILE_PROTO_UNKNOWN_TXT) ||
				ir_protocol_map_lookup(blk->protocol, &sig_out->protocol) )
			return -(int32_t)M1_IR_FILE_ERR_UNKNOWN_PROTOCOL; // strict-skip
		m1_ir_copy_trunc(blk->protocol, sig_out->protocol_name, (int32_t)sizeof(sig_out->protocol_name));
		sig_out->address = blk->address;
		sig_out->command = blk->command;
	} // if ( blk->flags & M1_IR_FILE_FLAG_PARSED )
	else
	{
		if ( !blk->have_data )
			return -(int32_t)M1_IR_FILE_ERR_MALFORMED;
		m1_ir_copy_trunc(M1_IR_FILE_VAL_RAW, sig_out->protocol_name, (int32_t)sizeof(sig_out->protocol_name));
		sig_out->protocol = 0; /* raw: consumers check the RAW flag first */
		/* odd-length invariant of the raw OTA path (leading pause already
		 * in): if the pad is due but the buffer is full, the even count
		 * would polarity-shift every TX consumer — fail instead */
		if ( sink->ticks && !(sink->count & 0x01u) )
		{
			if ( sink->count >= sink->cap )
				return -(int32_t)M1_IR_FILE_ERR_RAW_OVERFLOW;
			sink->ticks[sink->count++] = (uint16_t)(M1_IR_FILE_RAW_PAD_USEC / M1_IR_FILE_RAW_TICK_US);
		} // if ( sink->ticks && !(sink->count & 0x01u) )
	} // else
	sig_out->frequency = blk->frequency;
	sig_out->duty_cycle = blk->duty_cycle;
	sig_out->flags = blk->flags;
	/* raw fields are raw-only: a parsed block must never publish the pause
	 * already written into the buffer (bare-pause transmit hazard) */
	sig_out->raw_count = (blk->flags & M1_IR_FILE_FLAG_RAW) ? sink->count : 0u;
	sig_out->raw_ticks = sink->ticks;
	return 0;
} // static int32_t m1_ir_block_finish_load(...)

/*============================================================================*/
/**
  * @brief  Feed one "key: value" line into the open block
  * @retval 0 ok, 1 the line malformed the block (sticky in blk->malformed)
  */
/*============================================================================*/
static uint8_t m1_ir_block_line(S_M1_IR_Block_t *blk, const char *key, const char *val, int32_t overlong)
{
	if ( !strcmp(key, M1_IR_FILE_KEY_NAME) )
	{
		m1_ir_copy_trunc(m1_ir_trim(val), blk->name, (int32_t)sizeof(blk->name));
		blk->have_name = 1;
		if ( overlong )
			blk->flags |= M1_IR_FILE_FLAG_NAME_TRUNC; // truncation is tolerated
	} // if ( !strcmp(key, M1_IR_FILE_KEY_NAME) )
	else if ( !strcmp(key, M1_IR_FILE_KEY_TYPE) )
	{
		val = m1_ir_trim(val);
		if ( !strcmp(val, M1_IR_FILE_VAL_PARSED) )
		{
			blk->flags = (blk->flags & ~M1_IR_FILE_FLAG_RAW) | M1_IR_FILE_FLAG_PARSED;
			blk->have_type = 1;
		} // if ( !strcmp(val, M1_IR_FILE_VAL_PARSED) )
		else if ( !strcmp(val, M1_IR_FILE_VAL_RAW) )
		{
			blk->flags = (blk->flags & ~M1_IR_FILE_FLAG_PARSED) | M1_IR_FILE_FLAG_RAW;
			blk->have_type = 1;
		} // else if ( !strcmp(val, M1_IR_FILE_VAL_RAW) )
		else
			return 1;
	} // else if ( !strcmp(key, M1_IR_FILE_KEY_TYPE) )
	else if ( !strcmp(key, M1_IR_FILE_KEY_PROTOCOL) )
	{
		if ( overlong )
			return 1; // longer than any name in the map: strict-skip territory
		m1_ir_copy_trunc(m1_ir_trim(val), blk->protocol, (int32_t)sizeof(blk->protocol));
		blk->have_protocol = 1;
	} // else if ( !strcmp(key, M1_IR_FILE_KEY_PROTOCOL) )
	else if ( !strcmp(key, M1_IR_FILE_KEY_ADDRESS) )
	{
		if ( overlong || m1_ir_parse_hex32(val, &blk->address) )
			return 1;
		blk->have_addr = 1;
		blk->flags |= M1_IR_FILE_FLAG_HAS_ADDR;
	} // else if ( !strcmp(key, M1_IR_FILE_KEY_ADDRESS) )
	else if ( !strcmp(key, M1_IR_FILE_KEY_COMMAND) )
	{
		if ( overlong || m1_ir_parse_hex32(val, &blk->command) )
			return 1;
		blk->have_cmd = 1;
		blk->flags |= M1_IR_FILE_FLAG_HAS_CMD;
	} // else if ( !strcmp(key, M1_IR_FILE_KEY_COMMAND) )
	else if ( !strcmp(key, M1_IR_FILE_KEY_FREQUENCY) )
	{
		if ( overlong || m1_ir_parse_frequency(val, &blk->frequency) )
			return 1;
	} // else if ( !strcmp(key, M1_IR_FILE_KEY_FREQUENCY) )
	else if ( !strcmp(key, M1_IR_FILE_KEY_DUTY_CYCLE) )
	{
		if ( overlong || m1_ir_parse_duty(val, &blk->duty_cycle) )
			return 1;
	} // else if ( !strcmp(key, M1_IR_FILE_KEY_DUTY_CYCLE) )
	else
	{
		; // unknown key: tolerated and ignored (forward compatibility)
	} // else

	return 0;
} // static uint8_t m1_ir_block_line(...)

/*============================================================================*/
/**
  * @brief  Validate the file header: "Filetype: IR signals|library file"
  *          then "Version: 1". Blank/comment lines pass anywhere before them.
  * @retval 0 ok, negative S_M1_IR_File_Error_t otherwise
  */
/*============================================================================*/
static int32_t m1_ir_check_header(S_M1_IR_Reader_t *r)
{
	char key[M1_IR_FILE_KEY_LEN_MAX];
	char val[M1_IR_FILE_LINE_LEN_MAX];
	int32_t got;
	uint8_t has_colon, filetype_ok = 0, version_ok = 0;
	uint32_t version;

	while ( (!filetype_ok) || (!version_ok) )
	{
		got = m1_ir_read_key(r, key, (int32_t)sizeof(key), &has_colon);
		if ( got <= 0 )
			return -(int32_t)M1_IR_FILE_ERR_HEADER;
		if ( (key[0] == '\0') || (key[0] == '#') || (!has_colon) )
			continue; // blank line / comment: skip
		got = m1_ir_read_value(r, val, (int32_t)sizeof(val));
		if ( got != 0 )
			return -(int32_t)M1_IR_FILE_ERR_HEADER; // EOF or overlong in the header
		if ( !filetype_ok )
		{
			if ( strcmp(key, M1_IR_FILE_HEADER_FILETYPE) )
				return -(int32_t)M1_IR_FILE_ERR_HEADER;
			if ( !strstr(val, M1_IR_FILE_HEADER_SIG_TEXT) && !strstr(val, M1_IR_FILE_HEADER_LIB_TEXT) )
				return -(int32_t)M1_IR_FILE_ERR_HEADER;
			filetype_ok = 1;
		} // if ( !filetype_ok )
		else
		{
			if ( strcmp(key, M1_IR_FILE_HEADER_VERSION) )
				return -(int32_t)M1_IR_FILE_ERR_HEADER;
			if ( m1_ir_parse_decimal(val, &version) || (version != M1_IR_FILE_VERSION_SUPPORTED) )
				return -(int32_t)M1_IR_FILE_ERR_HEADER; // only Version: 1 is supported
			version_ok = 1;
		} // else
	} // while ( (!filetype_ok) || (!version_ok) )

	return 0;
} // static int32_t m1_ir_check_header(S_M1_IR_Reader_t *r)

/*============================================================================*/
/**
  * @brief  Main parser driver shared by both public entry points.
  *         Index mode (entries != NULL): the whole stream, finalizing every
  *         block into entries[]. Load mode (sig_out != NULL): exactly the
  *         one block the reader is positioned on.
  * @retval 0 end of stream, 1 (load mode) the block ended, negative on error
  */
/*============================================================================*/
static int32_t m1_ir_parse_stream(S_M1_IR_Reader_t *r, S_M1_IR_File_Entry_t *entries,
		uint16_t max_entries, S_M1_IR_File_Stats_t *stats,
		S_M1_IR_File_Notice_t notice, const char *expected_name,
		S_M1_IR_File_Signal_t *sig_out, S_M1_IR_RawSink_t *sink)
{
	S_M1_IR_Block_t blk;
	char key[M1_IR_FILE_KEY_LEN_MAX];
	char val[M1_IR_FILE_LINE_LEN_MAX];
	int32_t got, line_start;
	uint8_t has_colon, block_open = 0;
	uint8_t load_mode = (sig_out != NULL);

	m1_ir_block_reset(&blk, 0);

	while ( 1 )
	{
		line_start = (int32_t)r->consumed;
		got = m1_ir_read_key(r, key, (int32_t)sizeof(key), &has_colon);
		if ( got == 0 )
			break; // clean EOF: finalize below
		if ( (got < 0) || (!has_colon) )
			continue; // blank/comment/unknown-key line: skipped whole

		if ( !strcmp(key, M1_IR_FILE_KEY_NAME) )
		{
			if ( load_mode && block_open )
			{
				/* the next block starts here: the requested one is complete */
				got = m1_ir_block_finish_load(&blk, expected_name, sig_out, sink);
				return got ? got : 1;
			} // if ( load_mode && block_open )
			if ( block_open )
				m1_ir_block_finish_index(&blk, entries, max_entries, stats, notice);
			m1_ir_block_reset(&blk, (uint32_t)line_start);
			block_open = 1;
			if ( stats )
				stats->blocks_seen++;
			got = m1_ir_read_value(r, val, (int32_t)sizeof(val));
			if ( got < 0 )
				break; // EOF right after "name:"
			if ( m1_ir_block_line(&blk, key, val, got) )
				blk.malformed = 1;
			continue;
		} // if ( !strcmp(key, M1_IR_FILE_KEY_NAME) )

		if ( !strcmp(key, M1_IR_FILE_KEY_DATA) )
		{
			if ( block_open && (blk.flags & M1_IR_FILE_FLAG_RAW) )
			{
				got = m1_ir_parse_data_line(r, sink);
				if ( got == 2 )
					return -(int32_t)M1_IR_FILE_ERR_RAW_OVERFLOW;
				if ( got != 0 )
					blk.malformed = 1;
				else
					blk.have_data = 1;
			} // if ( block_open && (blk.flags & M1_IR_FILE_FLAG_RAW) )
			else
			{
				(void)m1_ir_read_value(r, val, (int32_t)sizeof(val)); // not a raw block (or "type: raw" not yet seen): ignore
			} // else
			continue;
		} // if ( !strcmp(key, M1_IR_FILE_KEY_DATA) )

		got = m1_ir_read_value(r, val, (int32_t)sizeof(val));
		if ( got < 0 )
			break; // EOF mid-file without newline: finalize below
		if ( !block_open )
			continue; // keys before the first block: ignored
		if ( m1_ir_block_line(&blk, key, val, got) )
			blk.malformed = 1;
	} // while ( 1 )

	if ( load_mode && !block_open )
		return -(int32_t)M1_IR_FILE_ERR_MALFORMED; // reader never saw a block

	if ( block_open )
	{
		if ( load_mode )
			return m1_ir_block_finish_load(&blk, expected_name, sig_out, sink);
		m1_ir_block_finish_index(&blk, entries, max_entries, stats, notice);
	} // if ( block_open )

	return 0;
} // static int32_t m1_ir_parse_stream(...)

/*============================================================================*/
/**
  * @brief  Index pass over a whole .ir stream (see m1_ir_file.h)
  */
/*============================================================================*/
int32_t m1_ir_file_parse_index(S_M1_IR_File_Read_fn read_fn, void *io_ctx,
		S_M1_IR_File_Entry_t *entries, uint16_t max_entries,
		S_M1_IR_File_Stats_t *stats, S_M1_IR_File_Notice_t notice)
{
	S_M1_IR_Reader_t r;
	S_M1_IR_File_Stats_t local_stats;
	S_M1_IR_RawSink_t sink;
	int32_t ret;

	if ( (read_fn == NULL) || ((entries == NULL) && (max_entries != 0)) )
		return -(int32_t)M1_IR_FILE_ERR_ARG;
	if ( stats == NULL )
		stats = &local_stats;
	memset(stats, 0, sizeof(*stats));
	memset(&sink, 0, sizeof(sink));
	sink.validate_only = 1; // index pass: validate raw tokens, never store

	memset(&r, 0, sizeof(r));
	r.read_fn = read_fn;
	r.io_ctx = io_ctx;

	ret = m1_ir_check_header(&r);
	if ( ret )
		return ret;

	ret = m1_ir_parse_stream(&r, entries, max_entries, stats, notice, NULL, NULL, &sink);
	if ( ret < 0 )
		return ret;

	return (int32_t)stats->blocks_indexed;
} // int32_t m1_ir_file_parse_index(...)

/*============================================================================*/
/**
  * @brief  On-demand parse of one block (see m1_ir_file.h)
  */
/*============================================================================*/
int32_t m1_ir_file_parse_signal(S_M1_IR_File_Read_fn read_fn, void *io_ctx,
		const char *expected_name, S_M1_IR_File_Signal_t *sig_out,
		uint16_t *raw_ticks, uint16_t raw_ticks_cap)
{
	S_M1_IR_Reader_t r;
	S_M1_IR_RawSink_t sink;
	int32_t ret;

	if ( (read_fn == NULL) || (sig_out == NULL) )
		return -(int32_t)M1_IR_FILE_ERR_ARG;
	if ( (raw_ticks == NULL) && (raw_ticks_cap != 0) )
		return -(int32_t)M1_IR_FILE_ERR_ARG;
	memset(sig_out, 0, sizeof(*sig_out));
	memset(&sink, 0, sizeof(sink));
	sink.ticks = raw_ticks;
	sink.cap = raw_ticks_cap;

	memset(&r, 0, sizeof(r));
	r.read_fn = read_fn;
	r.io_ctx = io_ctx;

	/* leading inter-frame pause, mirroring the legacy raw OTA invariant */
	if ( raw_ticks && raw_ticks_cap )
		(void)m1_ir_raw_sink_put(&sink, M1_IR_FILE_RAW_LEADING_PAUSE_TICKS * M1_IR_FILE_RAW_TICK_US);

	ret = m1_ir_parse_stream(&r, NULL, 0, NULL, NULL, expected_name, sig_out, &sink);
	if ( ret < 0 )
		return ret;

	return 0;
} // int32_t m1_ir_file_parse_signal(...)
/* ====================== M1_IR_FILE_PURE_CORE_END ====================== */

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static int32_t m1_ir_sd_read(void *io_ctx, char *dst, int32_t len);

/***************************** V A R I A B L E S ******************************/

typedef struct
{
	FIL fil;
	uint8_t err;
} S_M1_IR_SdIo_t;

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief  FatFs chunk source for the pure parser
  */
/*============================================================================*/
static int32_t m1_ir_sd_read(void *io_ctx, char *dst, int32_t len)
{
	S_M1_IR_SdIo_t *sd = (S_M1_IR_SdIo_t *)io_ctx;
	UINT got = 0;

	if ( f_read(&sd->fil, dst, (UINT)len, &got) != FR_OK )
	{
		sd->err = 1;
		return 0; /* the reader stops on 0; the shell converts to ERR_IO */
	} // if ( f_read(sd->fil, dst, (UINT)len, &got) != FR_OK )
	return (int32_t)got;
} // static int32_t m1_ir_sd_read(void *io_ctx, char *dst, int32_t len)

/*============================================================================*/
/**
  * @brief  Index a .ir file on the SD card (see m1_ir_file.h)
  */
/*============================================================================*/
int32_t m1_ir_file_index(const char *path, S_M1_IR_File_Entry_t *entries,
		uint16_t max_entries, S_M1_IR_File_Stats_t *stats, S_M1_IR_File_Notice_t notice)
{
	S_M1_IR_SdIo_t sd;
	FRESULT fret;
	int32_t ret;

	if ( (path == NULL) || ((entries == NULL) && (max_entries != 0)) )
		return -(int32_t)M1_IR_FILE_ERR_ARG;

	fret = m1_fb_open_file(&sd.fil, path);
	if ( fret != FR_OK )
	{
		M1_LOG_E(M1_LOGDB_TAG, "open %s failed: %d\r\n", path, fret);
		return -(int32_t)M1_IR_FILE_ERR_IO;
	} // if ( fret != FR_OK )
	sd.err = 0;

	ret = m1_ir_file_parse_index(m1_ir_sd_read, &sd, entries, max_entries, stats, notice);

	if ( sd.err && (ret >= 0) )
		ret = -(int32_t)M1_IR_FILE_ERR_IO; // do not silently truncate on IO errors
	m1_fb_close_file(&sd.fil);

	return ret;
} // int32_t m1_ir_file_index(...)

/*============================================================================*/
/**
  * @brief  Load one indexed block on demand (see m1_ir_file.h)
  */
/*============================================================================*/
int32_t m1_ir_file_signal_load(const char *path, const S_M1_IR_File_Entry_t *entry,
		S_M1_IR_File_Signal_t *sig_out, uint16_t *raw_ticks, uint16_t raw_ticks_cap)
{
	S_M1_IR_SdIo_t sd;
	FRESULT fret;
	int32_t ret;

	if ( (path == NULL) || (entry == NULL) || (sig_out == NULL) )
		return -(int32_t)M1_IR_FILE_ERR_ARG;

	fret = m1_fb_open_file(&sd.fil, path);
	if ( fret != FR_OK )
	{
		M1_LOG_E(M1_LOGDB_TAG, "open %s failed: %d\r\n", path, fret);
		return -(int32_t)M1_IR_FILE_ERR_IO;
	} // if ( fret != FR_OK )
	sd.err = 0;

	if ( f_lseek(&sd.fil, (FSIZE_t)entry->file_offset) != FR_OK )
	{
		m1_fb_close_file(&sd.fil);
		return -(int32_t)M1_IR_FILE_ERR_IO;
	} // if ( f_lseek(sd.fil, (FSIZE_t)entry->file_offset) != FR_OK )

	ret = m1_ir_file_parse_signal(m1_ir_sd_read, &sd, entry->name, sig_out, raw_ticks, raw_ticks_cap);

	if ( sd.err && (ret >= 0) )
		ret = -(int32_t)M1_IR_FILE_ERR_IO;
	m1_fb_close_file(&sd.fil);

	return ret;
} // int32_t m1_ir_file_signal_load(...)
