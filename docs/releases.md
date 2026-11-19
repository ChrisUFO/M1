# Release and version policy

## Current state

The current release is **v0.9.0.0-UFO** (2026-11-19), the fork's first
release cut on the adopted upstream 0.8.1.0 base (pin
`2af1f8a339dbe629f36a0551de4293b161179b71`, merge-1 ancestry graft — see
[DEVELOPMENT.md](../DEVELOPMENT.md) for the merge policy). The image metadata
fields in `m1_csrc/m1_fw_update_bl.h` are major `0`, minor `9`, build `0`, and
RC `0`, with the display-only fork tag `UFO`. The rollback comparator accepts
0.9.0.0 over both prior lines (legacy 0.8.11 and upstream 0.8.1.0); the fork
tag is excluded from that numeric compare. Release approval applies only to an
exact artifact hash and its qualification record; neither the label nor
metadata fields alone establish approval.

## Version authority

A release must have one canonical version in source. The single source of
truth is `m1_csrc/m1_fw_update_bl.h`:

- `FW_VERSION_MAJOR/MINOR/BUILD/RC` — the four image-metadata components
  (major, minor, build, RC). Consumers must preserve this existing wire and
  image format unless a separately reviewed migration changes it.
- `FW_VERSION_FORK_TAG` — the display-only suffix (`UFO`), excluded from the
  rollback comparator and the `S_M1_FW_CONFIG_t` metadata.
- `FW_BUILD_LABEL` — composed from the macros (`FW 0.9.0.0-UFO`), drawn by the
  About box and the boot log.

Generated filenames, display strings, metadata, changelog entries, and release
manifests must derive from or be verified against these macros. The derived
surfaces are enumerated with file:line evidence in the release verification
record ([`documentation/0.9.0.0-ufo-verification.md`](../documentation/0.9.0.0-ufo-verification.md)):
`CMakeLists.txt` and `build.sh` parse the header for the artifact stem
(`M1_v0.9.0.0-UFO`), the boot screen and boot log draw the running config
fields, the CLI `version`/`status` commands report them, and the About box
renders `FW_BUILD_LABEL`. Surfaces that are hardware identity — not version
carriers — are documented there too (USB descriptor strings, the CubeMX
`.ioc` project name); they deliberately do not change with the firmware
version.

## Artifact states

Use explicit states:

- **development** — local or CI output, not distributed;
- **test** — intended only for named validation;
- **release candidate** — full qualification candidate;
- **production** — approved for specified hardware and region;
- **withdrawn** — must not be installed;
- **superseded** — retained for traceability but replaced.

## Release manifest

`./build.sh --release` writes `<stem>_wCRC.manifest.txt` next to the binaries
after the CRC emitter parity gate passes, with the keys it produces:
`binary`, `commit`, `dirty`, `branch`, `flags`, `source_date_epoch`,
`source_date_utc`, `build_timestamp_utc`, `size_bytes`, `md5`, `sha256`,
`flash_text`, `flash_data`, `ram_bss` (see
[Build and debug](build-and-debug.md)). For a **published** release, the
release owner additionally records the qualification-facing fields against the
exact artifact hashes:

```text
product:
firmware_version:
source_commit:
workspace_clean:
hardware_revisions:
region:
bootloader_compatibility:
esp32_firmware_compatibility:
build_preset:
toolchain_versions:
image_filename:
image_size:
crc32:
sha256:
signing_status:
qualification_record:
known_issues:
approved_by:
approved_at:
```

The build-time manifest supplies the mechanical fields (`commit`, `dirty`,
hashes, sizes); the publishing fields above are release-process
responsibilities and live with the published release (e.g. its notes and
verification record), not in build output.

## Required release sequence

1. Freeze version, hardware targets, region, and compatibility.
2. Build from a clean, identified commit (`./build.sh --release` refuses a
   dirty tree and verifies the embedded commit banner).
3. Generate manifest and hashes.
4. Run the qualification matrix (host suites, CRC parity, delta audit).
5. Resolve or formally waive failures.
6. Approve the exact artifact hashes.
7. Publish artifacts and release notes together.
8. Mark superseded or withdrawn artifacts explicitly.

## Repository artifacts

Tracked build directories are not a release registry. Releases are published
as immutable GitHub-release assets built from merged `main` — the release
manifest, the verification record
([`documentation/0.9.0.0-ufo-verification.md`](../documentation/0.9.0.0-ufo-verification.md)),
and the release notes
([`docs/releases/0.9.0.0-UFO.md`](releases/0.9.0.0-UFO.md)) are the tracked
evidence; compiler objects, caches, and ambiguous test variants are never
committed. The one additional release asset class is the **IR starter
database**: a zip of CC0-1.0 Flipper-IRDB content at a pinned commit, built
reproducibly by `scripts/fetch_ir_starter_db.sh` and documented in
[IR database provisioning](ir-provisioning.md) and the attribution record
([`docs/releases/irdb-attribution.md`](releases/irdb-attribution.md)); the
`.ir` files themselves stay out of git.
