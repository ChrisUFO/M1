# M1 security architecture

## Purpose

This page separates controls evidenced in the repository from security goals
that still require validation. It does not replace the vulnerability-reporting
instructions in [`.github/SECURITY.md`](../.github/SECURITY.md).

## Trust boundaries

The primary boundaries are:

- firmware image and STM32 internal flash;
- removable SD-card content;
- STM32 ↔ ESP32 communication;
- Wi-Fi and Bluetooth radio interfaces;
- NFC, LF RFID, Sub-GHz, and infrared input;
- USB CDC/MSC host connection;
- UART/SWD/JTAG-style development access;
- external GPIO;
- manufacturing and firmware-programming tools.

All external and removable-media input must be treated as untrusted.

## Implementation evidence

| Control | Evidence | Security interpretation |
| --- | --- | --- |
| Firmware CRC | STM32 CRC generation and validation | Detects corruption; does not authenticate publisher |
| Dual-bank update | Inactive-bank programming and bank swap | Supports the update flow; recovery uses the documented SD/service paths |
| Firmware metadata magic values | Two constants in image metadata | Detects some malformed images; not cryptographic |
| Version and region checks | Update result codes and metadata | Policy hooks exist; exact enforced policy needs tests |
| FreeRTOS stack/malloc hooks | Fatal hooks in `m1_tasks.c` | Fail-stop behavior; not memory isolation |
| Bus mutexes | I2C, RF SPI, logging and subsystem locks | Protects concurrency when consistently used |
| Wi-Fi credential crypto | Soft AES-256-CBC + AES-CMAC over a UID-derived key (`m1_csrc/m1_crypto.c`), HW RNG for IVs (`m1_csrc/m1_rng.c`) | At-rest hiding from removed-SD-card reading; **not** secrecy against a physical attacker — see below |

## Wi-Fi credential crypto and hardware RNG

Issue #42 (part 1, PR-8) introduces the primitive layer; the CSV field
that uses it lands in a follow-up. The security posture, stated
plainly:

**What is protected.** Wi-Fi credentials written to the SD card are
encrypted (AES-256-CBC) and authenticated (AES-CMAC, RFC 4493) under a
key derived from the STM32H5 96-bit UID (SP 800-108 KDF in CMAC mode,
domain-separated). Someone who picks up the SD card — or images it —
cannot read the PSK without also having the device.

**What is NOT protected — the honesty list.**

- The UID is readable by any SWD debugger and by arbitrary code
  execution on the device. Anyone with the device *and* the card can
  re-derive the key. This is **key hiding, not secrecy against a
  physical attacker**; there is no secure element on this hardware.
- The soft AES implementation is not side-channel hardened (straight
  table lookups; only the CMAC tag comparison is constant-time).
- The STM32↔ESP32 SPI link carries the plaintext PSK during a join;
  SPI-link encryption is out of scope.
- No TLS, no 802.1X enterprise support, no credential roaming.

**Hardware RNG is mandatory.** IVs come only from the STM32H5 RNG
peripheral (HSI48 kernel clock, brought up at boot before any
consumer). On any init or generate failure the wrapper refuses
permanently — the crypto path fails closed. There is no time-based or
pseudo-random fallback anywhere in the tree; the host suite pins the
three pre-existing decorative `srand(HAL_GetTick())`/`rand()` sites
(saved-file name suffixes and synthetic NFC tag UIDs) as the complete
tolerated set — none of them produce keys or IVs.

**Out of scope (tracked as non-goals for this series):** secure
element or hardware key storage, TLS, SPI-link encryption, and
side-channel hardening.

## Unverified or undocumented claims

The existing security policy mentions secure boot, authenticated firmware
updates, MPU configuration, secure storage, tamper detection, and secure debug.
The documentation reviewed does not establish that these controls are enabled
in production.

Owners must classify each item as implemented, planned, not applicable, or
explicitly accepted risk.

## Required security decisions

1. Is every production STM32 image authenticated before boot and update?
2. Where are signing keys held, and how are public keys provisioned?
3. Is downgrade allowed, and how is the minimum version enforced?
4. Are SWD and other debug interfaces disabled or access-controlled in
   production?
5. What secrets or personal data can exist on the SD card or ESP32?
   (Wi-Fi credentials: see "Wi-Fi credential crypto and hardware RNG"
   above — encrypted at rest under a UID-derived key with an explicit
   physical-attacker limit.)
6. How are Wi-Fi credentials stored, cleared, and protected?
7. What is the trust model for ESP32 firmware and its update binary?
8. What parser fuzzing or malformed-input testing covers radio and storage
   formats?
9. Are MCU isolation features such as MPU/TrustZone used?
10. What is the manufacturing provisioning and recovery process?

## Minimum release security gate

- No credentials, private keys, or production secrets in source or artifacts.
- Update integrity and failure-path tests pass.
- Debug-port state is recorded for the target hardware.
- Dependency/license inventory is reviewed.
- New external parsers have bounds and malformed-input tests.
- Security-relevant changes receive an identified reviewer.
- Known exploitable issues are either fixed or explicitly accepted by an owner.
