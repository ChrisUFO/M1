# IR database provisioning

The **IR File Browser** (Infrared menu) plays Flipper Zero-compatible `.ir`
files side-loaded onto the SD card. This page documents where to put them,
what the browser scans, and what happens on a fresh card.

## SD card convention: `0:/IR`

Copy a Flipper IRDB tree to the card so its category folders sit directly
under `0:/IR`:

```
0:/IR/
├── TVs/                  <- category (first directory level)
│   ├── Samsung/          <- brand (Flipper IRDB layout)
│   │   ├── BN59-01312A.ir
│   │   └── ...
│   └── LG/
│       └── ...
├── Audio/
├── ACs/
├── Projectors/
└── ...
```

- **Canonical source:** the [Flipper IRDB](https://github.com/Lucaslhm/Flipper-IRDB)
  (CC0-1.0). Any Flipper-compatible `.ir` file works; `.ir` files that use a
  protocol this firmware cannot encode are skipped safely (see below).
- **Release starter pack:** firmware releases ship a curated starter database
  (`TVs`, `ACs`, `Audio_and_Video_Receivers`, `Streaming_Devices`,
  `Projectors` at a pinned Flipper-IRDB commit) as a downloadable zip whose
  `IR/` folder can be extracted straight to the card root — see the release
  notes and [`releases/irdb-attribution.md`](releases/irdb-attribution.md)
  for the pin, license and caveats, and
  [`scripts/fetch_ir_starter_db.sh`](../scripts/fetch_ir_starter_db.sh) for
  the reproducible packaging step.
- **Category** = the first directory level below `0:/IR`; it becomes a row of
  the browser's category list. Deeper directory levels (typically
  `Brand/Device.ir`) are flattened into that category's device list, so both
  `TVs/Samsung/foo.ir` and `TVs/foo.ir` layouts are browsable.
- The browser is a *user-content* consumer. It does not read or write the
  firmware-owned `0:/infrared/db/*.ir` files used by the upstream
  "Universal Remotes" menu entries, and mounting a fresh card does not create
  an empty `0:/IR` folder — an absent folder shows the empty-library hint
  below, by design.

### Side-load steps

1. Power the device off and remove the SD card (or use the USB Drive menu to
   expose the card over USB Mass Storage).
2. Create an `IR` folder in the card root.
3. Copy the wanted category folders from Flipper IRDB (e.g. `TVs/`, `Audio/`)
   into `IR/`. Whole-tree copies are fine; the scan bounds below keep the
   browser responsive on large databases.
4. Re-insert the card / exit USB Drive, then open
   **Infrared → IR File Browser**.

## Scan bounds

The browser scans are bounded and documented so a huge or deeply nested tree
can never stall the UI (constants live in `Infrared/m1_ir_browser.h`):

| Bound | Value | Meaning |
| --- | --- | --- |
| Scan depth | 4 directory levels below the scan root | Flipper IRDB needs 2 (`Brand/Device.ir`); deeper levels are ignored |
| Categories | 24 | more categories truncate the list (title shows `+`) |
| Category name length | 31 characters | longer category names are skipped during the scan (they cannot be re-opened from the bounded list) |
| Devices per list | 64 | per category list and per search; truncation marked `+` |
| Buttons per file | 128 indexed blocks | per-device index cap; truncation marked `+` |
| Path length | 128 bytes | longer paths are skipped during the scan |
| Raw signal length | 4096 ticks (10 µs each) | longer raw blocks surface a notice instead of transmitting |
| Favorites | 20 entries | LRU cap of `0:/System/ir_favorites.txt` (see below) |
| Recent | 10 entries | LRU cap of `0:/System/ir_recent.txt` (see below) |

Hidden/system attributes and names beginning with `.` are skipped, and so
are category folder names too long for the bounded category list (a
truncated name could not be re-opened). When a
list hits a cap, its title gains a trailing `+` and everything past the cap
is simply not scanned — nothing past a bound is read.

## Empty and error states

- **No SD card:** the standard SD-card error screen; BACK returns to the menu.
- **No `0:/IR` folder (or no categories):** an actionable empty-library hint
  tells the user to copy the database to `0:/IR`.
- **Category without `.ir` files:** a notice; BACK returns to the category
  list.
- **Unreadable file:** a notice; the browser stays on the device list.
- **Empty favorites/recent:** the stored list shows a short "list is
  empty" notice (this is also what a corrupt store renders — never an
  error screen or a hang).
- **Empty raw signal:** a `.ir` entry whose raw `data:` line carries no samples
  (or a count the transmitter could not play safely) is refused with an
  "Empty raw" notice — a bare pause is never transmitted.
- **Unknown protocol names inside a `.ir` file** (strict-skip): the affected
  buttons are skipped — never transmitted as garbage — and one aggregated
  notice names the count and the first offending protocol after the file is
  indexed. This includes a file whose every block is skipped: the aggregate
  then replaces the plain no-usable-buttons notice and explains it.

## Favorites and recent

The browser remembers devices in two plain text files under the
firmware's canonical `0:/System` folder (created lazily on the first
write):

- `0:/System/ir_favorites.txt` — up to **20** entries;
- `0:/System/ir_recent.txt` — up to **10** entries.

Format (the legacy fork's): one full device path per line, newest first,
e.g. `0:/IR/TVs/Samsung/BN59-00600A.ir`. Every change rewrites the whole
file (never more lines than the cap), so the files stay small and can be
edited or deleted on a PC — deleting a file simply empties the list.

Behavior:

- The dashboard lists **Favorites** and **Recent** ahead of *Browse
  files*/*Search*; entries open the same device → button flow as any
  other list, with the basename (`.ir` stripped) as the row label.
- **Toggle a favorite** by holding OK in the device's button view for
  about a second. A short press transmits; the long-press mark toggles
  the favorite exactly once — never while a frame is in flight (a
  long-press that lands mid-frame skips the toggle but still swallows
  that hold's auto-repeat clicks) — and the auto-repeat clicks that
  follow the toggle are swallowed until OK is released, so favoriting
  cannot re-send the selected button (holding a button outside that
  toggle still auto-repeats it, everywhere in this firmware). Favorited
  devices carry a `* ` marker in the button-view title.
- **Recent** records a device when its frame completes — sending (and
  finishing the send), not merely opening, is what makes a device
  recent; a send aborted with BACK mid-frame is not recorded.
- Loads are tolerant: a missing or unreadable file, blank lines, corrupt
  lines and lines longer than the 128-byte path bound are skipped, and
  an unreadable store renders an empty list, never an error screen. A
  stored line must also be a device path (`0:/IR/….ir`), so a rewrite
  torn by a full or removed card — a path cut mid-line — is skipped at
  load and dropped by the next rewrite instead of lingering as a
  phantom row. A stored path whose file has since disappeared from the
  card opens the normal "Read failed" notice, exactly like any
  unreadable device.

These stores are pure browser data: they are read and written only
between transmits inside the browser session and never touch the
universal-remote databases under `0:/infrared/db`.

## Transmit behavior

Selecting a device indexes it (names only) and shows its buttons. Pressing OK
on a button re-parses exactly that block from the card and transmits one
frame through the same encoder path as the upstream universal remotes —
including correct NEC/NECext/RC5X/SIRC15/SIRC20/RCA handling and raw timing
playback. There is no category-wide cycling from the browser.

The one place a full-category cycle exists is the explicit
**Infrared → Universal Remotes → Cycle all …** submenu actions (one per
category, over the firmware-owned `0:/infrared/db` databases, with the
upstream pause/resume and progress UI). They are deliberate, separately
invoked actions: the universal remote screens themselves transmit a
single frame per OK press, and the browser never cycles.
