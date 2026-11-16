# Windows verification adapters (issue #49)

The two run adapters that make the upstream host-test gate and the srecord
CRC emission reproducible on the verified Windows host, promoted from
PR-2's baseline-evidence recipe (`documentation/merge1-baseline-evidence.md`
§3/§5/§8) to repo artifacts. Both are **verification tooling only** — they
change nothing in the firmware build.

## 1. `emulate_srec_cat_crc.py` — emulated srec_cat CRC emitter

`srec_cat` (the srecord suite) is not installed on this host, which blocks
the CMake POST_BUILD CRC emission step (the "Windows emitter issue"). This
script reproduces that step exactly:

```
python tools/emulate_srec_cat_crc.py <in.bin> <out_wCRC.bin> [--crop-end 0xFFC14]
python tools/emulate_srec_cat_crc.py --self-test
```

- Algorithm: STM32 hardware CRC unit == CRC-32/MPEG-2 — poly `0x04C11DB7`,
  init `0xFFFFFFFF`, no reflection, no final XOR — fed 32-bit little-endian
  words; result appended little-endian at the crop end (`0x000FFC14`, where
  the linker places the end of `.FW_CONFIG_SECTION`).
- `--self-test` runs the reference vectors from the baseline evidence
  (`00000000 -> 0xC704DD7B`, `04030201 -> 0x793737CD`) and is the quick
  pipeline check.
- Determinism: repeated emission on identical input is byte-identical
  (verified; capture with `sha256sum` when recording evidence).

Reference values to compare against when re-verifying a tree: see the CRC
tables in `documentation/merge1-baseline-evidence.md` §3/§9.4 — note the
image hash/CRC *value* changes whenever firmware content changes; what must
reproduce is the *behavior* (crop length == image size `0xFFC14`, `_wCRC`
size `0xFFC18`, self-test vectors, byte-identical repeat emission).

## 2. `gcc-nosan/` — host-test compiler shim

The only host gcc (WinLibs MinGW-w64 15.2.0) lacks the ASan/UBSan runtimes
the upstream harnesses hardcode, and gcc 15 promotes three warning classes
to errors on the harnesses' *generated stub* code that the project's
reference gcc 14 kept as warnings. The shim closes exactly those two gaps:

```
gcc -O2 -o gcc-nosan.exe tools/gcc-nosan/gcc-nosan.c   # build once, host gcc
```

- Strips `-fsanitize=` options whose sanitizer set is only
  {address, undefined} (single or combined form); any other sanitizer
  passes through untouched.
- Appends `-Wno-error=<class>` for the three gcc-15 classes that fire on
  harness-generated stub code (`sign-compare`, `misleading-indentation`,
  `pointer-to-int-cast`) whenever the command line carries `-Werror` in
  any form (bare or classed).
- Execs the real compiler (`GCC_NOSAN_CC` env, default `gcc`) and
  propagates its exit status.
- Runs are **uninstrumented**: all assertions execute, ASan/UBSan
  memory-safety coverage is absent on this host (recorded, not silent).

### Using it with the upstream host suites

```sh
SHIM=/path/to/gcc-nosan.exe            # plus a copy named cc.exe for the
                                        # three harnesses that hardcode `cc`
export PATH="/path/to/shimdir:$PATH"    # shimdir contains gcc-nosan.exe + cc.exe
export CC=$SHIM
export PYTHONUTF8=1                     # harnesses read UTF-8 sources on cp1252 hosts

# python harnesses
python m1_csrc/test/m1_pass1_memory_safety_test.py        # etc. (see §5 table)
# shell harness
CC=$SHIM sh NFC/NFC_drv/common/test/run_mfc_suite.sh
```

Known upstream test debt (unchanged by these adapters, documented in the
baseline evidence §5/§7): `lfrfid/test/result_render_test.py` is genuinely
broken at the adopted pin (undefined `m1_lcd_wake_restart_timer` in the
generated test — fails on any toolchain), and `m1_pass4_api_test.py`'s
`uart()` subtest is revision-pinned to an object that exists in no public
clone (4/5 subtests run and pass).
