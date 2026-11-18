/* See COPYING.txt for license details. */
/*
* m1_ir_golden_tx_main.c -- transmit-path golden vectors (PR-4).
*
* Part of the golden-decode regression test (see m1_ir_golden_decode_test.py).
* This driver compiles together with:
*   - the REAL vendored Infrared/irmp-irsnd/irsnd.c (transmit side),
*   - the REAL vendored Infrared/irmp-irsnd/irmp.c  (receive side),
*   - the REAL streaming loader pure core + the REAL protocol mapping table
*     (extracted from Infrared/m1_ir_file.c / m1_ir_remotes.c by the python
*     wrapper), so every vector travels the exact production path:
*
*   .ir text -> m1_ir_file_parse_index/parse_signal (strict-skip applies)
*            -> IRMP_DATA { protocol via the ONE map, address, command }
*            -> irsnd_generate_tx_data + m1_make_ir_ota_multiframes
*            -> OTA mark/space tick buffer (what the timer plays on the pin)
*            -> irmp_data_sampler (the RX ISR entry)
*            -> irmp_get_data
*
* Assertions are byte-level and derived from the official protocol
* definitions (NOT from the encoder implementation): for each family the
* wire bytes are reconstructed from the decode and checked against the
* NEC/Sony/RC5/RCA framing rules. RCA has no decoder in the vendored IRMP,
* so its OTA buffer is demodulated by an independent reference demodulator
* built on the irmpprotocols.h constants.
*
* Usage: m1_ir_golden_tx_main  (no arguments; exit code 0 = all vectors pass)
*/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "irmp.h"
#include "irsnd.h"
#include "m1_ir_file.h"
#include "m1_ir_signals.h" /* stub on host: S_M1_IR_Payload_t for irsnd.c */
#include "irmpprotocols.h"

/* provided by the python wrapper from the real sources */
extern uint8_t ir_protocol_map_lookup(const char *protocol_name, uint8_t *pprotocol_number);

/* raw-OTA global the vendored irsnd.c references */
S_M1_IR_Payload_t ir_universal_payload;

/* fake timer registers behind the stub HAL (never read, just writable) */
static TIM_TypeDef stub_tim_regs;
static TIM_HandleTypeDef stub_timer;

/* stub-HAL error hook: never expected on the host */
void Error_Handler(void)
{
    printf("FAIL: unexpected Error_Handler() from the stubbed HAL layer\n");
    exit(3);
}

static int failures;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL %d: %s\n", __LINE__, msg); failures++; } \
} while (0)
#define CHECK_EQ(a, b, msg) do { \
    long _a = (long)(a), _b = (long)(b); \
    if (_a != _b) { printf("FAIL %d: %s (%ld != %ld)\n", __LINE__, msg, _a, _b); failures++; } \
} while (0)

/* --------------------------- in-memory .ir source ------------------------ */

typedef struct { const char *p; size_t pos; } str_io_t;

static int32_t str_read(void *ctx, char *dst, int32_t len)
{
    str_io_t *s = (str_io_t *)ctx;
    size_t avail = strlen(s->p) - s->pos;
    size_t n = avail < (size_t)len ? avail : (size_t)len;
    if (n) { memcpy(dst, s->p + s->pos, n); s->pos += n; }
    return (int32_t)n;
}

/* Build a Flipper-style .ir single-block file and load it through the real
* loader + real map. Returns 0 on success. */
static int load_ir_signal(const char *proto_name, const char *addr_hex,
                          const char *cmd_hex, S_M1_IR_File_Signal_t *sig)
{
    static char file_text[512];
    str_io_t io;
    S_M1_IR_File_Entry_t entry;
    S_M1_IR_File_Stats_t stats;
    int32_t n;

    snprintf(file_text, sizeof file_text,
             "Filetype: IR signals file\r\n"
             "Version: 1\r\n"
             "name: Vector\r\n"
             "type: parsed\r\n"
             "protocol: %s\r\n"
             "address: %s\r\n"
             "command: %s\r\n"
             "frequency: 38000\r\n"
             "duty_cycle: 0.330000\r\n",
             proto_name, addr_hex, cmd_hex);

    io.p = file_text;
    io.pos = 0;
    n = m1_ir_file_parse_index(str_read, &io, &entry, 1, &stats, NULL);
    if (n != 1)
    {
        printf("FAIL: vector '%s' not indexed (n=%ld stats: unk=%u mal=%u)\n",
               proto_name, (long)n, stats.skipped_unknown_protocol, stats.skipped_malformed);
        failures++;
        return 1;
    } // if ( n != 1 )

    io.pos = entry.file_offset;
    if ( m1_ir_file_parse_signal(str_read, &io, sig, NULL, 0) != 0 )
    {
        printf("FAIL: vector '%s' did not load\n", proto_name);
        failures++;
        return 1;
    } // if ( m1_ir_file_parse_signal(...) != 0 )
    return 0;
}

/* ------------------------------ decode sink ------------------------------ */

#define MAX_DECODES 16
static IRMP_DATA decodes[MAX_DECODES];
static int decode_count;

static void poll_decode(void)
{
    if ( decode_count < MAX_DECODES && irmp_get_data(&decodes[decode_count]) )
        decode_count++;
}

static void feed_gap(void)
{
    irmp_data_sampler(15501, 1);
    poll_decode();
    irmp_data_sampler(15501, 1);
    poll_decode();
}

/* Play one OTA frame into the decoder. Ticks are 10 us each; the entries
* alternate mark/space. A frame of odd length starts with the inter-frame
* pause (space), even length starts with a mark -- the invariant the
* firmware's infrared_transmit() uses. A TRAILING space entry is not fed as
* a falling edge: on air the transmitter simply goes silent there, so the
* darkness merges into the post-frame gap (only a timeout closes it). */
/* Play one OTA frame into the decoder. Ticks are 10 us each; the entries
* alternate mark/space. A frame of odd length starts with an inter-frame
* pause (space) -- the receiver is idle-dark then, so that entry is dropped
* and the first mark is preceded by the RX ISR's idle falling edge (te = 0),
* which resets the decoder's pulse accumulator. The TRAILING space entry is
* fed only for manchester frames (RC5/RC5X): there it is the final data
* half-bit, while pulse-distance frames end on a stop bit whose trailing
* space is on-air silence -- only a decoder timeout closes those. */
static void play_ota_frame(const uint16_t *buf, uint16_t len, int feed_trailing_space)
{
    int i;
    int first;

    if ( len == 0 )
        return;
    first = (len & 0x01u) ? 1 : 0; /* odd: entry 0 is the idle pause */

    irmp_data_sampler(0, 0); /* idle falling edge out of EDGE_DET_IDLE */
    poll_decode();

    for (i=first; i<len; i++)
    {
        uint32_t usec = (uint32_t)buf[i] * 10u;
        if ( (i == (len - 1)) && !feed_trailing_space && (((len - first) & 0x01u) == 0u) )
            break; /* trailing space of a stop-bit frame: merge into silence */
        if ( ((i - first) & 0x01u) == 0u )
        {
            irmp_data_sampler(usec, 1); /* rising edge: burst of usec ended */
            poll_decode();
        }
        else
        {
            irmp_data_sampler(usec, 0); /* falling edge: darkness of usec ended */
            poll_decode();
        }
    }
    feed_gap();
}

/* Reset the transmitter between vectors exactly like the firmware does when
* leaving the TX path (m1_ir_remotes.c calls the handler with
* IRMP_UNKNOWN_PROTOCOL as "Reset"). */
static void tx_reset(void)
{
    m1_ir_ota_frame_repeat_handler(IRMP_UNKNOWN_PROTOCOL);
}

/* Transmit one signal exactly like ir_remote_function_play(): fill IRMP_DATA
* from the loaded signal, generate the OTA buffer, play it into IRMP.
* manchester frames feed their trailing half-bit space; pulse-distance frames
* end in stop-bit silence (see play_ota_frame). */
static int transmit_and_decode(const S_M1_IR_File_Signal_t *sig, int manchester)
{
    IRMP_DATA d;
    uint8_t ok;

    memset(&d, 0, sizeof d);
    d.protocol = sig->protocol;
    d.address = sig->address;
    d.command = sig->command;
    d.duty_cycle = sig->duty_cycle;
    d.frequency = sig->frequency;
    d.flags = 1;

    irsnd_init(&stub_timer, 1); /* the firmware re-inits IRSND before every send */
    stub_timer.Instance = &stub_tim_regs;
    irmp_init();
    decode_count = 0;
    ok = irsnd_generate_tx_data(d);
    if ( !ok )
        return -1;
    if ( !m1_make_ir_ota_multiframes() )
        return -2;
    if ( !m1_check_ir_ota_frame_status() )
        return -3;
    play_ota_frame(m1_get_ir_ota_buffer_ptr(), m1_get_ir_ota_frame_len(), manchester);
    tx_reset(); /* leave the transmitter idle for the next vector */
    return decode_count;
}

/* Wire-byte helpers: the vendored IRMP reports NEC-family frames as
* 16-bit address/command where byte0 is the first byte on the wire. */
static unsigned wire_b0(unsigned v) { return v & 0xFFu; }
static unsigned wire_b1(unsigned v) { return (v >> 8) & 0xFFu; }

/* ------------------------- RCA reference demodulator --------------------- */

/* Independent demodulation of the OTA stream against the RCA protocol
* definition (irmpprotocols.h constants): start pulse/space, then 24 bits,
* MSB first, pulse constant, space length carries the bit. */
static int rca_demod(const uint16_t *buf, uint16_t len, uint32_t *out_bits)
{
    int i;
    uint32_t bits = 0;
    int nbits = 0;
    unsigned start_pulse, start_space;
    long up;

    /* skip a leading pause (odd-length frames start with the inter-frame
    * pause); find the first mark */
    i = 0;
    if ( len & 1u )
        i = 1; /* entry 0 is the leading space */
    if ( (i + 1) >= len )
        return 1;
    up = (long)buf[i] * 10;
    start_pulse = (unsigned)up;
    start_space = (unsigned)((long)buf[i + 1] * 10);
    if ( start_pulse < RCA_START_BIT_PULSE_TIME * 7 / 10 ||
         start_pulse > RCA_START_BIT_PULSE_TIME * 13 / 10 ||
         start_space < RCA_START_BIT_PAUSE_TIME * 7 / 10 ||
         start_space > RCA_START_BIT_PAUSE_TIME * 13 / 10 )
        return 2; /* start timings out of the RCA definition */
    for (i += 2; (i + 1) < len && nbits < 24; i += 2)
    {
        unsigned pulse = (unsigned)((long)buf[i] * 10);
        unsigned space = (unsigned)((long)buf[i + 1] * 10);
        if ( pulse < RCA_PULSE_TIME * 7 / 10 || pulse > RCA_PULSE_TIME * 13 / 10 )
            return 3; /* bit pulse out of the RCA definition */
        bits <<= 1;
        if ( space >= (RCA_0_PAUSE_TIME + RCA_1_PAUSE_TIME) / 2 )
            bits |= 1u;
        nbits++;
    }
    if ( nbits != 24 )
        return 4;
    *out_bits = bits;
    return 0;
}

/* --------------------------------- main ---------------------------------- */

int main(void)
{
    S_M1_IR_File_Signal_t sig;
    int n;
    uint8_t proto;

    irsnd_init(&stub_timer, 1);
    stub_timer.Instance = &stub_tim_regs;

    /* ---- NEC (official vocabulary): 8-bit addr/cmd, inverted copies ---- */
    if ( load_ir_signal("NEC", "84 00 00 00", "12 00 00 00", &sig) == 0 )
    {
        CHECK_EQ(sig.protocol, IRMP_NEC8_PROTOCOL, "NEC maps to the NEC8 encoder");
        n = transmit_and_decode(&sig, 0);
        CHECK(n >= 1, "NEC frame decodes");
        if ( n >= 1 )
        {
            CHECK_EQ(decodes[0].protocol, IRMP_NEC_PROTOCOL, "NEC decodes in the NEC family");
            CHECK_EQ(wire_b0(decodes[0].address), 0x84u, "NEC wire byte0 = address");
            CHECK_EQ(wire_b1(decodes[0].address), (~0x84u) & 0xFFu, "NEC wire byte1 = inverted address");
            /* the decoder only reports NEC after validating cmd == ~~cmd,
            * and then strips the inverted copy: the surviving byte IS the
            * transmitted command, so these two asserts prove byte2/byte3 */
            CHECK_EQ(decodes[0].command, 0x12u, "NEC command survives (inversion enforced)");
        }
    }

    /* ---- NECext: 16-bit address verbatim on the wire -------------------- */
    if ( load_ir_signal("NECext", "47 91 00 00", "C8 05 00 00", &sig) == 0 )
    {
        CHECK_EQ(sig.protocol, IRMP_NEC_PROTOCOL, "NECext maps to the extended-NEC encoder");
        n = transmit_and_decode(&sig, 0);
        CHECK(n >= 1, "NECext frame decodes");
        if ( n >= 1 )
        {
            CHECK_EQ(decodes[0].protocol, IRMP_NEC_PROTOCOL, "NECext stays in the NEC family");
            CHECK_EQ(wire_b0(decodes[0].address), 0x47u, "NECext wire byte0 = first address byte (file order)");
            CHECK_EQ(wire_b1(decodes[0].address), 0x91u, "NECext wire byte1 = second address byte (file order)");
            /* The 16-bit address round-trips VERBATIM -- the property the
            * fork's NEC aliasing destroyed (it truncated to 8 bits). */
            CHECK_EQ(decodes[0].address, sig.address, "NECext 16-bit address round-trips verbatim");
            CHECK_EQ(decodes[0].command, sig.command & 0xFFu,
                     "NECext command survives (inverted low byte enforced, documented stack behavior)");
        }
    }

    /* ---- SIRC (12-bit): cmd7 + addr5 ------------------------------------ */
    if ( load_ir_signal("SIRC", "17 00 00 00", "15 00 00 00", &sig) == 0 )
    {
        n = transmit_and_decode(&sig, 0);
        CHECK(n >= 1, "SIRC frame decodes");
        if ( n >= 1 )
        {
            CHECK_EQ(decodes[0].protocol, IRMP_SIRCS_PROTOCOL, "SIRC decodes as SIRCS");
            CHECK_EQ(decodes[0].address, 0x17u & 0x1Fu, "SIRC 5-bit address survives");
            CHECK_EQ(decodes[0].command, 0x15u & 0x7Fu, "SIRC 7-bit command survives");
        }
    }

    /* ---- SIRC15 (15-bit): cmd7 + addr8 ---------------------------------- */
    if ( load_ir_signal("SIRC15", "A1 00 00 00", "2E 00 00 00", &sig) == 0 )
    {
        CHECK_EQ(sig.protocol, IRMP_SIRCS15_PROTOCOL, "SIRC15 maps to the 15-bit Sony encoder");
        n = transmit_and_decode(&sig, 0);
        CHECK(n >= 1, "SIRC15 frame decodes");
        if ( n >= 1 )
        {
            CHECK_EQ(decodes[0].protocol, IRMP_SIRCS_PROTOCOL, "SIRC15 decodes in the SIRCS family");
            CHECK_EQ(decodes[0].address, 0xA1u, "SIRC15 8-bit address survives (15-bit frame)");
            CHECK_EQ(decodes[0].command, 0x2Eu & 0x7Fu, "SIRC15 7-bit command survives");
        }
    }

    /* ---- SIRC20 (20-bit): cmd7 + addr13 --------------------------------- */
    if ( load_ir_signal("SIRC20", "34 12 00 00", "07 00 00 00", &sig) == 0 )
    {
        CHECK_EQ(sig.protocol, IRMP_SIRCS20_PROTOCOL, "SIRC20 maps to the 20-bit Sony encoder");
        n = transmit_and_decode(&sig, 0);
        CHECK(n >= 1, "SIRC20 frame decodes");
        if ( n >= 1 )
        {
            CHECK_EQ(decodes[0].protocol, IRMP_SIRCS_PROTOCOL, "SIRC20 decodes in the SIRCS family");
            CHECK_EQ(decodes[0].address, 0x1234u, "SIRC20 13-bit address survives (20-bit frame)");
            CHECK_EQ(decodes[0].command, 0x07u & 0x7Fu, "SIRC20 7-bit command survives");
        }
    }

    /* ---- RC5 and RC5X ---------------------------------------------------- */
    if ( load_ir_signal("RC5", "0C 00 00 00", "11 00 00 00", &sig) == 0 )
    {
        n = transmit_and_decode(&sig, 1);
        CHECK(n >= 1, "RC5 frame decodes");
        if ( n >= 1 )
        {
            CHECK_EQ(decodes[0].protocol, IRMP_RC5_PROTOCOL, "RC5 decodes as RC5");
            CHECK_EQ(decodes[0].address & 0x1Fu, 0x0Cu, "RC5 5-bit address survives (toggle stripped)");
            CHECK_EQ(decodes[0].command & 0x3Fu, 0x11u, "RC5 6-bit command survives");
        }
    }
    if ( load_ir_signal("RC5X", "05 00 00 00", "23 00 00 00", &sig) == 0 )
    {
        CHECK_EQ(sig.protocol, IRMP_RC5X_PROTOCOL, "RC5X maps to the RC5X encoder");
        n = transmit_and_decode(&sig, 1);
        CHECK(n >= 1, "RC5X frame decodes");
        if ( n >= 1 )
        {
            CHECK_EQ(decodes[0].protocol, IRMP_RC5_PROTOCOL, "RC5X decodes in the RC5 family (vendored normalization)");
            CHECK_EQ(decodes[0].address & 0x1Fu, 0x05u, "RC5X address survives");
            CHECK_EQ(decodes[0].command & 0x3Fu, 0x23u, "RC5X 6-bit command part survives");
        }
    }

    /* ---- RCA: no decoder in the vendored IRMP -> wire-level reference ---- */
    if ( load_ir_signal("RCA", "09 00 00 00", "3B 00 00 00", &sig) == 0 )
    {
        IRMP_DATA d;
        uint32_t bits = 0;
        int rc;

        CHECK_EQ(sig.protocol, IRMP_RCA_PROTOCOL, "RCA maps to the RCA encoder");
        memset(&d, 0, sizeof d);
        d.protocol = sig.protocol;
        d.address = sig.address;
        d.command = sig.command;
        d.duty_cycle = sig.duty_cycle;
        d.frequency = sig.frequency;
        d.flags = 1;
        irsnd_init(&stub_timer, 1); /* the firmware re-inits IRSND before every send */
        stub_timer.Instance = &stub_tim_regs;
        CHECK(irsnd_generate_tx_data(d) != 0, "RCA TX data generated");
        CHECK(m1_make_ir_ota_multiframes() != 0, "RCA OTA frame built");
        rc = rca_demod(m1_get_ir_ota_buffer_ptr(), m1_get_ir_ota_frame_len(), &bits);
        tx_reset();
        CHECK_EQ(rc, 0, "RCA OTA stream matches the RCA protocol definition");
        if ( rc == 0 )
        {
            /* irsnd RCA layout (tx[0..2]): AAAA CCCC | CCCC aaaa | cccccccc */
            uint32_t expect = ((uint32_t)(sig.address & 0x0Fu) << 20) |
                              ((uint32_t)(sig.command & 0xFFu) << 12) |
                              (((uint32_t)(~sig.address) & 0x0Fu) << 8) |
                              ((uint32_t)(~sig.command) & 0xFFu);
            CHECK_EQ(bits, expect, "RCA wire bits = AAAA CCCC CCCC aaaa cccccccc");
        }
    }

    /* ---- strict-skip on the TX path: unknown name never yields data ------ */
    {
        str_io_t io;
        S_M1_IR_File_Entry_t entry;
        S_M1_IR_File_Stats_t stats;
        static const char *cassette =
            "Filetype: IR signals file\r\nVersion: 1\r\n"
            "name: Ghost\r\ntype: parsed\r\nprotocol: Cassette\r\n"
            "address: 01 00 00 00\r\ncommand: 02 00 00 00\r\n";
        io.p = cassette;
        io.pos = 0;
        n = (int)m1_ir_file_parse_index(str_read, &io, &entry, 1, &stats, NULL);
        CHECK_EQ(n, 0, "unknown protocol yields no transmittable signal");
        CHECK_EQ(stats.skipped_unknown_protocol, 1u, "unknown protocol counted");
    }

    /* ---- the map itself: previously-wrong names are distinct entries ----- */
    {
        uint8_t p_nec, p_necext, p_rc5x, p_s15, p_s20, p_rca;
        CHECK(ir_protocol_map_lookup("NEC", &p_nec) == 0, "NEC resolves");
        CHECK(ir_protocol_map_lookup("NECext", &p_necext) == 0, "NECext resolves");
        CHECK(ir_protocol_map_lookup("RC5X", &p_rc5x) == 0, "RC5X resolves");
        CHECK(ir_protocol_map_lookup("SIRC15", &p_s15) == 0, "SIRC15 resolves");
        CHECK(ir_protocol_map_lookup("SIRC20", &p_s20) == 0, "SIRC20 resolves");
        CHECK(ir_protocol_map_lookup("RCA", &p_rca) == 0, "RCA resolves");
        CHECK(p_nec != p_necext, "NEC and NECext are NOT aliased (fork defect fixed)");
        CHECK(p_rc5x != p_nec, "RC5X distinct");
        CHECK(p_s15 != p_s20, "SIRC15 and SIRC20 are NOT aliased (fork defect fixed)");
        CHECK(p_rca != 0, "RCA is a real protocol number");
    }

    printf(failures ? "m1_ir_golden_tx: %d FAILURE(S)\n" : "m1_ir_golden_tx: all vectors passed\n", failures);
    return failures ? 1 : 0;
}
