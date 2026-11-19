#!/bin/sh
# Fetch and pack the IR starter database for firmware releases (issue #45).
#
# Downloads exactly the curated category set of Lucaslhm/Flipper-IRDB at the
# PINNED commit below (immutable tree, so the output is reproducible), checks
# every fetched file against the size the GitHub tree API reports for it, and
# packs them under the SD-card convention (IR/<Category>/<Brand>/<file>.ir)
# into a deterministic zip for upload as a release asset.
#
# The .ir files are release assets only -- they are never committed to this
# repository (staging happens under the git-ignored build/ tree). License and
# attribution: docs/releases/irdb-attribution.md (CC0 1.0 Universal, with the
# upstream pre-boundary caveat).
#
# Requirements: POSIX sh, curl, python3 (same floor as build.sh --release).
# Usage (from the repository root):   sh scripts/fetch_ir_starter_db.sh

set -eu

PIN="d126fb1b6f1e114c52b4a8c19839ea65e3a9c24d"
SHORT_PIN="d126fb1b"
REPO="Lucaslhm/Flipper-IRDB"
CATEGORIES="TVs ACs Audio_and_Video_Receivers Streaming_Devices Projectors"

API_URL="https://api.github.com/repos/${REPO}/git/trees/${PIN}?recursive=1"
RAW_BASE="https://raw.githubusercontent.com/${REPO}/${PIN}"

OUT_DIR="artifacts"
STAGE_DIR="build/irdb_starter"
LIST_FILE="${STAGE_DIR}/files.txt"
ZIP_FILE="${OUT_DIR}/M1_IRDB_starter_${SHORT_PIN}.zip"

mkdir -p "${OUT_DIR}" "${STAGE_DIR}"
rm -rf "${STAGE_DIR}/IR"
mkdir -p "${STAGE_DIR}/IR"

echo "Fetching tree listing for ${REPO} @ ${PIN}..."
curl -sSf --retry 3 "${API_URL}" -o "${STAGE_DIR}/tree.json"

# Turn the tree JSON into the exact download list: "<path><TAB><size>" for
# every .ir blob inside the curated categories, sorted. python3 is already a
# hard requirement of the release path (CRC parity harness).
python3 - "${STAGE_DIR}/tree.json" "${LIST_FILE}" ${CATEGORIES} <<'PYEOF'
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

echo "Downloading ${RAW_BASE}/<Category>/<Brand>/<file>.ir ..."
while IFS="$(printf '\t')" read -r path size; do
    dest="${STAGE_DIR}/IR/${path}"
    mkdir -p "$(dirname "${dest}")"
    curl -sSf --retry 3 "${RAW_BASE}/${path}" -o "${dest}"
done < "${LIST_FILE}"

echo "Verifying fetched set against the pinned tree and packing ${ZIP_FILE}..."
python3 - "${STAGE_DIR}" "${LIST_FILE}" "${ZIP_FILE}" <<'PYEOF'
import hashlib, os, sys, zipfile

stage_dir, list_file, zip_file = sys.argv[1], sys.argv[2], sys.argv[3]
with open(list_file, encoding="utf-8") as f:
    rows = [line.rstrip("\n").split("\t") for line in f]

mismatches = []
for path, size in rows:
    disk = os.path.join(stage_dir, "IR", path)
    actual = os.path.getsize(disk)
    if actual != int(size):
        mismatches.append(f"{path}: fetched {actual} bytes, tree says {size}")
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
# compression-level surprises -- same pin in, same bytes out.
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
