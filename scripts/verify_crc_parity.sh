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
# The golden table is load-bearing in BOTH modes: each generated input image is
# checked against the table's recorded input digest (generator drift), and a
# vector produced by the generator but absent from the table (or vice versa)
# fails instead of silently escaping coverage.
#
# Real-artifact leg: the synthetic vectors prove the emitters agree on test
# patterns; the image the release path actually ships is a build output. When a
# path is given, or build/<artifact>.bin exists, both emitters also run on that
# image and are byte-compared, and its output must be exactly 0xFFC18 bytes.
#
# Usage (from the repo root):
#   sh scripts/verify_crc_parity.sh [<real-image.bin>]
#   sh scripts/verify_crc_parity.sh --update-golden   # re-baseline, needs srec_cat
#
# Exit status: 0 = all vectors pass, 1 = any mismatch or environment error.
set -eu

MODE=verify
REAL_IMAGE_ARG=""
for arg in "$@"; do
    case $arg in
        --update-golden) MODE=update-golden ;;
        -h|--help) MODE=help ;;
        -*) echo "unknown option: $arg (try --help)" >&2; exit 2 ;;
        *) REAL_IMAGE_ARG=$arg ;;
    esac
done

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$REPO_ROOT"
. "$REPO_ROOT/scripts/portable_hash.sh"

CROP_END=1047572    # 0xFFC14 -- crop end == CRC append offset
EXPECTED_BYTES=1047576 # 0xFFC18 -- cropped image + 4-byte CRC word
GOLDEN_FILE="scripts/crc_parity_golden.txt"
BUILD_DIR="build"
SREC_CAT_ARGS="-binary -crop 0x0 0x000FFC14 -STM32_Little_Endian 0x000FFC14"

FAIL_COUNT=0
fail() {
    echo "FAIL: $*" >&2
    FAIL_COUNT=$((FAIL_COUNT + 1))
}

usage() {
    # Print only the leading comment block: skip the shebang, strip the comment
    # marker, and stop at the first non-comment line. A hard-coded line range
    # silently leaked source code ("set -eu", assignments) into --help as soon
    # as the header shrank, so derive the end from the block itself.
    awk 'NR == 1 { next } /^#/ { sub(/^# ?/, ""); print; next } { exit }' "$0"
}

PYTHON=""
for candidate in python3 python py; do
    if command -v "$candidate" >/dev/null 2>&1; then
        PYTHON=$candidate
        break
    fi
done

SREC_CAT=""
if command -v srec_cat >/dev/null 2>&1; then
    SREC_CAT=srec_cat
fi

if [ "$MODE" = "help" ]; then
    usage
    exit 0
fi

if [ -z "$PYTHON" ]; then
    echo "FAIL: no Python 3 interpreter found on PATH (need python3/python/py)" >&2
    exit 1
fi

echo "=== CRC emitter parity harness ==="
echo "repo:     $REPO_ROOT"
echo "python:   $PYTHON"
echo "mode:     $MODE"
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

# The generator's exit status must be checked directly: piping it into the
# capture file masks a failure behind `tr`'s status and surfaces it later as
# four confusing "missing input image" messages.
generate_inputs() {
    echo "generating deterministic inputs via tools/crc_test_vectors.py"
    GEN_LOG="$WORK/generator.log"
    if ! "$PYTHON" tools/crc_test_vectors.py --out "$WORK/in" > "$GEN_LOG" 2>&1; then
        fail "input generator failed (tools/crc_test_vectors.py); output below"
        sed 's/^/    /' "$GEN_LOG" >&2
        echo "RESULT: FAIL -- cannot continue without generated inputs" >&2
        exit 1
    fi
    tr -d '\r' < "$GEN_LOG" > "$WORK/inputs.txt"
}

generate_inputs

# ---------------------------------------------------------------------------
# --update-golden: the ONLY sanctioned way to re-baseline the table.
#
# The table's whole value is that its values come from the authoritative
# srec_cat pipeline, not from the code under test. Regenerating it with the
# Python emulator would grade the emulator against itself and make the only
# host-independent parity check circular, so this refuses without srec_cat.
# ---------------------------------------------------------------------------
if [ "$MODE" = "update-golden" ]; then
    if [ -z "$SREC_CAT" ]; then
        echo "REFUSED: --update-golden requires srec_cat on PATH." >&2
        echo "         $GOLDEN_FILE records the AUTHORITATIVE srec_cat pipeline." >&2
        echo "         Re-baselining it from the Python emulator would make the only" >&2
        echo "         host-independent parity check circular (the emulator would be" >&2
        echo "         graded against itself). Install SRecord, then re-run this on a" >&2
        echo "         host where 'srec_cat' resolves." >&2
        exit 1
    fi

    echo "re-baselining $GOLDEN_FILE from $SREC_CAT"
    # Keep the existing header prose (the provenance statement and column
    # documentation live there); regenerate only the data rows.
    awk 'BEGIN{keep=1} keep && (/^#/ || /^[[:space:]]*$/) {print; next} {exit}' \
        "$GOLDEN_FILE" > "$WORK/golden.new"

    while read -r name gen_in_sha; do
        [ -n "$name" ] || continue
        in_file="$WORK/in/$name.bin"
        ref_file="$WORK/ref/$name.bin"
        emu_file="$WORK/emu/$name.bin"

        if ! $SREC_CAT "$in_file" $SREC_CAT_ARGS -o "$ref_file" -binary; then
            fail "$name: srec_cat failed while re-baselining (table not written)"
            continue
        fi
        ref_size=$(wc -c < "$ref_file" | tr -d ' ')
        if [ "$ref_size" != "$EXPECTED_BYTES" ]; then
            fail "$name: srec_cat output is $ref_size bytes, expected $EXPECTED_BYTES"
            continue
        fi
        # Verify with the emulator BEFORE recording the row: a table written
        # from a pipeline the emulator disagrees with would hide a regression.
        if ! "$PYTHON" tools/emulate_srec_cat_crc.py "$in_file" "$emu_file" \
                --crop-end 0xFFC14 > "$WORK/emu/$name.log" 2>&1; then
            fail "$name: python emitter failed while re-baselining (table not written)"
            continue
        fi
        if ! cmp -s "$emu_file" "$ref_file"; then
            fail "$name: python emitter disagrees with srec_cat (table not written)"
            continue
        fi

        out_sha=$(sha256_of "$ref_file")
        crc_le=$(tail -c 4 "$ref_file" | od -An -tx1 | tr -d ' \n')
        printf '%-10s %s  %s  %s\n' \
            "$name" "$gen_in_sha" "$out_sha" "$crc_le" >> "$WORK/golden.new"
        echo "  row: $name $gen_in_sha $out_sha $crc_le"
    done < "$WORK/inputs.txt"

    if [ "$FAIL_COUNT" -ne 0 ]; then
        echo "RESULT: FAIL -- golden table NOT written ($FAIL_COUNT check(s) failed)" >&2
        exit 1
    fi
    cp "$WORK/golden.new" "$GOLDEN_FILE"
    echo "wrote $GOLDEN_FILE ($(grep -c '^[^#[:space:]]' "$GOLDEN_FILE" | tr -d ' ') rows)"
    echo "RESULT: PASS -- golden table re-baselined from srec_cat (emulator agrees byte-for-byte)"
    exit 0
fi

# ---------------------------------------------------------------------------
# Verify mode.
# ---------------------------------------------------------------------------
echo "golden table: $VECTOR_COUNT vectors; generator spec: $(wc -l < "$WORK/inputs.txt" | tr -d ' ') vectors"

echo
echo "--- input provenance (generator drift) ---"
# A spec the generator emits but the table does not record is untested; a table
# row with no matching spec silently disappears. Both are drift, not coverage.
cut -d' ' -f1 "$WORK/inputs.txt" | sort > "$WORK/gen_names.txt"
cut -d' ' -f1 "$WORK/golden.txt" | sort > "$WORK/golden_names.txt"
if ! cmp -s "$WORK/gen_names.txt" "$WORK/golden_names.txt"; then
    fail "vector name sets differ between the generator and $GOLDEN_FILE"
    diff -u "$WORK/gen_names.txt" "$WORK/golden_names.txt" 2>/dev/null \
        | sed 's/^/    /' >&2 || true
fi

# Drive the input checks from the GOLDEN table -- that is the recorded
# provenance. (Comparing each generated file against the generator's own
# stdout from this same run proves nothing.)
: > "$WORK/bad_inputs.txt"
while read -r name in_sha _out_sha _crc_le; do
    [ -n "$name" ] || continue
    in_file="$WORK/in/$name.bin"
    in_fails_before=$FAIL_COUNT
    if [ ! -f "$in_file" ]; then
        fail "$name: golden row has no generated input ($name.bin missing)"
        echo "$name" >> "$WORK/bad_inputs.txt"
        continue
    fi
    size=$(wc -c < "$in_file" | tr -d ' ')
    if [ "$size" != "$CROP_END" ]; then
        fail "$name: input is $size bytes, expected $CROP_END (0xFFC14)"
    fi
    actual=$(sha256_of "$in_file")
    if [ "$actual" != "$in_sha" ]; then
        fail "$name: generated input digest $actual != golden $in_sha (generator drift)"
    fi
    if [ "$FAIL_COUNT" -ne "$in_fails_before" ]; then
        echo "$name" >> "$WORK/bad_inputs.txt"
    fi
done < "$WORK/golden.txt"

echo
echo "--- checking each vector ---"
while read -r name in_sha out_sha crc_le; do
    [ -n "$name" ] || continue
    in_file="$WORK/in/$name.bin"
    emu_file="$WORK/emu/$name.bin"
    ref_file="$WORK/ref/$name.bin"
    fails_before=$FAIL_COUNT

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

    # 2. Authoritative srec_cat output, byte-compared with the emulator.
    if [ -n "$SREC_CAT" ]; then
        if $SREC_CAT "$in_file" $SREC_CAT_ARGS -o "$ref_file" -binary; then
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
        else
            fail "$name: srec_cat (authoritative pipeline) failed"
        fi
    fi

    # Per-vector verdict from this vector's own failure delta (plus its input
    # provenance row), so a half-red run can never print "PASS:" for a vector
    # that failed -- the final summary stays the exit-code truth.
    if [ "$FAIL_COUNT" -eq "$fails_before" ] \
            && ! grep -qxF "$name" "$WORK/bad_inputs.txt"; then
        if [ -n "$SREC_CAT" ]; then
            echo "PASS: $name (srec_cat == python emitter, 0xFFC18, CRC $crc_le)"
        else
            echo "PASS: $name (python emitter == golden, 0xFFC18, CRC $crc_le)"
        fi
    else
        echo "FAIL: $name (see the check failure(s) above)" >&2
    fi
done < "$WORK/golden.txt"

# ---------------------------------------------------------------------------
# Pinned documented divergence: the emitters deliberately differ for images
# shorter than the crop end (srec_cat 0x00-pads and exits 0; the Python emitter
# refuses). Pin the Python side so the documented claim cannot silently rot.
# No golden row: a padded short image is not a valid image.
# ---------------------------------------------------------------------------
echo
echo "--- documented short-image divergence (pinned) ---"
head -c 400 /dev/zero > "$WORK/short.bin"
if "$PYTHON" tools/emulate_srec_cat_crc.py "$WORK/short.bin" "$WORK/short_wCRC.bin" \
        --crop-end 0xFFC14 > "$WORK/short.log" 2>&1; then
    fail "python emitter accepted a 400-byte image (docs claim it refuses; srec_cat pads 0x00)"
else
    echo "PASS: python emitter refuses a 400-byte image (srec_cat pads 0x00 -- documented divergence)"
fi

# ---------------------------------------------------------------------------
# Real-build-artifact leg: the synthetic vectors cover the content space, but
# the one image the release path ships is build/<artifact>.bin. Cross-compare
# the emitters on it when it exists (or when a path is passed explicitly).
# ---------------------------------------------------------------------------
echo
echo "--- real build artifact leg ---"
REAL_IMAGE=""
REAL_IMAGE_AMBIGUOUS=0
if [ -n "$REAL_IMAGE_ARG" ]; then
    REAL_IMAGE=$REAL_IMAGE_ARG
else
    for candidate in "$BUILD_DIR"/*.bin; do
        [ -f "$candidate" ] || continue
        case $candidate in
            *_wCRC.bin) continue ;;
        esac
        if [ -z "$REAL_IMAGE" ]; then
            REAL_IMAGE=$candidate
        else
            REAL_IMAGE=""
            REAL_IMAGE_AMBIGUOUS=1
            break
        fi
    done
fi

if [ -z "$REAL_IMAGE" ]; then
    if [ "$REAL_IMAGE_AMBIGUOUS" = 1 ]; then
        echo "skipped: several plain .bin files in $BUILD_DIR/ -- pass one as an argument"
    else
        echo "skipped: no $BUILD_DIR/<artifact>.bin (pass a build image path as an argument)"
    fi
else
    echo "input:    $REAL_IMAGE"
    real_fails_before=$FAIL_COUNT
    real_emu="$WORK/real_emu.bin"
    real_ref="$WORK/real_ref.bin"

    if [ ! -f "$REAL_IMAGE" ]; then
        fail "real artifact does not exist: $REAL_IMAGE"
    else
        real_in_size=$(wc -c < "$REAL_IMAGE" | tr -d ' ')
        echo "size:     $real_in_size bytes (expected $CROP_END / 0xFFC14)"
        if [ "$real_in_size" != "$CROP_END" ]; then
            fail "real artifact is $real_in_size bytes, expected $CROP_END (0xFFC14)"
        fi

        if ! "$PYTHON" tools/emulate_srec_cat_crc.py "$REAL_IMAGE" "$real_emu" \
                --crop-end 0xFFC14 > "$WORK/real_emu.log" 2>&1; then
            fail "real artifact: python emitter failed (see output below)"
            sed 's/^/    /' "$WORK/real_emu.log" >&2
        else
            real_emu_size=$(wc -c < "$real_emu" | tr -d ' ')
            if [ "$real_emu_size" != "$EXPECTED_BYTES" ]; then
                fail "real artifact: emitter output is $real_emu_size bytes, expected $EXPECTED_BYTES (0xFFC18)"
            fi
            head -c "$CROP_END" "$REAL_IMAGE" > "$WORK/real_in_prefix.bin"
            head -c "$CROP_END" "$real_emu" > "$WORK/real_out_prefix.bin"
            if ! cmp -s "$WORK/real_in_prefix.bin" "$WORK/real_out_prefix.bin"; then
                fail "real artifact: emitter did not preserve the first 0xFFC14 input bytes"
            fi

            if [ -n "$SREC_CAT" ]; then
                if $SREC_CAT "$REAL_IMAGE" $SREC_CAT_ARGS -o "$real_ref" -binary; then
                    real_ref_size=$(wc -c < "$real_ref" | tr -d ' ')
                    if [ "$real_ref_size" != "$EXPECTED_BYTES" ]; then
                        fail "real artifact: srec_cat output is $real_ref_size bytes, expected $EXPECTED_BYTES"
                    fi
                    if ! cmp -s "$real_emu" "$real_ref"; then
                        fail "real artifact: python emitter output differs from srec_cat output"
                    fi
                else
                    fail "real artifact: srec_cat (authoritative pipeline) failed"
                fi
            fi
        fi

        if [ "$FAIL_COUNT" -eq "$real_fails_before" ]; then
            if [ -n "$SREC_CAT" ]; then
                echo "PASS: real artifact (srec_cat == python emitter, 0xFFC18)"
            else
                echo "PASS: real artifact (python emitter, 0xFFC18; srec_cat absent -- size/prefix only)"
            fi
        else
            echo "FAIL: real artifact (see the check failure(s) above)" >&2
        fi
    fi
fi

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
if [ -n "$REAL_IMAGE" ]; then
    echo "         real artifact cross-checked: $REAL_IMAGE"
fi
exit 0
