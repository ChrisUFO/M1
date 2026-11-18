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
| Devices per list | 64 | per category list and per search; truncation marked `+` |
| Buttons per file | 128 indexed blocks | per-device index cap; truncation marked `+` |
| Path length | 128 bytes | longer paths are skipped during the scan |
| Raw signal length | 4096 ticks (10 µs each) | longer raw blocks surface a notice instead of transmitting |

Hidden/system attributes and names beginning with `.` are skipped. When a
list hits a cap, its title gains a trailing `+` and everything past the cap
is simply not scanned — nothing past a bound is read.

## Empty and error states

- **No SD card:** the standard SD-card error screen; BACK returns to the menu.
- **No `0:/IR` folder (or no categories):** an actionable empty-library hint
  tells the user to copy the database to `0:/IR`.
- **Category without `.ir` files:** a notice; BACK returns to the category
  list.
- **Unreadable file:** a notice; the browser stays on the device list.
- **Unknown protocol names inside a `.ir` file** (strict-skip): the affected
  buttons are skipped — never transmitted as garbage — and one aggregated
  notice names the count and the first offending protocol after the file is
  indexed.

## Transmit behavior

Selecting a device indexes it (names only) and shows its buttons. Pressing OK
on a button re-parses exactly that block from the card and transmits one
frame through the same encoder path as the upstream universal remotes —
including correct NEC/NECext/RC5X/SIRC15/SIRC20/RCA handling and raw timing
playback. There is no category-wide cycling from the browser (that action is
a separate submenu item planned for a later unit).
