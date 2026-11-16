#!/bin/sh
# Portable message-digest helpers (issue #40).
#
# Sourced (never executed) by build.sh and scripts/verify_crc_parity.sh so the
# hash-tool discovery exists in exactly one place. Both helpers print the bare
# lowercase hex digest and return non-zero -- with an actionable message on
# stderr -- when no supported tool is on PATH, so callers under `set -e` fail
# loudly instead of writing an empty manifest field.
#
# Tool preference per platform: GNU coreutils (md5sum/sha256sum) on Linux and
# Git-Bash for Windows, BSD variants (md5 -q / shasum -a 256) on macOS.

md5_of() {
    if command -v md5sum >/dev/null 2>&1; then
        md5sum "$1" | cut -d' ' -f1
    elif command -v md5 >/dev/null 2>&1; then
        md5 -q "$1"
    else
        echo "Error: no MD5 tool found on PATH (need md5sum or md5)." >&2
        return 1
    fi
}

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | cut -d' ' -f1
    else
        echo "Error: no SHA-256 tool found on PATH (need sha256sum or shasum)." >&2
        return 1
    fi
}
