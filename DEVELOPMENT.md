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

## Merge Policy (upstream tracking)

This fork tracks `upstream` (MonstaTek/M1). The fork's history was attached to
the upstream line with a one-time **ancestry graft** (`git merge -s ours`):
the tree at the graft is byte-identical to the pinned upstream base
`2af1f8a339dbe629f36a0551de4293b161179b71` (upstream 0.8.1.0) except
branch-added files (`.gitattributes`, the merge evidence doc), while the
entire legacy fork history (tagged `legacy/v0.8.11`, immutable) is recorded
as already merged. Consequences:

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

