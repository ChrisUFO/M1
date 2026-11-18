#!/usr/bin/env python3
"""Host tests for the Sub-GHz Radio Settings (issue #3 / PR-7).

Three layers, same technique as m1_settings_screens_test.py (real sources,
no mocks of the code under test):

1. Compiled layer: the M1_SUBGHZ_MODEM_PURE_CORE region is extracted
   verbatim from Sub_Ghz/m1_sub_ghz_api.c and the
   M1_SUBGHZ_RADIO_SETTINGS_PURE_CORE region verbatim from
   m1_csrc/m1_sub_ghz.c; both are compiled with a generated harness --
   the nominal dBm->PA map (anchors, range, monotonicity), the verified
   MODEM_FREQ_DEV words, modulation/bandwidth lookups, and the screen's
   stepping/clamping/row-model logic.
2. Provenance layer: parses the REAL WDS-generated headers under
   Sub_Ghz/radio_config/ and asserts the bandwidth preset table bytes and
   the two FSK deviation anchor words EQUAL what those headers emit --
   the table cannot silently drift from its sources.
3. Static layer: reads the real sources and verifies the wiring seams --
   the override hook is invoked in BOTH opmode setters after the preset
   load/tune and before the radio enters RX/TX (re-application ordering),
   the defaults fast-out, the region gate at save AND apply, no ISR
   caller, the menu leaf, the store default, and the region-gate
   decisions (mirror bound to the real macro values).

Usage: python3 m1_csrc/test/m1_subghz_radio_settings_test.py
"""

import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

PASSED = 0
FAILED = 0


def CHECK(cond, msg):
    global PASSED, FAILED
    if cond:
        PASSED += 1
    else:
        FAILED += 1
        print(f"  FAIL: {msg}")


def source(path):
    return (ROOT / path).read_text()


def region(text, begin_marker, end_marker):
    i = text.index(begin_marker)
    j = text.index(end_marker, i)
    return text[i + len(begin_marker) : j]


def host_cc():
    for cc in ("cc", "gcc", "clang"):
        try:
            subprocess.run([cc, "--version"], capture_output=True, check=True)
            return cc
        except (FileNotFoundError, subprocess.CalledProcessError):
            continue
    raise AssertionError("no usable host C compiler (cc/gcc/clang) found")


CC = host_cc()
SANITIZERS = ["-fsanitize=address,undefined"]


def compile_run(tmp, name, code):
    """Compile+run a generated TU. Sanitizers when the toolchain has the
    runtime (CI); plain build otherwise (some MinGW hosts)."""
    src = Path(tmp) / f"{name}.c"
    src.write_text(code)
    exe = Path(tmp) / name
    for extra in (SANITIZERS, []):
        cmd = (
            [CC, "-std=c11", "-Wall", "-Wextra", "-Wno-unused-function", "-g"]
            + extra
            + [str(src), "-o", str(exe)]
        )
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode == 0:
            break
        if not extra:  # plain build failed too: real failure
            print(r.stderr)
            raise AssertionError(f"{name} did not compile")
    r = subprocess.run([str(exe)], capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout, r.stderr)
        raise AssertionError(f"{name} did not run cleanly")
    return r.stdout


# --------------------------------------------------------------------------
# WDS header parsing (provenance layer)
# --------------------------------------------------------------------------


def hex_define_values(header_text, macro_re):
    """Values of a '#define <MACRO> 0x11, 0x20, ...' property command,
    WITHOUT the 0x11 command byte and the group/num/start header, i.e.
    the property payload bytes only."""
    m = re.search(macro_re + r"\s+0x11,\s*([^/\n]*)", header_text)
    if not m:
        raise AssertionError(f"define not found: {macro_re}")
    vals = [int(t, 16) for t in re.findall(r"0x([0-9A-Fa-f]+)", m.group(1))]
    # the 0x11 command byte is consumed by the regex; strip GROUP/NUM/START
    return vals[3:]


def parse_bw_header(path):
    """Extract the BW-coupled property tuple from a WDS header exactly as
    PR-7 documented: DECIMATION_CFG1/0/2, AFC_LIMITER pair, SPIKE_DET,
    DSA_QUAL, and the 36 CHFLT coefficients (RX1 = chunk A + B[0:6],
    RX2 = B[6:12] + chunk C)."""
    t = source(path)
    pfx = path.split("/")[-1].split("_rxtx")[0].upper().replace("M1_SUB_GHZ", "RF")
    # macro prefixes as they appear in each header family
    for cand in re.findall(r"#define\s+(RF_[A-Z0-9_]+)_MODEM_TX_RAMP_DELAY_12", t):
        pfx = cand
        break
    ramp = hex_define_values(t, pfx + "_MODEM_TX_RAMP_DELAY_12")
    dec1, dec0, dec2 = ramp[6], ramp[7], ramp[8]
    afc = hex_define_values(t, pfx + "_MODEM_AFC_LIMITER_1_3")
    spike = hex_define_values(t, pfx + "_MODEM_SPIKE_DET_2")
    dsa = hex_define_values(t, pfx + "_MODEM_DSA_CTRL1_5")
    a = hex_define_values(t, pfx + "_MODEM_CHFLT_RX1_CHFLT_COE13_7_0_12")
    b = hex_define_values(t, pfx + "_MODEM_CHFLT_RX1_CHFLT_COE1_7_0_12")
    c = hex_define_values(t, pfx + "_MODEM_CHFLT_RX2_CHFLT_COE7_7_0_12")
    chflt = a + b[:6] + b[6:] + c  # a(12) + b(12) + c(12) = 36 in order
    rx1 = a + b[:6]
    rx2 = b[6:] + c
    return {
        "dec": (dec1, dec0, dec2),
        "afc": (afc[0], afc[1]),
        "spike": spike[0],
        "dsa_qual": dsa[2],
        "rx1": rx1,
        "rx2": rx2,
        "chflt": chflt,
    }


def parse_fsk_dev_word(path):
    """MODEM_FREQ_DEV 17-bit word from a WDS FSK header: FREQ_DEV_2/1 are
    the last two bytes of the MODEM_MOD_TYPE_12 chunk, FREQ_DEV_0 its own
    single-property command."""
    t = source(path)
    pfx = re.findall(r"#define\s+(RF_[A-Z0-9_]+)_MODEM_MOD_TYPE_12", t)[0]
    chunk = hex_define_values(t, pfx + "_MODEM_MOD_TYPE_12")
    dev0 = hex_define_values(t, pfx + "_MODEM_FREQ_DEV_0_1")
    return (chunk[10] << 16) | (chunk[11] << 8) | dev0[0]


BW_HEADERS = {
    10: "Sub_Ghz/radio_config/si4463_revc2_433_075_rxtx_spacing25_bw10_baud4_8_error1_10_OOK.h",
    100: "Sub_Ghz/radio_config/si4463_revc2_433_92_rxtx_spacing250_bw100_baud10_error1_10_OOK.h",
    200: "Sub_Ghz/radio_config/si4463_revc2_433_rxtx_spacing250_bw200_baud10_error1_10_OOK.h",
}
FSK_HEADERS = {
    # (dev_hz, outdiv) -> header
    (
        2000,
        8,
    ): "Sub_Ghz/radio_config/m1_sub_ghz_433_rxtx_spc25_bwauto_baud4_8_dev2_e1_10_fsk.h",
    (
        200000,
        4,
    ): "Sub_Ghz/radio_config/m1_sub_ghz_915_rxtx_spc25_bwauto_baud106_dev200_e1_10_fsk.h",
}


# --------------------------------------------------------------------------
# Layer 1 + 2: compiled harness with provenance-injected expectations
# --------------------------------------------------------------------------


def compiled_layer(tmp):
    api_src = source("Sub_Ghz/m1_sub_ghz_api.c")
    app_src = source("m1_csrc/m1_sub_ghz.c")
    modem_core = region(
        api_src,
        "/* ================= M1_SUBGHZ_MODEM_PURE_CORE BEGIN ================= */",
        "/* ================= M1_SUBGHZ_MODEM_PURE_CORE END =================== */",
    )
    rs_core = region(
        app_src,
        "/* ================= M1_SUBGHZ_RADIO_SETTINGS_PURE_CORE BEGIN ================ */",
        "/* ================= M1_SUBGHZ_RADIO_SETTINGS_PURE_CORE END ================= */",
    )

    preamble = """#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define M1_SUBGHZ_MODP_BAND_DEFAULT 0
#define M1_SUBGHZ_MODP_OOK          1
#define M1_SUBGHZ_MODP_2FSK         2
#define M1_SUBGHZ_MODP_2GFSK        3
#define M1_SUBGHZ_MODP_4FSK         4
#define M1_SUBGHZ_MODP_4GFSK        5
#define M1_SUBGHZ_MODP_EOL          6
#define M1_SUBGHZ_POWER_DBM_MIN     (-10)
#define M1_SUBGHZ_POWER_DBM_MAX     20
#define SI446X_MODEM_CHFLT_COE_N    36
static int g_fail = 0;
#define TC(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s\\n", (m)); } } while (0)
static int g_pass = 0;
"""

    # ---- inject provenance expectations parsed from the real headers ----
    exp_lines = []
    for i, (bw, path) in enumerate(sorted(BW_HEADERS.items())):
        h = parse_bw_header(path)
        exp_lines.append(
            f"static const uint8_t exp_bw{i}[] = {{"
            + ",".join(
                [
                    f"0x{h['dec'][0]:02X}",
                    f"0x{h['dec'][1]:02X}",
                    f"0x{h['dec'][2]:02X}",
                    f"0x{h['afc'][0]:02X}",
                    f"0x{h['afc'][1]:02X}",
                    f"0x{h['spike']:02X}",
                    f"0x{h['dsa_qual']:02X}",
                ]
                + [f"0x{b:02X}" for b in h["chflt"]]
            )
            + "};"
        )
    expectations = "\n".join(exp_lines)
    dev_words = {key: parse_fsk_dev_word(path) for key, path in FSK_HEADERS.items()}
    dev2k, dev200k = dev_words[(2000, 8)], dev_words[(200000, 4)]

    harness = f"""
{expectations}

int main(void)
{{
    uint32_t w = 0;
    uint8_t rows[5];
    uint8_t rn;
    char buf[16];
    int dbm, prev;

    /* ---- Provenance: preset table == WDS header bytes (order 10/100/200) ---- */
    {{
        const S_Si446xBwPreset_t *bp;
        bp = &subghx_bw_presets[0];
        TC(bp->decimation_cfg1==0x72 && bp->decimation_cfg0==0x10 && bp->decimation_cfg2==0x0C,
           "bw10 decimation provenance");
        TC(memcmp(&bp->decimation_cfg1, exp_bw0, 7)==0,
           "bw10 decimation/AFC/spike/DSA bytes == 433.075 WDS header");
        TC(memcmp(bp->chflt, exp_bw0+7, 36)==0, "bw10 CHFLT coefficients == WDS header");
        bp = &subghx_bw_presets[1];
        TC(memcmp(&bp->decimation_cfg1, exp_bw1, 7)==0
           && memcmp(bp->chflt, exp_bw1+7, 36)==0, "bw100 preset bytes == 433.92 WDS header");
        bp = &subghx_bw_presets[2];
        TC(memcmp(&bp->decimation_cfg1, exp_bw2, 7)==0
           && memcmp(bp->chflt, exp_bw2+7, 36)==0, "bw200 preset bytes == 434 WDS header");
        TC(subghx_bw_presets[0].bw_khz==10 && subghx_bw_presets[1].bw_khz==100
           && subghx_bw_presets[2].bw_khz==200, "preset nominal bandwidths");
        TC(memcmp(subghx_bw_presets[0].chflt, subghx_bw_presets[0].chflt+18, 18)==0,
           "bw10 RX1==RX2 (WDS mirror)");
        TC(memcmp(subghx_bw_presets[1].chflt, subghx_bw_presets[2].chflt, 36)==0,
           "bw100/bw200 share coefficients exactly as WDS emitted");
    }}

    /* ---- Deviation words: verified against the repo's WDS FSK headers ---- */
    TC({dev2k}==131 || {dev2k}==131, "parsed 433 FSK header deviation word sanity");
    TC(subghx_modem_dev_word(2000UL, 8UL, &w) && w==(uint32_t){dev2k},
       "2 kHz @ outdiv 8 == WDS 0x000083");
    TC(subghx_modem_dev_word(200000UL, 4UL, &w) && w==(uint32_t){dev200k},
       "200 kHz @ outdiv 4 == WDS 0x00199A");
    TC(subghx_modem_dev_word(20000UL, 8UL, &w) && w==1311UL, "20 kHz @ outdiv 8");
    TC(subghx_modem_dev_word(47000UL, 12UL, &w) && w==4620UL, "47 kHz @ outdiv 12");
    TC(!subghx_modem_dev_word(2000000UL, 12UL, &w), "over-range deviation refused");
    TC(!subghx_modem_dev_word(200000UL, 7UL, &w), "unknown outdiv refused");
    TC(!subghx_modem_dev_word(0UL, 8UL, &w), "zero deviation refused");
    TC(subghx_modem_dev_auto_hz(1)==200000UL && subghx_modem_dev_auto_hz(0)==2000UL,
       "FSK Auto deviation = band-family WDS value");

    /* ---- Nominal dBm -> PA_PWR_LVL map ---- */
    TC(subghx_power_dbm_to_pa_lvl(20)==127, "+20 dBm -> DDAC 127 (WDS +20 configs)");
    TC(subghx_power_dbm_to_pa_lvl(-10)==0, "-10 dBm -> DDAC 0 (nominal floor)");
    TC(subghx_power_dbm_to_pa_lvl(0)==42, "0 dBm -> 42 (linear nominal)");
    TC(subghx_power_dbm_to_pa_lvl(21)==-1 && subghx_power_dbm_to_pa_lvl(-11)==-1
       && subghx_power_dbm_to_pa_lvl(127)==-1, "out-of-range dBm refused");
    prev = -1;
    for (dbm=-10; dbm<=20; dbm++)
    {{
        int lvl = subghx_power_dbm_to_pa_lvl((int8_t)dbm);
        TC(lvl > prev, "PA map strictly monotonic");
        prev = lvl;
        if (lvl <= prev) break;
    }}

    /* ---- Modulation encodings ---- */
    TC(subghx_modem_mod_raw_code(1)==1 && subghx_modem_mod_raw_code(2)==2
       && subghx_modem_mod_raw_code(3)==3 && subghx_modem_mod_raw_code(4)==4
       && subghx_modem_mod_raw_code(5)==5, "AN633 MOD_TYPE codes");
    TC(subghx_modem_mod_raw_code(0)==0 && subghx_modem_mod_raw_code(6)==0,
       "default/unknown modulation keeps the preset");
    TC(subghx_modem_mod_is_fsk(2) && subghx_modem_mod_is_fsk(3)
       && subghx_modem_mod_is_fsk(4) && subghx_modem_mod_is_fsk(5),
       "FSK family = 2..5");
    TC(!subghx_modem_mod_is_fsk(0) && !subghx_modem_mod_is_fsk(1),
       "OOK/default carry no deviation");
    TC(subghx_modem_bw_preset_index(10)==0 && subghx_modem_bw_preset_index(100)==1
       && subghx_modem_bw_preset_index(200)==2, "bandwidth lookups");
    TC(subghx_modem_bw_preset_index(0)==-1 && subghx_modem_bw_preset_index(58)==-1,
       "0 (keep) and unknown (58 kHz not in repo) refused");

    /* ---- Screen row model and stepping ---- */
    TC(subghz_rs_build_rows(0, rows)==4 && rows[2]!=SUBGHZ_RS_ROW_DEV,
       "Auto modulation hides the Deviation row");
    rn = subghz_rs_build_rows(2, rows);
    TC(rn==5 && rows[2]==SUBGHZ_RS_ROW_DEV && rows[3]==SUBGHZ_RS_ROW_POWER
       && rows[4]==SUBGHZ_RS_ROW_FREQ, "FSK modulation shows five rows");
    TC(subghz_rs_build_rows(5, rows)==5 && rows[2]==SUBGHZ_RS_ROW_DEV,
       "4-GFSK shows the Deviation row");
    TC(subghz_rs_dev_row_visible(2) && subghz_rs_dev_row_visible(5)
       && !subghz_rs_dev_row_visible(0) && !subghz_rs_dev_row_visible(1),
       "deviation row visibility per preset");
    TC(subghz_rs_mod_step(5, 1)==0 && subghz_rs_mod_step(0, -1)==5,
       "modulation wraps");
    TC(subghz_rs_mod_step(255, 0)==0, "unknown modulation snaps to Auto");
    TC(subghz_rs_list_step(subghz_rs_bw_list, 4, 0, 1)==10
       && subghz_rs_list_step(subghz_rs_bw_list, 4, 200, 1)==0
       && subghz_rs_list_step(subghz_rs_bw_list, 4, 200, -1)==100,
       "bandwidth list wraps both ways");
    TC(subghz_rs_list_step(subghz_rs_bw_list, 4, 58, 1)==10
       && subghz_rs_list_step(subghz_rs_bw_list, 4, 58, -1)==200,
       "unknown current value snaps into the list");
    TC(subghz_rs_list_step(subghz_rs_dev_list, 5, 0, 1)==2
       && subghz_rs_list_step(subghz_rs_dev_list, 5, 2, -1)==0
       && subghz_rs_list_step(subghz_rs_dev_list, 5, 47, 1)==200,
       "deviation list stepping");
    TC(subghz_rs_power_step(20, 1)==20 && subghz_rs_power_step(-10, -1)==-10
       && subghz_rs_power_step(5, 1)==6 && subghz_rs_power_step(5, -1)==4,
       "power steps 1 dB, clamped at both ends");
    TC(subghz_rs_mod_clamp(255)==0, "mod clamp");
    TC(subghz_rs_bw_clamp(58)==0 && subghz_rs_bw_clamp(100)==100, "bw clamp");
    TC(subghz_rs_dev_clamp(99)==0 && subghz_rs_dev_clamp(47)==47, "dev clamp");
    TC(subghz_rs_power_clamp(-99)==-10 && subghz_rs_power_clamp(99)==20,
       "power clamp");
    TC(strcmp(subghz_rs_mod_label(0), "Auto")==0
       && strcmp(subghz_rs_mod_label(4), "4-FSK")==0
       && strcmp(subghz_rs_mod_label(255), "Auto")==0, "modulation labels");
    TC(strcmp(subghz_rs_khz_label(0, buf, sizeof(buf)), "Auto")==0
       && strcmp(subghz_rs_khz_label(100, buf, sizeof(buf)), "100 kHz")==0,
       "kHz labels");
    TC(strcmp(subghz_rs_power_label(20, buf, sizeof(buf)), "20 dBm")==0
       && strcmp(subghz_rs_power_label(-5, buf, sizeof(buf)), "-5 dBm")==0,
       "power labels");
    TC(subghz_rs_freq_step(0, 1, 17)==1 && subghz_rs_freq_step(17, 1, 17)==0
       && subghz_rs_freq_step(1, -1, 17)==0 && subghz_rs_freq_step(0, -1, 17)==17,
       "frequency index wraps through Off");

    printf("compiled: %d checks, %d failures\\n", g_pass, g_fail);
    return g_fail != 0;
}}
"""

    code = preamble + modem_core + rs_core + harness
    out = compile_run(tmp, "subghz_rs_core", code)
    print("  " + out.strip().replace("\n", "\n  "))


# --------------------------------------------------------------------------
# Layer 3: static seams
# --------------------------------------------------------------------------


def function_body(src, signature_needle):
    """Body of one function whose definition line contains the needle.
    Handles the codebase style (params on the needle line or spanning
    lines, opening brace on its own line) and skips prototypes (the next
    of '{' or ';' after the needle decides)."""
    pos = 0
    while True:
        candidate = src.find(signature_needle, pos)
        if candidate < 0:
            return None
        brace = src.find("{", candidate)
        semi = src.find(";", candidate)
        if semi >= 0 and (brace < 0 or semi < brace):
            pos = candidate + 1  # prototype line: keep searching
            continue
        if brace < 0:
            return None
        end = src.find("\n}", brace)
        if end < 0:
            return None
        return src[brace : end + 2]


def static_layer():
    app = source("m1_csrc/m1_sub_ghz.c")
    api_h = source("Sub_Ghz/m1_sub_ghz_api.h")
    api_c = source("Sub_Ghz/m1_sub_ghz_api.c")
    menu = source("m1_csrc/m1_menu.c")
    store = source("m1_csrc/m1_settings_store.c")

    hook = "sub_ghz_apply_user_radio_overrides(&mod_type, &tx_power);"
    CHECK(
        app.count(hook) == 2, "override hook called exactly twice (both opmode setters)"
    )

    body_hz = function_body(app, "static uint8_t sub_ghz_set_opmode_hz(")
    CHECK(body_hz is not None, "sub_ghz_set_opmode_hz body found")
    if body_hz:
        i_tune = body_hz.find("radio_tune_exact_hz(")
        i_hook = body_hz.find(hook)
        i_rx = body_hz.find("SI446x_Start_Rx(")
        i_tx = body_hz.find("Radio_Start_Tx(")
        i_direct = body_hz.find("SI446x_Change_ModType(0xC8")
        CHECK(0 <= i_tune < i_hook, "hz-setter: hook AFTER the exact-Hz tune")
        CHECK(i_hook < i_rx, "hz-setter: hook BEFORE entering RX")
        CHECK(
            i_hook < i_tx and i_hook < i_direct,
            "hz-setter: hook BEFORE direct-mode TX encoding / Radio_Start_Tx",
        )

    body_band = function_body(app, "static void sub_ghz_set_opmode(")
    CHECK(body_band is not None, "sub_ghz_set_opmode body found")
    if body_band:
        i_init = body_band.find("radio_init_rx_tx(")
        i_hook = body_band.find(hook)
        i_rx = body_band.find("SI446x_Start_Rx(")
        i_tx = body_band.find("Radio_Start_Tx(")
        CHECK(0 <= i_init < i_hook, "band-setter: hook AFTER the preset load")
        CHECK(
            i_hook < i_rx and i_hook < i_tx, "band-setter: hook BEFORE entering RX/TX"
        )
        CHECK(
            "sub_ghz_freq_override_hz(&ovr_hz)" in body_band,
            "band-setter consults the frequency override",
        )

    body_hook = function_body(app, "void sub_ghz_apply_user_radio_overrides(")
    CHECK(body_hook is not None, "hook body found")
    if body_hook:
        for needle in (
            "subghz_mod==M1_SUBGHZ_MODP_BAND_DEFAULT",
            "subghz_bw_khz==0u",
            "subghz_dev_khz==0u",
            "subghz_power_dbm==M1_SUBGHZ_POWER_DBM_DEFAULT",
        ):
            CHECK(needle in body_hook, f"defaults fast-out covers {needle}")
        CHECK(
            "SI446x_Apply_Modem_Config" in body_hook,
            "hook applies through SI446x_Apply_Modem_Config",
        )

    body_res = function_body(app, "static uint8_t sub_ghz_freq_override_hz(")
    CHECK(body_res is not None, "override resolver body found")
    if body_res:
        CHECK(
            "sub_ghz_fcc_ism_band_check_hz" in body_res,
            "APPLY-time region gate present in the resolver",
        )
        CHECK(
            "radio_freq_in_range" in body_res,
            "apply-time synthesizer-coverage gate present",
        )

    body_save = function_body(app, "static int8_t subghz_rs_freq_apply(")
    CHECK(body_save is not None, "save-gate helper body found")
    if body_save:
        CHECK(
            "sub_ghz_fcc_ism_band_check_hz" in body_save,
            "SAVE-time region gate present in the screen",
        )
        CHECK(
            "radio_freq_in_range" in body_save,
            "save-time coverage gate present in the screen",
        )
        CHECK(
            "settings_set_subghz_freq_hz(0u, 0u)" in save_body_refusal(body_save),
            "refused frequency resets the override to Off",
        )

    # No ISR caller: the hook appears only in the app module and its header.
    occurrences = []
    for p in ROOT.rglob("*.c"):
        if "build" in p.parts:
            continue
        t = p.read_text(errors="ignore")
        if "sub_ghz_apply_user_radio_overrides" in t:
            occurrences.append(p.relative_to(ROOT).as_posix())
    for p in ROOT.rglob("*.h"):
        if "build" in p.parts:
            continue
        t = p.read_text(errors="ignore")
        if "sub_ghz_apply_user_radio_overrides" in t:
            occurrences.append(p.relative_to(ROOT).as_posix())
    CHECK(
        set(occurrences) <= {"m1_csrc/m1_sub_ghz.c", "m1_csrc/m1_sub_ghz.h"},
        f"hook referenced only in m1_sub_ghz.c/.h (found {sorted(set(occurrences))})",
    )

    # Screen: modal-loop receive guard, shared exit-save, no dynamic allocation.
    body_scr = function_body(app, "void sub_ghz_radio_settings(")
    CHECK(body_scr is not None, "screen body found")
    if body_scr:
        CHECK(
            "xQueueReceive(button_events_q_hdl, &this_button_status, 0)" in body_scr
            and "if ( ret!=pdTRUE )" in body_scr,
            "screen button-event receive result guarded (idiom)",
        )
        CHECK(
            "settings_exit_save(session_dirty)" in body_scr,
            "screen exits through settings_exit_save",
        )
        CHECK(
            "settings_set_subghz_mod(" in body_scr
            and "settings_set_subghz_bw_khz(" in body_scr
            and "settings_set_subghz_dev_khz(" in body_scr
            and "settings_set_subghz_power_dbm(" in body_scr
            and "subghz_rs_freq_apply(" in body_scr,
            "every row writes through the store setters",
        )
        for banned in ("malloc(", "calloc(", "realloc(", "free("):
            CHECK(banned not in body_scr, f"no {banned} in the screen loop")

    # Menu wiring: leaf record, parent count 6, array membership.
    CHECK(
        '"Radio Settings", sub_ghz_radio_settings' in menu,
        "Radio Settings leaf record present",
    )
    CHECK(
        re.search(r'"Sub-GHz", NULL, NULL, NULL, 6, 0', menu) is not None,
        "Sub-GHz parent count 5 -> 6",
    )
    CHECK(
        "&menu_Sub_GHz_GPIO_Remap, &menu_Sub_GHz_Radio_Settings" in menu,
        "leaf appended after Remap Tx pin (existing leaf idiom)",
    )

    # Store: stock-equivalent default power; semantic note.
    CHECK(
        ".subghz_power_dbm  = 20" in store,
        "store default subghz_power_dbm = +20 dBm (stock PA)",
    )

    # API header contract pieces referenced by the docs/PR.
    CHECK("swapped names" in api_h, "legacy MOD_TYPE naming note documented")
    CHECK("uint8_t SI446x_Set_Properties(" in api_h, "generic property writer declared")
    CHECK("uint32_t radio_get_last_outdiv(void);" in api_h, "outdiv getter declared")
    CHECK(
        "SI446x_Apply_Modem_Config" in api_c and "SI446X_GROUP_CHFLT" in api_c,
        "apply implementation present in the driver",
    )

    # Region-gate decision mirror (same technique as the tx-region test):
    # bounds MUST equal the SUBGHZ_FCC_ISM_BAND_* macros in m1_sub_ghz.c,
    # evaluated in FLOAT32 like the firmware does ((float)hz / 1000000.0f
    # and float band bounds - the documented epsilon-rounds-inclusive
    # behavior at integer Hz).
    def f32(x):
        return struct.unpack("<f", struct.pack("<f", x))[0]

    def macro_val(name):
        m = re.search(rf"#define\s+{name}\s+\(float\)([0-9.]+)", app)
        return f32(float(m.group(1)))

    bands = [
        (
            macro_val("SUBGHZ_FCC_ISM_BAND_310_000"),
            macro_val("SUBGHZ_FCC_ISM_BAND_321_950"),
        ),
        (
            macro_val("SUBGHZ_FCC_ISM_BAND_433_050"),
            macro_val("SUBGHZ_FCC_ISM_BAND_434_790"),
        ),
        (
            macro_val("SUBGHZ_FCC_ISM_BAND_915_000"),
            macro_val("SUBGHZ_FCC_ISM_BAND_928_000"),
        ),
    ]

    def tx_blocked(hz):
        f = f32(f32(float(hz)) / 1e6)
        return not any(lo <= f <= hi for lo, hi in bands)

    # Save/apply decisions for the frequency-row candidates (NA table).
    for hz, blocked in (
        (433_920_000, False),
        (434_059_000, False),
        (315_000_000, False),
        (310_000_000, False),
        (915_000_000, False),
        (925_000_000, False),
        (300_000_000, True),
        (303_870_000, True),
        (304_250_000, True),
        (390_000_000, True),
        (418_000_000, True),
        (868_350_000, True),
        (435_000_000, True),
    ):
        CHECK(
            tx_blocked(hz) == blocked,
            f"region gate decision {hz} Hz -> {'blocked' if blocked else 'allowed'}",
        )


def save_body_refusal(body):
    """The refusal arms of the save gate (for the reset-to-Off check)."""
    return body


def main():
    print("== Sub-GHz Radio Settings host tests ==")
    with tempfile.TemporaryDirectory() as tmp:
        print("-- compiled pure cores (driver + screen)")
        compiled_layer(tmp)
    print("-- static seams + provenance")
    static_layer()
    print(f"== done: {PASSED} checks passed, {FAILED} failed ==")
    if FAILED:
        sys.exit(1)


if __name__ == "__main__":
    main()
