#!/usr/bin/env python3
"""Host tests for the PR-2 settings screens (LCD & Notifications, System).

Two layers, same technique as m1_settings_store_test.py (real sources, no
mocks of the code under test):

1. Compiled layer: the M1_SETTINGS_SCREENS_PURE_CORE region is extracted
   verbatim from m1_csrc/m1_settings.c and compiled with a generated
   harness -- value stepping (brightness/timeout bounds incl. Off), label
   formatting, and the bounded-copy helper.
2. Static layer: reads the actual source files and verifies the wiring
   seams -- menu records and parent array, live-apply calls
   (fb_set_brightness_pct / fb_set_led_notifications_enabled), the
   exit-save path, the runtime screen-timeout consumer (and that no
   compile-time LCD_SAVER_PERIOD remains anywhere in the app sources),
   the LED-notification gate placement (success/notify gated, error and
   power never gated, no lp5814_* bypass), the boot application order,
   and the reset flow ordering.

Usage: python3 m1_csrc/test/m1_settings_screens_test.py
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
        last = r.stderr
    else:
        raise AssertionError(f"{name} compile failed:\n{last}")
    r = subprocess.run([str(exe)], capture_output=True, text=True)
    if r.returncode != 0:
        raise AssertionError(f"{name} failed at runtime:\n{r.stdout}{r.stderr}")
    return r.stdout


def pure_core_tests():
    """Layer 1: compile the extracted pure core and assert the math."""
    scr = source("m1_csrc/m1_settings.c")
    core = region(
        scr,
        "/* ================= M1_SETTINGS_SCREENS_PURE_CORE BEGIN ================= */",
        "/* ================= M1_SETTINGS_SCREENS_PURE_CORE END =================== */",
    )

    harness = (
        "#include <stdint.h>\n"
        "#include <stddef.h>\n"
        "#include <stdio.h>\n"
        "#include <string.h>\n"
        "#include <assert.h>\n"
        "\n"
        + core
        + """
static void ck_bright(uint8_t cur, int8_t dir, uint8_t exp) {
    char b[32];
    uint8_t got = settings_scr_bright_step(cur, dir);
    snprintf(b, sizeof(b), "bright %u dir %d", cur, dir);
    if (got != exp) { printf("FAIL %s -> %u (want %u)\\n", b, got, exp); return; }
    printf("ok %s -> %u\\n", b, got);
}
static void ck_timeout(uint16_t cur, int8_t dir, uint16_t exp) {
    char b[32];
    uint16_t got = settings_scr_timeout_step(cur, dir);
    snprintf(b, sizeof(b), "timeout %u dir %d", cur, dir);
    if (got != exp) { printf("FAIL %s -> %u (want %u)\\n", b, got, exp); return; }
    printf("ok %s -> %u\\n", b, got);
}
int main(void) {
    char buf[8];

    /* Brightness: +/-10 clamped to 0..100 */
    ck_bright(0, +1, 10); ck_bright(10, +1, 20); ck_bright(95, +1, 100);
    ck_bright(100, +1, 100); ck_bright(99, +1, 100);
    ck_bright(100, -1, 90); ck_bright(10, -1, 0); ck_bright(5, -1, 0);
    ck_bright(0, -1, 0); ck_bright(0, 0, 0); ck_bright(100, 0, 100);

    /* Timeout: +/-10 clamped to 0(Off)/10..120; Off is one Left below 10 */
    ck_timeout(0, +1, 10); ck_timeout(10, +1, 20); ck_timeout(115, +1, 120);
    ck_timeout(120, +1, 120); ck_timeout(119, +1, 120);
    ck_timeout(120, -1, 110); ck_timeout(20, -1, 10); ck_timeout(10, -1, 0);
    ck_timeout(0, -1, 0); ck_timeout(0, 0, 0);
    /* Off-grid stored values step arithmetically (compiled default 45) */
    ck_timeout(45, +1, 55); ck_timeout(45, -1, 35);
    ck_timeout(17, +1, 27); ck_timeout(17, -1, 7); /* mid-band still >=10 */

    /* Labels */
    assert(strcmp(settings_scr_timeout_label(0, buf, sizeof(buf)), "Off") == 0);
    assert(strcmp(settings_scr_timeout_label(45, buf, sizeof(buf)), "45s") == 0);
    assert(strcmp(settings_scr_timeout_label(120, buf, sizeof(buf)), "120s") == 0);
    assert(strcmp(settings_scr_onoff_label(1), "On") == 0);
    assert(strcmp(settings_scr_onoff_label(0), "Off") == 0);

    /* Bounded copy: exact, overlong, empty, degenerate size */
    {
        char d[8];
        settings_str_clip(d, sizeof(d), "abc");   assert(strcmp(d, "abc") == 0);
        settings_str_clip(d, sizeof(d), "0123456789ABCDEFGH"); assert(strcmp(d, "0123456") == 0);
        settings_str_clip(d, sizeof(d), "");      assert(d[0] == '\\0');
        settings_str_clip(d, 1, "xyz");           assert(d[0] == '\\0');
        settings_str_clip(NULL, 0, "xyz"); /* size 0 guard: no write, no crash */
    }
    puts("pure-core ok");
    return 0;
}
"""
    )

    out = compile_run(tempfile.gettempdir(), "m1_settings_screens_core", harness)
    CHECK("pure-core ok" in out, "pure core harness ran to completion")
    CHECK("FAIL" not in out, "no stepping/label/clip case failed")


def static_seam_tests():
    """Layer 2: verify the wiring seams in the real sources."""
    menu = source("m1_csrc/m1_menu.c")
    settings_c = source("m1_csrc/m1_settings.c")
    settings_h = source("m1_csrc/m1_settings.h")
    system_c = source("m1_csrc/m1_system.c")
    system_h = source("m1_csrc/m1_system.h")
    sysinit_c = source("m1_csrc/m1_sys_init.c")
    orch_c = source("m1_csrc/m1_feedback_orchestration.c")
    mgr_h = source("m1_csrc/m1_feedback_manager.h")
    mgr_c = source("m1_csrc/m1_feedback_manager.c")

    def contains(hay, needle):
        return needle in hay

    # --- Menu wiring: two new leaves, parent count 7, new screens first ---
    CHECK(
        contains(
            menu,
            '"LCD & Notifications", settings_lcd_notifications, NULL, NULL, 0, 0, NULL, NULL, {NULL}',
        ),
        "menu leaf record for LCD & Notifications dispatches settings_lcd_notifications",
    )
    CHECK(
        contains(
            menu, '"System", settings_system, NULL, NULL, 0, 0, NULL, NULL, {NULL}'
        ),
        "menu leaf record for System dispatches settings_system",
    )
    CHECK(
        contains(
            menu,
            '"Settings", menu_settings_init, NULL, NULL, 7, 0, menu_m1_icon_setting, NULL,',
        ),
        "menu_Settings parent declares 7 items",
    )
    CHECK(
        contains(
            menu,
            "{&menu_Settings_LCD_Notifications, &menu_Settings_System, &menu_Settings_Storage,",
        ),
        "parent array leads with the two new screens",
    )
    arr = menu[menu.index("S_M1_Menu_t menu_Settings =") :]
    arr = arr[: arr.index("};")]
    CHECK(
        arr.count("&menu_") == 7,
        f"parent array holds exactly 7 submenu pointers (got {arr.count('&menu_')})",
    )

    # --- Screen functions declared and defined ---
    CHECK(
        contains(settings_h, "void settings_lcd_notifications(void);"),
        "settings_lcd_notifications declared in m1_settings.h",
    )
    CHECK(
        contains(settings_h, "void settings_system(void);"),
        "settings_system declared in m1_settings.h",
    )
    CHECK(
        contains(settings_c, "void settings_lcd_notifications(void)\n{"),
        "settings_lcd_notifications defined",
    )
    CHECK(
        contains(settings_c, "void settings_system(void)\n{"), "settings_system defined"
    )

    # --- LCD screen: live application + RAM setters + exit save ---
    lcd_fn = settings_c[settings_c.index("void settings_lcd_notifications(void)\n{") :]
    lcd_fn = lcd_fn[: lcd_fn.index("} // void settings_lcd_notifications(void)")]
    CHECK(
        contains(lcd_fn, "settings_set_brightness_pct(bright);")
        and contains(lcd_fn, "fb_set_brightness_pct(bright);"),
        "brightness edits update the store image AND apply live (preview)",
    )
    CHECK(
        contains(lcd_fn, "settings_set_screen_timeout_s(timeout_s);"),
        "timeout edits update the store image (consumer reads it periodically)",
    )
    CHECK(
        contains(lcd_fn, "settings_set_led_notifications(led_notify);")
        and contains(lcd_fn, "fb_set_led_notifications_enabled(led_notify != 0u);"),
        "LED toggle updates the store image AND applies live",
    )
    CHECK(
        contains(lcd_fn, "settings_exit_save(); // Persist this session atomically"),
        "BACK persists through the shared exit-save path",
    )
    CHECK(
        contains(settings_c, "if ( !settings_save() )")
        and contains(settings_c, "Save failed"),
        "exit path surfaces a refused/failed save to the user",
    )
    CHECK(not contains(settings_c, "lp5814_"), "screens never drive lp5814_* directly")

    # --- System screen: vkb, storage, reset flow ordering ---
    CHECK(
        contains(
            settings_c,
            'm1_vkb_get_filename("Device name", new_name, new_name, generated)',
        ),
        "device name edited through m1_vkb_get_filename",
    )
    CHECK(
        contains(settings_c, "settings_set_device_name(new_name);"),
        "device name persisted through the store setter (bounded)",
    )
    CHECK(
        contains(settings_c, "fs_get_free_space(&free_b)"),
        "storage free space read via fs_get_free_space()",
    )
    reset_fn = settings_c[
        settings_c.index("static void settings_system_reset_confirm(void)\n{") :
    ]
    reset_fn = reset_fn[
        : reset_fn.index("} // static void settings_system_reset_confirm(void)")
    ]
    i_reset = reset_fn.index("settings_reset_defaults();")
    i_bright = reset_fn.index("fb_set_brightness_pct(settings_get()->brightness_pct);")
    i_led = reset_fn.index(
        "fb_set_led_notifications_enabled(settings_get()->led_notifications != 0u);"
    )
    i_save = reset_fn.index("settings_save()")
    i_reboot = reset_fn.index("power_reboot();")
    CHECK(
        i_reset < i_bright < i_led < i_save < i_reboot,
        "reset flow: defaults -> live re-apply -> persist -> reboot",
    )
    CHECK(
        not contains(reset_fn, "FLASH_") and not contains(reset_fn, "sector"),
        "reset never touches flash sectors (SD-only settings)",
    )

    # --- Runtime screen timeout consumer ---
    CHECK(
        contains(system_c, "timeout_s = settings_get()->screen_timeout_s;"),
        "lcd_saver_period_ms reads the stored timeout",
    )
    CHECK(
        contains(system_c, "if ( timeout_s==0u )")
        and contains(system_c, "return 0u; // Off: the display never sleeps"),
        "timeout 0 (Off) yields a never-sleep period",
    )
    CHECK(
        contains(system_c, "if ( period_ms==0u )")
        and contains(system_c, "fb_sleep_timer_wake();"),
        "saver wakes an already-asleep session when Off takes effect",
    )
    CHECK(
        not contains(system_c, "LCD_SAVER_PERIOD")
        and not contains(system_h, "LCD_SAVER_PERIOD"),
        "no compile-time LCD_SAVER_PERIOD remains in m1_system.[ch]",
    )

    # --- LED-notification gate placement ---
    CHECK(
        contains(mgr_h, "void fb_set_led_notifications_enabled(bool enabled);")
        and contains(mgr_h, "bool fb_get_led_notifications_enabled(void);"),
        "manager exposes the LED-notification gate setter/getter",
    )
    CHECK(
        contains(mgr_c, "g_led_notifications = true;"),
        "fb_manager_init resets the gate to the compiled default (enabled)",
    )
    CHECK(
        contains(orch_c, "if ( !fb_get_led_notifications_enabled() )"),
        "orchestration consults the gate before issuing notification LED legs",
    )
    err_fn = orch_c[orch_c.index("void fb_alert_error(fb_owner_t owner)\n{") :]
    err_fn = err_fn[: err_fn.index("} // void fb_alert_error(fb_owner_t owner)")]
    CHECK(
        not contains(err_fn, "led_notifications")
        and contains(err_fn, "SEQ_PROG_ALERT_ERROR_LED"),
        "error alert LED leg is never gated (critical cue)",
    )
    for critical in (
        "fb_pwr_boot",
        "fb_pwr_batt_crit",
        "fb_net_emulating_start",
        "fb_usb_connect",
    ):
        fn = orch_c[orch_c.index(f"void {critical}(") :]
        fn = fn[: fn.index(f"}} // void {critical}(")]
        CHECK(
            not contains(fn, "led_notifications"),
            f"{critical} (critical/state cue) is not gated by the LED setting",
        )

    # --- Boot application order ---
    i_tasks = sysinit_c.index("m1_tasks_init();")
    i_bright_boot = sysinit_c.index(
        "fb_set_brightness_pct(settings_get()->brightness_pct);"
    )
    i_led_boot = sysinit_c.index(
        "fb_set_led_notifications_enabled(settings_get()->led_notifications != 0u);"
    )
    CHECK(
        i_tasks < i_bright_boot < i_led_boot,
        "boot applies brightness then the LED gate, both after task creation",
    )


def main():
    pure_core_tests()
    static_seam_tests()
    print(f"m1_settings_screens_test: {PASSED} passed, {FAILED} failed")
    if FAILED:
        sys.exit(1)


if __name__ == "__main__":
    main()
