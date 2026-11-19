#!/bin/sh
# Fetch and pack the IR starter database for firmware releases (issue #45).
#
# Downloads Lucaslhm/Flipper-IRDB at the PINNED commit below as ONE source
# tarball (codeload), extracts exactly the curated category set, verifies the
# extracted files against the GitHub tree API listing for the same immutable
# commit (per-file sizes, count, total bytes), and packs them under the
# SD-card convention (IR/<Category>/<Brand>/<file>.ir) into a deterministic
# zip for upload as a release asset.
#
# Reproducibility: the pin is an immutable SHA, so the tarball bytes are as
# stable as per-file raw downloads would be; the zip is built with sorted
# entries, fixed timestamps and no platform file attributes, so the same pin
# always yields the same zip.
#
# The .ir files are release assets only -- they are never committed to this
# repository (staging happens under the git-ignored build/ tree). License and
# attribution: docs/releases/irdb-attribution.md (CC0 1.0 Universal, with the
# upstream pre-boundary caveat).
#
# Requirements: POSIX sh, curl, tar, python3 (same floor as build.sh
# --release; "python" is accepted where "python3" is absent, e.g. Git-Bash).
# Usage (from the repository root):   sh scripts/fetch_ir_starter_db.sh

set -eu

# python3 first, python fallback -- same resolution as scripts/run_host_tests.sh
# (Git-Bash/Windows hosts expose only "python").
PYTHON=${PYTHON:-python3}
command -v "$PYTHON" >/dev/null 2>&1 || PYTHON=python

PIN="d126fb1b6f1e114c52b4a8c19839ea65e3a9c24d"
SHORT_PIN="d126fb1b"
REPO="Lucaslhm/Flipper-IRDB"
CATEGORIES="TVs ACs Audio_and_Video_Receivers Streaming_Devices Projectors"

API_URL="https://api.github.com/repos/${REPO}/git/trees/${PIN}?recursive=1"
TARBALL_URL="https://codeload.github.com/${REPO}/tar.gz/${PIN}"

OUT_DIR="artifacts"
STAGE_DIR="build/irdb_starter"
LIST_FILE="${STAGE_DIR}/files.txt"
EXTRACT_DIR="${STAGE_DIR}/extract"
TARBALL="${STAGE_DIR}/src.tar.gz"
ZIP_FILE="${OUT_DIR}/M1_IRDB_starter_${SHORT_PIN}.zip"

mkdir -p "${OUT_DIR}" "${STAGE_DIR}"
rm -rf "${STAGE_DIR}/IR" "${EXTRACT_DIR}" "${TARBALL}"
mkdir -p "${STAGE_DIR}/IR" "${EXTRACT_DIR}"

echo "Fetching tree listing for ${REPO} @ ${PIN}..."
curl -sSf --retry 3 "${API_URL}" -o "${STAGE_DIR}/tree.json"

# The plan: "<path><TAB><size>" for every .ir blob inside the curated
# categories, sorted. This is the integrity record the extraction is verified
# against (and the file list the zip is built from).
"$PYTHON" - "${STAGE_DIR}/tree.json" "${LIST_FILE}" ${CATEGORIES} <<'PYEOF'
import json, sys

tree_json, list_file = sys.argv[1], sys.argv[2]
categories = set(sys.argv[3:])
with open(tree_json, encoding="utf-8") as f:
    tree = json.load(f)
if tree.get("truncated"):
    sys.exit("tree listing truncated by the API -- cannot verify completeness")
rows = []
for entry in tree["tree"]:
    path = entry["path"]
    if entry["type"] == "blob" and path.endswith(".ir"):
        if path.split("/")[0] in categories:
            rows.append((path, entry["size"]))
rows.sort()
if not rows:
    sys.exit("no .ir files matched the curated categories -- pin or API drift")
with open(list_file, "w", encoding="utf-8", newline="\n") as f:
    for path, size in rows:
        f.write(f"{path}\t{size}\n")
total = sum(size for _, size in rows)
print(f"planned: {len(rows)} files, {total} bytes across "
      f"{len(categories)} categories")
PYEOF

echo "Downloading source tarball ${TARBALL_URL} ..."
curl -sSf --retry 3 "${TARBALL_URL}" -o "${TARBALL}"

# codeload wraps the tree in "<repo>-<ref>/" -- detect it, then extract ONLY
# the curated category directories from under it.
ROOT_DIR=$(tar -tzf "${TARBALL}" | head -1 | cut -d/ -f1)
if [ -z "${ROOT_DIR}" ]; then
    echo "Error: cannot determine the tarball root directory." >&2
    exit 1
fi
MEMBERS=""
for cat in ${CATEGORIES}; do
    MEMBERS="${MEMBERS} ${ROOT_DIR}/${cat}"
done
# shellcheck disable=SC2086
tar -xzf "${TARBALL}" -C "${EXTRACT_DIR}" --strip-components=1 ${MEMBERS}
for cat in ${CATEGORIES}; do
    mv "${EXTRACT_DIR}/${cat}" "${STAGE_DIR}/IR/${cat}"
done
rm -rf "${EXTRACT_DIR}" "${TARBALL}"

# The starter asset carries exactly the .ir set the tree-API plan records;
# category folders also hold image previews and readme files upstream, which
# the browser cannot use -- prune them so the strict verification below (and
# the zip) contains only the planned .ir files.
find "${STAGE_DIR}/IR" -type f ! -name '*.ir' -exec rm -f {} +
find "${STAGE_DIR}/IR" -type d -empty -exec rmdir {} + 2>/dev/null || true

echo "Verifying extracted set against the pinned tree and packing ${ZIP_FILE}..."
"$PYTHON" - "${STAGE_DIR}" "${LIST_FILE}" "${ZIP_FILE}" <<'PYEOF'
import hashlib, os, sys, zipfile

stage_dir, list_file, zip_file = sys.argv[1], sys.argv[2], sys.argv[3]
with open(list_file, encoding="utf-8") as f:
    rows = [line.rstrip("\n").split("\t") for line in f]

mismatches = []
for path, size in rows:
    disk = os.path.join(stage_dir, "IR", path)
    actual = os.path.getsize(disk)
    if actual != int(size):
        mismatches.append(f"{path}: extracted {actual} bytes, tree says {size}")
extra = []
seen = {p for p, _ in rows}
for root, _, files in os.walk(os.path.join(stage_dir, "IR")):
    for name in files:
        rel = os.path.relpath(os.path.join(root, name),
                              os.path.join(stage_dir, "IR"))
        rel = rel.replace(os.sep, "/")
        if rel not in seen:
            extra.append(rel)
if mismatches or extra:
    for m in mismatches + extra:
        print(f"FAIL: {m}", file=sys.stderr)
    sys.exit(1)

# Deterministic zip: sorted entries, fixed timestamps, fixed permissions, no
# compression-level surprises -- same pin in, same bytes out. (Equivalent to
# `zip -X`: no host-dependent extra file attributes are stored.)
EPOCH = (1980, 1, 1, 0, 0, 0)
os.makedirs(os.path.dirname(zip_file), exist_ok=True)
if os.path.exists(zip_file):
    os.remove(zip_file)
with zipfile.ZipFile(zip_file, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
    for path, _ in rows:
        zf.write(os.path.join(stage_dir, "IR", path), f"IR/{path}",
                 compress_type=zipfile.ZIP_DEFLATED)
        zi = zf.getinfo(f"IR/{path}")
        zi.date_time = EPOCH
        zi.external_attr = 0o644 << 16
        zi.create_system = 3

with open(zip_file, "rb") as f:
    digest = hashlib.sha256(f.read()).hexdigest()
total = sum(int(s) for _, s in rows)
print(f"packed: {len(rows)} files, {total} bytes unzipped -> {zip_file}")
print(f"sha256: {digest}")
PYEOF

echo "Staging tree kept at ${STAGE_DIR}/IR (git-ignored); upload"
echo "${ZIP_FILE} as the release asset. Attribution: docs/releases/irdb-attribution.md"
