#!/bin/sh
# CRC emitter parity harness (issue #40).
#
# Verifies that the two interchangeable CRC emitters produce the SAME image:
#   1. srec_cat (authoritative, when installed):
#        srec_cat <in> -binary -crop 0x0 0x000FFC14 \
#            -STM32_Little_Endian 0x000FFC14 -o <out> -binary
#   2. the repo's pure-Python emulator used on hosts without SRecord:
#        python tools/emulate_srec_cat_crc.py <in> <out> --crop-end 0xFFC14
#
# Inputs are generated deterministically by tools/crc_test_vectors.py (four
# full-size 0xFFC14 images) so no ~1 MiB binaries are stored in git. Expected
# output digests + CRC words live in scripts/crc_parity_golden.txt and were
# captured from the authoritative srec_cat pipeline.
#
# Modes:
#   * srec_cat on PATH  -- both emitters run on every vector and are
#     byte-compared (cmp), plus size/CRC-field assertions on both outputs.
#   * srec_cat absent   -- the emitter output is checked against the committed
#     golden digests + CRC field only (host has no reference pipeline).
#
# Usage (from the repo root):  sh scripts/verify_crc_parity.sh
# Exit status: 0 = all vectors pass, 1 = any mismatch or environment error.
set -eu

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$REPO_ROOT"
. "$REPO_ROOT/scripts/portable_hash.sh"

CROP_END=1047572    # 0xFFC14 -- crop end == CRC append offset
EXPECTED_BYTES=1047576 # 0xFFC18 -- cropped image + 4-byte CRC word
GOLDEN_FILE="scripts/crc_parity_golden.txt"

FAIL_COUNT=0
fail() {
    echo "FAIL: $*" >&2
    FAIL_COUNT=$((FAIL_COUNT + 1))
}

PYTHON=""
for candidate in python3 python py; do
    if command -v "$candidate" >/dev/null 2>&1; then
        PYTHON=$candidate
        break
    fi
done
if [ -z "$PYTHON" ]; then
    echo "FAIL: no Python 3 interpreter found on PATH (need python3/python/py)" >&2
    exit 1
fi

SREC_CAT=""
if command -v srec_cat >/dev/null 2>&1; then
    SREC_CAT=srec_cat
fi

echo "=== CRC emitter parity harness ==="
echo "repo:     $REPO_ROOT"
echo "python:   $PYTHON"
if [ -n "$SREC_CAT" ]; then
    echo "emitters: srec_cat + python emulator (byte-compared)"
else
    echo "emitters: python emulator only (srec_cat absent -- checked against golden table)"
fi

WORK=$(mktemp -d 2>/dev/null || mktemp -d -t crcparity)
trap 'rm -rf "$WORK"' EXIT INT TERM
mkdir -p "$WORK/in" "$WORK/emu" "$WORK/ref"

# Load the golden table, stripping comments, blank lines, and any CR (Windows
# Python text-mode stdout emits CRLF; the repo pins LF but be defensive).
sed -e 's/#.*//' -e 's/[[:space:]]*$//' -e '/^[[:space:]]*$/d' "$GOLDEN_FILE" \
    | tr -d '\r' > "$WORK/golden.txt"
VECTOR_COUNT=$(wc -l < "$WORK/golden.txt" | tr -d ' ')
if [ "$VECTOR_COUNT" -lt 3 ]; then
    fail "golden table has $VECTOR_COUNT vectors, need at least 3"
fi

echo "generating $VECTOR_COUNT deterministic inputs via tools/crc_test_vectors.py"
"$PYTHON" tools/crc_test_vectors.py --out "$WORK/in" | tr -d '\r' \
    > "$WORK/inputs.txt"
while read -r name in_sha; do
    [ -n "$name" ] || continue
    if [ ! -f "$WORK/in/$name.bin" ]; then
        fail "$name: generator did not produce $name.bin"
        continue
    fi
    size=$(wc -c < "$WORK/in/$name.bin" | tr -d ' ')
    if [ "$size" != "$CROP_END" ]; then
        fail "$name: input is $size bytes, expected $CROP_END (0xFFC14)"
    fi
    actual=$(sha256_of "$WORK/in/$name.bin")
    if [ "$actual" != "$in_sha" ]; then
        fail "$name: generated input digest $actual != recorded $in_sha (generator drift)"
    fi
done < "$WORK/inputs.txt"

echo
echo "--- checking each vector ---"
while read -r name in_sha out_sha crc_le; do
    [ -n "$name" ] || continue
    in_file="$WORK/in/$name.bin"
    emu_file="$WORK/emu/$name.bin"

    if [ ! -f "$in_file" ]; then
        fail "$name: missing input image (skipped)"
        continue
    fi

    # 1. Python emulator output.
    if ! "$PYTHON" tools/emulate_srec_cat_crc.py "$in_file" "$emu_file" \
            --crop-end 0xFFC14 > "$WORK/emu/$name.log" 2>&1; then
        fail "$name: python emitter failed (see output below)"
        sed 's/^/    /' "$WORK/emu/$name.log" >&2
        continue
    fi
    emu_size=$(wc -c < "$emu_file" | tr -d ' ')
    if [ "$emu_size" != "$EXPECTED_BYTES" ]; then
        fail "$name: emitter output is $emu_size bytes, expected $EXPECTED_BYTES (0xFFC18)"
    fi
    emu_sha=$(sha256_of "$emu_file")
    if [ "$emu_sha" != "$out_sha" ]; then
        fail "$name: emitter output digest $emu_sha != golden $out_sha"
    fi
    emu_crc=$(tail -c 4 "$emu_file" | od -An -tx1 | tr -d ' \n')
    if [ "$emu_crc" != "$crc_le" ]; then
        fail "$name: emitter CRC field $emu_crc != golden $crc_le"
    fi
    if ! head -c "$CROP_END" "$emu_file" | cmp -s - "$in_file"; then
        fail "$name: emitter did not preserve the first 0xFFC14 input bytes"
    fi

    if [ -n "$SREC_CAT" ]; then
        # 2. Authoritative srec_cat output, byte-compared with the emulator.
        ref_file="$WORK/ref/$name.bin"
        if ! srec_cat "$in_file" -binary -crop 0x0 0x000FFC14 \
                -STM32_Little_Endian 0x000FFC14 -o "$ref_file" -binary; then
            fail "$name: srec_cat (authoritative pipeline) failed"
            continue
        fi
        ref_size=$(wc -c < "$ref_file" | tr -d ' ')
        if [ "$ref_size" != "$EXPECTED_BYTES" ]; then
            fail "$name: srec_cat output is $ref_size bytes, expected $EXPECTED_BYTES"
        fi
        if ! cmp -s "$emu_file" "$ref_file"; then
            fail "$name: python emitter output differs from srec_cat output"
        fi
        ref_crc=$(tail -c 4 "$ref_file" | od -An -tx1 | tr -d ' \n')
        if [ "$ref_crc" != "$crc_le" ]; then
            fail "$name: srec_cat CRC field $ref_crc != golden $crc_le"
        fi
        echo "PASS: $name (srec_cat == python emitter, 0xFFC18, CRC $crc_le)"
    else
        echo "PASS: $name (python emitter == golden, 0xFFC18, CRC $crc_le)"
    fi
done < "$WORK/golden.txt"

echo
if [ "$FAIL_COUNT" -ne 0 ]; then
    echo "RESULT: FAIL -- $FAIL_COUNT check(s) failed" >&2
    exit 1
fi
if [ -n "$SREC_CAT" ]; then
    echo "RESULT: PASS -- $VECTOR_COUNT/$VECTOR_COUNT vectors byte-identical across both emitters"
else
    echo "RESULT: PASS -- $VECTOR_COUNT/$VECTOR_COUNT vectors match the srec_cat golden table"
fi
exit 0
