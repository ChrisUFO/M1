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
- On the Windows verification host, use the repo adapters
  (`tools/WINDOWS_VERIFICATION.md`) for the upstream host-test suites and
  the CRC emission step.

## Scoped -Werror policy (warning hygiene)

Application sources compile under `-Wall -Wextra -Werror`; every other tree
in the build (HAL, CMSIS, FreeRTOS, u8g2, FatFs, IRMP, RFAL, the ESP and USB
stacks, lfrfid, …) stays relaxed: the toolchain file's global
`-Wall -Wextra -Wpedantic` as non-fatal warnings, untouched. Vendor code is
not ours to churn, and warning noise there must not gate the build.

**Mechanism.** The gate lives in `cmake/m1_01/CMakeLists.txt`:

- The app sources (the `m1_csrc/*.c` files) are listed once in the
  `M1_APP_SOURCES` variable, which feeds both `target_sources()` and
  ```cmake
  set_source_files_properties(${M1_APP_SOURCES}
      TARGET_DIRECTORY ${CMAKE_PROJECT_NAME}
      PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
  ```
- Source-file properties with `TARGET_DIRECTORY` visibility (CMake ≥ 3.18;
  this project's floor is 3.22) attach the flags to exactly those
  translation units as compiled into the root executable.
- Two mechanisms are deliberately NOT used: `target_compile_options()` on
  the INTERFACE `m1_01` library (or the executable) — INTERFACE flags leak
  into every vendor consumer of the source list; and
  `COMPILE_WARNING_AS_ERROR` — silently inert on CMake binaries below
  3.24, so it can never be trusted at the 3.22 floor.

**Extending the gate.** Add the file to `M1_APP_SOURCES` in
`cmake/m1_01/CMakeLists.txt`. That is the entire procedure — the property
and the target consume the same list and cannot drift apart.

**Machine-check both directions before merging.** Forward (no vendor file
gated): `ninja -C out/build/gcc-14_2_build-release -t commands | grep Werror`
must list only `m1_csrc` compile lines. Reverse (no compiled app source
ungated): `ninja -C out/build/gcc-14_2_build-release -t commands | grep -E -- "-c .*m1_csrc.*\.c" | grep -v Werror`
must be empty — an app source added to the build but missed by the gate
shows up here.

**Emergency escape hatch.** If a broken warning must be temporarily
tolerated to unblock a release: keep the file in `M1_APP_SOURCES` and add a
targeted `-Wno-error=<class>` to that one file's entry by splitting it out
into its own `set_source_files_properties()` call with
`"-Wall;-Wextra;-Werror;-Wno-error=<class>"` (smallest scope, per-file), and
open a follow-up issue the same day — the default state of `main` is
warning-clean app sources. Never remove `-Werror` from the shared property,
and never move a vendor file into the gated list to "fix" a warning in it.

**History.** The fork's zero-warning state was originally achieved by a
whole-tree restyle that poisoned upstream merges; the restyle was dropped in
merge 1 and the genuine fixes recovered from it (see
`docs/merge/LEDGER.md`, `recovered-warning-fix` tags). This policy keeps the
outcome (warning-clean app code) with a mechanism that touches no vendor
tree.


## Local case-sensitivity guard

The tree is authored on Windows/macOS (case-insensitive) but must compile on
Linux/WSL ext4, where `#include "Foo.h"` against an on-disk `foo.h` fails.
`tools/check_case_sensitivity.py` makes that class of breakage checkable:

```sh
python tools/check_case_sensitivity.py --repo-root .        # exit 0 = clean
python tools/check_case_sensitivity.py --repo-root . --fix  # rewrite unambiguous literals
```

It resolves every quoted (`"..."`) and angle-bracket (`<...>`) include literal
by basename against `git ls-files`, so system headers that name no tracked file
are ignored, and it also reports tracked paths that differ only by case.

**Install the guard once per clone.** The repository has no CI, so the only
enforcement is a local pre-commit hook — a fresh clone has none, and nothing
else fails when it is missing. Install it as `.git/hooks/pre-commit`
(`chmod +x` on POSIX), which skips the check when Python is absent:

```sh
#!/bin/sh
root="$(git rev-parse --show-toplevel)" || exit 1
py="$(command -v python3 || command -v python)" || {
    echo "python not found; skipping case-sensitivity check"; exit 0; }
exec "$py" "$root/tools/check_case_sensitivity.py" --repo-root "$root"
```

Bypass with `git commit --no-verify` only to unblock an emergency; a wrong-case
include that reaches `main` is invisible to every Windows contributor and every
reviewer who checks out on macOS/Windows.

## Merge Policy (upstream tracking)

This fork tracks `upstream` (MonstaTek/M1). The fork's history was attached to
the upstream line with a one-time **ancestry graft** (`git merge -s ours`):
the tree at the graft is byte-identical to the pinned upstream base
`2af1f8a339dbe629f36a0551de4293b161179b71` (upstream 0.8.1.0) except the
branch-added merge evidence doc (`.gitattributes` is re-landed in the
following EOL-normalization commit `07a24e8`), while the entire legacy
fork history (tagged `legacy/v0.8.11`, immutable) is recorded as already
merged. Consequences:

- **Ordinary merges from now on.** `git merge upstream/main` is a normal
  merge — no special strategy, no `-X ours`. Resolve conflicts as usual.
- **Legacy-only changes are treated as already merged.** The graft did not
  carry legacy fork deltas into the tree. To recover a specific legacy
  change, `git cherry-pick` it from `legacy/v0.8.11` (or its history) — do
  not re-merge the tag. Disposition of every legacy-changed file lives in
  `docs/merge/LEDGER.md` (added by the merge-ledger unit).
- **Bisecting upstream-vs-fork regressions:** walk the upstream line only —
  `git bisect start --first-parent` — so the search follows upstream commits
  rather than descending into legacy side history that the graft recorded as
  merged.

**Per-clone bootstrap (required once after cloning):** line-ending
normalization on merge is a clone-local setting, so every fresh clone must
run:

```sh
git config merge.renormalize true
```

Without it, merging branches whose text files were stored with different
line endings produces whole-file conflict noise instead of clean merges.
The repository-side policy lives in `.gitattributes` (LF in the repository
and on checkout for text files; see that file for the rationale).

