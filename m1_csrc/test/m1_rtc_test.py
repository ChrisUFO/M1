#!/usr/bin/env python3
"""Host tests for the RTC date/time feature (PR-3, issue #7 completion).

Two layers, same technique as m1_settings_screens_test.py (real sources,
no mocks of the code under test):

1. Compiled layer: the REAL m1_csrc/m1_rtc_date.c (pure domain, no HAL/
   FreeRTOS/u8g2 by contract) is compiled with a generated harness and
   the date/time math is asserted: leap years, days-in-month, full
   2000..2099 validation (impossible dates rejected), Sakamoto weekday
   in HAL numbering, editor field stepping (wraps, year clamps, day
   re-clamped across month/year steps so stepping alone cannot produce
   an impossible date) and zero-padded formatting.
2. Static layer: reads the actual sources and verifies the wiring
   seams -- the menu leaf and 8-child parent array, the modal editor's
   button-event receive guard, the OK path (validate -> set -> read-back
   re-sync), the no-persistence-by-design rule (the Date & Time editor
   never calls settings_save()/settings_set_* and never touches
   HAL_RTC_* directly; the RTC is the timebase, not a settings.cfg
   field), the read shadow-register order (GetTime before GetDate) and
   the validate-before-write order in m1_rtc.c, the backup-register
   untouchability (no RTC_BKP/TAMP anywhere in the RTC module), the
   pure module's include hygiene, the System screen's read-only time
   row, and the build wiring.

Usage: python3 m1_csrc/test/m1_rtc_test.py
"""

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


def compile_run(tmp, name, sources, code):
    """Compile real sources + a generated harness TU in one invocation
    (compile+link together, like the settings-screens suite). Sanitizers
    when the toolchain has the runtime (CI); plain build otherwise (some
    MinGW hosts)."""
    harness = Path(tmp) / f"{name}_harness.c"
    harness.write_text(code)
    exe = Path(tmp) / name
    for extra in (SANITIZERS, []):
        cmd = (
            [
                CC,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-g",
                "-I",
                str(ROOT / "m1_csrc"),
            ]
            + extra
            + [str(harness)]
            + [str(s) for s in sources]
            + ["-o", str(exe)]
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


def pure_domain_tests():
    """Layer 1: compile the REAL pure domain and assert the math."""
    harness = (
        "#include <stdint.h>\n"
        "#include <stdbool.h>\n"
        "#include <stdio.h>\n"
        "#include <string.h>\n"
        "#include <assert.h>\n"
        '#include "m1_rtc_date.h"\n'
        "\n"
        """
static S_M1_DateTime_t mk(unsigned y, unsigned mo, unsigned d,
                          unsigned h, unsigned mi) {
    S_M1_DateTime_t t = { (uint16_t)y, (uint8_t)mo, (uint8_t)d,
                          (uint8_t)h, (uint8_t)mi };
    return t;
}
static void ck_valid(unsigned y, unsigned mo, unsigned d,
                     unsigned h, unsigned mi, int exp) {
    S_M1_DateTime_t t = mk(y, mo, d, h, mi);
    char b[64];
    int got = m1_rtc_datetime_valid(&t);
    snprintf(b, sizeof(b), "valid %u-%02u-%02u %02u:%02u", y, mo, d, h, mi);
    if (got != exp) { printf("FAIL %s -> %d (want %d)\\n", b, got, exp); return; }
    printf("ok %s -> %d\\n", b, got);
}
static void ck_dim(unsigned y, unsigned mo, unsigned exp) {
    char b[48];
    unsigned got = m1_rtc_days_in_month((uint16_t)y, (uint8_t)mo);
    snprintf(b, sizeof(b), "dim %u-%02u", y, mo);
    if (got != exp) { printf("FAIL %s -> %u (want %u)\\n", b, got, exp); return; }
    printf("ok %s -> %u\\n", b, got);
}
static void ck_wd(unsigned y, unsigned mo, unsigned d, unsigned exp) {
    char b[48];
    unsigned got = m1_rtc_weekday((uint16_t)y, (uint8_t)mo, (uint8_t)d);
    snprintf(b, sizeof(b), "wd %u-%02u-%02u", y, mo, d);
    if (got != exp) { printf("FAIL %s -> %u (want %u)\\n", b, got, exp); return; }
    printf("ok %s -> %u\\n", b, got);
}
static void ck_step(S_M1_DateTime_t t, E_M1_RtcField_t f, int dir,
                    unsigned ey, unsigned emo, unsigned ed,
                    unsigned eh, unsigned emi) {
    char b[96];
    m1_rtc_datetime_step(&t, f, (int8_t)dir);
    snprintf(b, sizeof(b), "step f=%d dir=%+d", (int)f, dir);
    if (t.year != ey || t.month != emo || t.day != ed || t.hour != eh || t.minute != emi) {
        printf("FAIL %s -> %u-%02u-%02u %02u:%02u (want %u-%02u-%02u %02u:%02u)\\n",
               b, t.year, t.month, t.day, t.hour, t.minute, ey, emo, ed, eh, emi);
        return;
    }
    if (dir != 0 && !m1_rtc_datetime_valid(&t)) {
        printf("FAIL %s -> impossible result %u-%02u-%02u %02u:%02u\\n",
               b, t.year, t.month, t.day, t.hour, t.minute);
        return;
    }
    printf("ok %s -> %u-%02u-%02u %02u:%02u\\n", b, t.year, t.month, t.day, t.hour, t.minute);
}
int main(void) {
    static const unsigned dim_2026[12] =
        {31,28,31,30,31,30,31,31,30,31,30,31};
    char buf[M1_RTC_DATETIME_STR_LEN];
    char tiny[5];
    S_M1_DateTime_t t;
    unsigned i;

    /* Leap years (proleptic Gregorian; 2000 yes, 2100 no) */
    assert(m1_rtc_is_leap_year(2000)); assert(m1_rtc_is_leap_year(2024));
    assert(m1_rtc_is_leap_year(2028)); assert(m1_rtc_is_leap_year(2096));
    assert(m1_rtc_is_leap_year(1900) == false);
    assert(m1_rtc_is_leap_year(2001) == false);
    assert(m1_rtc_is_leap_year(2025) == false);
    assert(m1_rtc_is_leap_year(2100) == false);

    /* Days in month: 2026 table, leap Februaries, bad months -> 0 */
    for (i = 0; i < 12; i++) ck_dim(2026, i + 1, dim_2026[i]);
    ck_dim(2024, 2, 29); ck_dim(2000, 2, 29); ck_dim(2100, 2, 28);
    ck_dim(2026, 0, 0); ck_dim(2026, 13, 0);

    /* Validation: window bounds, calendar bounds, leap-aware Feb */
    ck_valid(2000, 1, 1, 0, 0, 1);
    ck_valid(2099, 12, 31, 23, 59, 1);
    ck_valid(2026, 11, 17, 14, 5, 1);
    ck_valid(2024, 2, 29, 0, 0, 1);
    ck_valid(1999, 12, 31, 23, 59, 0);
    ck_valid(2100, 1, 1, 0, 0, 0);
    ck_valid(2025, 2, 29, 12, 0, 0);
    ck_valid(2026, 0, 10, 0, 0, 0);
    ck_valid(2026, 13, 10, 0, 0, 0);
    ck_valid(2026, 11, 0, 0, 0, 0);
    ck_valid(2026, 11, 31, 0, 0, 0);
    ck_valid(2026, 4, 31, 0, 0, 0);
    ck_valid(2026, 11, 17, 24, 0, 0);
    ck_valid(2026, 11, 17, 255, 0, 0);
    ck_valid(2026, 11, 17, 0, 60, 0);
    ck_valid(2026, 11, 17, 0, 255, 0);
    assert(m1_rtc_datetime_valid(NULL) == false);

    /* Weekday (HAL numbering 1=Mon..7=Sun), verified dates */
    ck_wd(2000, 1, 1, 6);   /* Saturday */
    ck_wd(2000, 2, 29, 2);  /* Tuesday */
    ck_wd(2024, 2, 29, 4);  /* Thursday */
    ck_wd(2026, 1, 1, 4);   /* Thursday */
    ck_wd(2026, 11, 17, 2); /* Tuesday */
    ck_wd(2099, 12, 31, 4); /* Thursday */
    ck_wd(2026, 0, 10, 0); ck_wd(2026, 13, 10, 0);
    ck_wd(2026, 1, 0, 0);  ck_wd(2026, 1, 32, 0);

    /* Stepping: year clamps at the window, month/day/hour/minute wrap */
    ck_step(mk(2026, 11, 17, 14, 5), M1_RTC_FIELD_YEAR, +1, 2027, 11, 17, 14, 5);
    ck_step(mk(2026, 11, 17, 14, 5), M1_RTC_FIELD_YEAR, -1, 2025, 11, 17, 14, 5);
    ck_step(mk(2099, 6, 15, 0, 0), M1_RTC_FIELD_YEAR, +1, 2099, 6, 15, 0, 0);
    ck_step(mk(2000, 6, 15, 0, 0), M1_RTC_FIELD_YEAR, -1, 2000, 6, 15, 0, 0);
    ck_step(mk(2026, 12, 17, 0, 0), M1_RTC_FIELD_MONTH, +1, 2026, 1, 17, 0, 0);
    ck_step(mk(2026, 1, 17, 0, 0), M1_RTC_FIELD_MONTH, -1, 2026, 12, 17, 0, 0);
    ck_step(mk(2026, 11, 17, 0, 0), M1_RTC_FIELD_DAY, +1, 2026, 11, 18, 0, 0);
    ck_step(mk(2026, 11, 30, 0, 0), M1_RTC_FIELD_DAY, +1, 2026, 11, 1, 0, 0);
    ck_step(mk(2026, 11, 1, 0, 0), M1_RTC_FIELD_DAY, -1, 2026, 11, 30, 0, 0);
    ck_step(mk(2026, 2, 28, 0, 0), M1_RTC_FIELD_DAY, +1, 2026, 2, 1, 0, 0);
    ck_step(mk(2024, 2, 28, 0, 0), M1_RTC_FIELD_DAY, +1, 2024, 2, 29, 0, 0);
    ck_step(mk(2024, 2, 29, 0, 0), M1_RTC_FIELD_DAY, +1, 2024, 2, 1, 0, 0);
    ck_step(mk(2026, 11, 17, 23, 0), M1_RTC_FIELD_HOUR, +1, 2026, 11, 17, 0, 0);
    ck_step(mk(2026, 11, 17, 0, 0), M1_RTC_FIELD_HOUR, -1, 2026, 11, 17, 23, 0);
    ck_step(mk(2026, 11, 17, 0, 59), M1_RTC_FIELD_MINUTE, +1, 2026, 11, 17, 0, 0);
    ck_step(mk(2026, 11, 17, 0, 0), M1_RTC_FIELD_MINUTE, -1, 2026, 11, 17, 0, 59);

    /* Day re-clamped across month/year steps: impossible dates unenterable */
    ck_step(mk(2026, 1, 31, 0, 0), M1_RTC_FIELD_MONTH, +1, 2026, 2, 28, 0, 0);
    ck_step(mk(2024, 1, 31, 0, 0), M1_RTC_FIELD_MONTH, +1, 2024, 2, 29, 0, 0);
    ck_step(mk(2026, 3, 31, 0, 0), M1_RTC_FIELD_MONTH, -1, 2026, 2, 28, 0, 0);
    ck_step(mk(2026, 10, 31, 0, 0), M1_RTC_FIELD_MONTH, +1, 2026, 11, 30, 0, 0);
    ck_step(mk(2024, 2, 29, 0, 0), M1_RTC_FIELD_YEAR, +1, 2025, 2, 28, 0, 0);
    ck_step(mk(2024, 2, 29, 0, 0), M1_RTC_FIELD_YEAR, -1, 2023, 2, 28, 0, 0);
    ck_step(mk(2096, 2, 29, 0, 0), M1_RTC_FIELD_YEAR, +1, 2097, 2, 28, 0, 0);
    ck_step(mk(2026, 12, 31, 0, 0), M1_RTC_FIELD_MONTH, +1, 2026, 1, 31, 0, 0);

    /* dir == 0 and unknown fields change nothing; NULL is safe */
    ck_step(mk(2026, 11, 17, 14, 5), M1_RTC_FIELD_YEAR, 0, 2026, 11, 17, 14, 5);
    ck_step(mk(2026, 11, 17, 14, 5), M1_RTC_FIELD_COUNT, +1, 2026, 11, 17, 14, 5);
    m1_rtc_datetime_step(NULL, M1_RTC_FIELD_YEAR, +1);

    /* Off-grid day input snaps into range on step, never stays impossible */
    ck_step(mk(2026, 2, 30, 0, 0), M1_RTC_FIELD_DAY, +1, 2026, 2, 1, 0, 0);

    /* Formatting: zero-padded, fixed order, safe truncation */
    t = mk(2026, 11, 17, 14, 5);
    assert(strcmp(m1_rtc_format_datetime(&t, buf, sizeof(buf)), "2026-11-17 14:05") == 0);
    t = mk(2000, 1, 1, 0, 0);
    assert(strcmp(m1_rtc_format_datetime(&t, buf, sizeof(buf)), "2000-01-01 00:00") == 0);
    t = mk(2099, 12, 31, 23, 59);
    assert(strcmp(m1_rtc_format_datetime(&t, buf, sizeof(buf)), "2099-12-31 23:59") == 0);
    t = mk(2026, 11, 17, 14, 5);
    assert(strlen(m1_rtc_format_datetime(&t, tiny, sizeof(tiny))) < sizeof(tiny));
    assert(m1_rtc_format_datetime(&t, tiny, 1)[0] == '\\0');
    assert(strcmp(m1_rtc_format_datetime(&t, NULL, 32), "") == 0);
    assert(m1_rtc_format_datetime(NULL, buf, sizeof(buf))[0] == '\\0');

    puts("pure-domain ok");
    return 0;
}
"""
    )
    out = compile_run(
        tempfile.gettempdir(),
        "m1_rtc_pure_domain",
        [ROOT / "m1_csrc" / "m1_rtc_date.c"],
        harness,
    )
    CHECK("pure-domain ok" in out, "pure-domain harness ran to completion")
    CHECK("FAIL" not in out, "no date/math case failed")


def static_seam_tests():
    """Layer 2: verify the wiring seams in the real sources."""
    menu = source("m1_csrc/m1_menu.c")
    settings_c = source("m1_csrc/m1_settings.c")
    settings_h = source("m1_csrc/m1_settings.h")
    rtc_c = source("m1_csrc/m1_rtc.c")
    rtc_h = source("m1_csrc/m1_rtc.h")
    rtc_date_c = source("m1_csrc/m1_rtc_date.c")
    rtc_date_h = source("m1_csrc/m1_rtc_date.h")
    cmake = source("cmake/m1_01/CMakeLists.txt")

    def contains(hay, needle):
        return needle in hay

    def fn_body(text, marker, endmarker):
        fn = text[text.index(marker) :]
        return fn[: fn.index(endmarker)]

    # --- Menu wiring: Date & Time leaf, 8th child of Settings ---
    CHECK(
        contains(
            menu,
            '"Date & Time", settings_date_time, NULL, NULL, 0, 0, NULL, NULL, {NULL}',
        ),
        "menu leaf record for Date & Time dispatches settings_date_time",
    )
    CHECK(
        contains(
            menu,
            '"Settings", menu_settings_init, NULL, NULL, 8, 0, menu_m1_icon_setting, NULL,',
        ),
        "menu_Settings parent declares 8 items",
    )
    CHECK(
        contains(
            menu,
            "{&menu_Settings_LCD_Notifications, &menu_Settings_System, &menu_Settings_Date_Time, &menu_Settings_Storage,",
        ),
        "parent array places Date & Time right after System",
    )
    arr = menu[menu.index("S_M1_Menu_t menu_Settings =") :]
    arr = arr[: arr.index("};")]
    CHECK(
        arr.count("&menu_") == 8,
        f"parent array holds exactly 8 submenu pointers (got {arr.count('&menu_')})",
    )
    CHECK(
        contains(settings_h, "void settings_date_time(void);"),
        "settings_date_time declared in m1_settings.h",
    )
    CHECK(
        contains(settings_c, "void settings_date_time(void)\n{"),
        "settings_date_time defined",
    )

    # --- Editor: modal idiom seams (guard, axes, apply, re-sync, no store) ---
    editor = fn_body(
        settings_c,
        "void settings_date_time(void)\n{",
        "} // void settings_date_time(void)",
    )
    i_recv = editor.index("xQueueReceive(button_events_q_hdl, &this_button_status, 0);")
    CHECK(
        i_recv
        < editor.index("if ( ret!=pdTRUE )")
        < editor.index("this_button_status.event["),
        "Date & Time editor guards the button-event read before any event use",
    )
    CHECK(
        contains(editor, "m1_rtc_datetime_step(&edit, (E_M1_RtcField_t)sel_field,"),
        "value stepping goes through the pure domain (host-tested math)",
    )
    CHECK(
        contains(editor, "m1_rtc_get_datetime(&edit);")  # seed or re-sync read
        and editor.index("m1_rtc_set_datetime(&edit)")
        < editor.index("(void)m1_rtc_get_datetime(&edit);"),
        "OK applies through m1_rtc_set_datetime, then re-syncs from a read-back",
    )
    CHECK(
        contains(
            editor,
            "xQueueReset(main_q_hdl); // Reset main q before return (nothing to save)",
        ),
        "BACK exits through the queue-reset path without any save step",
    )
    CHECK(
        not contains(editor, "settings_save")
        and not contains(editor, "settings_set_")
        and not contains(editor, "session_dirty")
        and not contains(editor, "settings_get()"),
        "editor never touches the settings store (RTC is the timebase, no SD duplication)",
    )
    CHECK(
        not contains(editor, "HAL_RTC_"),
        "editor never calls the RTC HAL directly (m1_rtc is the only mediated path)",
    )

    # --- System screen: read-only time row, refreshed on entry ---
    sys_fn = fn_body(
        settings_c, "void settings_system(void)\n{", "} // void settings_system(void)"
    )
    CHECK(
        contains(sys_fn, "m1_rtc_get_datetime(&rtc_now);")
        and contains(
            sys_fn, "m1_rtc_format_datetime(&rtc_now, time_str, sizeof(time_str));"
        ),
        "System screen reads and formats the RTC time on entry",
    )
    CHECK(
        sys_fn.index("m1_rtc_get_datetime(&rtc_now);")
        < sys_fn.index(
            "settings_system_gui_update(sel_row, name_buf, time_ok, time_str, storage_ok, free_b, total_kb);"
        ),
        "time is read before the first System-screen draw",
    )
    CHECK(
        contains(settings_c, '"Time not set"'),
        "System screen shows an explicit not-set state after backup-domain loss",
    )
    CHECK(
        contains(sys_fn, "sel_row = (sel_row==0) ? 1 : 0; // Two actionable rows"),
        "the time row stays informational (Name/Reset remain the only selectable rows)",
    )

    # --- m1_rtc.c: HAL semantics and forbidden accesses ---
    get_fn = fn_body(
        rtc_c,
        "bool m1_rtc_get_datetime(S_M1_DateTime_t *out)\n{",
        "} // bool m1_rtc_get_datetime",
    )
    CHECK(
        get_fn.index("HAL_RTC_GetTime") < get_fn.index("HAL_RTC_GetDate"),
        "read path uses the mandatory GetTime-then-GetDate shadow-register order",
    )
    CHECK(
        get_fn.index("HAL_RTC_GetDate") < get_fn.index("m1_rtc_datetime_valid(&dt)"),
        "read path validates before accepting a calendar value",
    )
    set_fn = fn_body(
        rtc_c,
        "bool m1_rtc_set_datetime(const S_M1_DateTime_t *dt)\n{",
        "} // bool m1_rtc_set_datetime",
    )
    CHECK(
        set_fn.index("m1_rtc_datetime_valid(dt)") < set_fn.index("HAL_RTC_SetTime"),
        "write path validates (rejects impossible dates) before any register write",
    )
    CHECK(
        set_fn.index("HAL_RTC_SetTime") < set_fn.index("HAL_RTC_SetDate"),
        "write path sets time then date (HAL owns init-mode enter/exit)",
    )
    CHECK(
        contains(set_fn, "date.WeekDay = m1_rtc_weekday("),
        "write path stores the computed true weekday",
    )
    for bad in ("RTC_BKP", "TAMP", "BKUPWrite", "FLASH_", "sector"):
        CHECK(
            not contains(rtc_c, bad) and not contains(rtc_date_c, bad),
            f"RTC module never references {bad}",
        )
    CHECK(
        contains(rtc_c, "&hrtc") and contains(rtc_c, '#include "main.h"'),
        "m1_rtc uses the shared hrtc handle from main.h",
    )

    # --- Pure-module hygiene: no HAL/FreeRTOS/u8g2 in the pure domain ---
    for bad in (
        '#include "stm32',
        "#include " + '"FreeRTOS',
        '#include "main.h"',
        "u8g2_",
        "hrtc",
    ):
        CHECK(
            not contains(rtc_date_c, bad) and not contains(rtc_date_h, bad),
            f"pure domain stays free of {bad}",
        )
    CHECK(
        contains(rtc_h, "bool m1_rtc_get_datetime(S_M1_DateTime_t *out);")
        and contains(rtc_h, "bool m1_rtc_set_datetime(const S_M1_DateTime_t *dt);"),
        "m1_rtc.h exposes exactly the get/set contract",
    )

    # --- Build wiring ---
    CHECK(
        contains(cmake, "../../m1_csrc/m1_rtc.c")
        and contains(cmake, "../../m1_csrc/m1_rtc_date.c"),
        "RTC sources are compiled into the app (cmake/m1_01)",
    )


def main():
    pure_domain_tests()
    static_seam_tests()
    print(f"m1_rtc_test: {PASSED} passed, {FAILED} failed")
    if FAILED:
        sys.exit(1)


if __name__ == "__main__":
    main()
