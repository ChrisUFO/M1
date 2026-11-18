/* See COPYING.txt for license details. */
/*
* m1_ir_golden_corpus_main.c -- IRMP golden-decode corpus runner (PR-4).
*
* Part of the golden-decode regression test (see m1_ir_golden_decode_test.py
* for build orchestration, provenance and coverage policy). This driver
* compiles together with the REAL vendored Infrared/irmp-irsnd/irmp.c and
* feeds it real-world capture logs from Infrared/irmp-irsnd/IR-Data (the
* upstream IRMP project's community capture corpus, .txt files).
*
* Capture format (from the upstream IRMP project, mikrocontroller.net):
*   - comment lines starting with '#' carry the expected decode in the
*     form "[ <protocol-number> (<NAME>) 0x<address> 0x<command>]",
*     recorded by the IRMP author from physical remote controls;
*   - each following line of '0'/'1' characters is one sampled burst
*     stream at the sample rate given on the command line (Hz);
*   - '1' is IR light (carrier burst), '0' is darkness.
*
* The runner converts each run of equal samples into one edge event for
* irmp_data_sampler(<duration_us>, <new pin level>) -- the exact entry the
* firmware's RX interrupt uses -- then polls irmp_get_data(). A capture
* line passes when at least one decoded frame matches the expectation
* exactly (protocol + address + command); anything else is reported.
*
* Usage: m1_ir_golden_corpus_main <capture-file> <sample-rate-hz>
* Output (for the python aggregator):
*   LINE <n> OK    / LINE <n> FAIL exp=<p,0xA,0xC> got=<p,0xA,0xC>[/...]
*   SUMMARY lines=<n> ok=<n> fail=<n> noexp=<n>
* Exit code: 0 iff fail==0.
*/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "irmp.h"

#define MAX_LINE (1 << 20) /* capture data lines can be long; host side only */
#define MAX_DECODES 64

typedef struct
{
	int proto;
	unsigned addr;
	unsigned cmd;
} decode_t;

static decode_t decodes[MAX_DECODES];
static int decode_count;

static void reset_decodes(void)
{
	decode_count = 0;
}

static void poll_decode(void)
{
	IRMP_DATA d;

	if ( decode_count >= MAX_DECODES )
		return;
	if ( irmp_get_data(&d) )
	{
		decodes[decode_count].proto = d.protocol;
		decodes[decode_count].addr = d.address;
		decodes[decode_count].cmd = d.command;
		decode_count++;
	} // if ( irmp_get_data(&d) )
}

/* Feed the decoder exactly the edge events the firmware's RX ISR produces
* (HAL_TIM_IC_CaptureCallback + the timeout update event). Every edge
* carries the duration of the level that just ended and the new level:
*   - the very first falling edge out of idle (burst start) carries te = 0;
*   - a burst end (rising edge) carries the burst duration, level 1;
*   - a darkness end (falling edge) carries the darkness duration, level 0;
*   - darkness reaching the 15.5 ms timeout produces ONE update event with
*     te = Period+1 and the (still dark) level, re-arms the idle edge
*     detector, and the surplus darkness produces no further events -- the
*     next burst then begins with a te = 0 falling edge again. */
static void feed_first_burst(long usec)
{
	irmp_data_sampler(0, 0);
	poll_decode();
	irmp_data_sampler((uint32_t)usec, 1);
	poll_decode();
}

static void feed_burst(long usec)
{
	irmp_data_sampler((uint32_t)usec, 1);
	poll_decode();
}

static void feed_dark(long usec)
{
	if ( !irmp_start_bit_is_detected() )
		return; /* idle darkness produces no ISR events at all */
	if ( usec >= 15500 )
	{
		irmp_data_sampler(15501, 1);
		poll_decode();
		return; /* surplus darkness: no events (ISR gates on start detected) */
	} // if ( usec >= 15500 )
	irmp_data_sampler((uint32_t)usec, 0);
	poll_decode();
}

static void feed_frame_gap(void)
{
	if ( !irmp_start_bit_is_detected() )
		return;
	/* End of stream: one timeout update event. The pin is dark here -- a
	* trailing burst was already closed by its rising edge, and a trailing
	* darkness never ended -- and the ISR always reports te = Period+1 with
	* the live (dark) level. */
	irmp_data_sampler(15501, 1);
	poll_decode();
	irmp_data_sampler(15501, 1);
	poll_decode();
}

static void feed_sample_line(const char *line, long rate)
{
	long run_len = 0;
	int run_val = -1;
	const char *p = line;

	while ( *p )
	{
		int v = (*p == '0') ? 1 : ((*p == '1') ? 0 : -1); /* capture: '0' is IR light */
		if ( (v < 0) || ((run_val >= 0) && (v != run_val)) )
		{
			long usec = (run_len * 1000000L + rate / 2) / rate;
			if ( run_val == 1 )
			{
				if ( irmp_start_bit_is_detected() )
					feed_burst(usec);
				else
					feed_first_burst(usec);
			} // if ( run_val == 1 )
			else if ( run_val == 0 )
			{
				feed_dark(usec);
			} // else if ( run_val == 0 )
			run_len = 0;
			run_val = v;
		} // if ( (v < 0) || ((run_val >= 0) && (v != run_val)) )
		if ( v >= 0 )
		{
			if ( run_val < 0 )
				run_val = v;
			run_len++;
		} // if ( v >= 0 )
		p++;
	} // while ( *p )
	{
		long usec = (run_len * 1000000L + rate / 2) / rate;
		if ( run_val == 1 )
		{
			/* trailing burst: close it with its rising edge */
			if ( irmp_start_bit_is_detected() )
				feed_burst(usec);
			else
				feed_first_burst(usec);
		} // if ( run_val == 1 )
		/* a trailing darkness never ended: no falling edge, just the gap */
	}
	feed_frame_gap();
}

static bool parse_expectation(const char *comment, int *proto, unsigned *addr, unsigned *cmd)
{
	const char *lb = strrchr(comment, '[');

	if ( lb == NULL )
		return false;
	if ( sscanf(lb, "[%d (%*[^)]) %x %x]", proto, addr, cmd) != 3 )
		return false;
	return true;
}

static void print_decodes(void)
{
	int i;

	for (i=0; i<decode_count; i++)
		printf("%s%d,0x%X,0x%X", i ? "/" : "", decodes[i].proto, decodes[i].addr, decodes[i].cmd);
}

int main(int argc, char **argv)
{
	FILE *f;
	static char *line;
	long rate;
	int lineno = 0, ok = 0, fail = 0, noexp = 0;
	bool have_exp = false;
	int exp_proto = 0;
	unsigned exp_addr = 0, exp_cmd = 0;

	if ( argc != 3 )
	{
		fprintf(stderr, "usage: %s <capture-file> <sample-rate-hz>\n", argv[0]);
		return 2;
	}
	f = fopen(argv[1], "r");
	if ( f == NULL )
	{
		fprintf(stderr, "cannot open %s\n", argv[1]);
		return 2;
	}
	rate = atol(argv[2]);
	if ( rate <= 0 )
	{
		fprintf(stderr, "bad rate %ld\n", rate);
		fclose(f);
		return 2;
	}
	line = malloc(MAX_LINE);
	if ( line == NULL )
		return 2;

	/* The whole file is ONE continuous decoder stream, like the upstream
	* IRMP test main: multi-frame protocols (GRUNDIG/NOKIA/IR60 send a start
	* frame before the data frame) and auto-repetition sequences span what
	* would otherwise look like independent lines. Decodes are attributed to
	* the expectation of the comment that preceded them. */
	irmp_init();
	reset_decodes();

	while ( fgets(line, MAX_LINE, f) )
	{
		lineno++;
		if ( line[0] == '#' )
		{
			int p;
			unsigned a, c;
			if ( have_exp && decode_count > 0 )
			{
				/* previous expectation had decodes: evaluate now */
				int i;
				bool matched = false;
				for (i=0; i<decode_count; i++)
				{
					if ( (decodes[i].proto == exp_proto) && (decodes[i].addr == exp_addr) && (decodes[i].cmd == exp_cmd) )
					{
						matched = true;
						break;
					} // if ( (decodes[i].proto == exp_proto) && ... )
				} // for (i=0; i<decode_count; i++)
				if ( matched )
				{
					ok++;
				} // if ( matched )
				else
				{
					fail++;
					printf("LINE %d FAIL exp=%d,0x%X,0x%X got=", lineno - 1, exp_proto, exp_addr, exp_cmd);
					print_decodes();
					printf(" (%s)\n", argv[1]);
				} // else
			} // if ( have_exp && decode_count > 0 )
			else if ( have_exp )
			{
				fail++;
				printf("LINE %d FAIL exp=%d,0x%X,0x%X got= (%s)\n", lineno - 1, exp_proto, exp_addr, exp_cmd, argv[1]);
			} // else if ( have_exp )
			reset_decodes();

			if ( parse_expectation(line, &p, &a, &c) )
			{
				exp_proto = p;
				exp_addr = a;
				exp_cmd = c;
				have_exp = true;
			} // if ( parse_expectation(line, &p, &a, &c) )
			continue;
		} // if ( line[0] == '#' )
		if ( (line[0] != '0') && (line[0] != '1') )
			continue; /* blank or junk line */

		feed_sample_line(line, rate);
	} // while ( fgets(line, MAX_LINE, f) )

	/* final expectation */
	if ( have_exp )
	{
		int i;
		bool matched = false;
		for (i=0; i<decode_count; i++)
		{
			if ( (decodes[i].proto == exp_proto) && (decodes[i].addr == exp_addr) && (decodes[i].cmd == exp_cmd) )
			{
				matched = true;
				break;
			} // if ( (decodes[i].proto == exp_proto) && ... )
		} // for (i=0; i<decode_count; i++)
		if ( matched )
		{
			ok++;
		} // if ( matched )
		else
		{
			fail++;
			printf("LINE %d FAIL exp=%d,0x%X,0x%X got=", lineno, exp_proto, exp_addr, exp_cmd);
			print_decodes();
			printf(" (%s)\n", argv[1]);
		} // else
	} // if ( have_exp )

	printf("SUMMARY lines-file=%d ok=%d fail=%d noexp=%d (%s)\n", lineno, ok, fail, noexp, argv[1]);

	free(line);
	fclose(f);
	return fail ? 1 : 0;
}
