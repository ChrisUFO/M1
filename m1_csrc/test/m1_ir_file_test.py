#!/usr/bin/env python3
"""Host tests for Infrared/m1_ir_file.c (streaming .ir loader, issue #43 / PR-4).

Compiles the REAL pure parsing core (extracted verbatim between the
M1_IR_FILE_PURE_CORE markers in Infrared/m1_ir_file.c) together with the REAL
protocol mapping table + lookup (extracted between the IR_PROTOCOL_MAP_PURE
markers in Infrared/m1_ir_remotes.c) and exercises it against in-memory .ir
sources: index pass, on-demand per-block load, tolerance (blank lines,
comments, CRLF, overlong lines, malformed blocks), bounded-memory raw
handling, and the strict-skip policy for unknown protocol names.

From the repository root: python3 m1_csrc/test/m1_ir_file_test.py
"""

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IR_FILE_C = ROOT / "Infrared" / "m1_ir_file.c"
IR_REMOTES_C = ROOT / "Infrared" / "m1_ir_remotes.c"
IR_INCLUDE = ROOT / "Infrared"

PURE_BEGIN = (
    "/* ===================== M1_IR_FILE_PURE_CORE_BEGIN ===================== */"
)
PURE_END = (
    "/* ====================== M1_IR_FILE_PURE_CORE_END ====================== */"
)
MAP_BEGIN = (
    "/* ===================== IR_PROTOCOL_MAP_PURE_BEGIN ===================== */"
)
MAP_END = "/* ====================== IR_PROTOCOL_MAP_PURE_END ====================== */"

# IRMP protocol constants the extracted map region references. The numbers are
# asserted against the vendored irmpprotocols.h in the golden-decode test; the
# only property this test relies on is that distinct names map to distinct
# numbers and UNKNOWN is 0.
IRMP_CONSTANTS = """
#define IRMP_UNKNOWN_PROTOCOL 0
#define IRMP_SIRCS_PROTOCOL 1
#define IRMP_NEC_PROTOCOL 2
#define IRMP_SAMSUNG_PROTOCOL 3
#define IRMP_KASEIKYO_PROTOCOL 4
#define IRMP_RC5_PROTOCOL 5
#define IRMP_RC6_PROTOCOL 6
#define IRMP_SAMSUNG32_PROTOCOL 7
#define IRMP_NEC42_PROTOCOL 8
#define IRMP_NEC8_PROTOCOL 9
#define IRMP_RCA_PROTOCOL 10
#define IRMP_PIONEER_PROTOCOL 11
#define IRMP_SIRCS15_PROTOCOL 12
#define IRMP_SIRCS20_PROTOCOL 13
#define IRMP_RC5X_PROTOCOL 14
#define IRMP_RAW_PROTOCOL 15
"""

# In-memory .ir sources. CRLF everywhere to prove CRLF tolerance.
FILE1 = (
    "Filetype: IR signals file\r\n"
    "Version: 1\r\n"
    "\r\n"
    "# leading comment\r\n"
    "name: Power\r\n"
    "type: parsed\r\n"
    "protocol: NECext\r\n"
    "address: 12 34 00 00\r\n"
    "command: 56 78 00 00\r\n"
    "# comment inside a block\r\n"
    "name: Vol_up\r\n"
    "type: raw\r\n"
    "frequency: 38000\r\n"
    "duty_cycle: 0.330000\r\n"
    "data: 175 7436 179 4913 175 600\r\n"
    "name: Ghost\r\n"
    "type: parsed\r\n"
    "protocol: Cassette\r\n"
    "address: 01 00 00 00\r\n"
    "command: 02 00 00 00\r\n"
    "name: Broken\r\n"
    "type: parsed\r\n"
    "protocol: NEC\r\n"
    "command: 02 00 00 00\r\n"
    "name: Last"
)

GHOST_BLOCK = (
    "name: Ghost\r\n"
    "type: parsed\r\n"
    "protocol: Cassette\r\n"
    "address: 01 00 00 00\r\n"
    "command: 02 00 00 00\r\n"
)

UNK_BLOCK = (
    "name: X\r\n"
    "type: parsed\r\n"
    "protocol: UNKNOWN\r\n"
    "address: 01 00 00 00\r\n"
    "command: 02 00 00 00\r\n"
)

BAD_ADDR_FILE = (
    "Filetype: IR signals file\r\nVersion: 1\r\n"
    "name: A\r\ntype: parsed\r\nprotocol: NEC\r\naddress: XYZ\r\ncommand: 02 00 00 00\r\n"
)
BAD_HEADER_FILE = "Filetype: something else\r\nVersion: 1\r\n"
BAD_VERSION_FILE = "Filetype: IR signals file\r\nVersion: 2\r\n"
NO_VERSION_FILE = (
    "Filetype: IR signals file\r\nname: A\r\ntype: parsed\r\nprotocol: NEC\r\n"
    "address: 01 00 00 00\r\ncommand: 02 00 00 00\r\n"
)
EXTRA_KEY_FILE = (
    "Filetype: IR signals file\r\nVersion: 1\r\n"
    "custom_field: whatever\r\n"
    "name: A\r\ntype: parsed\r\nprotocol: RC5X\r\n"
    "address: 01 00 00 00\r\ncommand: 02 00 00 00\r\nanother_key: 5\r\n"
)
BIG_RAW_FILE = (
    "Filetype: IR signals file\r\nVersion: 1\r\n"
    "name: R\r\ntype: raw\r\ndata: 100 200 300 400 500 600 700 800\r\n"
)
# Odd sample count N=3: 1 leading pause + 3 samples = 4 (even) -> pad due at
# finish time. With raw_ticks_cap == 4 the pad has no room (the cap-exact-even
# boundary); with cap == 5 it pads to an odd count.
EXACT_EVEN_RAW_FILE = (
    "Filetype: IR signals file\r\nVersion: 1\r\n"
    "name: E\r\ntype: raw\r\ndata: 100 200 300\r\n"
)
STRAY_DATA_FILE = (
    "Filetype: IR signals file\r\nVersion: 1\r\n"
    "name: A\r\ntype: parsed\r\nprotocol: SIRC\r\ndata: 1 2 3\r\n"
    "address: 01 00 00 00\r\ncommand: 02 00 00 00\r\n"
)
# The one documented exception to any-order fields: "data:" before "type:
# raw" -- the samples arrive before the block type is known, so the line is
# dropped and the block skips as malformed (safe, counted, never garbage).
DATA_FIRST_FILE = (
    "Filetype: IR signals file\r\nVersion: 1\r\n"
    "name: D\r\ndata: 100 200 300\r\ntype: raw\r\n"
)

# The C harness. %s placeholders are filled with the C-escaped sources above;
# this keeps every backslash sequence under python's control (no raw-string
# ambiguity, no shell mangling).
HARNESS_MAIN_TEMPLATE = r"""
/* ---- in-memory character source ---- */
typedef struct { const char *p; size_t pos; } str_io_t;
static int32_t str_read(void *ctx, char *dst, int32_t len)
{
    str_io_t *s = (str_io_t *)ctx;
    size_t avail = strlen(s->p) - s->pos;
    size_t n = avail < (size_t)len ? avail : (size_t)len;
    if (n) { memcpy(dst, s->p + s->pos, n); s->pos += n; }
    return (int32_t)n;
}

static char notice_name[64];
static uint32_t notice_offset;
static uint32_t notice_calls;
static void notice_cb(const char *name, uint32_t offset)
{
    if (notice_calls == 0) {
        snprintf(notice_name, sizeof notice_name, "%s", name);
        notice_offset = offset;
    }
    notice_calls++;
}

static int failures;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)
#define CHECK_EQ(a, b, msg) do { \
    long _a = (long)(a), _b = (long)(b); \
    if (_a != _b) { printf("FAIL %s:%d %s (%ld != %ld)\n", __FILE__, __LINE__, msg, _a, _b); failures++; } \
} while (0)

static const char *S_FILE1 = "@FILE1@";
static const char *S_GHOST = "@GHOST@";
static const char *S_UNK = "@UNK@";
static const char *S_BAD_ADDR = "@BAD_ADDR@";
static const char *S_BAD_HEADER = "@BAD_HEADER@";
static const char *S_BAD_VERSION = "@BAD_VERSION@";
static const char *S_NO_VERSION = "@NO_VERSION@";
static const char *S_EXTRA_KEY = "@EXTRA_KEY@";
static const char *S_BIG_RAW = "@BIG_RAW@";
static const char *S_EXACT_EVEN_RAW = "@EXACT_EVEN_RAW@";
static const char *S_STRAY_DATA = "@STRAY_DATA@";
static const char *S_DATA_FIRST = "@DATA_FIRST@";

int main(void)
{
    S_M1_IR_File_Entry_t entries[8];
    S_M1_IR_File_Stats_t stats;
    S_M1_IR_File_Signal_t sig;
    static uint16_t ticks[16];
    static uint16_t small[4];
    static char overlong[900];
    int32_t n, r;
    int i;

    /* ---------- 1: happy path, tolerance, strict-skip ---------- */
    str_io_t io1 = { S_FILE1, 0 };
    notice_calls = 0; notice_name[0] = 0;
    n = m1_ir_file_parse_index(str_read, &io1, entries, 8, &stats, notice_cb);
    CHECK_EQ(n, 2, "index count (strict-skip removes Cassette, malformed removes Broken/Last)");
    CHECK_EQ(stats.blocks_seen, 5, "blocks_seen");
    CHECK_EQ(stats.blocks_indexed, 2, "blocks_indexed");
    CHECK_EQ(stats.skipped_unknown_protocol, 1, "unknown protocol skipped");
    CHECK_EQ(stats.skipped_malformed, 2, "Broken (no address) + Last (no type) malformed");
    CHECK_EQ(stats.index_full, 0, "index not full");
    CHECK_EQ(notice_calls, 1, "notice hook called once");
    CHECK(strcmp(notice_name, "Cassette") == 0, "notice carries the unknown name");
    CHECK(notice_offset > 0, "notice carries the block offset");

    CHECK(strcmp(entries[0].name, "Power") == 0, "entry 0 name");
    CHECK(strcmp(entries[0].protocol, "NECext") == 0, "entry 0 protocol");
    CHECK_EQ(entries[0].address, 0x00003412UL, "entry 0 address little endian");
    CHECK_EQ(entries[0].flags,
             (M1_IR_FILE_FLAG_PARSED | M1_IR_FILE_FLAG_HAS_ADDR | M1_IR_FILE_FLAG_HAS_CMD),
             "entry 0 flags");
    CHECK(strcmp(entries[1].name, "Vol_up") == 0, "entry 1 name");
    CHECK(strcmp(entries[1].protocol, "raw") == 0, "entry 1 protocol is raw");
    CHECK_EQ(entries[1].flags, M1_IR_FILE_FLAG_RAW, "entry 1 flags");

    /* offsets must point at the "name:" lines (verifiable by reloading) */
    io1.pos = entries[0].file_offset;
    r = m1_ir_file_parse_signal(str_read, &io1, entries[0].name, &sig, NULL, 0);
    CHECK_EQ(r, 0, "load entry 0");
    CHECK_EQ(sig.protocol, IRMP_NEC_PROTOCOL, "NECext resolves through the real map");
    CHECK_EQ(sig.address, 0x00003412UL, "signal address");
    CHECK_EQ(sig.command, 0x00007856UL, "signal command");
    CHECK_EQ(sig.frequency, 38000UL, "default frequency");
    CHECK_EQ(sig.duty_cycle, 33, "default duty cycle");
    CHECK(strcmp(sig.protocol_name, "NECext") == 0, "signal protocol name");

    /* parsed load with a raw buffer supplied: raw fields stay zero (they are
     * raw-block fields), so a flag-forgetting consumer can never transmit
     * the bare leading pause the loader wrote into the buffer */
    io1.pos = entries[0].file_offset;
    r = m1_ir_file_parse_signal(str_read, &io1, entries[0].name, &sig, ticks, 16);
    CHECK_EQ(r, 0, "parsed load with a raw buffer supplied");
    CHECK_EQ(sig.raw_count, 0, "parsed load publishes no raw ticks");

    /* stale-index guard: a different block at the seek offset is refused */
    io1.pos = entries[0].file_offset;
    r = m1_ir_file_parse_signal(str_read, &io1, entries[1].name, &sig, NULL, 0);
    CHECK_EQ(r, -(long)M1_IR_FILE_ERR_MALFORMED, "expected-name mismatch refuses the load");

    io1.pos = entries[1].file_offset;
    r = m1_ir_file_parse_signal(str_read, &io1, entries[1].name, &sig, ticks, 16);
    CHECK_EQ(r, 0, "load raw entry");
    CHECK_EQ(sig.flags & M1_IR_FILE_FLAG_RAW, M1_IR_FILE_FLAG_RAW, "raw flag");
    CHECK_EQ(sig.frequency, 38000UL, "raw frequency honored");
    CHECK_EQ(sig.duty_cycle, 33, "raw duty honored");
    /* leading pause + 6 samples = 7 entries, odd by construction: no pad */
    CHECK_EQ(sig.raw_count, 7, "raw tick count (pause + samples)");
    CHECK_EQ(sig.raw_count & 1u, 1u, "odd OTA invariant");
    CHECK_EQ(ticks[0], 10000, "leading 100 ms pause");
    CHECK_EQ(ticks[1], 17, "175 us in ticks (floor, like the legacy parser)");
    CHECK_EQ(ticks[2], 743, "7436 us in ticks");
    CHECK_EQ(ticks[6], 60, "600 us in ticks");
    CHECK(sig.raw_ticks == ticks, "raw buffer is the caller's");

    /* ---------- 2: strict-skip on load ---------- */
    str_io_t io2 = { S_GHOST, 0 };
    memset(&sig, 0, sizeof sig);
    r = m1_ir_file_parse_signal(str_read, &io2, NULL, &sig, NULL, 0);
    CHECK_EQ(r, -(long)M1_IR_FILE_ERR_UNKNOWN_PROTOCOL, "load refuses unknown protocol");
    CHECK_EQ(sig.protocol, 0, "no protocol number on refusal");

    str_io_t io2b = { S_UNK, 0 };
    r = m1_ir_file_parse_signal(str_read, &io2b, NULL, &sig, NULL, 0);
    CHECK_EQ(r, -(long)M1_IR_FILE_ERR_UNKNOWN_PROTOCOL, "sentinel name refused");

    /* ---------- 3: malformed variants never crash, always counted ---------- */
    str_io_t io3 = { S_BAD_ADDR, 0 };
    n = m1_ir_file_parse_index(str_read, &io3, entries, 8, &stats, NULL);
    CHECK_EQ(n, 0, "bad address -> nothing indexed");
    CHECK_EQ(stats.skipped_malformed, 1, "bad address counted");

    str_io_t io4 = { S_BAD_HEADER, 0 };
    n = m1_ir_file_parse_index(str_read, &io4, entries, 8, &stats, NULL);
    CHECK_EQ(n, -(long)M1_IR_FILE_ERR_HEADER, "wrong Filetype rejected");

    str_io_t io5 = { S_BAD_VERSION, 0 };
    n = m1_ir_file_parse_index(str_read, &io5, entries, 8, &stats, NULL);
    CHECK_EQ(n, -(long)M1_IR_FILE_ERR_HEADER, "future Version rejected");

    str_io_t io6 = { S_NO_VERSION, 0 };
    n = m1_ir_file_parse_index(str_read, &io6, entries, 8, &stats, NULL);
    CHECK_EQ(n, -(long)M1_IR_FILE_ERR_HEADER, "missing Version rejected");

    /* overlong name line (300 chars): truncated + flagged, still indexed */
    {
        static const char longname[300] = {
            [0 ... 298] = 'L', [299] = 0
        };
        snprintf(overlong, sizeof overlong,
                 "Filetype: IR signals file\r\nVersion: 1\r\n"
                 "name: %s\r\ntype: parsed\r\nprotocol: NEC\r\n"
                 "address: 01 00 00 00\r\ncommand: 02 00 00 00\r\n", longname);
    }
    str_io_t io7 = { overlong, 0 };
    n = m1_ir_file_parse_index(str_read, &io7, entries, 8, &stats, NULL);
    CHECK_EQ(n, 1, "overlong name still indexes (truncated)");
    CHECK_EQ(entries[0].flags & M1_IR_FILE_FLAG_NAME_TRUNC, M1_IR_FILE_FLAG_NAME_TRUNC, "truncation flagged");
    CHECK_EQ(strlen(entries[0].name), M1_IR_FILE_NAME_LEN_MAX, "name truncated at cap");

    str_io_t io8 = { S_EXTRA_KEY, 0 };
    n = m1_ir_file_parse_index(str_read, &io8, entries, 8, &stats, NULL);
    CHECK_EQ(n, 1, "unknown keys tolerated");
    CHECK(strcmp(entries[0].protocol, "RC5X") == 0, "RC5X indexed");

    /* ---------- 4: bounded raw memory ---------- */
    str_io_t io9 = { S_BIG_RAW, 0 };
    n = m1_ir_file_parse_index(str_read, &io9, entries, 8, &stats, NULL);
    CHECK_EQ(n, 1, "raw block indexes without storing samples");

    str_io_t io10 = { S_BIG_RAW, 0 };
    io10.pos = entries[0].file_offset;
    r = m1_ir_file_parse_signal(str_read, &io10, NULL, &sig, small, 4);
    CHECK_EQ(r, -(long)M1_IR_FILE_ERR_RAW_OVERFLOW, "raw overflow reported");
    CHECK_EQ(small[0], 10000, "bounded buffer holds the leading pause first");
    for (i = 1; i < 4; i++) CHECK(small[i] <= 80, "no out-of-bounds tick value");

    /* cap-exact-even boundary: an odd sample count N that fills the buffer
     * exactly (1 leading pause + N == cap) leaves no room for the odd-pad.
     * Returning success there would publish an even raw_count, which every
     * TX consumer reads as "entry 0 is a mark" and polarity-shifts the whole
     * frame -- overflow is the only safe answer. */
    memset(&sig, 0, sizeof sig);
    str_io_t io10b = { S_EXACT_EVEN_RAW, 0 };
    r = m1_ir_file_parse_signal(str_read, &io10b, NULL, &sig, small, 4); /* cap = 1 + N */
    CHECK_EQ(r, -(long)M1_IR_FILE_ERR_RAW_OVERFLOW, "cap-exact-even raw overflows (pad has no room)");
    CHECK_EQ(sig.raw_count, 0, "no even raw_count published");
    CHECK_EQ(small[3], 30, "all N samples stored before the failed pad");

    str_io_t io10c = { S_EXACT_EVEN_RAW, 0 };
    memset(&sig, 0, sizeof sig);
    r = m1_ir_file_parse_signal(str_read, &io10c, "E", &sig, ticks, 5); /* cap = 2 + N */
    CHECK_EQ(r, 0, "one slot of headroom: padded odd success");
    CHECK_EQ(sig.raw_count, 5, "pause + 3 samples + pad");
    CHECK_EQ(sig.raw_count & 1u, 1u, "odd OTA invariant upheld");
    CHECK_EQ(ticks[0], 10000, "leading 100 ms pause first");
    CHECK_EQ(ticks[4], 56, "trailing 560 us pad in ticks");

    str_io_t io11 = { S_STRAY_DATA, 0 };
    n = m1_ir_file_parse_index(str_read, &io11, entries, 8, &stats, NULL);
    CHECK_EQ(n, 1, "stray data in parsed block tolerated (line ignored)");
    CHECK(strcmp(entries[0].protocol, "SIRC") == 0, "SIRC indexed");

    /* documented any-order exception: data: before type: raw drops the
     * samples (block type not yet known) and skips the block malformed */
    str_io_t io11b = { S_DATA_FIRST, 0 };
    n = m1_ir_file_parse_index(str_read, &io11b, entries, 8, &stats, NULL);
    CHECK_EQ(n, 0, "data: before type: raw is not indexed");
    CHECK_EQ(stats.blocks_seen, 1, "data-first block seen");
    CHECK_EQ(stats.skipped_malformed, 1, "data-first block counted malformed");

    /* ---------- 5: index capacity bound ---------- */
    str_io_t io12 = { S_FILE1, 0 };
    n = m1_ir_file_parse_index(str_read, &io12, entries, 1, &stats, NULL);
    CHECK_EQ(n, 1, "capacity-bounded index");
    CHECK_EQ(stats.index_full, 1, "overflowing blocks counted");

    /* ---------- 6: every official name resolves through the real map ---------- */
    {
        static const char *names[] = {
            "NEC", "NECext", "RC5X", "SIRC15", "SIRC20", "RCA",
            "SIRC", "RC5", "RC6", "NEC42", "Samsung32", "Kaseikyo",
            "Pioneer", "raw"
        };
        uint8_t num, first = 0;
        for (i = 0; i < (int)(sizeof names / sizeof names[0]); i++) {
            CHECK(ir_protocol_map_lookup(names[i], &num) == 0, "official name resolves");
            CHECK(num != IRMP_UNKNOWN_PROTOCOL, "resolves to a real protocol number");
            if (i == 0) first = num;
            else CHECK(num != first || i == 0, "names resolve distinctly");
        }
        CHECK(ir_protocol_map_lookup("Cassette", &num) != 0, "unknown name rejected by map");
        CHECK(ir_protocol_map_lookup("nec", &num) != 0, "lookup is case sensitive (documented)");
        CHECK(ir_protocol_map_lookup(NULL, &num) != 0, "NULL name rejected");
        CHECK(ir_protocol_map_lookup("NEC", NULL) != 0, "NULL out rejected");
    }

    printf(failures ? "m1_ir_file_test: %d FAILURE(S)\n" : "m1_ir_file_test: all checks passed\n", failures);
    return failures ? 1 : 0;
}
"""


def c_escape(s: str) -> str:
    """Escape a python string as the body of a C string literal."""
    out = []
    for ch in s:
        if ch == "\r":
            out.append("\\r")
        elif ch == "\n":
            out.append("\\n")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        else:
            out.append(ch)
    return "".join(out)


def extract(path: Path, begin: str, end: str) -> str:
    text = path.read_text(encoding="utf-8")
    try:
        return text.split(begin, 1)[1].split(end, 1)[0]
    except IndexError:
        sys.exit(f"m1_ir_file_test: markers not found in {path}")


def find_compiler() -> str:
    import shutil

    for candidate in ("gcc", "cc", "clang"):
        if shutil.which(candidate):
            return candidate
    sys.exit("m1_ir_file_test: no host C compiler (gcc/cc/clang) found")


def main() -> int:
    core = extract(IR_FILE_C, PURE_BEGIN, PURE_END)
    table = extract(IR_REMOTES_C, MAP_BEGIN, MAP_END)

    harness = HARNESS_MAIN_TEMPLATE
    for token, value in (
        ("@FILE1@", FILE1),
        ("@GHOST@", GHOST_BLOCK),
        ("@UNK@", UNK_BLOCK),
        ("@BAD_ADDR@", BAD_ADDR_FILE),
        ("@BAD_HEADER@", BAD_HEADER_FILE),
        ("@BAD_VERSION@", BAD_VERSION_FILE),
        ("@NO_VERSION@", NO_VERSION_FILE),
        ("@EXTRA_KEY@", EXTRA_KEY_FILE),
        ("@BIG_RAW@", BIG_RAW_FILE),
        ("@EXACT_EVEN_RAW@", EXACT_EVEN_RAW_FILE),
        ("@STRAY_DATA@", STRAY_DATA_FILE),
        ("@DATA_FIRST@", DATA_FIRST_FILE),
    ):
        harness = harness.replace(token, c_escape(value))

    source = (
        "#include <stdint.h>\n"
        "#include <stdbool.h>\n"
        "#include <string.h>\n"
        "#include <stdio.h>\n"
        '#include "m1_ir_file.h"\n'
        + IRMP_CONSTANTS
        + "typedef struct { const char *protocol_name; uint8_t protocol_number; } "
        "S_M1_IR_Protocol_Mapping_t;\n" + core + table + harness
    )

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        src = tmp_path / "m1_ir_file_host_test.c"
        src.write_text(source, encoding="utf-8")
        exe = tmp_path / "m1_ir_file_host_test"
        cmd = [
            find_compiler(),
            "-std=c99",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(IR_INCLUDE),
            str(src),
            "-o",
            str(exe),
        ]
        build = subprocess.run(cmd, capture_output=True, text=True)
        if build.returncode != 0:
            print(build.stdout)
            print(build.stderr)
            sys.exit("m1_ir_file_test: host build failed")
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        print(run.stdout, end="")
        if run.returncode != 0:
            print(run.stderr, end="")
            sys.exit(1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
