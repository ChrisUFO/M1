# Wi-Fi credential threat model

Scope: the optional encrypted-PSK (`credential`) column of the
saved-networks store (`0:/wifi/saved_networks.csv`), introduced for
issue #42 part 2 (PR-9). The crypto primitives live in
`m1_csrc/m1_crypto.c/.h` (PR-8); this page states plainly what they do
and do not buy. It complements — and is linked from —
[`docs/security.md`](../security.md), which carries the project-wide
posture.

## What is stored

Each saved-network row may carry one optional trailing column:

```
credential = base64(iv ‖ ciphertext) ‖ ":" ‖ base64(cmac)
```

- **ciphertext** — AES-256-CBC (NIST SP 800-38A, no padding; the
  plaintext is a fixed 64-byte frame: PSK bytes then zero padding) of
  the PSK, so the PSK's exact length never leaks into the ciphertext
  size and both WPA forms (8..63-char passphrase, 64-hex-char PMK)
  fit.
- **iv** — 16 bytes from the STM32H5 hardware RNG
  (`m1_rng_bytes()`). If the RNG refuses, the save refuses; there is
  no time-based or counter-based fallback anywhere in the tree.
- **cmac** — AES-CMAC (RFC 4493 / SP 800-38B, 256-bit key) computed
  over `iv ‖ ciphertext`, so both the IV and the ciphertext are
  integrity-protected. Compared constant-time (`m1_ct_equal`).
- **key** — derived from the 96-bit STM32H5 device UID via the
  SP 800-108 KDF in CMAC mode with the domain-separation label
  `m1-wifi-cred-v1` (`m1_crypto_derive_wifi_key`).

`FMT_VER` stays `1`. Rows without the column (or with an empty one)
are legacy rows: valid, with no stored PSK — *unencrypted by absence*.
There is exactly one credential store (this CSV); the legacy fork's
`m1_wifi_cred.c` second store was deliberately not imported. No
migration shim exists or is needed: a fresh store simply starts with
the column present (empty), and old files keep loading unchanged.

## Protected: removed-SD-card casual access

The concrete attacker this defends against: someone picks up the SD
card — or images it, or salvages it after the device is sold or
scrapped — and reads `saved_networks.csv` with any ordinary tool. For
that attacker the PSK is opaque AES-256-CBC ciphertext under a key
they do not have, and any edit they make (flipping a ciphertext or
tag byte, truncating the field, splicing fields) fails the CMAC
verification.

## NOT protected — explicit exclusions

- **An attacker with the device** (and the SD card). The 96-bit UID is
  readable by any SWD debugger and by arbitrary code execution on the
  device, so the derived key can be recomputed. This mechanism is
  **key hiding, not secrecy against a physical attacker**. There is no
  secure element on this hardware; a secure element / hardware key
  store is a tracked non-goal for this series.
- **The STM32 ↔ ESP32 link.** During a join (PR-11) the PSK is
  decrypted and sent to the ESP32 in plaintext over the SPI/UART
  console path. SPI-link encryption is out of scope; the on-device
  plaintext window and the link are accepted exposures.
- **Side channels.** The soft AES in `m1_crypto.c` is a straight-table
  implementation with no side-channel hardening; only the CMAC tag
  comparison is constant-time.
- **802.1X / enterprise / TLS / credential roaming** — out of scope;
  personal PSKs only.
- **An attacker who can write the file *and* observe joins.** The CMAC
  gives integrity, not confidentiality of traffic or padding-oracle
  resistance in any interactive sense — the device never decrypts a
  field that fails verification first.

## CMAC: integrity, not confidentiality

The CMAC exists to make tampering *detectable*, so a modified or
foreign-sealed field can never be decrypted into garbage that is then
sent to an access point. It is not an additional layer of secrecy over
the ciphertext (AES-256-CBC already provides the at-rest
confidentiality claim above), and it does not authenticate the file to
any external party — the key is device-local.

## Tamper and failure behavior (fail closed, never brick)

At load, every credential field is parsed and CMAC-verified (no
decryption). A field that is structurally bad, truncated, tampered, or
was sealed under a different device's UID marks **that row's
credential** `WIFI_CRED_INVALID`: the row still loads — it remains a
valid reconnaissance target, which is the store's primary purpose —
but the join flow (PR-11) skips it, and the boot sequence is
unaffected: settings still come up clean, no crash, no store wipe. At
join time the field is re-verified and decrypted (`wifi_cred_get_psk`);
any failure marks the entry unusable for the rest of the session.
Plaintext PSK buffers are zeroized with `m1_secure_zero()` on every
path (absorbed input after sealing, decrypted frame and output buffer
after use/failure, derived keys after each use).

When the store is rewritten, whatever text the loader accepted —
including attacker-supplied or foreign-device fields — is re-emitted
verbatim in a CSV-quoted `credential` column: the quoting keeps every
row at 8 columns even when the field text contains commas, and it
preserves sealed data for a card moved back to its sealing device
instead of destroying it. A quoted field so long that it cannot fit
the writer's bounded line buffer fails that save fail-closed (the
previous database stays in place), exactly like any other overflow.

## Residual limits of a UID-derived key

The UID is *unique per device* but *not secret*. It is also the input
to the USB serial number derivation, so it is exposed to USB hosts.
Consequences: moving the card between devices invalidates credentials
(treated as tamper — by design, fail closed), and no rotation,
revocation, or KEK-wrapping hierarchy exists. If a stronger posture is
ever required, the store's per-record format already carries its own
IV and tag, so only the KDF input would change.
