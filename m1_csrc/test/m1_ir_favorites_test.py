#!/usr/bin/env python3
"""Host tests for the IR favorites/recent stores and the cycle-all action
(issue #43 part 3 / PR-6).

Two layers, same technique as m1_ir_browser_test.py (real sources, no
mocks of the code under test):

1. Compiled layer: the M1_IR_BROWSER_PURE_CORE region is extracted
   verbatim from Infrared/m1_ir_browser.c and compiled with a generated
   harness -- store-line parsing (in-place trim; the tolerant-load rule:
   blank and over-long lines are skipped, never stored truncated), the
   LRU list model (front insert, dedup, cap eviction, removal), the
   legacy basename row labels of stored device paths, and the favorite
   "* " title marker.
2. Static layer: reads the real sources and verifies the wiring seams --
   the 0:/System store files and the LRU caps (20 favorites / 10 recent),
   the dashboard rows and their dispatch, the long-OK favorites toggle
   with the title-marker refresh (gated off active transmits), the
   recent entry per successful transmit, corruption tolerance (missing
   or unreadable store renders an empty list), the serialization rules
   (no store/fatfs writes inside the transmit path; store operations are
   browser-session-only and touch no IRMP/irsnd API), and the cycle-all
   action dispatch (exposed session entry acquiring its own guard,
   single-frame default per press, explicit submenu leaves, menu parent
   counts).

Usage: python3 m1_csrc/test/m1_ir_favorites_test.py
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


def compile_run(tmp, name, code, includes=None):
    """Compile+run a generated TU. Sanitizers when the toolchain has the
    runtime (CI); plain build otherwise (some MinGW hosts)."""
    src = Path(tmp) / f"{name}.c"
    src.write_text(code)
    exe = Path(tmp) / name
    inc = []
    for d in includes or []:
        inc += ["-I", str(d)]
    for extra in (SANITIZERS, []):
        cmd = (
            [CC, "-std=c11", "-Wall", "-Wextra", "-Wno-unused-function", "-g"]
            + extra
            + inc
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


BROWSER_CORE_BEGIN = (
    "/* ===================== M1_IR_BROWSER_PURE_CORE_BEGIN ===================== */"
)
BROWSER_CORE_END = (
    "/* ====================== M1_IR_BROWSER_PURE_CORE_END ====================== */"
)


def pure_core_tests():
    """Layer 1: compile the extracted pure core and assert the store model."""
    browser = source("Infrared/m1_ir_browser.c")
    browser_h = source("Infrared/m1_ir_browser.h")
    core = region(browser, BROWSER_CORE_BEGIN, BROWSER_CORE_END)
    rows = define_of(browser_h, "M1_IR_BROWSER_ROWS")
    name_len = define_of(browser_h, "M1_IR_BROWSER_NAME_LEN_MAX")
    path_len = define_of(browser_h, "M1_IR_BROWSER_PATH_LEN_MAX")
    fav_cap = define_of(browser_h, "M1_IR_BROWSER_FAVORITES_MAX")
    rec_cap = define_of(browser_h, "M1_IR_BROWSER_RECENT_MAX")

    harness = (
        "#include <stdint.h>\n"
        "#include <stddef.h>\n"
        "#include <stdio.h>\n"
        "#include <string.h>\n"
        "#include <assert.h>\n"
        f"#define M1_IR_BROWSER_ROWS {rows}\n"
        f"#define M1_IR_BROWSER_NAME_LEN_MAX {name_len}\n"
        f"#define M1_IR_BROWSER_PATH_LEN_MAX {path_len}\n"
        "#define M1_IR_FILE_PROTO_LEN_MAX 16\n"
        "\n"
        + core
        + f"""
    static char lst[{fav_cap}][M1_IR_BROWSER_PATH_LEN_MAX];

    static void eq(const char *what, const char *got, const char *want)
    {{
        if ( got == NULL || want == NULL )
            assert((got == want) && what);
        else
            assert(strcmp(got, want) == 0 && what);
    }}

    int main(void)
    {{
        char buf[160], t1[64], t2[64];
        uint16_t n;
        int i;

        /* ---- line trim (in place) ---- */
        strcpy(buf, "  0:/IR/TVs/a.ir \\r\\n");
        eq("trim ws+crlf", m1_irb_list_trim(buf), "0:/IR/TVs/a.ir");
        strcpy(buf, "\\t0:/x\\n");
        eq("trim tab+lf", m1_irb_list_trim(buf), "0:/x");
        strcpy(buf, "plain");
        eq("trim nothing", m1_irb_list_trim(buf), "plain");
        strcpy(buf, "");
        eq("trim empty", m1_irb_list_trim(buf), "");
        strcpy(buf, "   \\t  ");
        eq("trim all-ws", m1_irb_list_trim(buf), "");
        assert(m1_irb_list_trim(NULL) == NULL);

        /* ---- tolerant-load rule ---- */
        assert(m1_irb_list_line_ok("0:/IR/TVs/Samsung/BN59.ir") == 1);
        assert(m1_irb_list_line_ok("x") == 1);              /* any non-empty
                                                            * fitting line */
        assert(m1_irb_list_line_ok("") == 0);               /* blank: skipped */
        assert(m1_irb_list_line_ok(NULL) == 0);
        memset(buf, 'a', sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\\0';
        assert(strlen(buf) == 159);
        assert(m1_irb_list_line_ok(buf) == 0);              /* over-long */
        memset(buf, 'a', M1_IR_BROWSER_PATH_LEN_MAX);
        buf[M1_IR_BROWSER_PATH_LEN_MAX] = '\\0';
        assert(m1_irb_list_line_ok(buf) == 0);              /* exactly cap: no
                                                            * room for NUL */
        memset(buf, 'a', M1_IR_BROWSER_PATH_LEN_MAX - 1);
        buf[M1_IR_BROWSER_PATH_LEN_MAX - 1] = '\\0';
        assert(m1_irb_list_line_ok(buf) == 1);              /* cap-1 fits */

        /* ---- LRU list model ---- */
        n = 0;
        m1_irb_list_add_front(lst, &n, 3, "0:/A");
        assert(n == 1 && !strcmp(lst[0], "0:/A"));
        m1_irb_list_add_front(lst, &n, 3, "0:/B");
        assert(n == 2 && !strcmp(lst[0], "0:/B") && !strcmp(lst[1], "0:/A"));
        /* dedup: existing entry moves to the front, never duplicated */
        m1_irb_list_add_front(lst, &n, 3, "0:/A");
        assert(n == 2 && !strcmp(lst[0], "0:/A") && !strcmp(lst[1], "0:/B"));

        /* fill to the cap, then evict the LRU tail */
        m1_irb_list_add_front(lst, &n, 3, "0:/C");
        assert(n == 3);
        m1_irb_list_add_front(lst, &n, 3, "0:/D");
        assert(n == 3);                                     /* capped */
        assert(!strcmp(lst[0], "0:/D") && !strcmp(lst[1], "0:/C")
               && !strcmp(lst[2], "0:/A"));                  /* B evicted */
        m1_irb_list_add_front(lst, &n, 3, "0:/E");
        assert(n == 3 && !strcmp(lst[0], "0:/E") && !strcmp(lst[1], "0:/D")
               && !strcmp(lst[2], "0:/C"));                  /* A evicted */

        assert(m1_irb_list_contains(lst, n, "0:/D") == 1);
        assert(m1_irb_list_contains(lst, n, "0:/B") == 0);
        assert(m1_irb_list_contains(lst, 0, "0:/E") == 0);
        assert(m1_irb_list_contains(lst, n, NULL) == 0);
        assert(m1_irb_list_contains(NULL, n, "0:/D") == 0);

        /* removal closes the gap */
        assert(m1_irb_list_remove(lst, &n, "0:/D") == 1);
        assert(n == 2 && !strcmp(lst[0], "0:/E") && !strcmp(lst[1], "0:/C"));
        assert(m1_irb_list_remove(lst, &n, "0:/ZZ") == 0);
        assert(n == 2);
        assert(m1_irb_list_remove(lst, &n, "0:/C") == 1);
        assert(m1_irb_list_remove(lst, &n, "0:/E") == 1);
        assert(n == 0);
        m1_irb_list_remove(NULL, &n, "x");                   /* no crash */
        m1_irb_list_remove(lst, NULL, "x");
        m1_irb_list_add_front(NULL, &n, 2, "x");
        m1_irb_list_add_front(lst, NULL, 2, "x");
        m1_irb_list_add_front(lst, &n, 0, "x");              /* cap 0: no-op */
        assert(n == 0);
        m1_irb_list_add_front(lst, &n, {fav_cap}, NULL);     /* NULL: no-op */
        assert(n == 0);

        /* at the real store cap the count saturates and the tail rolls */
        for ( i = 0; i < {fav_cap} + 5; i++ )
        {{
            sprintf(buf, "0:/dev%d", i);
            m1_irb_list_add_front(lst, &n, {fav_cap}, buf);
        }}
        assert(n == {fav_cap});
        sprintf(buf, "0:/dev%d", {fav_cap} + 4);
        assert(!strcmp(lst[0], buf));                        /* newest first */
        assert(m1_irb_list_contains(lst, n, "0:/dev0") == 0);/* oldest gone */
        sprintf(buf, "0:/dev5");
        assert(m1_irb_list_contains(lst, n, buf) == 1);      /* recent kept */
        n = 0;                                               /* reset */

        /* ---- legacy basename row labels ---- */
        m1_irb_basename_label(buf, sizeof(buf), "0:/IR/TVs/Samsung/BN59-00600A.ir");
        eq("basename", buf, "BN59-00600A");
        m1_irb_basename_label(buf, sizeof(buf), "plain.IR");
        eq("basename case", buf, "plain");
        m1_irb_basename_label(buf, sizeof(buf), "a/b/ccc");
        eq("basename deep", buf, "ccc");
        m1_irb_basename_label(buf, sizeof(buf), "0:/only.ir");
        eq("basename last-sep", buf, "only");
        m1_irb_basename_label(buf, 8, "0:/IR/TVs/VeryLongDeviceName.ir");
        eq("basename trunc", buf, "VeryLon");
        m1_irb_basename_label(buf, sizeof(buf), NULL);
        eq("basename NULL", buf, "");
        m1_irb_basename_label(NULL, 8, "a.ir");              /* no crash */
        m1_irb_basename_label(buf, 0, "a.ir");               /* no crash */

        /* ---- favorite title marker ---- */
        m1_irb_dev_title(t1, sizeof(t1), "Samsung TV", 0);
        eq("title plain", t1, "Samsung TV");
        m1_irb_dev_title(t1, sizeof(t1), "Samsung TV", 1);
        eq("title fav", t1, "* Samsung TV");
        m1_irb_dev_title(t1, sizeof(t1), NULL, 1);
        eq("title fav only", t1, "* ");
        m1_irb_dev_title(t1, 6, "LongDeviceName", 1);
        eq("title trunc", t1, "* Lon");
        m1_irb_dev_title(t1, 2, "x", 1);                     /* star skipped,
                                                            * 1 char + NUL */
        eq("title tiny", t1, "x");
        m1_irb_dev_title(NULL, 8, "x", 0);                   /* no crash */
        m1_irb_dev_title(t1, 0, "x", 0);                     /* no crash */

        /* ---- shared workspace shape: recent cap fits below the
         * favorites-capped buffer the pure model is exercised with ---- */
        assert({rec_cap} <= {fav_cap});

        puts("pure-core ok");
        return 0;
    }}
    """
    )

    out = compile_run(tempfile.gettempdir(), "m1_ir_favorites_core", harness)
    CHECK("pure-core ok" in out, "pure core harness ran to completion")
    CHECK("FAIL" not in out, "no pure-core case failed (see assert output)")


def static_seam_tests():
    """Layer 2: verify the wiring seams in the real sources."""
    browser = source("Infrared/m1_ir_browser.c")
    browser_h = source("Infrared/m1_ir_browser.h")
    menu = source("m1_csrc/m1_menu.c")
    remotes_c = source("Infrared/m1_ir_remotes.c")
    remotes_h = source("Infrared/m1_ir_remotes.h")
    provision_h = source("m1_csrc/m1_sdcard_provision.h")

    # --- stores: canonical folder, legacy file names, LRU caps
    CHECK(
        '#define M1_SD_DIR_SYSTEM         "0:/System"' in provision_h,
        "canonical System folder constant exists",
    )
    CHECK(
        re.search(
            r'#define M1_IRB_FAV_FILE\s+M1_SD_DIR_SYSTEM "/ir_favorites\.txt"', browser
        )
        is not None
        and re.search(
            r'#define M1_IRB_RECENT_FILE\s+M1_SD_DIR_SYSTEM "/ir_recent\.txt"', browser
        )
        is not None,
        "store files live under the canonical 0:/System with the legacy names",
    )
    CHECK(
        define_of(browser_h, "M1_IR_BROWSER_FAVORITES_MAX") == 20
        and define_of(browser_h, "M1_IR_BROWSER_RECENT_MAX") == 10,
        "LRU caps are 20 favorites / 10 recent (legacy values)",
    )
    CHECK(
        "static char irb_list_paths[M1_IR_BROWSER_FAVORITES_MAX]"
        "[M1_IR_BROWSER_PATH_LEN_MAX];"
        in browser
        and "static char irb_list_names[M1_IR_BROWSER_FAVORITES_MAX]"
        "[M1_IR_BROWSER_NAME_LEN_MAX];"
        in browser,
        "store workspace is module-static and bounded (shared by both stores)",
    )
    CHECK(
        not re.search(r"\b(malloc|calloc|realloc|free)\s*\(", browser),
        "browser performs no dynamic allocation",
    )

    # --- tolerant load: missing/unreadable file renders an empty list
    load = fn_body(
        browser,
        "static uint16_t irb_store_load(const char *file, uint16_t cap)\n{",
        "} // static uint16_t irb_store_load(",
    )
    CHECK(
        "f_open(&f, file, FA_READ) != FR_OK" in load and "return 0;" in load,
        "a missing/unreadable store loads as an empty list (no error screen)",
    )
    CHECK(
        "m1_irb_list_trim(line)" in load and "m1_irb_list_line_ok(p)" in load,
        "every line goes through the host-tested trim + tolerant rule",
    )
    CHECK(
        "strchr(line, '\\n') == NULL" in load,
        "over-long lines are detected (no newline in a full buffer)",
    )
    CHECK(
        load.index("while ( count < cap )") < load.index("f_close"),
        "loading stops at the cap (bounded memory, no unbounded growth)",
    )
    CHECK(
        "cap > M1_IR_BROWSER_FAVORITES_MAX" in load,
        "load clamps the caller cap to the workspace bound",
    )

    # --- bounded rewrite + lazy folder ensure
    save = fn_body(
        browser,
        "static void irb_store_save(const char *file, uint16_t count)\n{",
        "} // static void irb_store_save(",
    )
    CHECK(
        "FA_WRITE | FA_CREATE_ALWAYS" in save and "f_printf" in save,
        "save is a bounded whole-file rewrite (legacy one-path-per-line)",
    )
    CHECK(
        "count > M1_IR_BROWSER_FAVORITES_MAX" in save,
        "save clamps the row count to the workspace bound",
    )
    CHECK(
        "(void)f_mkdir(M1_SD_DIR_SYSTEM);" in save,
        "0:/System is ensured lazily on first write (settings-store policy)",
    )

    # --- recent/favorites operations
    recent = fn_body(
        browser,
        "static void irb_recent_add(const char *path)\n{",
        "} // static void irb_recent_add(const char *path)",
    )
    CHECK(
        "irb_store_load(M1_IRB_RECENT_FILE, M1_IR_BROWSER_RECENT_MAX)" in recent
        and "m1_irb_list_add_front" in recent
        and "irb_store_save(M1_IRB_RECENT_FILE" in recent,
        "recent add = load + LRU front insert + bounded rewrite",
    )
    toggle = fn_body(
        browser,
        "static uint8_t irb_fav_toggle(const char *path)\n{",
        "} // static uint8_t irb_fav_toggle(const char *path)",
    )
    CHECK(
        "m1_irb_list_contains" in toggle
        and "m1_irb_list_remove" in toggle
        and "m1_irb_list_add_front" in toggle,
        "toggle = load + contains ? remove : add-front + rewrite",
    )
    CHECK(
        'irb_notice("Favorite"' in toggle,
        "toggle shows the legacy short feedback notice",
    )

    # --- dashboard rows + dispatch
    CHECK(
        'static const char *irb_dash_rows[4] = {"Favorites", "Recent", '
        '"Browse files", "Search"};' in browser,
        "dashboard lists Favorites/Recent ahead of the PR-5 rows (legacy order)",
    )
    dash = fn_body(
        browser,
        "static void irb_screen_dashboard(void)\n{",
        "} // static void irb_screen_dashboard(void)",
    )
    CHECK(
        re.search(
            r'irb_screen_stored_list\("Favorites", M1_IRB_FAV_FILE,\s*'
            r"M1_IR_BROWSER_FAVORITES_MAX\);",
            dash,
        )
        is not None
        and re.search(
            r'irb_screen_stored_list\("Recent", M1_IRB_RECENT_FILE,\s*'
            r"M1_IR_BROWSER_RECENT_MAX\);",
            dash,
        )
        is not None,
        "dashboard rows 0/1 open the stored lists with their own caps",
    )
    CHECK(
        "irb_screen_categories();" in dash and "irb_screen_search();" in dash,
        "dashboard rows 2/3 keep the PR-5 browse/search flows",
    )
    for t in ("Favorites", "Recent"):
        CHECK(len(t) <= 12, f'stored-list title "{t}" fits the 12-char cap')

    # --- stored-list screen: same device/button flow, reload after return
    stored = fn_body(
        browser,
        "static void irb_screen_stored_list(const char *title, const char *file, uint16_t cap)\n{",
        "} // static void irb_screen_stored_list(",
    )
    CHECK(
        'irb_notice(title, "list is empty"' in stored,
        "an empty (or fully corrupt) store renders the empty-list notice",
    )
    CHECK(
        "irb_screen_buttons(sel_path, sel_label);" in stored,
        "stored rows open the same device/button flow as every other list",
    )
    CHECK(
        stored.count("irb_store_load(file, cap)") == 2
        and stored.rindex("irb_store_load(file, cap)")
        > stored.index("irb_screen_buttons(sel_path, sel_label)"),
        "the store is reloaded after the button view (recent/toggle changed it)",
    )

    # --- button view: local copies (aliasing), favorite toggle, recent
    btn = fn_body(
        browser,
        "static uint8_t irb_screen_buttons(const char *path, const char *title)\n{",
        "} // static uint8_t irb_screen_buttons(",
    )
    CHECK(
        'm1_irb_join_path(dev_path, (uint8_t)sizeof(dev_path), "", (path != NULL) ? path : "");'
        in btn
        and "fav = irb_fav_is(dev_path);" in btn,
        "button view works on a local path copy and checks the store at entry",
    )
    i_lclick = btn.find("bs.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_LCLICK")
    i_toggle = btn.find("fav = irb_fav_toggle(dev_path);")
    i_title = btn.find(
        "m1_irb_dev_title(dev_title, (uint8_t)sizeof(dev_title), title, fav);",
        i_toggle,
    )
    i_redraw = btn.find(
        "irb_draw_list(dev_title, count, &w, irb_row_entry, truncated);", i_toggle
    )
    CHECK(
        -1 not in (i_lclick, i_toggle, i_title, i_redraw)
        and i_lclick < i_toggle < i_title < i_redraw,
        "long OK toggles the favorite and refreshes the title marker",
    )
    lclick_block = btn[i_lclick:i_toggle]
    CHECK(
        "if ( tx_active )" in lclick_block and "continue;" in lclick_block,
        "the favorites toggle is refused while a frame is in flight",
    )
    i_tx = btn.find("tx_err = irb_transmit(dev_path, w.sel);")
    i_active = btn.find("tx_active = 1;", i_tx)
    i_rec = btn.find("irb_recent_add(dev_path);", i_tx)
    CHECK(
        -1 not in (i_tx, i_active, i_rec) and i_tx < i_active < i_rec,
        "a successful transmit records the device in the recent store",
    )

    # --- serialization: store writes stay out of the transmit path, and
    # store operations touch no IRMP/irsnd API (filesystem-only)
    tx = fn_body(
        browser,
        "static uint8_t irb_transmit(const char *path, uint16_t entry_idx)\n{",
        "} // static uint8_t irb_transmit(",
    )
    CHECK(
        not re.search(r"\bf_(open|close|gets|printf|mkdir|unlink|rename)\s*\(", tx),
        "the transmit path performs no filesystem calls of its own",
    )
    CHECK(
        "irb_recent_add" not in tx
        and "irb_store_save" not in tx
        and "irb_fav_toggle" not in tx,
        "the transmit path performs no favorites/recent writes",
    )
    for name, start, end in (
        (
            "irb_recent_add",
            "static void irb_recent_add(const char *path)\n{",
            "} // static void irb_recent_add(const char *path)",
        ),
        (
            "irb_fav_is",
            "static uint8_t irb_fav_is(const char *path)\n{",
            "} // static uint8_t irb_fav_is(const char *path)",
        ),
        (
            "irb_fav_toggle",
            "static uint8_t irb_fav_toggle(const char *path)\n{",
            "} // static uint8_t irb_fav_toggle(const char *path)",
        ),
        (
            "irb_store_load",
            "static uint16_t irb_store_load(const char *file, uint16_t cap)\n{",
            "} // static uint16_t irb_store_load(",
        ),
        (
            "irb_store_save",
            "static void irb_store_save(const char *file, uint16_t count)\n{",
            "} // static void irb_store_save(",
        ),
    ):
        body = fn_body(browser, start, end)
        CHECK(
            not re.search(
                r"(irsnd_|infrared_transmit|infrared_encode|m1_ir_ota)", body
            ),
            f"{name} is filesystem-only (no IRMP/irsnd/encode API)",
        )

    # store operations exist only inside the browser session module
    for f in ("irb_recent_add", "irb_fav_toggle"):
        CHECK(
            browser.count(f) >= 3,
            f"{f} is defined and dispatched inside the browser module",
        )
    CHECK(
        "irb_recent_add" not in remotes_c and "irb_fav_toggle" not in remotes_c,
        "the universal-remotes module never touches the stores",
    )

    # --- cycle-all action dispatch: exposed session, guard, mode split
    CHECK(
        "void infrared_universal_all_remotes(uint8_t remote_type, uint8_t cycle_all);"
        in remotes_h,
        "the session entry is exposed in the header (was static)",
    )
    for w in (
        "infrared_universal_cycle_all_tv",
        "infrared_universal_cycle_all_audio",
        "infrared_universal_cycle_all_projector",
        "infrared_universal_cycle_all_ac",
    ):
        CHECK(
            f"void {w}(void);" in remotes_h and f"void {w}(void)\n{{" in remotes_c,
            f"cycle wrapper {w} declared and defined",
        )
    session = fn_body(
        remotes_c,
        "void infrared_universal_all_remotes(uint8_t remote_type, uint8_t cycle_all)\n{",
        "} // void infrared_universal_all_remotes(uint8_t remote_type, uint8_t cycle_all)",
    )
    CHECK(
        "if ( !m1_ir_subsys_acquire() )" in session
        and session.index("m1_ir_subsys_acquire()")
        < session.index("ir_remote_file_header_check(")
        and session.rfind("m1_ir_subsys_release();")
        > session.index("m1_ir_subsys_acquire()"),
        "the cycle-all session acquires/releases the single-owner IRMP guard itself",
    )
    tx_done = session[
        session.index("} // switch ( ir_remote_state )") : session.index(
            "} // else if ( q_item.q_evt_type==Q_EVENT_IRRED_TX )"
        )
    ]
    i_gate = tx_done.find("if ( cycle_all )")
    i_next = tx_done.find("next_transmission_req = 1; // Request")
    i_else = tx_done.find("else")
    CHECK(
        -1 not in (i_gate, i_next, i_else) and i_gate < i_next < i_else,
        "the automatic next-device request is gated on cycle_all "
        "(single-frame default never auto-advances)",
    )
    CHECK(
        "ir_remote_state = IR_REMOTES_STATE_STANDBY;" in session[i_next:],
        "single-frame mode returns to STANDBY after the one frame",
    )
    CHECK(
        "( cycle_all ) ? IR_REMOTES_STATE_COMPLETE : IR_REMOTES_STATE_STANDBY"
        in session,
        "END_OF_FILE ends cycle mode as COMPLETE, single-frame as STANDBY",
    )
    # the default remote entries pass cycle_all = 0
    for w, cat in (
        ("infrared_universal_tv_remotes", "IR_REMOTETYPE_TV"),
        ("infrared_universal_audio_remotes", "IR_REMOTETYPE_AUDIO"),
        ("infrared_universal_projector_remotes", "IR_REMOTETYPE_PROJECTOR"),
        ("infrared_universal_ac_remotes", "IR_REMOTETYPE_AC"),
    ):
        body = fn_body(remotes_c, f"void {w}(void)\n{{", f"}} // void {w}(void)")
        CHECK(
            f"infrared_universal_all_remotes({cat}, 0);" in body,
            f"{w} runs the single-frame default (cycle_all = 0)",
        )
    for w, cat in (
        ("infrared_universal_cycle_all_tv", "IR_REMOTETYPE_TV"),
        ("infrared_universal_cycle_all_audio", "IR_REMOTETYPE_AUDIO"),
        ("infrared_universal_cycle_all_projector", "IR_REMOTETYPE_PROJECTOR"),
        ("infrared_universal_cycle_all_ac", "IR_REMOTETYPE_AC"),
    ):
        body = fn_body(remotes_c, f"void {w}(void)\n{{", f"}} // void {w}(void)")
        CHECK(
            f"infrared_universal_all_remotes({cat}, 1);" in body,
            f"{w} is the explicit cycle-all action (cycle_all = 1)",
        )

    # --- menu wiring: explicit leaves, parent counts
    for label, fn in (
        ('"Cycle all TVs", infrared_universal_cycle_all_tv', "TVs"),
        ('"Cycle all Audio", infrared_universal_cycle_all_audio', "Audio"),
        (
            '"Cycle all Projectors", infrared_universal_cycle_all_projector',
            "Projectors",
        ),
        ('"Cycle all ACs", infrared_universal_cycle_all_ac', "ACs"),
    ):
        CHECK(
            f"{label}, NULL, NULL, 0, 0, NULL, NULL, {{NULL}}" in menu,
            f"explicit submenu leaf wired: {fn}",
        )
    uni = menu[
        menu.index("S_M1_Menu_t menu_Infrared_Universal_Remotes =") : menu.index(
            "{NULL}\n};", menu.index("S_M1_Menu_t menu_Infrared_Universal_Remotes =")
        )
    ]
    CHECK(
        '"Universal Remotes", NULL, NULL, NULL, 8, 0, NULL, NULL,' in menu,
        "Universal Remotes parent declares 8 items",
    )
    CHECK(
        uni.count("&menu_") == 8,
        f"Universal Remotes array holds exactly 8 submenu pointers (got {uni.count('&menu_')})",
    )
    CHECK(
        "menu_Infrared_Universal_Cycle_TVs" in uni
        and "menu_Infrared_Universal_Cycle_Audios" in uni
        and "menu_Infrared_Universal_Cycle_Projectors" in uni
        and "menu_Infrared_Universal_Cycle_ACs" in uni,
        "all four cycle leaves sit under Universal Remotes",
    )
    CHECK(
        '"Infrared", menu_infrared_init, NULL, NULL, 5, 0, menu_m1_icon_infrared, NULL,'
        in menu,
        "Infrared parent still declares 5 items",
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


def main():
    pure_core_tests()
    static_seam_tests()
    print(f"\n{PASSED} passed, {FAILED} failed")
    if FAILED:
        sys.exit(1)


if __name__ == "__main__":
    main()
