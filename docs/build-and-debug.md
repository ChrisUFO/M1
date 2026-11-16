# Build and debug

This is the canonical entry point. Detailed commands remain in
[`documentation/mbt.md`](../documentation/mbt.md), and the graphical VS Code
walkthrough remains in
[`documentation/usingVSCode.md`](../documentation/usingVSCode.md).

## Supported build paths

| Environment | Toolchain | Build definition | Output |
| --- | --- | --- | --- |
| Linux/macOS shell | Arm GNU toolchain, CMake, Ninja | `Makefile`, CMake presets | `artifacts/` |
| VS Code | Arm GNU 14.2.Rel1 | CMake presets | `out/build/gcc-14_2_build-{debug,release}` |
| STM32CubeIDE | IDE-bundled GNU tools | Eclipse project files | `Debug/` or `Release/` |

STM32CubeIDE 1.17.0 with GNU Tools for STM32 12.3.rel1 and standalone Arm GNU
14.2.Rel1 are documented toolchains. Cross-toolchain binary identity is not
currently guaranteed.

## Command-line build

```sh
./build.sh --release
```

The script requires a clean Git tree and records the source revision and build
tools. Expected release artifacts include ELF, BIN, HEX, CRC-appended BIN, and
a text manifest under `artifacts/`.

### Windows build path (no SRecord required)

`build.sh` runs under Git-Bash with the Arm GNU toolchain, CMake, and Ninja on
`PATH`. SRecord is optional: `CMakeLists.txt` resolves exactly one CRC emitter
at configure time and prints the choice as a `CRC emitter:` status line.

| Emitter | Selected when | Invocation |
| --- | --- | --- |
| `srec_cat` (SRecord) | `srec_cat` is on `PATH` (canonical) | `srec_cat <bin> -binary -crop 0x0 0x000FFC14 -STM32_Little_Endian 0x000FFC14 -o <out> -binary` |
| `tools/emulate_srec_cat_crc.py` | `srec_cat` is absent and Python 3 is installed (typical Windows host) | `python tools/emulate_srec_cat_crc.py <bin> <out> --crop-end 0xFFC14` |

If neither is available, CMake fails at configure time with an actionable
message — the CRC step is never silently skipped, because the update path
verifies the appended CRC. The two emitters are byte-equivalent **for images of
exactly `0xFFC14` bytes** — the only shape the canonical build produces; for
shorter images `srec_cat` pads with `0x00` and exits 0 while the Python emitter
refuses (exit 2). See [CRC emitter parity](#crc-emitter-parity) below.

After the link, `build.sh` asserts the exact sizes before copying anything into
`artifacts/`, in both plain and `--release` mode: the application image
`<artifact>.bin` must be exactly `1047572` bytes (`0xFFC14`), the CRC-appended
`_wCRC.bin` exactly `1047576` bytes (`0xFFC18`), and the former must be a
byte-identical prefix of the latter. Checking only the total would accept a
grown image: both emitters crop at `0xFFC14`, so an application image that
outgrew the crop end would be silently truncated to exactly `0xFFC18`.

Size and prefix still leave the four CRC bytes at `0xFFC14` unconstrained, so
`--release` does not rely on them: the parity harness re-derives the
CRC-appended image from `<artifact>.bin` and byte-compares it against the exact
`_wCRC.bin` that is copied to `artifacts/` and hashed into the manifest. A
wrongly encoded (for example big-endian) CRC word keeps both the size and the
prefix, so without that comparison it would ship.

### Release manifest

`./build.sh --release` writes `<artifact>_wCRC.manifest.txt` next to the
binaries, and only after the [CRC emitter parity](#crc-emitter-parity) gate
passes. A manifest left by an earlier run is removed as the build starts — in
plain mode too, because a plain build overwrites the `artifacts/` binaries but
never writes a manifest, so a leftover one would describe bytes that are no
longer there — and the new one is written aside and renamed into place, so an
aborted build never leaves a manifest beside freshly built (or previously built)
binaries. Keys:
`binary`, `commit`, `dirty`, `branch`, `flags`, `source_date_epoch`,
`source_date_utc`, `build_timestamp_utc`, `size_bytes`, `md5`, `sha256`,
`flash_text`, `flash_data`, `ram_bss`. Hashing is portable: `md5sum` is
preferred with `md5 -q` as the macOS fallback, and `sha256sum` with
`shasum -a 256` (see `scripts/portable_hash.sh`), so the same keys are produced
identically on Git-Bash/Windows, Linux, and macOS.

## CRC-appended image

The build appends the STM32 CRC to every image as a post-link step. The layout
is fixed by the firmware update path:

- the application image is cropped at `0x000FFC14` (`1047572` bytes);
- the 32-bit CRC of exactly those bytes is appended **little-endian** at
  `0x000FFC14`;
- the resulting file is exactly `1047576` bytes (`0xFFC18`).

This address is coupled to `S_M1_FW_CONFIG_t`; see
[Firmware image and update](protocols/firmware-update.md). Do not change the
metadata structure, linker section, crop address, or CRC command independently.

### Endianness and word-order cross-check

The appended word is the STM32 hardware CRC unit, i.e. CRC-32/MPEG-2:

| Parameter | Value |
| --- | --- |
| Polynomial | `0x04C11DB7` |
| Initial value | `0xFFFFFFFF` |
| Input reflection | none |
| Output reflection | none |
| Final XOR | none |
| Data word width | 32-bit, fed as **little-endian** words |
| Result placement | little-endian at `0x000FFC14` |

Trailing bytes of a partial final word are zero-padded; a correct image is
word-aligned (`0xFFC14` is a multiple of 4), so this never triggers in practice.
This is the configuration `HAL_CRC_Accumulate()` feeds on-device — the same
recipe ST publishes for its post-build `srec_cat -STM32_Little_Endian` step —
hence `srec_cat` and the Python emitter agree byte for byte.

Self-check: `python tools/emulate_srec_cat_crc.py --self-test` asserts the
published reference vectors (`00000000` → `0xC704DD7B`, `04030201` →
`0x793737CD`).

### CRC emitter parity

`sh scripts/verify_crc_parity.sh` (from the repo root) generates four
deterministic full-size images with `tools/crc_test_vectors.py` — nothing
~1 MiB is stored in git — and checks each one:

- **input provenance:** every generated image's SHA-256 is compared against the
  digest recorded for that vector in `scripts/crc_parity_golden.txt`, and the
  generator's vector names are compared against the table's, so a changed
  generator (or a spec added without a golden row) fails as drift instead of
  escaping coverage;
- with `srec_cat` on `PATH`: both emitters run and their outputs are
  byte-compared (`cmp`), plus size (`0xFFC18`) and CRC-field placement;
- without `srec_cat`: the Python emitter's output digest and CRC word are
  compared against the same golden table, captured from the authoritative
  `srec_cat` pipeline;
- each vector reports its own `PASS`/`FAIL` line, derived from that vector's
  checks, so a partially failing run can never read as green per vector.

The script also pins the documented short-image divergence (the Python emitter
must refuse a 400-byte image) and, when `build/<artifact>.bin` exists or a path
is passed as `$1`, runs the **real build image** through both emitters and
byte-compares them, asserting a `0xFFC18`-byte output. That is the image the
release path actually ships; the synthetic vectors only cover the content space.
`--expect-wcrc <file>` adds the shipped-image check: the CRC-appended file is
compared byte-for-byte against the derivation from the real build image, which
is the only check that constrains the appended CRC word itself.

The script exits non-zero on any mismatch and prints a one-line
`RESULT: PASS/FAIL` summary.

`./build.sh --release` runs this harness on `build/<artifact>.bin` with
`--expect-wcrc build/<artifact>_wCRC.bin` — the exact bytes it copies to
`artifacts/` and hashes into the manifest — before anything is copied and before
the manifest is written, and aborts the release if any check fails. That is the
only automated invocation the repository has (there is no CI), so a release
always proves emitter equivalence, golden-table integrity and shipped-image byte
equality. Plain `./build.sh` builds skip the harness deliberately: it generates
four ~1 MiB images and would slow local iteration.

`--release` therefore also requires Python 3 on `PATH`, even on a host where
`srec_cat` is installed and CMake would not need it, because the harness runs
the Python emitter on every vector and on the real build image.

`scripts/crc_parity_golden.txt` may only be re-baselined with
`sh scripts/verify_crc_parity.sh --update-golden` on a host where `srec_cat` is
on `PATH`: the script refuses otherwise and requires the Python emitter to
agree byte-for-byte before writing, because a table regenerated from the
emulator would grade the emulator against itself.

## Debug probes

The repository includes VS Code configurations for ST-Link and J-Link. Verify
local executable paths before launching. The external connector sheet assigns:

| External GPIO | Function |
| --- | --- |
| 10 | SWCLK |
| 11 | SWDIO |
| 12 | UART1 TX |
| 13 | UART1 RX |

Confirm ground, target voltage, connector orientation, and hardware revision
before attaching a probe.

## Reproducibility record

Every production build should record:

- source commit SHA;
- clean/dirty workspace state;
- build preset and build type;
- compiler, linker, CMake, Ninja, and SRecord versions;
- firmware version and region constants;
- hardware target;
- artifact SHA-256 values;
- release qualification reference.

The release build emits a generated manifest alongside its artifacts.
