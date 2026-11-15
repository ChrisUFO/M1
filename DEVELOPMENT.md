<!-- See COPYING.txt for license details. -->

# M1 Development Guidelines

## Coding Standards

- Follow STM32 HAL coding conventions
- Use meaningful variable and function names
- Prefer camelCase for variables/functions, UPPER_CASE for constants
- Use appropriate integer types (`uint8_t`, `uint16_t`, etc.)
- Add comments for non-obvious logic

## Branch Naming

- `feature/` – New features
- `fix/` – Bug fixes
- `docs/` – Documentation only
- `refactor/` – Code refactoring

## Commit Messages

Follow [Conventional Commits](https://www.conventionalcommits.org/):

- `feat:` – New feature
- `fix:` – Bug fix
- `docs:` – Documentation
- `style:` – Formatting
- `refactor:` – Code restructure
- `test:` – Tests
- `chore:` – Maintenance

## Testing

- Test on hardware when possible
- Document test scenarios and edge cases
- Ensure NFC, RFID, and Sub-GHz functionality are verified
- For stability hardening changes, follow `documentation/stability_phase5_validation.md`

## Tag Policy

- `legacy/v0.8.11` is an **immutable archive tag** marking the final release
  of the fork's own 0.8.x line before upstream base adoption.
- Never rewrite, delete, move, or force-push this tag, and never reuse its
  name. If it points at the wrong commit, cut a new tag instead.
- The tag is the reference point for merge-audit evidence (fork-changed-file
  ledger); rewriting it would invalidate that history.

## Version Bumps

`FW_VERSION_*` in `m1_csrc/m1_fw_update_bl.h` is the single source for the
CMake-derived artifact name (`M1_v<MAJOR>.<MINOR>.<BUILD>.<RC>-<FORK_TAG>`).
The `./build` script re-derives the stem from the same header and takes the
authoritative target name from the CMake cache, so it needs no manual sweep.
Tracked files that hardcode the versioned artifact name and must be swept on
every bump: `.vscode/launch.json` (debug-launch `executable` and
`projectName`), `scripts/program.jlink` (`loadbin` path), and the artifact
examples in `README.md` (manual-build target and output-file table).
Vendor/Eclipse-era identifiers (`MonstaTek_M1_v*` in `.project`, `*.ioc`,
`Project_files.txt`, the commented size/objcopy lines in `CMakeLists.txt`,
and the README CubeIDE output example) belong to a different build system
and are deliberately excluded from the sweep.

## Line-Ending Normalization (EOL)

`.gitattributes` defines the policy:

- `* text=auto` — LF is canonical in the repository; checkouts follow the
  platform's `core.autocrlf`/`core.eol`.
- `*.sh text eol=lf` and `*.bat text eol=crlf` — scripts always check out
  with the line endings their interpreters require, on every platform.
- Binary types (including `*.bin`, `*.elf`, `*.dfu`, and `*.hex` — ASCII
  records that must not be touched) are marked `-text` and never normalized.

Workflow conventions:

- After any `.gitattributes` (or line-ending config) change, run
  `git add --renormalize .` and commit the result as **one standalone
  commit** containing only the renormalization — no feature changes mixed in.
- Prove the renormalization is content-neutral:
  `git diff --ignore-cr-at-eol <pre-normalize-SHA> HEAD` must be empty.
- **Required per clone:** `git config merge.renormalize true`. This setting
  is clone-local and cannot be committed — every contributor and CI runner
  must set it right after cloning (or after a fresh `git init` from a
  bundle) so merges compare canonical LF content and never surface phantom
  EOL conflicts.

## Artifact Hygiene

Build outputs and local tooling state are never committed:

- Build trees (`out/`, `CMakeFiles/`), packaged release artifacts
  (`distribution/`), and build logs (`build_error.log`) stay out of Git.
- Python bytecode (`__pycache__/`, `*.pyc`) is never tracked.
- Review artifacts (`review_comments*.json`) and local planning docs
  (`PLAN.md`) are working state, not repo content.
- `.gitignore` is the authoritative list; extend it there rather than
  deleting stray artifacts by hand.
