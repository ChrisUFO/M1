#!/usr/bin/env python3
"""Host tests for Infrared/m1_ir_browser.c (SD IR database browser, PR-5).

Two layers, same technique as m1_settings_screens_test.py (real sources, no
mocks of the code under test):

1. Compiled layer: the M1_IR_BROWSER_PURE_CORE region is extracted verbatim
   from Infrared/m1_ir_browser.c and compiled with a generated harness --
   6-row pagination window math (row steps, page steps, clamping, scroll
   follow), .ir device-file classification (the scan-entry type mapping
   behind category/device lists), case-insensitive search matching,
   display-name derivation (relative path kept, ".ir" stripped, truncation),
   bounded path joins, and the aggregated strict-skip notice lines.
2. Static layer: reads the actual source files and verifies the wiring
   seams -- the Infrared menu entry (browser leaf record, parent count 5,
   the four upstream category entries intact), the single-owner IRMP busy
   guard (declared in m1_infrared.h, defined with a critical-section
   test-and-set, acquired/released on every IR entry point: learn,
   saved-remote replay, universal remotes, the browser session, and the
   CLI mtest transmit), the empty-library hint screen, and the aggregated
   strict-skip hook passed to the PR-4 loader.

Usage: python3 m1_csrc/test/m1_ir_browser_test.py
"""

import re
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


def fn_body(text, start_marker, end_marker):
    i = text.index(start_marker)
    return text[i : text.index(end_marker, i)]


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
        last = r.stderr
    else:
        raise AssertionError(f"{name} compile failed:\n{last}")
    r = subprocess.run([str(exe)], capture_output=True, text=True)
    if r.returncode != 0:
        raise AssertionError(f"{name} failed at runtime:\n{r.stdout}{r.stderr}")
    return r.stdout


def define_of(text, name):
    m = re.search(rf"#define\s+{name}\s+(\d+)", text)
    assert m, f"{name} not found"
    return int(m.group(1))


def pure_core_tests():
    """Layer 1: compile the extracted pure core and assert the model."""
    browser = source("Infrared/m1_ir_browser.c")
    browser_h = source("Infrared/m1_ir_browser.h")
    ir_file_h = source("Infrared/m1_ir_file.h")
    core = region(
        browser,
        "/* ===================== M1_IR_BROWSER_PURE_CORE_BEGIN ===================== */",
        "/* ====================== M1_IR_BROWSER_PURE_CORE_END ====================== */",
    )
    rows = define_of(browser_h, "M1_IR_BROWSER_ROWS")
    proto_len = define_of(ir_file_h, "M1_IR_FILE_PROTO_LEN_MAX")

    harness = (
        "#include <stdint.h>\n"
        "#include <stddef.h>\n"
        "#include <stdio.h>\n"
        "#include <string.h>\n"
        "#include <assert.h>\n"
        f"#define M1_IR_BROWSER_ROWS {rows}\n"
        f"#define M1_IR_FILE_PROTO_LEN_MAX {proto_len}\n"
        "\n"
        + core
        + """
    int main(void)
    {
        char buf[40], l1[32], l2[32];
        S_M1_IRB_Win_t w;

        /* ---- pagination window: row steps ---- */
        w.sel = 0; w.top = 0;
        m1_irb_win_move(&w, 20, 1);
        assert(w.sel == 1 && w.top == 0);
        m1_irb_win_move(&w, 20, -1);
        assert(w.sel == 0 && w.top == 0);            /* clamp at the top */
        m1_irb_win_move(&w, 20, -5);
        assert(w.sel == 0 && w.top == 0);            /* negative clamp */

        /* ---- scroll follow: sel leaving the window drags top ---- */
        w.sel = 5; w.top = 0;
        m1_irb_win_move(&w, 20, 1);
        assert(w.sel == 6 && w.top == 1);
        w.sel = 1; w.top = 1;
        m1_irb_win_move(&w, 20, -1);
        assert(w.sel == 0 && w.top == 0);

        /* ---- page steps (LEFT/RIGHT) and bottom clamp ---- */
        w.sel = 0; w.top = 0;
        m1_irb_win_move(&w, 20, (int16_t)(3 * M1_IR_BROWSER_ROWS));
        assert(w.sel == 18 && w.top == 13);
        m1_irb_win_move(&w, 20, 99);
        assert(w.sel == 19);                          /* clamp at the end */
        m1_irb_win_move(&w, 20, -99);
        assert(w.sel == 0 && w.top == 0);

        /* ---- short lists never scroll ---- */
        w.sel = 2; w.top = 0;
        m1_irb_win_move(&w, 3, 1);
        assert(w.sel == 2 && w.top == 0);
        m1_irb_win_move(&w, 0, 1);                    /* empty list: no-op */
        assert(w.sel == 2 && w.top == 0);
        m1_irb_win_move(NULL, 5, 1);                  /* NULL window: no crash */

        /* ---- .ir classification (scan-entry type mapping) ---- */
        assert(m1_irb_is_ir_file("foo.ir") == 1);
        assert(m1_irb_is_ir_file("BN59-00600A.IR") == 1);
        assert(m1_irb_is_ir_file("a.Ir") == 1);
        assert(m1_irb_is_ir_file(".hidden.ir") == 0); /* dot-prefixed skip */
        assert(m1_irb_is_ir_file("foo.irx") == 0);
        assert(m1_irb_is_ir_file("foo.txt") == 0);
        assert(m1_irb_is_ir_file("fo.ir ") == 0);     /* trailing space */
        assert(m1_irb_is_ir_file("ir") == 0);
        assert(m1_irb_is_ir_file("") == 0);
        assert(m1_irb_is_ir_file(NULL) == 0);

        /* ---- search matching ---- */
        assert(m1_irb_search_match("Samsung-remote.ir", "samsung") == 1);
        assert(m1_irb_search_match("SAMSUNG.ir", "sam") == 1);
        assert(m1_irb_search_match("lg.foo", "samsung") == 0);
        assert(m1_irb_search_match("anything", "") == 0);   /* empty query */
        assert(m1_irb_search_match(NULL, "a") == 0);
        assert(m1_irb_search_match("short", "shorter-than-this") == 0);

        /* ---- query lowering ---- */
        strcpy(buf, "SaMsUnG 12");
        assert(m1_irb_query_lower(buf) == 10);
        assert(strcmp(buf, "samsung 12") == 0);
        assert(m1_irb_query_lower(NULL) == 0);

        /* ---- row labels: relative path kept, ".ir" stripped, truncation ---- */
        m1_irb_row_label(buf, sizeof(buf), "Samsung/BN59-00600A.ir");
        assert(strcmp(buf, "Samsung/BN59-00600A") == 0);
        m1_irb_row_label(buf, sizeof(buf), "plain.IR");
        assert(strcmp(buf, "plain") == 0);
        m1_irb_row_label(buf, sizeof(buf), "a.b.ir");
        assert(strcmp(buf, "a.b") == 0);
        m1_irb_row_label(buf, 8, "0123456789.ir");
        assert(strcmp(buf, "0123456") == 0);          /* truncated, no .ir strip */
        m1_irb_row_label(buf, sizeof(buf), NULL);
        assert(buf[0] == '\\0');
        m1_irb_row_label(NULL, 4, "x.ir");            /* no crash */

        /* ---- bounded path join ---- */
        m1_irb_join_path(buf, sizeof(buf), "0:/IR", "TVs");
        assert(strcmp(buf, "0:/IR/TVs") == 0);
        m1_irb_join_path(buf, sizeof(buf), "", "TVs");
        assert(strcmp(buf, "TVs") == 0);
        m1_irb_join_path(buf, sizeof(buf), NULL, "TVs");
        assert(strcmp(buf, "TVs") == 0);
        m1_irb_join_path(buf, 6, "0:/IR", "TVs");
        assert(strcmp(buf, "0:/IR") == 0 && strlen(buf) == 5);
        m1_irb_join_path(buf, 0, "a", "b");           /* no crash */
        m1_irb_join_path(NULL, 8, "a", "b");          /* no crash */

        /* ---- decimal text ---- */
        m1_irb_u16_text(buf, sizeof(buf), 0);
        assert(strcmp(buf, "0") == 0);
        m1_irb_u16_text(buf, sizeof(buf), 65535);
        assert(strcmp(buf, "65535") == 0);
        m1_irb_u16_text(buf, 3, 12345);
        assert(strcmp(buf, "12") == 0);

        /* ---- aggregated strict-skip notices ---- */
        {
            S_M1_IRB_SkipAcc_t acc;
            memset(&acc, 0, sizeof(acc));
            assert(m1_irb_skip_lines(&acc, l1, sizeof(l1), l2, sizeof(l2)) == 0);
            m1_irb_skip_add(&acc, "RCA");
            assert(m1_irb_skip_lines(&acc, l1, sizeof(l1), l2, sizeof(l2)) == 1);
            assert(strcmp(l1, "1 skipped") == 0);
            assert(strcmp(l2, "RCA") == 0);
            m1_irb_skip_add(&acc, "Kaseikyo");
            m1_irb_skip_add(&acc, NULL);
            assert(m1_irb_skip_lines(&acc, l1, sizeof(l1), l2, sizeof(l2)) == 1);
            assert(strcmp(l1, "3 skipped") == 0);
            assert(strcmp(l2, "RCA") == 0);            /* first name kept */
            m1_irb_skip_add(NULL, "x");               /* no crash */
        }

        puts("pure-core ok");
        return 0;
    }
    """
    )

    out = compile_run(tempfile.gettempdir(), "m1_ir_browser_core", harness)
    CHECK("pure-core ok" in out, "pure core harness ran to completion")
    CHECK("FAIL" not in out, "no pure-core case failed (see assert output)")


def static_seam_tests():
    """Layer 2: verify the wiring seams in the real sources."""
    menu = source("m1_csrc/m1_menu.c")
    browser = source("Infrared/m1_ir_browser.c")
    browser_h = source("Infrared/m1_ir_browser.h")
    ir_h = source("m1_csrc/m1_infrared.h")
    ir_c = source("m1_csrc/m1_infrared.c")
    remotes_c = source("Infrared/m1_ir_remotes.c")
    cli_c = source("m1_csrc/m1_cli.c")

    # --- menu wiring: browser leaf + parent count + upstream entries intact
    CHECK(
        '#include "m1_ir_browser.h"' in menu,
        "menu includes the browser header",
    )
    CHECK(
        '"IR File Browser", m1_ir_browser_run, NULL, NULL, 0, 0, NULL, NULL, {NULL}'
        in menu,
        "browser leaf record dispatches m1_ir_browser_run",
    )
    CHECK(
        '"Infrared", menu_infrared_init, NULL, NULL, 5, 0, menu_m1_icon_infrared, NULL,'
        in menu,
        "Infrared parent declares 5 items",
    )
    infrared_arr = menu[
        menu.index("S_M1_Menu_t menu_Infrared =") : menu.index(
            "{NULL}\n};", menu.index("S_M1_Menu_t menu_Infrared =")
        )
    ]
    CHECK(
        infrared_arr.count("&menu_") == 5,
        f"Infrared parent array holds exactly 5 submenu pointers (got {infrared_arr.count('&menu_')})",
    )
    for ptr in (
        "&menu_Infrared_File_Browser",
        "&menu_Infrared_Universal_Remotes",
        "&menu_Infrared_Learn_New_Remote",
        "&menu_Infrared_Saved_Remotes",
        "&menu_Infrared_Remap_GPIO",
    ):
        CHECK(ptr in infrared_arr, f"{ptr} still wired in the Infrared menu")
    for leaf in (
        '"TV remotes", infrared_universal_tv_remotes',
        '"Audio player remotes", infrared_universal_audio_remotes',
        '"Projector remotes", infrared_universal_projector_remotes',
        '"AC remotes", infrared_universal_ac_remotes',
    ):
        CHECK(leaf in menu, f"upstream entry intact: {leaf.split(',')[0]}")

    # --- single-owner IRMP guard: declared, defined, and enforced
    CHECK(
        "uint8_t m1_ir_subsys_acquire(void);" in ir_h
        and "void m1_ir_subsys_release(void);" in ir_h,
        "m1_infrared.h declares the acquire/release guard API",
    )
    CHECK(
        "static volatile uint8_t\tm1_ir_subsys_busy = 0;" in ir_c,
        "m1_infrared.c owns the module-level busy flag",
    )
    acq = fn_body(
        ir_c,
        "uint8_t m1_ir_subsys_acquire(void)\n{",
        "} // uint8_t m1_ir_subsys_acquire(void)",
    )
    CHECK(
        "taskENTER_CRITICAL()" in acq
        and "taskEXIT_CRITICAL()" in acq
        and acq.index("!m1_ir_subsys_busy") < acq.index("m1_ir_subsys_busy = 1"),
        "acquire is a critical-section test-and-set",
    )

    guarded_paths = (
        (
            "infrared_learn_new_remote",
            ir_c,
            "void infrared_learn_new_remote(void)\n{",
            "} // void infrared_learn_new_remote(void)",
            "infrared_decode_sys_init();",
        ),
        (
            "infrared_saved_remotes",
            ir_c,
            "void infrared_saved_remotes(void)\n{",
            "} // void infrared_saved_remotes(void)",
            "infrared_encode_sys_init();",
        ),
        (
            "infrared_universal_all_remotes",
            remotes_c,
            "static void infrared_universal_all_remotes(uint8_t remote_type)\n{",
            "} // static void infrared_universal_all_remotes(uint8_t remote_type)",
            "ir_remote_file_header_check(",
        ),
        (
            "m1_ir_browser_run",
            browser,
            "void m1_ir_browser_run(void)\n{",
            "} // void m1_ir_browser_run(void)",
            "irb_screen_dashboard();",
        ),
    )
    for name, src, start, end, first_irmp_use in guarded_paths:
        body = fn_body(src, start, end)
        i_acq = body.find("if ( !m1_ir_subsys_acquire() )")
        i_rel = body.rfind("m1_ir_subsys_release();")
        i_use = body.find(first_irmp_use)
        CHECK(i_acq != -1, f"{name} acquires the IR subsystem guard")
        CHECK(i_rel != -1, f"{name} releases the IR subsystem guard")
        CHECK(
            -1 not in (i_acq, i_rel, i_use) and i_acq < i_use,
            f"{name} acquires before its first IRMP/IR-system use",
        )
        CHECK(
            -1 not in (i_acq, i_rel) and i_rel > i_acq,
            f"{name} releases after acquiring",
        )

    # saved-remotes early exit (nothing learned) must also release
    saved = fn_body(
        ir_c,
        "void infrared_saved_remotes(void)\n{",
        "} // void infrared_saved_remotes(void)",
    )
    CHECK(
        "m1_ir_subsys_release(); // nothing to replay" in saved,
        "saved-remote early exit releases the guard",
    )

    # CLI mtest transmit takes the guard too (cross-task enforcement)
    cli_ir = fn_body(
        cli_c,
        "void cmd_m1_mtest_infrared(",
        "} // void cmd_m1_mtest_infrared(",
    )
    if not cli_ir:  # fall back to the whole file section
        cli_ir = cli_c[cli_c.index("case 40:") : cli_c.index("case 40:") + 6000]
    CHECK(
        "if ( !m1_ir_subsys_acquire() )" in cli_ir,
        "CLI mtest IR transmit acquires the guard",
    )
    CHECK(
        cli_ir.find("m1_ir_subsys_acquire()")
        < cli_ir.find("infrared_encode_sys_init();")
        and cli_ir.find("m1_ir_subsys_release();")
        > cli_ir.find("infrared_encode_sys_deinit();"),
        "CLI mtest guards the whole encode/transmit session",
    )

    # --- empty-library hint and aggregated strict-skip seams
    CHECK(
        "static void irb_draw_empty_library(void)\n{" in browser,
        "empty-library hint screen exists",
    )
    empty_scr = fn_body(
        browser,
        "static void irb_draw_empty_library(void)\n{",
        "} // static void irb_draw_empty_library(void)",
    )
    CHECK(
        '"Copy Flipper IRDB"' in empty_scr and "m1_wait_back_to_exit();" in empty_scr,
        "empty-library hint is actionable and waits for BACK",
    )
    cats = fn_body(
        browser,
        "static void irb_screen_categories(void)\n{",
        "} // static void irb_screen_categories(void)",
    )
    CHECK(
        "irb_draw_empty_library();" in cats,
        "category screen renders the hint when the scan is empty",
    )
    CHECK(
        "m1_ir_file_index(path, irb_entries, M1_IR_BROWSER_BUTTONS_MAX,\n\t\t\t&irb_file_stats, irb_notice_hook)"
        in browser,
        "device indexing goes through the PR-4 loader with the notice hook",
    )
    hook = fn_body(
        browser,
        "static void irb_notice_hook(const char *protocol_name, uint32_t file_offset)\n{",
        "} // static void irb_notice_hook(",
    )
    CHECK(
        "m1_irb_skip_add(&irb_skip_acc, protocol_name);" in hook,
        "notice hook aggregates",
    )
    idx = fn_body(
        browser,
        "static int32_t irb_index_device(const char *path)\n{",
        "} // static int32_t irb_index_device(const char *path)",
    )
    CHECK(
        idx.count("m1_irb_skip_lines") == 0 and "irb_notice(skip_l1" in browser,
        "strict-skip notices render through the aggregated lines (one per file)",
    )

    # --- documented scan caps (docs/ir-provisioning.md table)
    caps = {
        "M1_IR_BROWSER_ROWS": 6,
        "M1_IR_BROWSER_CATS_MAX": 24,
        "M1_IR_BROWSER_DEVICES_MAX": 64,
        "M1_IR_BROWSER_BUTTONS_MAX": 128,
        "M1_IR_BROWSER_DEPTH_MAX": 4,
        "M1_IR_BROWSER_RAW_TICKS_MAX": 4096,
    }
    for name, value in caps.items():
        CHECK(
            define_of(browser_h, name) == value, f"{name} == {value} (documented cap)"
        )

    # --- no legacy parser / protocol map ported
    CHECK(
        "ir_universal_proto_to_id" not in browser
        and "S_IR_ProtoMap_t" not in browser
        and "IR_UNIVERSAL_CMDS_MAX" not in browser,
        "fork whole-file parser and protocol-map symbols not ported into the browser",
    )
    CHECK(
        "m1_ir_file_signal_load" in browser and "m1_ir_file_index" in browser,
        "browser data access goes through the PR-4 streaming loader",
    )


def main():
    pure_core_tests()
    static_seam_tests()
    print(f"\n{PASSED} passed, {FAILED} failed")
    if FAILED:
        sys.exit(1)


if __name__ == "__main__":
    main()
