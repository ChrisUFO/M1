#!/usr/bin/env python3
"""
Check for case sensitivity mismatches in #include statements.

This script ensures that #include statements match the actual filename casing,
which is critical for cross-platform compatibility (Linux is case-sensitive,
Windows/macOS are typically case-insensitive).

Usage:
    python tools/check_case_sensitivity.py
    python tools/check_case_sensitivity.py --fix  # Auto-fix issues where possible

Returns:
    0 if no issues found
    1 if case mismatches detected
"""

import argparse
import os
import re
import sys
from collections import defaultdict
from pathlib import Path


def get_all_headers(repo_root):
    """Find all .h files in the repository."""
    headers = []
    for root, dirs, files in os.walk(repo_root):
        # Skip certain directories
        dirs[:] = [d for d in dirs if d not in [".git", "out", "build", "distribution"]]
        for file in files:
            if file.endswith(".h"):
                full_path = os.path.join(root, file)
                rel_path = os.path.relpath(full_path, repo_root)
                headers.append(rel_path)
    return headers


def get_all_includes(repo_root):
    """Find all #include statements in .c and .h files."""
    includes = []
    include_pattern = re.compile(r'#include\s+["<]([^">]+)[">]')

    for root, dirs, files in os.walk(repo_root):
        # Skip certain directories
        dirs[:] = [d for d in dirs if d not in [".git", "out", "build", "distribution"]]

        for file in files:
            if file.endswith((".c", ".h")):
                filepath = os.path.join(root, file)
                rel_path = os.path.relpath(filepath, repo_root)

                try:
                    with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                        for line_num, line in enumerate(f, 1):
                            match = include_pattern.search(line)
                            if match:
                                included_file = match.group(1)
                                # Skip system headers (angle brackets without ./ or ../)
                                if not included_file.startswith(("./", "../")):
                                    # Check if it looks like a local header
                                    if not any(
                                        included_file.startswith(x)
                                        for x in [
                                            "stm32",
                                            "m1_",
                                            "lfrfid",
                                            "ff",
                                            "diskio",
                                            "FreeRTOS",
                                            "app_",
                                            "battery",
                                            "logger",
                                            "mui",
                                            "u8g2",
                                            "u8x8",
                                            "NFC",
                                            "rfal",
                                            "nfc_",
                                            "si4463",
                                            "irmp",
                                            "irsnd",
                                            "t5577",
                                            "usbd_",
                                            "privateprofilestring",
                                            "Res_String",
                                            "uiView",
                                            "bit_util",
                                            "sdkconfig",
                                            "ctrl_api",
                                            "esp_",
                                            "spi_master",
                                            "spi_types",
                                            "spi_hal",
                                            "spi_struct",
                                            "soc_caps",
                                            "esp_loader",
                                            "serial_io",
                                            "app_common",
                                            "stm32_port",
                                            "slip",
                                            "protocol",
                                            "md5_hash",
                                            "sip",
                                            "esp_stubs",
                                            "esp_targets",
                                        ]
                                    ):
                                        continue

                                includes.append(
                                    {
                                        "file": rel_path,
                                        "line": line_num,
                                        "include": included_file,
                                        "line_content": line.strip(),
                                    }
                                )
                except Exception as e:
                    print(f"Warning: Could not read {rel_path}: {e}", file=sys.stderr)

    return includes


def find_mismatches(headers, includes):
    """Find case mismatches between includes and actual filenames."""
    # Create mapping of lowercase filename to actual filename
    file_map = {}
    for h in headers:
        basename = os.path.basename(h)
        file_map[basename.lower()] = basename

    mismatches = []

    for inc in includes:
        included_basename = os.path.basename(inc["include"])
        included_lower = included_basename.lower()

        if included_lower in file_map:
            actual_name = file_map[included_lower]
            if included_basename != actual_name:
                mismatches.append(
                    {
                        "file": inc["file"],
                        "line": inc["line"],
                        "include": inc["include"],
                        "actual": actual_name,
                        "line_content": inc["line_content"],
                    }
                )

    return mismatches


def fix_mismatches(repo_root, mismatches):
    """Auto-fix case mismatches in files."""
    fixed_count = 0

    # Group by file
    by_file = defaultdict(list)
    for m in mismatches:
        by_file[m["file"]].append(m)

    for filepath, issues in by_file.items():
        full_path = os.path.join(repo_root, filepath)

        try:
            with open(full_path, "r", encoding="utf-8") as f:
                lines = f.readlines()

            # Sort issues by line number in reverse order to avoid offset issues
            issues.sort(key=lambda x: x["line"], reverse=True)

            for issue in issues:
                line_idx = issue["line"] - 1
                if line_idx < len(lines):
                    old_line = lines[line_idx]
                    # Replace the incorrect case with correct case
                    new_line = old_line.replace(
                        f'"{issue["include"]}"',
                        f'"{issue["include"].replace(os.path.basename(issue["include"]), issue["actual"])}"',
                    )
                    if new_line != old_line:
                        lines[line_idx] = new_line
                        fixed_count += 1
                        print(
                            f"Fixed: {filepath}:{issue['line']} - {issue['include']} -> {issue['actual']}"
                        )

            # Write back
            with open(full_path, "w", encoding="utf-8") as f:
                f.writelines(lines)

        except Exception as e:
            print(f"Error fixing {filepath}: {e}", file=sys.stderr)

    return fixed_count


def main():
    parser = argparse.ArgumentParser(
        description="Check for case sensitivity mismatches in #include statements"
    )
    parser.add_argument(
        "--fix", action="store_true", help="Auto-fix mismatches where possible"
    )
    parser.add_argument(
        "--repo-root",
        default=".",
        help="Repository root directory (default: current directory)",
    )

    args = parser.parse_args()

    repo_root = os.path.abspath(args.repo_root)

    print("Checking for case sensitivity mismatches...")
    print(f"Repository: {repo_root}")
    print()

    # Gather data
    headers = get_all_headers(repo_root)
    includes = get_all_includes(repo_root)

    print(f"Found {len(headers)} header files")
    print(f"Found {len(includes)} #include statements")
    print()

    # Find mismatches
    mismatches = find_mismatches(headers, includes)

    if not mismatches:
        print("[OK] No case mismatches found!")
        return 0

    print(f"[FAIL] Found {len(mismatches)} case mismatch(es):\n")

    # Group by include
    by_include = defaultdict(list)
    for m in mismatches:
        by_include[m["include"]].append(m)

    for included, entries in sorted(by_include.items()):
        actual = entries[0]["actual"]
        print(f"Include: '{included}' should be '{actual}'")
        for entry in entries:
            print(f"  {entry['file']}:{entry['line']}")
        print()

    if args.fix:
        print("Attempting to fix mismatches...")
        fixed = fix_mismatches(repo_root, mismatches)
        print(f"\nFixed {fixed} issue(s)")
        return 0
    else:
        print("Run with --fix to automatically correct these issues")
        return 1


if __name__ == "__main__":
    sys.exit(main())
